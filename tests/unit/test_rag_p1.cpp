#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtConcurrent>
#include <QtTest/QtTest>

#include "rag/domain/RagTypes.h"
#include "rag/extract/PdfChunker.h"
#include "rag/index/RagIndexStore.h"
#include "rag/retrieve/LexicalRetriever.h"

using namespace Rag;

class TestRagP1 : public QObject {
    Q_OBJECT

private slots:
    void testLexicalRetrieverUsesPersistentIndex();
    void testIndexCancellationDoesNotCommit();
    void testReplacingPathRemovesOldDocumentVersion();
    void testSharedContentKeepsOtherPathIndex();
    void testSchemaMigratesLegacyDocuments();
    void testChunkerPolicyInvalidatesIndex();
    void testIndexFileIsOwnerOnly();
    void testClearAllRemovesTextBytes();
    void testConcurrentWritersAreSerialized();
};

void TestRagP1::testLexicalRetrieverUsesPersistentIndex() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString databasePath =
        directory.filePath(QStringLiteral("rag.sqlite"));
    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-p1.pdf");
    document.contentHash = QStringLiteral("rag-p1-document");
    document.pageCount = 1;

    PdfChunk chunk;
    chunk.id = QStringLiteral("rag-p1-chunk");
    chunk.document = document;
    chunk.pageStart = 0;
    chunk.pageEnd = 0;
    chunk.text = QStringLiteral("Persistent lexical evidence");
    chunk.chunkHash = QStringLiteral("rag-p1-chunk-hash");
    chunk.locations = {
        {0, 0, static_cast<int>(chunk.text.size()), {QRectF(10, 20, 100, 12)}}};

    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(databasePath, &error), qPrintable(error));
    QVERIFY2(store.replaceDocument(document, {chunk}, &error),
             qPrintable(error));
    QCOMPARE(store.documentChunkCount(document, &error), 1);

    const LexicalRetriever retriever(databasePath);
    const QVector<RagEvidence> evidence =
        retriever.retrieve(document, QStringLiteral("lexical"), 5, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(evidence.size(), 1);
    QCOMPARE(evidence.first().chunkId, chunk.id);
    QCOMPARE(evidence.first().locations.first().boundingRects.first(),
             QRectF(10, 20, 100, 12));
}

void TestRagP1::testIndexCancellationDoesNotCommit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString databasePath =
        directory.filePath(QStringLiteral("rag.sqlite"));
    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-p1-cancel.pdf");
    document.contentHash = QStringLiteral("rag-p1-cancel-document");

    PdfChunk chunk;
    chunk.id = QStringLiteral("rag-p1-cancel-chunk");
    chunk.document = document;
    chunk.pageStart = 0;
    chunk.pageEnd = 0;
    chunk.text = QStringLiteral("Cancelled evidence");
    chunk.chunkHash = QStringLiteral("rag-p1-cancel-hash");

    RagCancellationToken cancellation;
    cancellation.cancel();
    RagIndexStore store;
    QString error;
    QVERIFY(store.open(databasePath, &error));
    QVERIFY(!store.replaceDocument(document, {chunk}, &error, &cancellation));
    QCOMPARE(error, QStringLiteral("Indexing cancelled"));
    QVERIFY(!store.containsDocument(document));
}

void TestRagP1::testReplacingPathRemovesOldDocumentVersion() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString databasePath =
        directory.filePath(QStringLiteral("rag.sqlite"));
    DocumentKey oldDocument;
    oldDocument.canonicalPath = QStringLiteral("/tmp/versioned.pdf");
    oldDocument.contentHash = QStringLiteral("old-hash");
    DocumentKey newDocument = oldDocument;
    newDocument.contentHash = QStringLiteral("new-hash");

    auto makeChunk = [](const DocumentKey& document, const QString& id) {
        PdfChunk chunk;
        chunk.id = id;
        chunk.document = document;
        chunk.pageStart = 0;
        chunk.pageEnd = 0;
        chunk.text = id + QStringLiteral(" evidence");
        chunk.chunkHash = id + QStringLiteral("-hash");
        return chunk;
    };

    RagIndexStore store;
    QString error;
    QVERIFY(store.open(databasePath, &error));
    QVERIFY(store.replaceDocument(
        oldDocument, {makeChunk(oldDocument, "old-chunk")}, &error));
    QVERIFY(store.replaceDocument(
        newDocument, {makeChunk(newDocument, "new-chunk")}, &error));
    QVERIFY(!store.containsDocument(oldDocument));
    QVERIFY(store.containsDocument(newDocument));
}

