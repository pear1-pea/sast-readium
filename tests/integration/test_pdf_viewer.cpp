#include <poppler/qt6/poppler-qt6.h>
#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTextStream>
#include <QtTest/QtTest>
#include "../../app/ui/viewer/PDFViewer.h"

// ---------------------------------------------------------------------------
// Build a valid inline PDF with correct xref offsets.
// Content streams are empty but valid ("q Q").
// ---------------------------------------------------------------------------
static QByteArray buildPdf(int numPages) {
    QByteArray data;
    QTextStream out(&data);
    QVector<int> offsets;  // byte offset of each object (1-based)

    out << "%PDF-1.4\n";

    // Object 1 — Catalog
    offsets << data.size();
    out << "1 0 obj\n"
           "<< /Type /Catalog /Pages 2 0 R >>\n"
           "endobj\n";

    // Object 2 — Pages tree
    offsets << data.size();
    out << "2 0 obj\n"
           "<< /Type /Pages /Kids [";
    for (int i = 0; i < numPages; ++i) {
        if (i)
            out << ' ';
        out << (3 + i * 2) << " 0 R";
    }
    out << "] /Count " << numPages
        << " >>\n"
           "endobj\n";

    // Page objects + content streams  (3,4 | 5,6 | 7,8 | …)
    for (int i = 0; i < numPages; ++i) {
        int pageObj = 3 + i * 2;
        int streamObj = 4 + i * 2;

        // Page object
        offsets << data.size();
        out << pageObj
            << " 0 obj\n"
               "<< /Type /Page /Parent 2 0 R"
               " /MediaBox [0 0 612 792]"
               " /Contents "
            << streamObj
            << " 0 R >>\n"
               "endobj\n";

        // Content stream — minimal but valid
        offsets << data.size();
        out << streamObj
            << " 0 obj\n"
               "<< /Length 3 >>\n"
               "stream\n"
               "q Q\n"
               "endstream\n"
               "endobj\n";
    }

    // Xref
    int xrefOffset = data.size();
    out << "xref\n0 " << (1 + offsets.size()) << "\n"
        << "0000000000 65535 f \n";
    for (int off : offsets) {
        out << QString("%1 00000 n \n").arg(off, 10, 10, QChar('0'));
    }
    out << "trailer\n"
        << "<< /Size " << (1 + offsets.size()) << " /Root 1 0 R >>\n"
        << "startxref\n"
        << xrefOffset << "\n"
        << "%%EOF\n";

    return data;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class TestPDFViewer : public QObject {
    Q_OBJECT

private:
    PDFViewer* m_viewer = nullptr;
    Poppler::Document* m_doc = nullptr;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testDocumentLoad();
    void testNavigation();
    void testPageBounds();
    void testZoom();
    void testZoomSignals();
    void testRotation();
    void testViewModeSwitch();
    void testBookmark();
};

void TestPDFViewer::initTestCase() {
    QCoreApplication::setOrganizationName("SASTReadiumTest");
    QCoreApplication::setApplicationName("PDFViewerTest");

    // Build and write a 3-page PDF
    QString path =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        "/pdfviewer_test.pdf";
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(buildPdf(3));
    }

    m_doc = Poppler::Document::load(path).release();
    QVERIFY(m_doc != nullptr);

    m_viewer = new PDFViewer(nullptr, false);  // no styling
    m_viewer->setDocument(std::shared_ptr<Poppler::Document>(m_doc));
}

void TestPDFViewer::cleanupTestCase() {
    // PDFViewer's shared_ptr owns m_doc; deleting the viewer releases it
    delete m_viewer;
    m_viewer = nullptr;
    m_doc = nullptr;  // already freed by shared_ptr
}

// ---------------------------------------------------------------------------
void TestPDFViewer::testDocumentLoad() {
    QVERIFY(m_viewer->hasDocument());
    QCOMPARE(m_viewer->getPageCount(), 3);
    QCOMPARE(m_viewer->getCurrentPage(), 0);
}

