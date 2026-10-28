#include <poppler/qt6/poppler-qt6.h>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTextStream>
#include <QtTest/QtTest>

#include "../../app/ui/continuous/PDFContinuousRenderScheduler.h"

Q_DECLARE_METATYPE(PDFContinuousRenderResult)

static QByteArray buildPdf(int numPages) {
    QByteArray data;
    QTextStream out(&data);
    QVector<int> offsets;

    out << "%PDF-1.4\n";

    offsets << data.size();
    out << "1 0 obj\n"
           "<< /Type /Catalog /Pages 2 0 R >>\n"
           "endobj\n";

    offsets << data.size();
    out << "2 0 obj\n"
           "<< /Type /Pages /Kids [";
    for (int i = 0; i < numPages; ++i) {
        if (i) {
            out << ' ';
        }
        out << (3 + i * 2) << " 0 R";
    }
    out << "] /Count " << numPages
        << " >>\n"
           "endobj\n";

    for (int i = 0; i < numPages; ++i) {
        const int pageObj = 3 + i * 2;
        const int streamObj = 4 + i * 2;

        offsets << data.size();
        out << pageObj
            << " 0 obj\n"
               "<< /Type /Page /Parent 2 0 R"
               " /MediaBox [0 0 612 792]"
               " /Contents "
            << streamObj
            << " 0 R >>\n"
               "endobj\n";

        offsets << data.size();
        out << streamObj
            << " 0 obj\n"
               "<< /Length 3 >>\n"
               "stream\n"
               "q Q\n"
               "endstream\n"
               "endobj\n";
    }

    const int xrefOffset = data.size();
    out << "xref\n0 " << (1 + offsets.size()) << "\n"
        << "0000000000 65535 f \n";
    for (int offset : offsets) {
        out << QString("%1 00000 n \n").arg(offset, 10, 10, QChar('0'));
    }
    out << "trailer\n"
        << "<< /Size " << (1 + offsets.size()) << " /Root 1 0 R >>\n"
        << "startxref\n"
        << xrefOffset << "\n"
        << "%%EOF\n";

    return data;
}

static QString writeTestDocument(const QString& fileName) {
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" +
        fileName;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(buildPdf(1));
    file.close();
    return path;
}

static std::shared_ptr<Poppler::Document> loadTestDocument(
    const QString& fileName) {
    const QString path = writeTestDocument(fileName);
    if (path.isEmpty()) {
        return nullptr;
    }

    return std::shared_ptr<Poppler::Document>(
        Poppler::Document::load(path).release());
}

static PDFContinuousRenderRequest makeRequest(std::uint64_t generation,
                                              std::uint64_t documentToken) {
    PDFContinuousRenderRequest request;
    request.key.pageIndex = 0;
    request.key.documentToken = documentToken;
    request.key.zoom = 1.0;
    request.key.rotation = 0;
    request.key.devicePixelRatio = 1.0;
    request.targetPixelSize = QSize(612, 792);
    request.generation = generation;
    return request;
}

class TestPDFContinuousRenderScheduler : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testRendersFromDocumentSource();
    void testCurrentGenerationRequestRenders();
    void testStaleGenerationRequestIsDropped();
    void testDocumentChangeAllowsSameRenderKeyAgain();
    void testDocumentTokenChangesAcrossDocuments();
};

void TestPDFContinuousRenderScheduler::initTestCase() {
    qRegisterMetaType<PDFContinuousRenderResult>("PDFContinuousRenderResult");
}

void TestPDFContinuousRenderScheduler::testRendersFromDocumentSource() {
    PDFContinuousRenderScheduler scheduler;
    QVector<PDFContinuousRenderResult> results;
    connect(&scheduler, &PDFContinuousRenderScheduler::pageRendered, this,
            [&results](const PDFContinuousRenderResult& result) {
                results.push_back(result);
            });

    const QString path =
        writeTestDocument("continuous_scheduler_source_only.pdf");
    QVERIFY(!path.isEmpty());
    scheduler.setDocumentSource(path, 1);

    const std::uint64_t generation = scheduler.currentGeneration();
    scheduler.requestPage(makeRequest(generation, scheduler.documentToken()));

    QTRY_VERIFY_WITH_TIMEOUT(results.size() == 1, 3000);
    QVERIFY2(results.first().success,
             results.first().error.toUtf8().constData());
    QVERIFY(!results.first().image.isNull());
}