void TestRagP1::testSharedContentKeepsOtherPathIndex() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        directory.filePath(QStringLiteral("rag.sqlite"));

    auto makeChunk = [](const DocumentKey& document, const QString& id) {
        PdfChunk chunk;
        chunk.id = id;
        chunk.document = document;
        chunk.pageStart = 0;
        chunk.pageEnd = 0;
        chunk.text = id + QStringLiteral(" evidence");
        chunk.chunkHash = id + QStringLiteral("-hash");
        return chunk;
    };

    DocumentKey first;
    first.canonicalPath = directory.filePath(QStringLiteral("first.pdf"));
    first.contentHash = QStringLiteral("shared-hash");
    DocumentKey second = first;
    second.canonicalPath = directory.filePath(QStringLiteral("second.pdf"));

    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(databasePath, &error), qPrintable(error));
    QVERIFY2(
        store.replaceDocument(
            first, {makeChunk(first, QStringLiteral("first-chunk"))}, &error),
        qPrintable(error));
    // This is the cache-hit path used when a byte-identical copy is opened.
    QVERIFY2(store.registerDocumentPath(second, &error), qPrintable(error));
    QCOMPARE(store.documentChunkCount(second, &error), 1);

    DocumentKey changed = first;
    changed.contentHash = QStringLiteral("changed-hash");
    QVERIFY2(store.replaceDocument(
                 changed, {makeChunk(changed, QStringLiteral("changed-chunk"))},
                 &error),
             qPrintable(error));

    QCOMPARE(store.documentChunkCount(second, &error), 1);
    QCOMPARE(store.search(second, QStringLiteral("evidence"), 5, &error).size(),
             1);
    QVERIFY(store.containsDocument(second));
    QVERIFY(store.containsDocument(changed));

    QVERIFY2(store.clearAll(&error), qPrintable(error));
    QVERIFY(!store.containsDocument(second));
    QVERIFY(!store.containsDocument(changed));
    QCOMPARE(store.documentChunkCount(second, &error), 0);
}

void TestRagP1::testSchemaMigratesLegacyDocuments() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        directory.filePath(QStringLiteral("legacy.sqlite"));

    const QString connectionName = QStringLiteral("legacy-schema-test");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(
            query.exec(QStringLiteral("CREATE TABLE documents ("
                                      "document_hash TEXT PRIMARY KEY,"
                                      "canonical_path TEXT NOT NULL,"
                                      "file_size INTEGER NOT NULL,"
                                      "modified_time TEXT NOT NULL,"
                                      "page_count INTEGER NOT NULL"
                                      ")")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO documents VALUES('legacy-hash', '/tmp/legacy.pdf', "
            "1, '2024-01-01T00:00:00', 1)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE chunks ("
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
            ")")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO chunks VALUES('legacy-chunk', 'legacy-hash', 0, 0, "
            "'', 'legacy evidence', 'legacy-chunk-hash', 0, 15, '[]')")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/legacy.pdf");
    document.contentHash = QStringLiteral("legacy-hash");
    document.pageCount = 1;

    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(databasePath, &error), qPrintable(error));
    QVERIFY(store.containsDocument(document));

    RagIndexStore check;
    QVERIFY2(check.open(databasePath, &error), qPrintable(error));
    // The public store API is the contract; the path mapping is exercised by
    // clearAll below.
    QCOMPARE(check.documentChunkCount(document, &error), 1);
    // A migrated index has no recorded strategy, so it never matches and the
    // document is rebuilt once with the current chunker.
    QVERIFY(!check.documentMatchesChunkerPolicy(
        document, PdfChunker::policyId(), &error));
    QVERIFY(check.clearAll(&error));
    QVERIFY(!check.containsDocument(document));
}

