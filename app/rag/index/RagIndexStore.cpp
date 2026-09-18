#include "RagIndexStore.h"

#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <cmath>

namespace Rag {
namespace {

QString databaseError(const QSqlQuery& query) {
    return query.lastError().text();
}

QString databaseError(const QSqlDatabase& database) {
    return database.lastError().text();
}

QMutex& writeMutex() {
    static QMutex mutex;
    return mutex;
}

bool isCjkCharacter(const QChar character) {
    const ushort code = character.unicode();
    return (code >= 0x3400 && code <= 0x4DBF) ||
           (code >= 0x4E00 && code <= 0x9FFF) ||
           (code >= 0xF900 && code <= 0xFAFF);
}

QStringList ftsTokensFor(const QString& text) {
    QStringList tokens;
    QString token;

    auto flushToken = [&tokens, &token]() {
        if (!token.isEmpty()) {
            tokens.append(token);
            token.clear();
        }
    };

    for (const QChar character : text) {
        if (isCjkCharacter(character)) {
            flushToken();
            tokens.append(QString(character));
            continue;
        }

        if (character.isLetterOrNumber() || character == QChar('_')) {
            token.append(character);
            continue;
        }

        flushToken();
    }
    flushToken();
    return tokens;
}

QString ftsTextFor(const QString& text) {
    return ftsTokensFor(text).join(QChar(' '));
}

}  // namespace

RagIndexStore::RagIndexStore() = default;

RagIndexStore::~RagIndexStore() { close(); }

bool RagIndexStore::open(const QString& databasePath, QString* error) {
    setError(error, QString());
    close();

    m_connectionName =
        QStringLiteral("rag_index_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_database =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(
        databasePath.isEmpty() ? QStringLiteral(":memory:") : databasePath);
    if (!m_database.open()) {
        setError(error, databaseError(m_database));
        close();
        return false;
    }

    QSqlQuery pragma(m_database);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(error, databaseError(pragma));
        close();
        return false;
    }
    if (!pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"))) {
        setError(error, databaseError(pragma));
        close();
        return false;
    }
    QMutexLocker schemaLock(&writeMutex());
    return initializeSchema(error);
}

void RagIndexStore::close() {
    if (m_connectionName.isEmpty()) {
        return;
    }

    if (m_database.isValid()) {
        m_database.close();
    }
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
}

bool RagIndexStore::isOpen() const {
    return m_database.isValid() && m_database.isOpen();
}

bool RagIndexStore::initializeSchema(QString* error) {
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS documents ("
                       "document_hash TEXT PRIMARY KEY,"
                       "canonical_path TEXT NOT NULL,"
                       "file_size INTEGER NOT NULL,"
                       "modified_time TEXT NOT NULL,"
                       "page_count INTEGER NOT NULL"
                       ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS chunks ("
            "chunk_id TEXT PRIMARY KEY,"
            "document_hash TEXT NOT NULL,"
            "page_start INTEGER NOT NULL,"
            "page_end INTEGER NOT NULL,"
            "section_path TEXT,"
            "text TEXT NOT NULL,"
            "chunk_hash TEXT NOT NULL,"
            "char_start INTEGER NOT NULL,"
            "char_length INTEGER NOT NULL,"
            "locations_json TEXT NOT NULL,"
            "FOREIGN KEY(document_hash) REFERENCES documents(document_hash)"
            ")"),
        QStringLiteral(
            "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
            "chunk_id UNINDEXED,"
            "document_hash UNINDEXED,"
            "section_path,"
            "text,"
            "tokenize='unicode61'"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS chunk_vectors ("
            "document_hash TEXT NOT NULL, chunk_id TEXT NOT NULL, "
            "chunk_hash TEXT NOT NULL, model_id TEXT NOT NULL, "
            "model_version TEXT NOT NULL, dimension INTEGER NOT NULL, "
            "vector BLOB NOT NULL, "
            "PRIMARY KEY(chunk_id, model_id, model_version), "
            "FOREIGN KEY(chunk_id) REFERENCES chunks(chunk_id) ON DELETE "
            "CASCADE)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_vectors_doc_model "
            "ON chunk_vectors(document_hash, model_id, model_version)")};

    for (const QString& statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            setError(error, databaseError(query));
            return false;
        }
    }
    return true;
}

