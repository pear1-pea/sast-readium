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

PdfChunk makeChunk(const DocumentKey& document, const QString& id,
                   const QString& text, int pageIndex = 0) {
    PdfChunk chunk;
    chunk.id = id;
    chunk.document = document;
    chunk.pageStart = pageIndex;
    chunk.pageEnd = pageIndex;
    chunk.text = text;
    chunk.chunkHash = id + QStringLiteral("-hash");
    chunk.locations = {{pageIndex,
                        0,
                        static_cast<int>(text.size()),
                        {QRectF(10, 20, 100, 12)}}};
    return chunk;
}

}  // namespace

class TestRagP0 : public QObject {
    Q_OBJECT

private slots:
    void testPdfTextExtraction();
    void testExtractionCancellation();
    void testChunkingPreservesLocations();
    void testChunkerCoversTextAtEveryConfiguration();
    void testSentenceAwareOverlapStartsCompleteSentences();
    void testChunkerPropagatesSectionPath();
    void testFtsIndexAndEvidenceRoundTrip();
    void testNaturalLanguageQueries();
    void testCjkStopwordsDoNotSplitWords();
    void testBm25ScoresAreMonotonic();
    void testExtractionFailurePreservesFileIdentity();
    void documentStatsAndPathRemoval();
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

void TestRagP0::testChunkerCoversTextAtEveryConfiguration() {
    QString text;
    for (int i = 0; i < 12; ++i) {
        text +=
            QStringLiteral(
                "Sentence %1 states a complete condition with enough words.")
                .arg(i);
        text += QLatin1Char('\n');
    }
    text += QStringLiteral(
        "\nOverlong paragraph without blank lines carries an exception "
        "that must stay searchable. ");
    text += text.right(120);
    const QString nextText = QStringLiteral("Next page premise.");
    const PdfPageText page = makePage(2, text);
    const PdfPageText next = makePage(3, nextText);

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-p0-coverage.pdf");
    document.contentHash = QStringLiteral("coverage-hash");
    document.pageCount = 4;

    RagJobContext context;
    const QVector<QPair<QString, PdfChunkerOptions>> configurations{
        {QStringLiteral("default"), {800, 1200, 120, false}},
        {QStringLiteral("no-overlap"), {800, 1200, 0, false}},
        {QStringLiteral("sentence"), {800, 1200, 120, true}},
        {QStringLiteral("smaller"), {180, 260, 120, false}}};
    QSet<QString> policies;
    for (const auto& configuration : configurations) {
        const auto& options = configuration.second;
        const QVector<PdfChunk> chunks =
            PdfChunker::chunk({page, next}, document, context, options);
        QVERIFY(!chunks.isEmpty());
        policies.insert(PdfChunker::policyId(options));

        QSet<int> covered;
        bool nextPageCovered = false;
        for (const PdfChunk& chunk : chunks) {
            // Boundaries never cross pages.
            QCOMPARE(chunk.pageStart, chunk.pageEnd);
            QVERIFY(chunk.text.size() <= options.maxCharacters);
            const QString& source = chunk.pageStart == 2 ? text : nextText;
            QCOMPARE(chunk.text, source.mid(chunk.charStart, chunk.charLength));
            for (const auto& location : chunk.locations) {
                QCOMPARE(location.pageIndex, chunk.pageStart);
                QCOMPARE(location.charStart, chunk.charStart);
                QCOMPARE(location.charLength, chunk.charLength);
            }
            if (chunk.pageStart == 2) {
                for (int i = chunk.charStart;
                     i < chunk.charStart + chunk.charLength; ++i) {
                    covered.insert(i);
                }
            } else {
                nextPageCovered = true;
            }
        }
        for (int i = 0; i < text.size(); ++i) {
            if (!text.at(i).isSpace())
                QVERIFY2(covered.contains(i), qPrintable(configuration.first));
        }
        QVERIFY(nextPageCovered);
    }
    // Every strategy has its own identity, so changing one invalidates old
    // chunks instead of mixing strategies in a single index.
    QCOMPARE(policies.size(), configurations.size());
}

void TestRagP0::testSentenceAwareOverlapStartsCompleteSentences() {
    QString text;
    for (int i = 0; i < 20; ++i) {
        text +=
            QStringLiteral("Sentence number %1 has exactly enough words here.")
                .arg(i);
        text += QLatin1Char(' ');
    }
    const PdfPageText page = makePage(0, text);
    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-p0-sentences.pdf");
    document.contentHash = QStringLiteral("sentence-hash");
    document.pageCount = 1;
    RagJobContext context;

    PdfChunkerOptions options;
    options.targetCharacters = 80;
    options.maxCharacters = 120;
    options.overlapCharacters = 60;

    const QVector<PdfChunk> plain =
        PdfChunker::chunk({page}, document, context, options);
    options.sentenceAwareOverlap = true;
    const QVector<PdfChunk> aware =
        PdfChunker::chunk({page}, document, context, options);
    QVERIFY(aware.size() >= 3);

    auto startsAfterSentence = [&text](const PdfChunk& chunk) {
        int index = chunk.charStart - 1;
        while (index >= 0 && text.at(index).isSpace())
            --index;
        return index < 0 || text.at(index) == QChar('.');
    };
    bool plainCutMidSentence = false;
    for (const PdfChunk& chunk : plain)
        plainCutMidSentence =
            plainCutMidSentence || !startsAfterSentence(chunk);
    QVERIFY(plainCutMidSentence);
    for (const PdfChunk& chunk : aware)
        QVERIFY2(startsAfterSentence(chunk), qPrintable(chunk.text.left(40)));

    // A condition that straddles a cut is still present verbatim in one chunk.
    const QString premise =
        QStringLiteral("Sentence number 5 has exactly enough words here.");
    bool premiseIntact = false;
    for (const PdfChunk& chunk : aware)
        premiseIntact = premiseIntact || chunk.text.contains(premise);
    QVERIFY(premiseIntact);
}

void TestRagP0::testChunkerPropagatesSectionPath() {
    PdfPageText page =
        makePage(1, QStringLiteral("Body text under a heading."));
    page.sectionPath = QStringLiteral("Chapter 2 › Risks");
    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-p0-outline.pdf");
    document.contentHash = QStringLiteral("outline-hash");
    document.pageCount = 2;

    const QVector<PdfChunk> chunks =
        PdfChunker::chunk({page}, document, RagJobContext{});
    QVERIFY(!chunks.isEmpty());
    for (const PdfChunk& chunk : chunks)
        QCOMPARE(chunk.sectionPath, page.sectionPath);
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

void TestRagP0::testNaturalLanguageQueries() {
    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(QString(), &error), qPrintable(error));

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-natural.pdf");
    document.contentHash = QStringLiteral("rag-natural-document");
    document.pageCount = 3;

    const PdfChunk english = makeChunk(
        document, QStringLiteral("english"),
        QStringLiteral(
            "Alpha evidence describes the main conclusion of document one."),
        0);
    const PdfChunk chinese =
        makeChunk(document, QStringLiteral("chinese"),
                  QStringLiteral("本文的主要结论是索引可以提升检索质量。"), 1);
    const PdfChunk noise =
        makeChunk(document, QStringLiteral("noise"),
                  QStringLiteral("Completely unrelated weather notes."), 2);

    QVERIFY2(store.replaceDocument(document, {english, chinese, noise}, &error),
             qPrintable(error));

    const QVector<RagEvidence> englishResults = store.search(
        document, QStringLiteral("What evidence is in document one?"), 5,
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!englishResults.isEmpty());
    QCOMPARE(englishResults.first().chunkId, english.id);

    const QVector<RagEvidence> chineseResults = store.search(
        document, QStringLiteral("这篇文档的主要结论是什么"), 5, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!chineseResults.isEmpty());
    QCOMPARE(chineseResults.first().chunkId, chinese.id);
}

void TestRagP0::testCjkStopwordsDoNotSplitWords() {
    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(QString(), &error), qPrintable(error));

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-cjk-stopwords.pdf");
    document.contentHash = QStringLiteral("rag-cjk-stopwords-document");
    document.pageCount = 1;
    const PdfChunk chunk = makeChunk(document, QStringLiteral("question"),
                                     QStringLiteral("问题需要单独成词处理"), 0);