void TestRagP1::testChunkerPolicyInvalidatesIndex() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        directory.filePath(QStringLiteral("policy.sqlite"));

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/policy.pdf");
    document.contentHash = QStringLiteral("policy-document");
    document.pageCount = 1;

    PdfChunk chunk;
    chunk.id = QStringLiteral("policy-chunk");
    chunk.document = document;
    chunk.text = QStringLiteral("Policy evidence");
    chunk.chunkHash = QStringLiteral("policy-chunk-hash");

    PdfChunkerOptions other;
    other.overlapCharacters = 0;
    const QString current = PdfChunker::policyId();
    const QString changed = PdfChunker::policyId(other);
    QVERIFY(current != changed);

    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(databasePath, &error), qPrintable(error));
    QVERIFY2(store.replaceDocument(document, {chunk}, current, &error),
             qPrintable(error));
    QVERIFY(store.documentMatchesChunkerPolicy(document, current, &error));
    QVERIFY(!store.documentMatchesChunkerPolicy(document, changed, &error));

    // Re-indexing with another strategy replaces the recorded policy.
    QVERIFY2(store.replaceDocument(document, {chunk}, changed, &error),
             qPrintable(error));
    QVERIFY(store.documentMatchesChunkerPolicy(document, changed, &error));
    QVERIFY(!store.documentMatchesChunkerPolicy(document, current, &error));
    QCOMPARE(store.documentChunkCount(document, &error), 1);

    // Indexes written without a policy never claim to match a strategy.
    QVERIFY2(store.replaceDocument(document, {chunk}, &error),
             qPrintable(error));
    QVERIFY(!store.documentMatchesChunkerPolicy(document, current, &error));
}

void TestRagP1::testIndexFileIsOwnerOnly() {
#ifdef Q_OS_WIN
    QSKIP("POSIX permissions are not available on Windows");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        directory.filePath(QStringLiteral("permissions.sqlite"));

    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(databasePath, &error), qPrintable(error));

    const auto permissions = QFileInfo(databasePath).permissions();
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteOther));
#endif
}

void TestRagP1::testClearAllRemovesTextBytes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        directory.filePath(QStringLiteral("secure-clear.sqlite"));

    const QString marker = QStringLiteral("SECURE_DELETE_MARKER_9f4a");
    DocumentKey document;
    document.canonicalPath = directory.filePath(QStringLiteral("secure.pdf"));
    document.contentHash = QStringLiteral("secure-hash");

    PdfChunk chunk;
    chunk.id = QStringLiteral("secure-chunk");
    chunk.document = document;
    chunk.pageStart = 0;
    chunk.pageEnd = 0;
    chunk.text = marker;
    chunk.chunkHash = QStringLiteral("secure-chunk-hash");

    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(databasePath, &error), qPrintable(error));
    QVERIFY2(store.replaceDocument(document, {chunk}, &error),
             qPrintable(error));
    QFile databaseFile(databasePath);
    QVERIFY(databaseFile.open(QIODevice::ReadOnly));
    QVERIFY(databaseFile.readAll().contains(marker.toUtf8()));
    databaseFile.close();

    QVERIFY2(store.clearAll(&error), qPrintable(error));
    store.close();

    QVERIFY(databaseFile.open(QIODevice::ReadOnly));
    QVERIFY(!databaseFile.readAll().contains(marker.toUtf8()));
}

void TestRagP1::testConcurrentWritersAreSerialized() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        directory.filePath(QStringLiteral("rag.sqlite"));

    auto writeDocument = [databasePath](const QString& id) {
        RagIndexStore store;
        QString error;
        if (!store.open(databasePath, &error)) {
            return error;
        }
        DocumentKey document;
        document.canonicalPath = QStringLiteral("/tmp/%1.pdf").arg(id);
        document.contentHash = id;
        QVector<PdfChunk> chunks;
        chunks.reserve(100);
        for (int index = 0; index < 100; ++index) {
            PdfChunk chunk;
            chunk.id = QStringLiteral("%1-%2").arg(id).arg(index);
            chunk.document = document;
            chunk.pageStart = index;
            chunk.pageEnd = index;
            chunk.text = QStringLiteral("concurrent evidence %1").arg(index);
            chunk.chunkHash = chunk.id + QStringLiteral("-hash");
            chunks.append(std::move(chunk));
        }
        return store.replaceDocument(document, chunks, &error) ? QString()
                                                               : error;
    };

    QFuture<QString> first =
        QtConcurrent::run(writeDocument, QStringLiteral("document-a"));
    QFuture<QString> second =
        QtConcurrent::run(writeDocument, QStringLiteral("document-b"));
    first.waitForFinished();
    second.waitForFinished();
    QVERIFY2(first.result().isEmpty(), qPrintable(first.result()));
    QVERIFY2(second.result().isEmpty(), qPrintable(second.result()));
}

QTEST_MAIN(TestRagP1)
#include "test_rag_p1.moc"