bool RagIndexStore::replaceDocument(const DocumentKey& document,
                                    const QVector<PdfChunk>& chunks,
                                    QString* error,
                                    const RagCancellationToken* cancellation) {
    setError(error, QString());
    if (!isOpen()) {
        setError(error, QStringLiteral("Index store is not open"));
        return false;
    }
    if (document.contentHash.isEmpty()) {
        setError(error, QStringLiteral("Document content hash is empty"));
        return false;
    }
    if (cancellation && cancellation->isCancelled()) {
        setError(error, QStringLiteral("Indexing cancelled"));
        return false;
    }

    QMutexLocker writeLock(&writeMutex());
    if (cancellation && cancellation->isCancelled()) {
        setError(error, QStringLiteral("Indexing cancelled"));
        return false;
    }

    if (!m_database.transaction()) {
        setError(error, databaseError(m_database));
        return false;
    }

    auto rollback = [this, error](const QString& message) {
        m_database.rollback();
        setError(error, message);
        return false;
    };

    QSqlQuery deleteOldFts(m_database);
    QSqlQuery deleteOldVectors(m_database);
    deleteOldVectors.prepare(QStringLiteral(
        "DELETE FROM chunk_vectors WHERE document_hash IN ("
        "SELECT document_hash FROM documents WHERE canonical_path = ? "
        "AND document_hash <> ?)"));
    deleteOldVectors.addBindValue(document.canonicalPath);
    deleteOldVectors.addBindValue(document.contentHash);
    if (!deleteOldVectors.exec())
        return rollback(databaseError(deleteOldVectors));
    deleteOldFts.prepare(
        QStringLiteral("DELETE FROM chunks_fts WHERE document_hash IN ("
                       "SELECT document_hash FROM documents "
                       "WHERE canonical_path = ? AND document_hash <> ?)"));
    deleteOldFts.addBindValue(document.canonicalPath);
    deleteOldFts.addBindValue(document.contentHash);
    if (!deleteOldFts.exec()) {
        return rollback(databaseError(deleteOldFts));
    }

    QSqlQuery deleteOldChunks(m_database);
    deleteOldChunks.prepare(
        QStringLiteral("DELETE FROM chunks WHERE document_hash IN ("
                       "SELECT document_hash FROM documents "
                       "WHERE canonical_path = ? AND document_hash <> ?)"));
    deleteOldChunks.addBindValue(document.canonicalPath);
    deleteOldChunks.addBindValue(document.contentHash);
    if (!deleteOldChunks.exec()) {
        return rollback(databaseError(deleteOldChunks));
    }

    QSqlQuery deleteOldDocuments(m_database);
    deleteOldDocuments.prepare(
        QStringLiteral("DELETE FROM documents WHERE canonical_path = ? "
                       "AND document_hash <> ?"));
    deleteOldDocuments.addBindValue(document.canonicalPath);
    deleteOldDocuments.addBindValue(document.contentHash);
    if (!deleteOldDocuments.exec()) {
        return rollback(databaseError(deleteOldDocuments));
    }

    QSqlQuery deleteFts(m_database);
    QSqlQuery deleteVectors(m_database);
    deleteVectors.prepare(
        QStringLiteral("DELETE FROM chunk_vectors WHERE document_hash = ?"));
    deleteVectors.addBindValue(document.contentHash);
    if (!deleteVectors.exec())
        return rollback(databaseError(deleteVectors));
    deleteFts.prepare(
        QStringLiteral("DELETE FROM chunks_fts WHERE document_hash = ?"));
    deleteFts.addBindValue(document.contentHash);
    if (!deleteFts.exec()) {
        return rollback(databaseError(deleteFts));
    }

    QSqlQuery deleteChunks(m_database);
    deleteChunks.prepare(
        QStringLiteral("DELETE FROM chunks WHERE document_hash = ?"));
    deleteChunks.addBindValue(document.contentHash);
    if (!deleteChunks.exec()) {
        return rollback(databaseError(deleteChunks));
    }

    QSqlQuery deleteDocument(m_database);
    deleteDocument.prepare(
        QStringLiteral("DELETE FROM documents WHERE document_hash = ?"));
    deleteDocument.addBindValue(document.contentHash);
    if (!deleteDocument.exec()) {
        return rollback(databaseError(deleteDocument));
    }

    QSqlQuery insertDocument(m_database);
    insertDocument.prepare(QStringLiteral(
        "INSERT INTO documents(document_hash, canonical_path, file_size, "
        "modified_time, page_count) VALUES(?, ?, ?, ?, ?)"));
    insertDocument.addBindValue(document.contentHash);
    insertDocument.addBindValue(document.canonicalPath);
    insertDocument.addBindValue(document.fileSize);
    const QString modifiedTime =
        document.modifiedTime.isValid()
            ? document.modifiedTime.toString(Qt::ISODate)
            : QStringLiteral("");
    insertDocument.addBindValue(modifiedTime);
    insertDocument.addBindValue(document.pageCount);
    if (!insertDocument.exec()) {
        return rollback(databaseError(insertDocument));
    }

    QSqlQuery insertChunk(m_database);
    insertChunk.prepare(QStringLiteral(
        "INSERT INTO chunks(chunk_id, document_hash, page_start, page_end, "
        "section_path, text, chunk_hash, char_start, char_length, "
        "locations_json) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    QSqlQuery insertFts(m_database);
    insertFts.prepare(QStringLiteral(
        "INSERT INTO chunks_fts(chunk_id, document_hash, section_path, text) "
        "VALUES(?, ?, ?, ?)"));

    for (const PdfChunk& chunk : chunks) {
        if (cancellation && cancellation->isCancelled()) {
            return rollback(QStringLiteral("Indexing cancelled"));
        }
        insertChunk.bindValue(0, chunk.id);
        insertChunk.bindValue(1, document.contentHash);
        insertChunk.bindValue(2, chunk.pageStart);
        insertChunk.bindValue(3, chunk.pageEnd);
        insertChunk.bindValue(4, chunk.sectionPath);
        insertChunk.bindValue(5, chunk.text);
        insertChunk.bindValue(6, chunk.chunkHash);
        insertChunk.bindValue(7, chunk.charStart);
        insertChunk.bindValue(8, chunk.charLength);
        insertChunk.bindValue(9, locationsToJson(chunk.locations));
        if (!insertChunk.exec()) {
            return rollback(databaseError(insertChunk));
        }

        insertFts.bindValue(0, chunk.id);
        insertFts.bindValue(1, document.contentHash);
        insertFts.bindValue(2, ftsTextFor(chunk.sectionPath));
        insertFts.bindValue(3, ftsTextFor(chunk.text));
        if (!insertFts.exec()) {
            return rollback(databaseError(insertFts));
        }
    }

    if (cancellation && cancellation->isCancelled()) {
        return rollback(QStringLiteral("Indexing cancelled"));
    }
    if (!m_database.commit()) {
        setError(error, databaseError(m_database));
        m_database.rollback();
        return false;
    }
    return true;
}

bool RagIndexStore::containsDocument(const DocumentKey& document) const {
    if (!isOpen() || document.contentHash.isEmpty()) {
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM documents WHERE document_hash = ? LIMIT 1"));
    query.addBindValue(document.contentHash);
    return query.exec() && query.next();
}

int RagIndexStore::documentChunkCount(const DocumentKey& document,
                                      QString* error) const {
    setError(error, QString());
    if (!isOpen()) {
        setError(error, QStringLiteral("Index store is not open"));
        return -1;
    }
    if (document.contentHash.isEmpty()) {
        setError(error, QStringLiteral("Document content hash is empty"));
        return -1;
    }

    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("SELECT COUNT(*) FROM chunks WHERE document_hash = ?"));
    query.addBindValue(document.contentHash);
    if (!query.exec() || !query.next()) {
        setError(error, databaseError(query));
        return -1;
    }
    return query.value(0).toInt();
}

QVector<RagEvidence> RagIndexStore::search(const DocumentKey& document,
                                           const QString& queryText, int limit,
                                           QString* error) const {
    setError(error, QString());
    QVector<RagEvidence> results;
    if (!isOpen()) {
        setError(error, QStringLiteral("Index store is not open"));
        return results;
    }
    if (document.contentHash.isEmpty() || queryText.trimmed().isEmpty()) {
        return results;
    }

    const QString ftsQuery = ftsQueryFor(queryText);
    if (ftsQuery.isEmpty()) {
        return results;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT c.chunk_id, c.document_hash, c.page_start, c.page_end, "
        "c.section_path, c.text, c.locations_json, bm25(chunks_fts) "
        "FROM chunks_fts JOIN chunks c ON c.chunk_id = chunks_fts.chunk_id "
        "WHERE chunks_fts.document_hash = ? AND chunks_fts MATCH ? "
        "ORDER BY bm25(chunks_fts) LIMIT ?"));
    query.addBindValue(document.contentHash);
    query.addBindValue(ftsQuery);
    query.addBindValue(qMax(1, limit));
    if (!query.exec()) {
        setError(error, databaseError(query));
        return results;
    }

    int rank = 0;
    while (query.next()) {
        RagEvidence evidence;
        evidence.chunkId = query.value(0).toString();
        evidence.documentHash = query.value(1).toString();
        evidence.pageStart = query.value(2).toInt();
        evidence.pageEnd = query.value(3).toInt();
        evidence.sectionPath = query.value(4).toString();
        evidence.text = query.value(5).toString();
        evidence.locations = locationsFromJson(query.value(6).toString());
        const double bm25Score = query.value(7).toDouble();
        evidence.lexicalScore = 1.0 / (1.0 + qMax(0.0, bm25Score));
        evidence.finalScore = evidence.lexicalScore;
        evidence.rank = rank++;
        results.append(std::move(evidence));
    }
    return results;
}

QVector<RagIndexStore::VectorChunk> RagIndexStore::chunksMissingVectors(
    const DocumentKey& document, const QString& model, const QString& version,
    int limit, QString* error) const {
    setError(error, {});
    QVector<VectorChunk> chunks;
    QSqlQuery query(m_database);
    QString sql = QStringLiteral(
        "SELECT c.chunk_id, c.chunk_hash, c.text FROM chunks c "
        "LEFT JOIN chunk_vectors v ON v.chunk_id = c.chunk_id "
        "AND v.chunk_hash = c.chunk_hash AND v.model_id = ? "
        "AND v.model_version = ? WHERE c.document_hash = ? "
        "AND v.chunk_id IS NULL ORDER BY c.page_start, c.char_start");
    if (limit > 0)
        sql += QStringLiteral(" LIMIT ?");
    query.prepare(sql);
    query.addBindValue(model);
    query.addBindValue(version);
    query.addBindValue(document.contentHash);
    if (limit > 0)
        query.addBindValue(limit);
    if (!query.exec()) {
        setError(error, databaseError(query));
        return {};
    }
    while (query.next()) {
        chunks.append({query.value(0).toString(),
                       query.value(1).toString(),
                       query.value(2).toString(),
                       {}});
    }
    return chunks;
}

bool RagIndexStore::storeVectors(const DocumentKey& document,
                                 const QString& model, const QString& version,
                                 const QVector<VectorChunk>& chunks,
                                 const RagCancellationToken& cancel,
                                 QString* error) {
    setError(error, {});
    QMutexLocker lock(&writeMutex());
    if (cancel.isCancelled()) {
        setError(error, QStringLiteral("Embedding cancelled"));
        return false;
    }
    if (!m_database.transaction()) {
        setError(error, databaseError(m_database));
        return false;
    }
    if (!chunks.isEmpty()) {
        const int dimension = chunks.first().vector.size();
        if (dimension <= 0) {
            m_database.rollback();
            setError(error, QStringLiteral("Empty embedding"));
            return false;
        }
        QSqlQuery removeIncompatible(m_database);
        removeIncompatible.prepare(QStringLiteral(
            "DELETE FROM chunk_vectors WHERE document_hash = ? "
            "AND model_id = ? AND model_version = ? AND dimension <> ?"));
        removeIncompatible.addBindValue(document.contentHash);
        removeIncompatible.addBindValue(model);
        removeIncompatible.addBindValue(version);
        removeIncompatible.addBindValue(dimension);
        if (!removeIncompatible.exec()) {
            setError(error, databaseError(removeIncompatible));
            m_database.rollback();
            return false;
        }
    }
    QSqlQuery chunkCheck(m_database);
    chunkCheck.prepare(QStringLiteral(
        "SELECT 1 FROM chunks WHERE chunk_id = ? AND document_hash = ? "
        "AND chunk_hash = ? LIMIT 1"));
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO chunk_vectors "
        "(document_hash, chunk_id, chunk_hash, model_id, model_version, "
        "dimension, vector) VALUES(?, ?, ?, ?, ?, ?, ?)"));
    for (const VectorChunk& chunk : chunks) {
        if (cancel.isCancelled() || chunk.vector.isEmpty() ||
            chunk.vector.size() != chunks.first().vector.size()) {
            m_database.rollback();
            setError(error,
                     QStringLiteral("Embedding cancelled or empty vector"));
            return false;
        }
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for (float value : chunk.vector) {
            if (!std::isfinite(value)) {
                m_database.rollback();
                setError(error, QStringLiteral("Non-finite vector"));
                return false;
            }
            stream << value;
        }
        chunkCheck.bindValue(0, chunk.chunkId);
        chunkCheck.bindValue(1, document.contentHash);
        chunkCheck.bindValue(2, chunk.chunkHash);
        if (!chunkCheck.exec() || !chunkCheck.next()) {
            m_database.rollback();
            setError(error,
                     chunkCheck.lastError().isValid()
                         ? databaseError(chunkCheck)
                         : QStringLiteral(
                               "Chunk changed before embedding was saved"));
            return false;
        }
        query.bindValue(0, document.contentHash);
        query.bindValue(1, chunk.chunkId);
        query.bindValue(2, chunk.chunkHash);
        query.bindValue(3, model);
        query.bindValue(4, version);
        query.bindValue(5, chunk.vector.size());
        query.bindValue(6, bytes);
        if (!query.exec()) {
            m_database.rollback();
            setError(error, databaseError(query));
            return false;
        }
    }
    if (cancel.isCancelled()) {
        m_database.rollback();
        setError(error, QStringLiteral("Embedding cancelled"));
        return false;
    }
    if (!m_database.commit()) {
        setError(error, databaseError(m_database));
        m_database.rollback();
        return false;
    }
    return true;
}