    QVERIFY2(store.replaceDocument(document, {chunk}, &error),
             qPrintable(error));
    const auto results =
        store.search(document, QStringLiteral("这个问题的答案"), 5, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!results.isEmpty());
    QCOMPARE(results.first().chunkId, chunk.id);
}

void TestRagP0::testBm25ScoresAreMonotonic() {
    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(QString(), &error), qPrintable(error));

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/rag-bm25.pdf");
    document.contentHash = QStringLiteral("rag-bm25-document");
    document.pageCount = 2;

    const PdfChunk stronger = makeChunk(
        document, QStringLiteral("stronger"),
        QStringLiteral("alpha evidence conclusion evidence details"), 0);
    const PdfChunk weaker =
        makeChunk(document, QStringLiteral("weaker"),
                  QStringLiteral("alpha only unrelated notes"), 1);

    QVERIFY2(store.replaceDocument(document, {stronger, weaker}, &error),
             qPrintable(error));

    const QVector<RagEvidence> results =
        store.search(document, QStringLiteral("alpha evidence"), 5, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().chunkId, stronger.id);

    double strongerScore = -1.0;
    double weakerScore = -1.0;
    for (const RagEvidence& item : results) {
        if (item.chunkId == stronger.id)
            strongerScore = item.lexicalScore;
        if (item.chunkId == weaker.id)
            weakerScore = item.lexicalScore;
    }
    QVERIFY(strongerScore > weakerScore);
    QVERIFY(strongerScore < 1.0);
    QVERIFY(weakerScore >= 0.0);
}

