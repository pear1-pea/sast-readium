#include "RagIndexStore.h"

#include <QChar>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <cmath>

namespace Rag {
namespace {

constexpr int kSchemaVersion = 2;

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

bool isEnglishStopword(const QString& token) {
    static const QSet<QString> stopwords = {
        QStringLiteral("a"),      QStringLiteral("an"),
        QStringLiteral("and"),    QStringLiteral("are"),
        QStringLiteral("as"),     QStringLiteral("at"),
        QStringLiteral("be"),     QStringLiteral("been"),
        QStringLiteral("but"),    QStringLiteral("by"),
        QStringLiteral("can"),    QStringLiteral("could"),
        QStringLiteral("did"),    QStringLiteral("do"),
        QStringLiteral("does"),   QStringLiteral("for"),
        QStringLiteral("from"),   QStringLiteral("has"),
        QStringLiteral("have"),   QStringLiteral("he"),
        QStringLiteral("her"),    QStringLiteral("his"),
        QStringLiteral("how"),    QStringLiteral("i"),
        QStringLiteral("if"),     QStringLiteral("in"),
        QStringLiteral("is"),     QStringLiteral("it"),
        QStringLiteral("its"),    QStringLiteral("may"),
        QStringLiteral("me"),     QStringLiteral("might"),
        QStringLiteral("my"),     QStringLiteral("of"),
        QStringLiteral("on"),     QStringLiteral("or"),
        QStringLiteral("our"),    QStringLiteral("she"),
        QStringLiteral("should"), QStringLiteral("than"),
        QStringLiteral("that"),   QStringLiteral("the"),
        QStringLiteral("their"),  QStringLiteral("them"),
        QStringLiteral("then"),   QStringLiteral("there"),
        QStringLiteral("these"),  QStringLiteral("they"),
        QStringLiteral("this"),   QStringLiteral("those"),
        QStringLiteral("to"),     QStringLiteral("was"),
        QStringLiteral("we"),     QStringLiteral("were"),
        QStringLiteral("what"),   QStringLiteral("when"),
        QStringLiteral("where"),  QStringLiteral("which"),
        QStringLiteral("who"),    QStringLiteral("why"),
        QStringLiteral("will"),   QStringLiteral("with"),
        QStringLiteral("would"),  QStringLiteral("you"),
        QStringLiteral("your")};
    return stopwords.contains(token.toLower());
}

bool isCjkStopword(const QChar character) {
    static const QSet<QChar> stopwords = {
        QChar(0x7684),  // 的
        QChar(0x662F),  // 是
        QChar(0x4EC0),  // 什
        QChar(0x4E48),  // 么
        QChar(0x8FD9),  // 这
        QChar(0x90A3),  // 那
        QChar(0x5417),  // 吗
        QChar(0x5462),  // 呢
        QChar(0x4E86),  // 了
        QChar(0x4E0E),  // 与
        QChar(0x8BF7)   // 请
    };
    return stopwords.contains(character);
}

QString quoteFtsTerm(const QString& term) {
    QString escaped = term;
    escaped.replace(QChar('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

void appendTerm(QStringList* terms, QSet<QString>* seen, const QString& term) {
    if (term.isEmpty() || seen->contains(term)) {
        return;
    }
    seen->insert(term);
    terms->append(term);
}

QStringList queryTermsFor(const QString& query) {
    QStringList terms;
    QSet<QString> seen;
    QString cjkRun;
    QString englishToken;
    QVector<QString> cjkRuns;

    auto flushCjkRun = [&cjkRuns, &cjkRun]() {
        if (!cjkRun.isEmpty()) {
            cjkRuns.append(cjkRun);
            cjkRun.clear();
        }
    };

    auto flushEnglish = [&terms, &seen, &englishToken]() {
        if (englishToken.isEmpty()) {
            return;
        }
        if (!isEnglishStopword(englishToken)) {
            appendTerm(&terms, &seen, englishToken.toLower());
        }
        englishToken.clear();
    };

    for (const QChar character : query) {
        if (isCjkCharacter(character)) {
            flushEnglish();
            if (isCjkStopword(character)) {
                flushCjkRun();
            } else {
                cjkRun.append(character);
            }
            continue;
        }

        flushCjkRun();
        if (character.isLetterOrNumber() || character == QChar('_')) {
            englishToken.append(character);
        } else {
            flushEnglish();
        }
    }
    flushCjkRun();
    flushEnglish();

    bool hasCjkBigram = false;
    for (const QString& run : cjkRuns) {
        if (run.size() >= 2) {
            hasCjkBigram = true;
            break;
        }
    }
    for (const QString& run : cjkRuns) {
        if (run.size() >= 2) {
            // FTS5 phrases match adjacent tokens. Indexing stores CJK text
            // one character per token, so adjacent-character phrases avoid
            // the "every chunk matches" problem of OR-ing single characters.
            for (int i = 0; i + 1 < run.size(); ++i) {
                appendTerm(
                    &terms, &seen,
                    QStringLiteral("%1 %2").arg(run.at(i)).arg(run.at(i + 1)));
            }
        } else if (!hasCjkBigram) {
            appendTerm(&terms, &seen, run);
        }
    }

    if (terms.isEmpty()) {
        // A query consisting only of stopwords still needs a useful fallback.
        for (const QString& token : ftsTokensFor(query)) {
            appendTerm(&terms, &seen, token);
        }
    }
    return terms;
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
    if (!databasePath.isEmpty()) {
        // Create the database with owner-only permissions before SQLite writes
        // anything into it. This avoids a window where the file is readable by
        // other local users.
        QFile file(databasePath);
        if (!file.exists()) {
            if (!file.open(QIODevice::WriteOnly)) {
                setError(error, QStringLiteral("Cannot create index database"));
                close();
                return false;
            }
            file.close();
            file.setPermissions(QFileDevice::ReadOwner |
                                QFileDevice::WriteOwner);
        }
    }

    m_database.setDatabaseName(
        databasePath.isEmpty() ? QStringLiteral(":memory:") : databasePath);
    if (!m_database.open()) {
        setError(error, databaseError(m_database));
        close();
        return false;
    }

    QSqlQuery pragma(m_database);
    if (!pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"))) {
        setError(error, databaseError(pragma));
        close();
        return false;
    }
    // Migrations rebuild tables, so foreign keys must be disabled until the
    // schema is at the expected version.
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = OFF"))) {
        setError(error, databaseError(pragma));
        close();
        return false;
    }
    if (!initializeSchema(error)) {
        close();
        return false;
    }
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(error, databaseError(pragma));
        close();
        return false;
    }
    return true;
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

bool RagIndexStore::createSchema(QString* error) {
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS documents ("
                       "document_hash TEXT PRIMARY KEY,"
                       "file_size INTEGER NOT NULL,"
                       "modified_time TEXT NOT NULL,"
                       "page_count INTEGER NOT NULL,"
                       "chunker_policy TEXT NOT NULL DEFAULT ''"
                       ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS document_paths ("
            "document_hash TEXT NOT NULL,"
            "canonical_path TEXT NOT NULL,"
            "PRIMARY KEY(document_hash, canonical_path),"
            "FOREIGN KEY(document_hash) REFERENCES documents(document_hash) "
            "ON DELETE CASCADE"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_document_paths_path "
                       "ON document_paths(canonical_path)"),
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

bool RagIndexStore::migrateSchemaFromVersionZero(QString* error) {
    if (!m_database.transaction()) {
        setError(error, databaseError(m_database));
        return false;
    }

    auto rollback = [this, error](const QString& message) {
        m_database.rollback();
        setError(error, message);
        return false;
    };

    QSqlQuery createNew(m_database);
    if (!createNew.exec(QStringLiteral("CREATE TABLE documents_new ("
                                       "document_hash TEXT PRIMARY KEY,"
                                       "file_size INTEGER NOT NULL,"
                                       "modified_time TEXT NOT NULL,"
                                       "page_count INTEGER NOT NULL"
                                       ")"))) {
        return rollback(databaseError(createNew));
    }

    QSqlQuery copyDocuments(m_database);
    if (!copyDocuments.exec(QStringLiteral(
            "INSERT INTO documents_new(document_hash, file_size, "
            "modified_time, page_count) "
            "SELECT document_hash, file_size, modified_time, page_count "
            "FROM documents"))) {
        return rollback(databaseError(copyDocuments));
    }
    copyDocuments.finish();

    QSqlQuery createPaths(m_database);
    if (!createPaths.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS document_paths ("
            "document_hash TEXT NOT NULL,"
            "canonical_path TEXT NOT NULL,"
            "PRIMARY KEY(document_hash, canonical_path),"
            "FOREIGN KEY(document_hash) REFERENCES documents(document_hash) "
            "ON DELETE CASCADE"
            ")"))) {
        return rollback(databaseError(createPaths));
    }

    QSqlQuery copyPaths(m_database);
    if (!copyPaths.exec(QStringLiteral(
            "INSERT OR IGNORE INTO document_paths(document_hash, "
            "canonical_path) SELECT document_hash, canonical_path "
            "FROM documents"))) {
        return rollback(databaseError(copyPaths));
    }
    copyPaths.finish();

    QSqlQuery dropOld(m_database);
    if (!dropOld.exec(QStringLiteral("DROP TABLE documents"))) {
        return rollback(databaseError(dropOld));
    }

    QSqlQuery renameNew(m_database);
    if (!renameNew.exec(
            QStringLiteral("ALTER TABLE documents_new RENAME TO documents"))) {
        return rollback(databaseError(renameNew));
    }

    if (!m_database.commit()) {
        setError(error, databaseError(m_database));
        m_database.rollback();
        return false;
    }
    return true;
}

bool RagIndexStore::initializeSchema(QString* error) {
    auto readVersion = [this]() {
        QSqlQuery query(m_database);
        int version = 0;
        if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
            version = query.value(0).toInt();
        }
        query.finish();
        return version;
    };

    int version = readVersion();
    if (version > kSchemaVersion) {
        setError(error, QStringLiteral("Unsupported index schema version"));
        return false;
    }
    // Fast path: an up-to-date database must not take the global write lock
    // or repeat CREATE/PRAGMA work on every query.
    if (version == kSchemaVersion) {
        return true;
    }

    QMutexLocker schemaLock(&writeMutex());
    version = readVersion();
    if (version > kSchemaVersion) {
        setError(error, QStringLiteral("Unsupported index schema version"));
        return false;
    }
    if (version == kSchemaVersion) {
        return true;
    }

    QSqlQuery tableQuery(m_database);
    tableQuery.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?"));
    tableQuery.addBindValue(QStringLiteral("documents"));
    if (!tableQuery.exec()) {
        setError(error, databaseError(tableQuery));
        return false;
    }
    const bool hasDocuments = tableQuery.next();
    tableQuery.finish();

    if (!hasDocuments) {
        if (!createSchema(error)) {
            return false;
        }
    } else {
        QSqlQuery columnsQuery(m_database);
        if (!columnsQuery.exec(
                QStringLiteral("PRAGMA table_info(documents)"))) {
            setError(error, databaseError(columnsQuery));
            return false;
        }
        bool hasLegacyCanonicalPath = false;
        while (columnsQuery.next()) {
            if (columnsQuery.value(1).toString() ==
                QStringLiteral("canonical_path")) {
                hasLegacyCanonicalPath = true;
                break;
            }
        }
        columnsQuery.finish();
        if (hasLegacyCanonicalPath) {
            if (!migrateSchemaFromVersionZero(error)) {
                return false;
            }
        }
        if (!createSchema(error)) {
            return false;
        }
    }

    if (!ensureChunkerPolicyColumn(error)) {
        return false;
    }

    QSqlQuery setVersion(m_database);
    if (!setVersion.exec(
            QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        setError(error, databaseError(setVersion));
        return false;
    }
    return true;
}

bool RagIndexStore::deleteContentForHash(const QString& documentHash,
                                         QString* error) {
    const QStringList statements = {
        QStringLiteral("DELETE FROM chunk_vectors WHERE document_hash = ?"),
        QStringLiteral("DELETE FROM chunks_fts WHERE document_hash = ?"),
        QStringLiteral("DELETE FROM chunks WHERE document_hash = ?"),
        QStringLiteral("DELETE FROM document_paths WHERE document_hash = ?"),
        QStringLiteral("DELETE FROM documents WHERE document_hash = ?")};
    for (const QString& sql : statements) {
        QSqlQuery query(m_database);
        query.prepare(sql);
        query.addBindValue(documentHash);
        if (!query.exec()) {
            setError(error, databaseError(query));
            return false;
        }
    }
    return true;
}

bool RagIndexStore::ensureChunkerPolicyColumn(QString* error) {
    QSqlQuery columns(m_database);
    if (!columns.exec(QStringLiteral("PRAGMA table_info(documents)"))) {
        setError(error, databaseError(columns));
        return false;
    }
    bool hasColumn = false;
    while (columns.next()) {
        if (columns.value(1).toString() == QStringLiteral("chunker_policy")) {
            hasColumn = true;
            break;
        }
    }
    columns.finish();
    if (hasColumn) {
        return true;
    }
    // Version 1 databases predate the policy column. Indexes written before the
    // column existed cannot prove which strategy produced their chunks, so the
    // empty default never matches and the document is rebuilt once.
    QSqlQuery alter(m_database);
    if (!alter.exec(QStringLiteral(
            "ALTER TABLE documents ADD COLUMN chunker_policy TEXT NOT NULL "
            "DEFAULT ''"))) {
        setError(error, databaseError(alter));
        return false;
    }
    return true;
}

bool RagIndexStore::replaceDocument(const DocumentKey& document,
                                    const QVector<PdfChunk>& chunks,
                                    QString* error,
                                    const RagCancellationToken* cancellation) {
    // An explicit empty string, not a null QString: the column is NOT NULL and
    // Qt binds a null QString as SQL NULL.
    return replaceDocument(document, chunks, QStringLiteral(""), error,
                           cancellation);
}

bool RagIndexStore::replaceDocument(const DocumentKey& document,
                                    const QVector<PdfChunk>& chunks,
                                    const QString& chunkerPolicy,
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
    if (document.canonicalPath.isEmpty()) {
        setError(error, QStringLiteral("Document canonical path is empty"));
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

    QStringList oldHashes;
    {
        QSqlQuery query(m_database);
        query.prepare(
            QStringLiteral("SELECT document_hash FROM document_paths WHERE "
                           "canonical_path = ?"));
        query.addBindValue(document.canonicalPath);
        if (!query.exec()) {
            return rollback(databaseError(query));
        }
        while (query.next()) {
            oldHashes.append(query.value(0).toString());
        }
        query.finish();
    }

    for (const QString& oldHash : oldHashes) {
        if (oldHash == document.contentHash) {
            continue;
        }
        QSqlQuery removePath(m_database);
        removePath.prepare(
            QStringLiteral("DELETE FROM document_paths WHERE document_hash = ? "
                           "AND canonical_path = ?"));
        removePath.addBindValue(oldHash);
        removePath.addBindValue(document.canonicalPath);
        if (!removePath.exec()) {
            return rollback(databaseError(removePath));
        }

        QSqlQuery remainingPaths(m_database);
        remainingPaths.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM document_paths WHERE document_hash = ?"));
        remainingPaths.addBindValue(oldHash);
        if (!remainingPaths.exec() || !remainingPaths.next()) {
            return rollback(databaseError(remainingPaths));
        }
        const int remainingPathCount = remainingPaths.value(0).toInt();
        remainingPaths.finish();
        if (remainingPathCount == 0 && !deleteContentForHash(oldHash, error)) {
            return rollback(error ? *error
                                  : QStringLiteral("Cannot delete old index"));
        }
    }

    // Replace the content for the new hash while preserving mappings for
    // other paths that point at the same content.
    const QStringList clearStatements = {
        QStringLiteral("DELETE FROM chunk_vectors WHERE document_hash = ?"),
        QStringLiteral("DELETE FROM chunks_fts WHERE document_hash = ?"),
        QStringLiteral("DELETE FROM chunks WHERE document_hash = ?")};
    for (const QString& sql : clearStatements) {
        QSqlQuery query(m_database);
        query.prepare(sql);
        query.addBindValue(document.contentHash);
        if (!query.exec()) {
            return rollback(databaseError(query));
        }
    }

    const QString modifiedTime =
        document.modifiedTime.isValid()
            ? document.modifiedTime.toString(Qt::ISODate)
            : QStringLiteral("");

    QSqlQuery insertDocument(m_database);
    insertDocument.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO documents(document_hash, file_size, "
        "modified_time, page_count, chunker_policy) VALUES(?, ?, ?, ?, ?)"));
    insertDocument.addBindValue(document.contentHash);
    insertDocument.addBindValue(document.fileSize);
    insertDocument.addBindValue(modifiedTime);
    insertDocument.addBindValue(document.pageCount);
    insertDocument.addBindValue(chunkerPolicy);
    if (!insertDocument.exec()) {
        return rollback(databaseError(insertDocument));
    }

    QSqlQuery updateDocument(m_database);
    updateDocument.prepare(
        QStringLiteral("UPDATE documents SET file_size = ?, modified_time = ?, "
                       "page_count = ?, chunker_policy = ? "
                       "WHERE document_hash = ?"));
    updateDocument.addBindValue(document.fileSize);
    updateDocument.addBindValue(modifiedTime);
    updateDocument.addBindValue(document.pageCount);
    updateDocument.addBindValue(chunkerPolicy);
    updateDocument.addBindValue(document.contentHash);
    if (!updateDocument.exec()) {
        return rollback(databaseError(updateDocument));
    }

    QSqlQuery insertPath(m_database);
    insertPath.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO document_paths(document_hash, canonical_path) "
        "VALUES(?, ?)"));
    insertPath.addBindValue(document.contentHash);
    insertPath.addBindValue(document.canonicalPath);
    if (!insertPath.exec()) {
        return rollback(databaseError(insertPath));
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

bool RagIndexStore::registerDocumentPath(const DocumentKey& document,
                                         QString* error) {
    setError(error, QString());
    if (!isOpen()) {
        setError(error, QStringLiteral("Index store is not open"));
        return false;
    }
    if (document.contentHash.isEmpty() || document.canonicalPath.isEmpty()) {
        setError(error, QStringLiteral("Document identity is incomplete"));
        return false;
    }

    QMutexLocker writeLock(&writeMutex());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO document_paths(document_hash, canonical_path) "
        "VALUES(?, ?)"));
    query.addBindValue(document.contentHash);
    query.addBindValue(document.canonicalPath);
    if (!query.exec()) {
        setError(error, databaseError(query));
        return false;
    }
    return true;
}

bool RagIndexStore::clearAll(QString* error) {
    setError(error, QString());
    if (!isOpen()) {
        setError(error, QStringLiteral("Index store is not open"));
        return false;
    }

    QMutexLocker writeLock(&writeMutex());

    QSqlQuery secureDelete(m_database);
    if (!secureDelete.exec(QStringLiteral("PRAGMA secure_delete = ON"))) {
        setError(error, databaseError(secureDelete));
        return false;
    }

    if (!m_database.transaction()) {
        setError(error, databaseError(m_database));
        QSqlQuery disableSecureDelete(m_database);
        disableSecureDelete.exec(QStringLiteral("PRAGMA secure_delete = OFF"));
        return false;
    }

    const QStringList statements = {
        QStringLiteral("DELETE FROM chunk_vectors"),
        QStringLiteral("DELETE FROM chunks_fts"),
        QStringLiteral("DELETE FROM chunks"),
        QStringLiteral("DELETE FROM document_paths"),
        QStringLiteral("DELETE FROM documents")};
    for (const QString& sql : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(sql)) {
            m_database.rollback();
            QSqlQuery disableSecureDelete(m_database);
            disableSecureDelete.exec(
                QStringLiteral("PRAGMA secure_delete = OFF"));
            setError(error, databaseError(query));
            return false;
        }
    }
    if (!m_database.commit()) {
        setError(error, databaseError(m_database));
        m_database.rollback();
        QSqlQuery disableSecureDelete(m_database);
        disableSecureDelete.exec(QStringLiteral("PRAGMA secure_delete = OFF"));
        return false;
    }

    // VACUUM must run outside a transaction. Content is already zeroed by
    // secure_delete, so a busy VACUUM is only a warning.
    QSqlQuery vacuum(m_database);
    if (!vacuum.exec(QStringLiteral("VACUUM"))) {
        qWarning() << "RAG: VACUUM after clear failed:"
                   << databaseError(vacuum);
    }
    QSqlQuery disableSecureDelete(m_database);
    if (!disableSecureDelete.exec(
            QStringLiteral("PRAGMA secure_delete = OFF"))) {
        qWarning() << "RAG: cannot disable secure_delete:"
                   << databaseError(disableSecureDelete);
    }
    return true;
}

bool RagIndexStore::containsDocument(const DocumentKey& document,
                                     QString* error) const {
    setError(error, QString());
    if (!isOpen()) {
        setError(error, QStringLiteral("Index store is not open"));
        return false;
    }
    if (document.contentHash.isEmpty()) {
        setError(error, QStringLiteral("Document content hash is empty"));
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM documents d "
        "JOIN document_paths p ON p.document_hash = d.document_hash "
        "WHERE d.document_hash = ? LIMIT 1"));
    query.addBindValue(document.contentHash);
    if (!query.exec()) {
        setError(error, databaseError(query));
        return false;
    }
    return query.next();
}

bool RagIndexStore::documentMatchesChunkerPolicy(const DocumentKey& document,
                                                 const QString& policy,
                                                 QString* error) const {
    setError(error, QString());
    if (!isOpen()) {
        setError(error, QStringLiteral("Index store is not open"));
        return false;
    }
    if (document.contentHash.isEmpty()) {
        setError(error, QStringLiteral("Document content hash is empty"));
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT chunker_policy FROM documents d "
        "JOIN document_paths p ON p.document_hash = d.document_hash "
        "WHERE d.document_hash = ? LIMIT 1"));
    query.addBindValue(document.contentHash);
    if (!query.exec()) {
        setError(error, databaseError(query));
        return false;
    }
    if (!query.next()) {
        return false;
    }
    return query.value(0).toString() == policy;
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
        // SQLite FTS5 returns a negative bm25 value where more negative is
        // more relevant. Convert it to a monotonic score in [0, 1).
        const double relevance = qMax(0.0, -query.value(7).toDouble());
        evidence.lexicalScore = relevance / (1.0 + relevance);
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

QVector<RagEvidence> RagIndexStore::adjacentChunks(const DocumentKey& document,
                                                   const RagEvidence& anchor,
                                                   int radius,
                                                   QString* error) const {
    setError(error, {});
    QVector<RagEvidence> result;
    if (radius <= 0 || anchor.chunkId.isEmpty()) {
        return result;
    }
    const int page = anchor.pageStart;
    const int charStart =
        anchor.locations.isEmpty() ? 0 : anchor.locations.first().charStart;

    auto read = [this, &document](QSqlQuery& query, QVector<RagEvidence>& out) {
        while (query.next()) {
            RagEvidence item;
            item.chunkId = query.value(0).toString();
            item.documentHash = document.contentHash;
            item.pageStart = query.value(1).toInt();
            item.pageEnd = query.value(2).toInt();
            item.sectionPath = query.value(3).toString();
            item.text = query.value(4).toString();
            item.locations = locationsFromJson(query.value(5).toString());
            out.append(std::move(item));
        }
    };

    QSqlQuery previous(m_database);
    previous.prepare(QStringLiteral(
        "SELECT chunk_id, page_start, page_end, section_path, text, "
        "locations_json, char_start FROM chunks WHERE document_hash = ? "
        "AND (page_start < ? OR (page_start = ? AND char_start < ?)) "
        "ORDER BY page_start DESC, char_start DESC LIMIT ?"));
    previous.addBindValue(document.contentHash);
    previous.addBindValue(page);
    previous.addBindValue(page);
    previous.addBindValue(charStart);
    previous.addBindValue(radius);
    if (!previous.exec()) {
        setError(error, databaseError(previous));
        return {};
    }
    QVector<RagEvidence> before;
    read(previous, before);
    for (int i = before.size() - 1; i >= 0; --i)
        result.append(before.at(i));

    QSqlQuery next(m_database);
    next.prepare(QStringLiteral(
        "SELECT chunk_id, page_start, page_end, section_path, text, "
        "locations_json, char_start FROM chunks WHERE document_hash = ? "
        "AND (page_start > ? OR (page_start = ? AND char_start > ?)) "
        "ORDER BY page_start ASC, char_start ASC LIMIT ?"));
    next.addBindValue(document.contentHash);
    next.addBindValue(page);
    next.addBindValue(page);
    next.addBindValue(charStart);
    next.addBindValue(radius);
    if (!next.exec()) {
        setError(error, databaseError(next));
        return {};
    }
    read(next, result);
    return result;
}

QString RagIndexStore::ftsQueryFor(const QString& query) {
    QStringList quotedTerms;
    for (const QString& term : queryTermsFor(query)) {
        quotedTerms.append(quoteFtsTerm(term));
    }
    return quotedTerms.join(QStringLiteral(" OR "));
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