QVector<RagIndexStore::VectorChunk> RagIndexStore::loadVectors(
    const DocumentKey& document, const QString& model, const QString& version,
    QString* error) const {
    setError(error, {});
    QVector<VectorChunk> chunks;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT v.chunk_id, v.chunk_hash, v.dimension, v.vector "
        "FROM chunk_vectors v JOIN chunks c ON c.chunk_id = v.chunk_id "
        "WHERE c.document_hash = ? AND v.document_hash = ? "
        "AND v.model_id = ? AND v.model_version = ? AND v.chunk_hash = "
        "c.chunk_hash"));
    query.addBindValue(document.contentHash);
    query.addBindValue(document.contentHash);
    query.addBindValue(model);
    query.addBindValue(version);
    if (!query.exec()) {
        setError(error, databaseError(query));
        return {};
    }
    while (query.next()) {
        const int dimension = query.value(2).toInt();
        const QByteArray bytes = query.value(3).toByteArray();
        if (dimension <= 0 || qint64(dimension) * 4 != bytes.size())
            continue;
        VectorChunk chunk;
        chunk.chunkId = query.value(0).toString();
        chunk.chunkHash = query.value(1).toString();
        QDataStream stream(bytes);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for (int i = 0; i < dimension; ++i) {
            float value = 0;
            stream >> value;
            if (!std::isfinite(value))
                break;
            chunk.vector.append(value);
        }
        if (chunk.vector.size() == dimension)
            chunks.append(std::move(chunk));
    }
    return chunks;
}

