#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest/QtTest>

#include "rag/domain/RagTypes.h"
#include "rag/extract/PdfChunker.h"
#include "rag/extract/PdfTextExtractor.h"
#include "rag/index/RagIndexStore.h"

using namespace Rag;

namespace {

QByteArray buildTextPdf() {
    QByteArray data("%PDF-1.4\n");
    QVector<int> offsets(6, 0);

    auto appendObject = [&data, &offsets](int id, const QByteArray& body) {
        offsets[id] = data.size();
        data += QByteArray::number(id);
        data += " 0 obj\n";
        data += body;
        data += "\nendobj\n";
    };

    const QByteArray stream(
        "BT\n/F1 14 Tf\n72 720 Td\n"
        "(First paragraph for RAG.) Tj\n"
        "0 -24 Td\n(Second paragraph with evidence.) Tj\nET\n");

    appendObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    appendObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    appendObject(3,
                 "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                 "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    appendObject(4, QByteArray("<< /Length ") +
                        QByteArray::number(stream.size()) + " >>\nstream\n" +
                        stream + "endstream");
    appendObject(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

    const int xrefOffset = data.size();
    data += "xref\n0 6\n0000000000 65535 f \n";
    for (int id = 1; id <= 5; ++id) {
        data += QByteArray::number(offsets[id]).rightJustified(10, '0');
        data += " 00000 n \n";
    }
    data += "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n";
    data += QByteArray::number(xrefOffset);
    data += "\n%%EOF\n";
    return data;
}

QString writeTestPdf(QTemporaryDir& directory) {
    const QString path = directory.filePath(QStringLiteral("rag-p0.pdf"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(buildTextPdf());
    file.close();
    return path;
}

PdfPageText makePage(int pageIndex, const QString& text) {
    PdfPageText page;
    page.pageIndex = pageIndex;
    page.pageSize = QSizeF(612.0, 792.0);
    page.plainText = text;
    page.extractionSucceeded = true;

    PdfTextBox box;
    box.text = text;
    box.boundingRect = QRectF(72.0, 72.0, 300.0, 24.0);
    box.charStart = 0;
    box.charLength = text.size();
    page.textBoxes.append(box);
    return page;
}

}  // namespace

class TestRagP0 : public QObject {
    Q_OBJECT

private slots:
    void testPdfTextExtraction();
    void testExtractionCancellation();
    void testChunkingPreservesLocations();
    void testFtsIndexAndEvidenceRoundTrip();
};

void TestRagP0::testPdfTextExtraction() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeTestPdf(directory);
    QVERIFY(!path.isEmpty());

    RagJobContext context;
    context.jobId = QStringLiteral("extract-test");
    const PdfTextExtractionResult result =
        PdfTextExtractor::extractFile(path, context);

    QVERIFY2(result.success, qPrintable(result.error));
    QVERIFY(result.hasText);
    QVERIFY(result.document.hasContentHash());
    QCOMPARE(result.document.pageCount, 1);
    QCOMPARE(result.pages.size(), 1);
    QVERIFY(result.pages.first().plainText.contains(QStringLiteral("evidence"),
                                                    Qt::CaseInsensitive));
    QVERIFY(!result.pages.first().textBoxes.isEmpty());
    QVERIFY(result.pages.first().textBoxes.first().charLength > 0);
}

void TestRagP0::testExtractionCancellation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeTestPdf(directory);
    QVERIFY(!path.isEmpty());

    RagJobContext context;
    context.cancellation.cancel();
    const PdfTextExtractionResult result =
        PdfTextExtractor::extractFile(path, context);

    QVERIFY(!result.success);
    QCOMPARE(result.error, QStringLiteral("Extraction cancelled"));
}

void TestRagP0::testChunkingPreservesLocations() {
    const QString text = QStringLiteral(
        "First paragraph has enough text for a chunk with spaces.\n\n"
        "Second paragraph carries evidence for the query.");
    const PdfPageText page = makePage(4, text);

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-p0.pdf");
    document.contentHash = QStringLiteral("document-hash");
    document.pageCount = 5;

    RagJobContext context;
    PdfChunkerOptions options;
    options.targetCharacters = 40;
    options.maxCharacters = 50;
    options.overlapCharacters = 8;

    const QVector<PdfChunk> chunks =
        PdfChunker::chunk({page}, document, context, options);

    QVERIFY(chunks.size() >= 2);
    for (const PdfChunk& chunk : chunks) {
        QCOMPARE(chunk.pageStart, 4);
        QCOMPARE(chunk.pageEnd, 4);
        QVERIFY(!chunk.id.isEmpty());
        QVERIFY(!chunk.chunkHash.isEmpty());
        QCOMPARE(chunk.text, text.mid(chunk.charStart, chunk.charLength));
        QCOMPARE(chunk.locations.size(), 1);
        QCOMPARE(chunk.locations.first().pageIndex, 4);
        QCOMPARE(chunk.locations.first().charStart, chunk.charStart);
        QCOMPARE(chunk.locations.first().charLength, chunk.charLength);
        QVERIFY(!chunk.locations.first().boundingRects.isEmpty());
    }
}

void TestRagP0::testFtsIndexAndEvidenceRoundTrip() {
    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(QString(), &error), qPrintable(error));
    QCOMPARE(error, QString());

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-p0.pdf");
    document.contentHash = QStringLiteral("document-hash");
    document.pageCount = 1;

    PdfChunk first;
    first.id = QStringLiteral("chunk_first");
    first.document = document;
    first.pageStart = 0;
    first.pageEnd = 0;
    first.text = QStringLiteral("The evidence is stored on the first page.");
    first.chunkHash = QStringLiteral("chunk-hash-first");
    first.locations = {
        {0, 0, static_cast<int>(first.text.size()), {QRectF(10, 20, 100, 12)}}};

    PdfChunk second;
    second.id = QStringLiteral("chunk_second");
    second.document = document;
    second.pageStart = 0;
    second.pageEnd = 0;
    second.text = QStringLiteral("A different paragraph without the keyword.");
    second.chunkHash = QStringLiteral("chunk-hash-second");
    second.locations = {{0,
                         0,
                         static_cast<int>(second.text.size()),
                         {QRectF(10, 40, 100, 12)}}};

    PdfChunk chinese;
    chinese.id = QStringLiteral("chunk_chinese");
    chinese.document = document;
    chinese.pageStart = 0;
    chinese.pageEnd = 0;
    chinese.text = QStringLiteral("这是一个中文证据片段，用于验证 FTS5 检索。");
    chinese.chunkHash = QStringLiteral("chunk-hash-chinese");
    chinese.locations = {{0,
                          0,
                          static_cast<int>(chinese.text.size()),
                          {QRectF(10, 60, 100, 12)}}};

    QVERIFY2(store.replaceDocument(document, {first, second, chinese}, &error),
             qPrintable(error));
    QCOMPARE(error, QString());
    QVERIFY(store.containsDocument(document));

    const QVector<RagEvidence> results =
        store.search(document, QStringLiteral("evidence"), 5, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().chunkId, first.id);
    QCOMPARE(results.first().documentHash, document.contentHash);
    QCOMPARE(results.first().locations.size(), 1);
    QCOMPARE(results.first().locations.first().pageIndex, 0);
    QCOMPARE(results.first().locations.first().boundingRects.first(),
             QRectF(10, 20, 100, 12));

    const QVector<RagEvidence> chineseResults =
        store.search(document, QStringLiteral("中文证据"), 5, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(chineseResults.size(), 1);
    QCOMPARE(chineseResults.first().chunkId, chinese.id);

    error = QStringLiteral("stale error");
    const QVector<RagEvidence> emptyResults =
        store.search(document, QStringLiteral(""), 5, &error);
    QVERIFY(emptyResults.isEmpty());
    QCOMPARE(error, QString());
}

QTEST_MAIN(TestRagP0)
#include "test_rag_p0.moc"