void TestPDFContinuousRenderScheduler::testCurrentGenerationRequestRenders() {
    PDFContinuousRenderScheduler scheduler;
    QVector<PDFContinuousRenderResult> results;
    connect(&scheduler, &PDFContinuousRenderScheduler::pageRendered, this,
            [&results](const PDFContinuousRenderResult& result) {
                results.push_back(result);
            });

    const auto document = loadTestDocument("continuous_scheduler_current.pdf");
    QVERIFY(document != nullptr);
    scheduler.setDocumentSource(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
            "/continuous_scheduler_current.pdf",
        document->numPages());

    const std::uint64_t generation = scheduler.currentGeneration();
    QVERIFY(generation > 0);
    scheduler.requestPage(makeRequest(generation, scheduler.documentToken()));

    QTRY_VERIFY_WITH_TIMEOUT(results.size() == 1, 3000);
    QCOMPARE(results.first().request.generation, generation);
    QVERIFY2(results.first().success,
             results.first().error.toUtf8().constData());
    QVERIFY(!results.first().image.isNull());
}

void TestPDFContinuousRenderScheduler::testStaleGenerationRequestIsDropped() {
    PDFContinuousRenderScheduler scheduler;
    QVector<PDFContinuousRenderResult> results;
    connect(&scheduler, &PDFContinuousRenderScheduler::pageRendered, this,
            [&results](const PDFContinuousRenderResult& result) {
                results.push_back(result);
            });

    const auto document = loadTestDocument("continuous_scheduler_stale.pdf");
    QVERIFY(document != nullptr);
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        "/continuous_scheduler_stale.pdf";
    scheduler.setDocumentSource(path, document->numPages());
    const std::uint64_t staleGeneration = scheduler.currentGeneration();
    scheduler.setDocumentSource(path, document->numPages());
    QVERIFY(scheduler.currentGeneration() > staleGeneration);

    scheduler.requestPage(
        makeRequest(staleGeneration, scheduler.documentToken()));
    QTest::qWait(100);

    QCOMPARE(results.size(), 0);
    QCOMPARE(scheduler.pendingRequestCount(), 0);
}

void TestPDFContinuousRenderScheduler::
    testDocumentChangeAllowsSameRenderKeyAgain() {
    PDFContinuousRenderScheduler scheduler;
    QVector<PDFContinuousRenderResult> results;
    connect(&scheduler, &PDFContinuousRenderScheduler::pageRendered, this,
            [&results](const PDFContinuousRenderResult& result) {
                results.push_back(result);
            });

    const auto firstDocument =
        loadTestDocument("continuous_scheduler_first.pdf");
    const auto secondDocument =
        loadTestDocument("continuous_scheduler_second.pdf");
    QVERIFY(firstDocument != nullptr);
    QVERIFY(secondDocument != nullptr);

    scheduler.setDocumentSource(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
            "/continuous_scheduler_first.pdf",
        firstDocument->numPages());
    scheduler.requestPage(
        makeRequest(scheduler.currentGeneration(), scheduler.documentToken()));
    QTRY_VERIFY_WITH_TIMEOUT(results.size() == 1, 3000);
    QVERIFY(results.first().success);

    results.clear();
    scheduler.setDocumentSource(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
            "/continuous_scheduler_second.pdf",
        secondDocument->numPages());
    const std::uint64_t secondGeneration = scheduler.currentGeneration();
    scheduler.requestPage(
        makeRequest(secondGeneration, scheduler.documentToken()));

    QTRY_VERIFY_WITH_TIMEOUT(results.size() == 1, 3000);
    QCOMPARE(results.first().request.generation, secondGeneration);
    QVERIFY2(results.first().success,
             results.first().error.toUtf8().constData());
}

void TestPDFContinuousRenderScheduler::
    testDocumentTokenChangesAcrossDocuments() {
    PDFContinuousRenderScheduler scheduler;
    const auto firstDocument =
        loadTestDocument("continuous_scheduler_token_first.pdf");
    const auto secondDocument =
        loadTestDocument("continuous_scheduler_token_second.pdf");
    QVERIFY(firstDocument != nullptr);
    QVERIFY(secondDocument != nullptr);

    scheduler.setDocumentSource(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
            "/continuous_scheduler_token_first.pdf",
        firstDocument->numPages());
    const std::uint64_t firstToken = scheduler.documentToken();
    QVERIFY(firstToken > 0);

    scheduler.setDocumentSource(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
            "/continuous_scheduler_token_second.pdf",
        secondDocument->numPages());
    const std::uint64_t secondToken = scheduler.documentToken();
    QVERIFY(secondToken > firstToken);

    PDFContinuousRenderKey firstKey;
    firstKey.documentToken = firstToken;
    firstKey.pageIndex = 0;
    PDFContinuousRenderKey secondKey = firstKey;
    secondKey.documentToken = secondToken;
    QVERIFY(!(firstKey == secondKey));
}

QTEST_MAIN(TestPDFContinuousRenderScheduler)
#include "test_pdf_continuous_render_scheduler.moc"
