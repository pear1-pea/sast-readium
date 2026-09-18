#include <QTemporaryDir>
#include <QtConcurrent>
#include <QtTest/QtTest>

#include "rag/domain/RagTypes.h"
#include "rag/index/RagIndexStore.h"
#include "rag/retrieve/LexicalRetriever.h"

using namespace Rag;

class TestRagP1 : public QObject {
    Q_OBJECT

private slots:
    void testLexicalRetrieverUsesPersistentIndex();
    void testIndexCancellationDoesNotCommit();
    void testReplacingPathRemovesOldDocumentVersion();
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