void TestRagP0::testExtractionFailurePreservesFileIdentity() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("this is not a pdf") > 0);
    file.close();

    RagJobContext context;
    context.jobId = QStringLiteral("broken-pdf");
    const PdfTextExtractionResult result =
        PdfTextExtractor::extractFile(path, context);

    QVERIFY(!result.success);
    QCOMPARE(result.reason, RagIndexFailureReason::Corrupt);
    QVERIFY(!result.document.canonicalPath.isEmpty());
    QVERIFY(!result.document.contentHash.isEmpty());
    QVERIFY(!result.error.isEmpty());

    const PdfTextExtractionResult missing = PdfTextExtractor::extractFile(
        directory.filePath("missing.pdf"), context);
    QVERIFY(!missing.success);
    QCOMPARE(missing.reason, RagIndexFailureReason::FileMissing);
}

void TestRagP0::documentStatsAndPathRemoval() {
    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(QString(), &error), qPrintable(error));

    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/stats-a.pdf");
    document.contentHash = QStringLiteral("stats-hash");
    document.pageCount = 2;
    const QVector<PdfChunk> chunks = {
        makeChunk(document, QStringLiteral("stats-a"),
                  QStringLiteral("alpha text for stats"), 0),
        makeChunk(document, QStringLiteral("stats-b"),
                  QStringLiteral("beta text for stats"), 1)};
    QVERIFY2(store.replaceDocument(document, chunks, &error),
             qPrintable(error));

    RagDocumentIndexStats stats;
    QVERIFY2(store.documentStats(document, &stats, &error), qPrintable(error));
    QCOMPARE(stats.chunkCount, 2);
    QVERIFY(stats.present);
    QVERIFY(stats.textBytes > 0);
    QCOMPARE(store.documentCount(&error), 1);
    QCOMPARE(store.vectorCount(document, QStringLiteral("m"),
                               QStringLiteral("1"), &error),
             0);

    RagIndexStore::VectorChunk vector;
    vector.chunkId = chunks.first().id;
    vector.chunkHash = chunks.first().chunkHash;
    vector.text = chunks.first().text;
    vector.vector = {1.0f, 0.0f};
    QVERIFY2(store.storeVectors(document, QStringLiteral("m"),
                                QStringLiteral("1"), {vector}, {}, &error),
             qPrintable(error));
    QCOMPARE(store.vectorCount(document, QStringLiteral("m"),
                               QStringLiteral("1"), &error),
             1);
    QVERIFY2(store.documentStats(document, &stats, &error), qPrintable(error));
    QCOMPARE(stats.vectorCount, 1);
    QVERIFY(stats.vectorBytes > 0);

    DocumentKey second = document;
    second.canonicalPath = QStringLiteral("/tmp/stats-b.pdf");
    QVERIFY2(store.registerDocumentPath(second, &error), qPrintable(error));
    QVERIFY2(store.removeDocumentPath(second.canonicalPath, &error),
             qPrintable(error));
    // The first path still references the content.
    QVERIFY(store.containsDocument(document, &error));
    QVERIFY2(store.removeDocumentPath(document.canonicalPath, &error),
             qPrintable(error));
    QVERIFY(!store.containsDocument(document, &error));
    QCOMPARE(store.documentCount(&error), 0);
}

QTEST_MAIN(TestRagP0)
#include "test_rag_p0.moc"