bool RagIndexStore::invalidateVectors(const DocumentKey& document,
                                      const QString& model,
                                      const QString& version, QString* error) {
    setError(error, {});
    QMutexLocker lock(&writeMutex());
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("DELETE FROM chunk_vectors WHERE document_hash = ? "
                       "AND model_id = ? AND model_version = ?"));
    query.addBindValue(document.contentHash);
    query.addBindValue(model);
    query.addBindValue(version);
    if (!query.exec()) {
        setError(error, databaseError(query));
        return false;
    }
    return true;
}

QVector<RagEvidence> RagIndexStore::evidenceByIds(const DocumentKey& document,
                                                  const QStringList& ids,
                                                  QString* error) const {
    setError(error, {});
    QVector<RagEvidence> evidence;
    if (ids.isEmpty())
        return evidence;
    QStringList placeholders;
    for (int i = 0; i < ids.size(); ++i)
        placeholders.append(QStringLiteral("?"));
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT chunk_id, document_hash, page_start, "
                                 "page_end, section_path, "
                                 "text, locations_json FROM chunks WHERE "
                                 "document_hash = ? AND chunk_id IN (%1)")
                      .arg(placeholders.join(QStringLiteral(","))));
    query.addBindValue(document.contentHash);
    for (const QString& id : ids)
        query.addBindValue(id);
    if (!query.exec()) {
        setError(error, databaseError(query));
        return {};
    }
    while (query.next()) {
        RagEvidence item;
        item.chunkId = query.value(0).toString();
        item.documentHash = query.value(1).toString();
        item.pageStart = query.value(2).toInt();
        item.pageEnd = query.value(3).toInt();
        item.sectionPath = query.value(4).toString();
        item.text = query.value(5).toString();
        item.locations = locationsFromJson(query.value(6).toString());
        evidence.append(std::move(item));
    }
    return evidence;
}

