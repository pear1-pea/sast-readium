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

static std::shared_ptr<Poppler::Document> loadTestDocument(
    const QString& fileName) {
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" +
        fileName;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return nullptr;
    }
    file.write(buildPdf(1));
    file.close();

    return std::shared_ptr<Poppler::Document>(
        Poppler::Document::load(path).release());
}

static PDFContinuousRenderRequest makeRequest(std::uint64_t generation) {
    PDFContinuousRenderRequest request;
    request.key.pageIndex = 0;
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
    void testCurrentGenerationRequestRenders();
    void testStaleGenerationRequestIsDropped();
    void testDocumentChangeAllowsSameRenderKeyAgain();
};

void TestPDFContinuousRenderScheduler::initTestCase() {
    qRegisterMetaType<PDFContinuousRenderResult>("PDFContinuousRenderResult");
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
    scheduler.setDocument(document);

    const std::uint64_t generation = scheduler.currentGeneration();
    QVERIFY(generation > 0);
    scheduler.requestPage(makeRequest(generation));

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
    scheduler.setDocument(document);
    const std::uint64_t staleGeneration = scheduler.currentGeneration();
    scheduler.setDocument(document);
    QVERIFY(scheduler.currentGeneration() > staleGeneration);

    scheduler.requestPage(makeRequest(staleGeneration));
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

    scheduler.setDocument(firstDocument);
    scheduler.requestPage(makeRequest(scheduler.currentGeneration()));
    QTRY_VERIFY_WITH_TIMEOUT(results.size() == 1, 3000);
    QVERIFY(results.first().success);

    results.clear();
    scheduler.setDocument(secondDocument);
    const std::uint64_t secondGeneration = scheduler.currentGeneration();
    scheduler.requestPage(makeRequest(secondGeneration));

    QTRY_VERIFY_WITH_TIMEOUT(results.size() == 1, 3000);
    QCOMPARE(results.first().request.generation, secondGeneration);
    QVERIFY2(results.first().success,
             results.first().error.toUtf8().constData());
}

QTEST_MAIN(TestPDFContinuousRenderScheduler)
#include "test_pdf_continuous_render_scheduler.moc"