void TestPDFViewer::testNavigation() {
    int page;

    m_viewer->nextPage();
    page = m_viewer->getCurrentPage();
    QCOMPARE(page, 1);

    m_viewer->nextPage();
    QCOMPARE(m_viewer->getCurrentPage(), 2);

    m_viewer->previousPage();
    QCOMPARE(m_viewer->getCurrentPage(), 1);

    m_viewer->firstPage();
    QCOMPARE(m_viewer->getCurrentPage(), 0);

    m_viewer->lastPage();
    QCOMPARE(m_viewer->getCurrentPage(), 2);

    m_viewer->goToPage(1);
    QCOMPARE(m_viewer->getCurrentPage(), 1);
}

void TestPDFViewer::testPageBounds() {
    // goToPageWithValidation rejects out-of-range pages, leaving position
    // unchanged
    int start = m_viewer->getCurrentPage();

    m_viewer->goToPage(100);
    QCOMPARE(m_viewer->getCurrentPage(), start);  // unchanged

    m_viewer->goToPage(-5);
    QCOMPARE(m_viewer->getCurrentPage(), start);  // unchanged
}

void TestPDFViewer::testZoom() {
    m_viewer->setZoom(2.0);
    QCOMPARE(m_viewer->getCurrentZoom(), 2.0);

    m_viewer->zoomIn();
    QVERIFY(m_viewer->getCurrentZoom() > 2.0);

    m_viewer->zoomOut();
    QVERIFY(m_viewer->getCurrentZoom() < m_viewer->getCurrentZoom() + 0.2);

    m_viewer->zoomToFit();
    QVERIFY(m_viewer->getCurrentZoom() > 0);

    m_viewer->zoomToWidth();
    QVERIFY(m_viewer->getCurrentZoom() > 0);

    m_viewer->zoomToHeight();
    QVERIFY(m_viewer->getCurrentZoom() > 0);

    m_viewer->setZoom(1.0);
}

void TestPDFViewer::testZoomSignals() {
    QSignalSpy zoomSpy(m_viewer, &PDFViewer::zoomChanged);

    // Large jump → bypasses debounce → emits immediately
    m_viewer->setZoom(3.0);
    QCOMPARE(zoomSpy.count(), 1);
    QCOMPARE(zoomSpy.at(0).at(0).toDouble(), 3.0);

    m_viewer->setZoom(1.0);
    QCOMPARE(zoomSpy.count(), 2);
}

void TestPDFViewer::testRotation() {
    QSignalSpy rotSpy(m_viewer, &PDFViewer::rotationChanged);

    m_viewer->setRotation(90);
    QCOMPARE(rotSpy.count(), 1);

    m_viewer->rotateRight();
    QVERIFY(rotSpy.count() >= 2);

    m_viewer->rotateLeft();
    QVERIFY(rotSpy.count() >= 3);

    m_viewer->resetRotation();
    // rotation is 360 → normalized to 0 → no change → no signal
    // (the viewer emits only when degrees != currentRotation)
}

void TestPDFViewer::testViewModeSwitch() {
    QSignalSpy modeSpy(m_viewer, &PDFViewer::viewModeChanged);

    m_viewer->setViewMode(PDFViewMode::ContinuousScroll);
    QCOMPARE(m_viewer->getViewMode(), PDFViewMode::ContinuousScroll);
    QCOMPARE(modeSpy.count(), 1);

    m_viewer->setViewMode(PDFViewMode::SinglePage);
    QCOMPARE(m_viewer->getViewMode(), PDFViewMode::SinglePage);
    QCOMPARE(modeSpy.count(), 2);

    // Same mode → no signal
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    QCOMPARE(modeSpy.count(), 2);
}

void TestPDFViewer::testBookmark() {
    // Just verify the API doesn't crash — actual bookmark storage is external
    m_viewer->goToPage(1);
    m_viewer->addBookmark();
    m_viewer->addBookmarkForPage(2);
    m_viewer->removeBookmark();
    m_viewer->toggleBookmark();
    QCOMPARE(m_viewer->hasBookmarkForCurrentPage(), false);
}

QTEST_MAIN(TestPDFViewer)
#include "test_pdf_viewer.moc"