QString RagIndexStore::ftsQueryFor(const QString& query) {
    QStringList quotedTerms;
    for (const QString& rawTerm : ftsTokensFor(query)) {
        QString term = rawTerm.trimmed();
        if (!term.isEmpty()) {
            quotedTerms.append(QStringLiteral("\"%1\"").arg(
                term.replace(QStringLiteral("\""), QStringLiteral("\"\""))));
        }
    }
    return quotedTerms.join(QStringLiteral(" AND "));
}

QString RagIndexStore::locationsToJson(
    const QVector<ChunkPageLocation>& locations) {
    QJsonArray locationArray;
    for (const ChunkPageLocation& location : locations) {
        QJsonObject locationObject;
        locationObject[QStringLiteral("pageIndex")] = location.pageIndex;
        locationObject[QStringLiteral("charStart")] = location.charStart;
        locationObject[QStringLiteral("charLength")] = location.charLength;

        QJsonArray rectArray;
        for (const QRectF& rect : location.boundingRects) {
            QJsonObject rectObject;
            rectObject[QStringLiteral("x")] = rect.x();
            rectObject[QStringLiteral("y")] = rect.y();
            rectObject[QStringLiteral("width")] = rect.width();
            rectObject[QStringLiteral("height")] = rect.height();
            rectArray.append(rectObject);
        }
        locationObject[QStringLiteral("rects")] = rectArray;
        locationArray.append(locationObject);
    }
    return QString::fromUtf8(
        QJsonDocument(locationArray).toJson(QJsonDocument::Compact));
}

QVector<ChunkPageLocation> RagIndexStore::locationsFromJson(
    const QString& json) {
    QVector<ChunkPageLocation> locations;
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return locations;
    }

    for (const QJsonValue& locationValue : document.array()) {
        const QJsonObject locationObject = locationValue.toObject();
        ChunkPageLocation location;
        location.pageIndex =
            locationObject.value(QStringLiteral("pageIndex")).toInt(-1);
        location.charStart =
            locationObject.value(QStringLiteral("charStart")).toInt();
        location.charLength =
            locationObject.value(QStringLiteral("charLength")).toInt();
        for (const QJsonValue& rectValue :
             locationObject.value(QStringLiteral("rects")).toArray()) {
            const QJsonObject rectObject = rectValue.toObject();
            location.boundingRects.append(
                QRectF(rectObject.value(QStringLiteral("x")).toDouble(),
                       rectObject.value(QStringLiteral("y")).toDouble(),
                       rectObject.value(QStringLiteral("width")).toDouble(),
                       rectObject.value(QStringLiteral("height")).toDouble()));
        }
        locations.append(std::move(location));
    }
    return locations;
}

void RagIndexStore::setError(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
}

}  // namespace Rag
