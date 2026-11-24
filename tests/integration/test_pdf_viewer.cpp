#include <poppler/qt6/poppler-qt6.h>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStyle>
#include <QWheelEvent>
#include <QtTest/QtTest>
#include "../../app/ui/continuous/PDFContinuousCanvas.h"
#include "../../app/ui/page/PDFPageWidget.h"
#include "../../app/ui/viewer/PDFViewer.h"

// ---------------------------------------------------------------------------
// Build a valid inline PDF with correct xref offsets.
//
// Each page carries a unique text marker and a non-square 600x800pt media box
// so tests can prove which page or document is actually displayed.
// ---------------------------------------------------------------------------
static QByteArray buildPdf(int numPages,
                           const QString& markerPrefix = QStringLiteral("PAGE"),
                           int pageWidth = 600, int pageHeight = 800) {
    QByteArray data = "%PDF-1.4\n";
    QVector<int> offsets;

    auto appendObject = [&data, &offsets](const QByteArray& body) {
        offsets.append(data.size());
        data.append(body);
        data.append("endobj\n");
    };

    appendObject("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\n");

    QByteArray kids;
    for (int i = 0; i < numPages; ++i) {
        kids += QByteArray::number(3 + i * 2) + " 0 R ";
    }
    appendObject("2 0 obj\n<< /Type /Pages /Kids [" + kids + "] /Count " +
                 QByteArray::number(numPages) + " >>\n");

    for (int i = 0; i < numPages; ++i) {
        const int pageObj = 3 + i * 2;
        const int streamObj = 4 + i * 2;
        const QString marker =
            QStringLiteral("%1_%2_ALPHA").arg(markerPrefix).arg(i);
        const QByteArray content = "BT /F1 24 Tf 72 " +
                                   QByteArray::number(pageHeight - 96) +
                                   " Td (" + marker.toLatin1() + ") Tj ET\n";

        QByteArray page = QByteArray::number(pageObj) +
                          " 0 obj\n<< /Type /Page /Parent 2 0 R"
                          " /MediaBox [0 0 " +
                          QByteArray::number(pageWidth) + " " +
                          QByteArray::number(pageHeight) +
                          "] /Resources << /Font << /F1 << /Type /Font"
                          " /Subtype /Type1 /BaseFont /Helvetica >> >> >>"
                          " /Contents " +
                          QByteArray::number(streamObj) + " 0 R >>\n";
        appendObject(page);

        QByteArray stream = QByteArray::number(streamObj) +
                            " 0 obj\n<< /Length " +
                            QByteArray::number(content.size()) +
                            " >>\nstream\n" + content + "endstream\n";
        appendObject(stream);
    }

    const int xrefOffset = data.size();
    data.append("xref\n0 " + QByteArray::number(1 + offsets.size()) +
                "\n0000000000 65535 f \n");
    for (int offset : offsets) {
        data.append(QStringLiteral("%1 00000 n \n")
                        .arg(offset, 10, 10, QChar('0'))
                        .toLatin1());
    }
    data.append("trailer\n<< /Size " + QByteArray::number(1 + offsets.size()) +
                " /Root 1 0 R >>\nstartxref\n" +
                QByteArray::number(xrefOffset) + "\n%%EOF\n");
    return data;
}

static QString writePdf(const QString& fileName, const QByteArray& data) {
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" +
        fileName;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(data);
    return path;
}

static void dragSelectOn(PDFPageWidget* widget, const QPointF& from,
                         const QPointF& to) {
    QMouseEvent press(QEvent::MouseButtonPress, from,
                      widget->mapToGlobal(from.toPoint()), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);

    QMouseEvent move(QEvent::MouseMove, to, widget->mapToGlobal(to.toPoint()),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &move);

    QMouseEvent release(QEvent::MouseButtonRelease, to,
                        widget->mapToGlobal(to.toPoint()), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &release);
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class TestPDFViewer : public QObject {
    Q_OBJECT

private:
    PDFViewer* m_viewer = nullptr;
    Poppler::Document* m_doc = nullptr;
    QString m_pdfPath;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testDocumentLoad();
    void testCopyShortcutScope();
    void testNavigation();
    void testPageBounds();
    void testZoom();
    void testZoomSignals();
    void testRotation();
    void testViewModeSwitch();
    void testViewportCaptureRestore();
    void testContinuousModeRendersCanvas();
    void testSinglePagePaintDoesNotCrash();
    void testAnnotationPlacementDoesNotCrash();
    void testBookmark();
    void testSinglePageLogicalSizeAtHighZoom();
    void testSelectionCopySurvivesZoom();
    void testRapidPageSwitchCommitsCurrentPage();
    void testDocumentSwitchDoesNotReusePreviousFrame();
    void testRepeatedPageReplacementRemainsReady();
    void testClearDocumentResetsSinglePageFrame();
    void testFitWidthFollowsViewportResize();
    void testZoomKeepsPagePointUnderViewportCenter();
    void testRotationUpdatesLogicalSizeAndSelection();
    void testWheelZoomUsesInputMagnitude();
    void testWheelZoomKeepsPointUnderCursor();
    void testEmptySelectionKeepsClipboard();
    void testCopyIsBlockedWhileFrameIsPending();
    void testRenderFailureEmitsError();
    void testSinglePageAnchorSurvivesModeRoundTrip();
    void testSearchResultCoordinateTransform();
    void testStylesheetPolishBeforeContinuousAreaDoesNotCrash();
};

void TestPDFViewer::initTestCase() {
    QCoreApplication::setOrganizationName("SASTReadiumTest");
    QCoreApplication::setApplicationName("PDFViewerTest");

    // Build and write a 3-page PDF
    m_pdfPath = writePdf("pdfviewer_test.pdf", buildPdf(3));
    QVERIFY(!m_pdfPath.isEmpty());

    m_doc = Poppler::Document::load(m_pdfPath).release();
    QVERIFY(m_doc != nullptr);

    m_viewer = new PDFViewer(nullptr, false);  // no styling
    m_viewer->resize(1000, 1200);
    m_viewer->show();
    m_viewer->setDocument(std::shared_ptr<Poppler::Document>(m_doc), m_pdfPath);
    QCoreApplication::processEvents();
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

void TestPDFViewer::testCopyShortcutScope() {
    bool found = false;
    for (QShortcut* shortcut : m_viewer->findChildren<QShortcut*>()) {
        if (shortcut->key() != QKeySequence(QKeySequence::Copy)) {
            continue;
        }
        found = true;
        // A window-wide Copy shortcut would steal Ctrl/Cmd+C from the Ask PDF
        // answer view and any other focusable widget in the same window.
        QCOMPARE(shortcut->context(), Qt::WidgetWithChildrenShortcut);
    }
    QVERIFY(found);
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

    m_viewer->resize(800, 1000);
    m_viewer->show();
    QCoreApplication::processEvents();
    m_viewer->goToPage(1);

    m_viewer->setViewMode(PDFViewMode::ContinuousScroll);
    QCOMPARE(m_viewer->getViewMode(), PDFViewMode::ContinuousScroll);
    QCOMPARE(modeSpy.count(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(m_viewer->getCurrentPage(), 1);

    m_viewer->setViewMode(PDFViewMode::DoublePage);
    QCOMPARE(m_viewer->getViewMode(), PDFViewMode::DoublePage);
    QCOMPARE(modeSpy.count(), 2);
    QCoreApplication::processEvents();
    QCOMPARE(m_viewer->getCurrentPage(), 1);

    m_viewer->setViewMode(PDFViewMode::GridView);
    QCOMPARE(m_viewer->getViewMode(), PDFViewMode::GridView);
    QCOMPARE(modeSpy.count(), 3);
    QCoreApplication::processEvents();
    QCOMPARE(m_viewer->getCurrentPage(), 1);

    m_viewer->setViewMode(PDFViewMode::SinglePage);
    QCOMPARE(m_viewer->getViewMode(), PDFViewMode::SinglePage);
    QCOMPARE(modeSpy.count(), 4);
    QCOMPARE(m_viewer->getCurrentPage(), 1);

    // Same mode → no signal
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    QCOMPARE(modeSpy.count(), 4);
}

void TestPDFViewer::testViewportCaptureRestore() {
    m_viewer->resize(800, 1000);
    m_viewer->show();
    QCoreApplication::processEvents();
    m_viewer->setViewMode(PDFViewMode::ContinuousScroll);
    QCoreApplication::processEvents();
    m_viewer->goToPage(1);
    m_viewer->setZoom(1.5);
    QCoreApplication::processEvents();

    const PDFViewerPosition position = m_viewer->captureViewport();
    QCOMPARE(position.page, 1);
    QCOMPARE(position.zoom, 1.5);

    m_viewer->goToPage(0);
    m_viewer->setZoom(1.0);
    QCoreApplication::processEvents();
    m_viewer->restoreViewport(position);
    QCoreApplication::processEvents();

    const PDFViewerPosition restored = m_viewer->captureViewport();
    QCOMPARE(restored.page, position.page);
    QCOMPARE(restored.zoom, position.zoom);
    QVERIFY(qAbs(restored.offsetY - position.offsetY) <= 5.0);
}

void TestPDFViewer::testContinuousModeRendersCanvas() {
    m_viewer->resize(800, 1000);
    m_viewer->show();
    QCoreApplication::processEvents();
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    QCoreApplication::processEvents();

    auto* canvas = m_viewer->findChild<PDFContinuousCanvas*>();
    QVERIFY(canvas != nullptr);

    QSignalSpy renderSpy(canvas, &PDFContinuousCanvas::renderApplied);
    QVERIFY(renderSpy.isValid());

    m_viewer->setViewMode(PDFViewMode::ContinuousScroll);

    QVERIFY(renderSpy.count() > 0 || renderSpy.wait(3000));
    QVERIFY(renderSpy.first().at(0).toInt() >= 0);

    m_viewer->setViewMode(PDFViewMode::SinglePage);
}

void TestPDFViewer::testSinglePagePaintDoesNotCrash() {
    m_viewer->resize(800, 1000);
    m_viewer->show();
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->goToPage(0);
    QTest::qWait(250);
    QCoreApplication::processEvents();
    QVERIFY(m_viewer->isVisible());
}

void TestPDFViewer::testAnnotationPlacementDoesNotCrash() {
    m_viewer->resize(800, 1000);
    m_viewer->show();
    m_viewer->setViewMode(PDFViewMode::ContinuousScroll);
    QTest::qWait(100);

    auto* canvas = m_viewer->findChild<PDFContinuousCanvas*>();
    QVERIFY(canvas != nullptr);
    QVERIFY(canvas->width() > 0);
    QVERIFY(canvas->height() > 0);

    AnnotationModel* model = m_viewer->annotationModel();
    QVERIFY(model != nullptr);
    const int initialCount = model->getTotalAnnotationCount();

    m_viewer->setAnnotationTool(AnnotationType::Highlight);
    const QPoint start(qMax(20, canvas->width() / 4),
                       qMax(20, canvas->height() / 4));
    const QPoint end(start.x() + 30, start.y() + 20);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas, end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QCoreApplication::processEvents();

    QCOMPARE(model->getTotalAnnotationCount(), initialCount + 1);
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

void TestPDFViewer::testSinglePageLogicalSizeAtHighZoom() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);

    m_viewer->setZoom(3.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    // 600x800pt page at 300% must occupy 1800x2400 logical pixels; the
    // 300 DPI cap may only reduce raster resolution.
    QVERIFY(qAbs(page->width() - 1800) <= 1);
    QVERIFY(qAbs(page->height() - 2400) <= 1);

    m_viewer->setZoom(5.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QVERIFY(qAbs(page->width() - 3000) <= 1);
    QVERIFY(qAbs(page->height() - 4000) <= 1);

    m_viewer->setZoom(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
}

void TestPDFViewer::testSelectionCopySurvivesZoom() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    QApplication::clipboard()->clear();
    dragSelectOn(page, QPointF(10, 10), QPointF(590, 260));
    QVERIFY(page->copySelectedText());
    const QString atOneHundred = QApplication::clipboard()->text();
    QCOMPARE(atOneHundred, QStringLiteral("PAGE_0_ALPHA"));

    // Selection is stored in page coordinates, so the same text region must
    // be selected after zooming to 200%.
    m_viewer->setZoom(2.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QApplication::clipboard()->clear();
    dragSelectOn(page, QPointF(20, 20), QPointF(1180, 520));
    QVERIFY(page->copySelectedText());
    QCOMPARE(QApplication::clipboard()->text(), atOneHundred);

    // Selection persists through a zoom change without a new drag.
    QApplication::clipboard()->clear();
    QVERIFY(page->copySelectedText());
    QCOMPARE(QApplication::clipboard()->text(), atOneHundred);

    m_viewer->setZoom(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
}

void TestPDFViewer::testRapidPageSwitchCommitsCurrentPage() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);
    QSignalSpy committed(page, &PDFPageWidget::frameCommitted);

    m_viewer->goToPage(1);
    m_viewer->goToPage(2);
    m_viewer->goToPage(0);

    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QCOMPARE(page->pageIndex(), 0);
    QCOMPARE(m_viewer->getCurrentPage(), 0);

    // A late result for page 1 or 2 must not replace the committed page 0.
    QTest::qWait(400);
    QVERIFY(!committed.isEmpty());
    QCOMPARE(committed.last().at(0).toInt(), 0);
    QCOMPARE(page->pageIndex(), 0);
    QVERIFY(page->isFrameReady());
}

void TestPDFViewer::testDocumentSwitchDoesNotReusePreviousFrame() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);
    m_viewer->goToPage(0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    const QString secondPath =
        writePdf("pdfviewer_second.pdf", buildPdf(1, QStringLiteral("DOC_B")));
    QVERIFY(!secondPath.isEmpty());
    auto secondDocument = std::shared_ptr<Poppler::Document>(
        Poppler::Document::load(secondPath).release());
    QVERIFY(secondDocument != nullptr);

    m_viewer->setDocument(secondDocument, secondPath);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    QApplication::clipboard()->clear();
    dragSelectOn(page, QPointF(5, 5), QPointF(595, 795));
    const bool copiedAfterSwitch = page->copySelectedText();
    const QString copied = QApplication::clipboard()->text();

    // Restore the shared fixture document before asserting so a failure here
    // does not corrupt the remaining slots.
    auto restored = std::shared_ptr<Poppler::Document>(
        Poppler::Document::load(m_pdfPath).release());
    QVERIFY(restored != nullptr);
    m_viewer->setDocument(restored, m_pdfPath);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    QVERIFY(copiedAfterSwitch);
    QVERIFY2(copied.contains(QStringLiteral("DOC_B_0_ALPHA")),
             copied.toUtf8().constData());
    QVERIFY(!copied.contains(QStringLiteral("PAGE_0_ALPHA")));
}

void TestPDFViewer::testRepeatedPageReplacementRemainsReady() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);

    for (int i = 0; i < 30; ++i) {
        m_viewer->goToPage(i % 3);
    }
    m_viewer->goToPage(2);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QCOMPARE(page->pageIndex(), 2);
    QCOMPARE(m_viewer->getCurrentPage(), 2);
}

void TestPDFViewer::testClearDocumentResetsSinglePageFrame() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    m_viewer->clearDocument();
    QVERIFY(!m_viewer->hasDocument());
    QCOMPARE(page->pageIndex(), -1);
    QVERIFY(!page->isFrameReady());
    QVERIFY(!page->copySelectedText());

    auto restored = std::shared_ptr<Poppler::Document>(
        Poppler::Document::load(m_pdfPath).release());
    QVERIFY(restored != nullptr);
    m_viewer->setDocument(restored, m_pdfPath);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QCOMPARE(page->pageIndex(), 0);
}

void TestPDFViewer::testFitWidthFollowsViewportResize() {
    m_viewer->resize(1000, 1200);
    QCoreApplication::processEvents();
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    auto* area = m_viewer->findChild<QScrollArea*>("singlePageScrollArea");
    QVERIFY(page != nullptr);
    QVERIFY(area != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    m_viewer->zoomToWidth();
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    const double wideZoom = m_viewer->getCurrentZoom();
    QVERIFY(wideZoom > 0.0);

    m_viewer->resize(700, 900);
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(m_viewer->getCurrentZoom() - wideZoom) > 0.01,
                             3000);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    // Width fit keeps the page inside the new viewport width (0.98 margin).
    // The fit reserves one scroll-bar extent for the vertical bar, so the
    // page lands between (viewport - extent) * 0.98 and viewport * 0.98.
    const int extent =
        area->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, area);
    const double maxWidth = area->viewport()->width() * 0.98 + 1.0;
    const double minWidth = (area->viewport()->width() - extent) * 0.98 - 1.0;
    QVERIFY2(page->width() <= maxWidth && page->width() >= minWidth,
             qPrintable(QStringLiteral("page width %1 expected in [%2, %3]")
                            .arg(page->width())
                            .arg(minWidth)
                            .arg(maxWidth)));

    // The fitted value must be a fixed point, not a scroll-bar oscillation.
    const double fittedZoom = m_viewer->getCurrentZoom();
    QTest::qWait(300);
    QCOMPARE(m_viewer->getCurrentZoom(), fittedZoom);

    m_viewer->resize(1000, 1200);
    QCoreApplication::processEvents();
    m_viewer->setZoom(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
}

void TestPDFViewer::testZoomKeepsPagePointUnderViewportCenter() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    auto* area = m_viewer->findChild<QScrollArea*>("singlePageScrollArea");
    QVERIFY(page != nullptr);
    QVERIFY(area != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    const QPoint center = area->viewport()->rect().center();
    const QPointF pagePointBefore =
        page->pagePointAtWidgetPoint(page->mapFrom(area->viewport(), center));

    m_viewer->setZoom(2.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QTest::qWait(100);

    const QPointF pagePointAfter = page->pagePointAtWidgetPoint(
        page->mapFrom(area->viewport(), area->viewport()->rect().center()));
    QVERIFY2(qAbs(pagePointAfter.x() - pagePointBefore.x()) <= 5.0,
             qPrintable(QStringLiteral("x %1 vs %2")
                            .arg(pagePointAfter.x())
                            .arg(pagePointBefore.x())));
    QVERIFY2(qAbs(pagePointAfter.y() - pagePointBefore.y()) <= 5.0,
             qPrintable(QStringLiteral("y %1 vs %2")
                            .arg(pagePointAfter.y())
                            .arg(pagePointBefore.y())));

    m_viewer->setZoom(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
}

void TestPDFViewer::testRotationUpdatesLogicalSizeAndSelection() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    m_viewer->setRotation(0);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QVERIFY(qAbs(page->width() - 600) <= 1);
    QVERIFY(qAbs(page->height() - 800) <= 1);

    m_viewer->rotateRight();
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    // 90 degrees swaps the logical width and height.
    QVERIFY2(qAbs(page->width() - 800) <= 1,
             qPrintable(QString::number(page->width())));
    QVERIFY2(qAbs(page->height() - 600) <= 1,
             qPrintable(QString::number(page->height())));

    QApplication::clipboard()->clear();
    dragSelectOn(page, QPointF(5, 5), QPointF(790, 590));
    QVERIFY(page->copySelectedText());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("PAGE_0_ALPHA"));

    m_viewer->resetRotation();
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QVERIFY(qAbs(page->width() - 600) <= 1);
}

void TestPDFViewer::testWheelZoomUsesInputMagnitude() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    const QPointF local(100, 100);
    QWheelEvent zoomWheel(local, page->mapToGlobal(local.toPoint()), QPoint(),
                          QPoint(0, 240), Qt::NoButton, Qt::ControlModifier,
                          Qt::NoScrollPhase, false);
    QApplication::sendEvent(page, &zoomWheel);
    // Two wheel notches scale by 1.15^2 instead of one fixed 15% step.
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(page->getScaleFactor() - 1.3225) < 1e-6,
                             2000);

    const double beforePlainWheel = page->getScaleFactor();
    QWheelEvent plainWheel(local, page->mapToGlobal(local.toPoint()), QPoint(),
                           QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
    QApplication::sendEvent(page, &plainWheel);
    QTest::qWait(50);
    QCOMPARE(page->getScaleFactor(), beforePlainWheel);

    m_viewer->setZoom(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
}

void TestPDFViewer::testWheelZoomKeepsPointUnderCursor() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(2.0);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    auto* area = m_viewer->findChild<QScrollArea*>("singlePageScrollArea");
    QVERIFY(page != nullptr);
    QVERIFY(area != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    area->horizontalScrollBar()->setValue(100);
    area->verticalScrollBar()->setValue(400);
    QTest::qWait(50);

    const QPoint viewportPoint(400, 400);
    const QPoint widgetPoint = page->mapFrom(area->viewport(), viewportPoint);
    const QPointF before = page->pagePointAtWidgetPoint(widgetPoint);
    QVERIFY(!before.isNull());

    const QPointF globalPoint(area->viewport()->mapToGlobal(viewportPoint));
    QWheelEvent wheel(QPointF(widgetPoint), globalPoint, QPoint(),
                      QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(page, &wheel);
    QVERIFY(qAbs(page->getScaleFactor() - 2.3) < 1e-6);
    QTest::qWait(150);

    const QPointF after = page->pagePointAtWidgetPoint(
        page->mapFrom(area->viewport(), viewportPoint));
    QVERIFY2(qAbs(after.x() - before.x()) <= 5.0,
             qPrintable(
                 QStringLiteral("x %1 vs %2").arg(after.x()).arg(before.x())));
    QVERIFY2(qAbs(after.y() - before.y()) <= 5.0,
             qPrintable(
                 QStringLiteral("y %1 vs %2").arg(after.y()).arg(before.y())));

    m_viewer->setZoom(1.0);
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
}

void TestPDFViewer::testEmptySelectionKeepsClipboard() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    page->clearSelection();
    QApplication::clipboard()->setText(QStringLiteral("KEEP_ME"));
    QVERIFY(!page->copySelectedText());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("KEEP_ME"));
}

void TestPDFViewer::testCopyIsBlockedWhileFrameIsPending() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(1.0);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    QVERIFY(page != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    dragSelectOn(page, QPointF(10, 10), QPointF(590, 260));
    QVERIFY(page->copySelectedText());

    m_viewer->setZoom(2.5);
    QVERIFY(!page->isFrameReady());
    QVERIFY(!page->copySelectedText());

    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QApplication::clipboard()->clear();
    QVERIFY(page->copySelectedText());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("PAGE_0_ALPHA"));

    m_viewer->setZoom(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
}

void TestPDFViewer::testRenderFailureEmitsError() {
    PDFPageWidget broken;
    QSignalSpy failed(&broken, &PDFPageWidget::renderFailed);

    auto document = std::shared_ptr<Poppler::Document>(
        Poppler::Document::load(m_pdfPath).release());
    QVERIFY(document != nullptr);
    std::unique_ptr<Poppler::Page> page(document->page(0));
    QVERIFY(page != nullptr);

    broken.setDocumentSource(QStringLiteral("/nonexistent/readium-missing.pdf"),
                             12345);
    broken.setPage(std::move(page), 1.0, 0);

    QTRY_VERIFY_WITH_TIMEOUT(failed.count() > 0, 5000);
    QCOMPARE(failed.first().at(0).toInt(), 0);
    QVERIFY(!broken.isFrameReady());
}

void TestPDFViewer::testSinglePageAnchorSurvivesModeRoundTrip() {
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    m_viewer->setZoom(2.0);
    m_viewer->goToPage(0);
    auto* page = m_viewer->findChild<PDFPageWidget*>();
    auto* area = m_viewer->findChild<QScrollArea*>("singlePageScrollArea");
    QVERIFY(page != nullptr);
    QVERIFY(area != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);

    area->verticalScrollBar()->setValue(300);
    QTest::qWait(50);
    const PDFViewerPosition saved = m_viewer->captureViewport();
    QVERIFY(!saved.pagePoint.isNull());

    m_viewer->setViewMode(PDFViewMode::ContinuousScroll);
    QCoreApplication::processEvents();
    m_viewer->setViewMode(PDFViewMode::SinglePage);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
    QTest::qWait(150);

    const PDFViewerPosition restored = m_viewer->captureViewport();
    QVERIFY2(qAbs(restored.pagePoint.x() - saved.pagePoint.x()) <= 5.0,
             qPrintable(QStringLiteral("x %1 vs %2")
                            .arg(restored.pagePoint.x())
                            .arg(saved.pagePoint.x())));
    QVERIFY2(qAbs(restored.pagePoint.y() - saved.pagePoint.y()) <= 5.0,
             qPrintable(QStringLiteral("y %1 vs %2")
                            .arg(restored.pagePoint.y())
                            .arg(saved.pagePoint.y())));

    m_viewer->setZoom(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(page->isFrameReady(), 5000);
}

void TestPDFViewer::testStylesheetPolishBeforeContinuousAreaDoesNotCrash() {
    // The styled constructor applies a stylesheet to the single-page area
    // before the continuous area exists; its polish events reach the viewer's
    // event filter during setup.
    PDFViewer styled(nullptr, true);
    styled.resize(800, 1000);
    QCOMPARE(styled.getViewMode(), PDFViewMode::SinglePage);
}

void TestPDFViewer::testSearchResultCoordinateTransform() {
    // Independent expected geometry: 600x800pt page, 200% zoom, no rotation.
    SearchResult result;
    result.pageNumber = 0;
    // PDF space is bottom-left origin: y=680..704 becomes top=96..120.
    result.boundingRect = QRectF(72, 800 - 120, 100, 24);
    result.transformToWidgetCoordinates(2.0, 0, QSizeF(600, 800),
                                        QSize(1200, 1600));
    QCOMPARE(result.widgetRect, QRectF(144, 192, 200, 48));

    SearchResult rotated;
    rotated.pageNumber = 0;
    rotated.boundingRect = QRectF(72, 800 - 120, 100, 24);
    rotated.transformToWidgetCoordinates(2.0, 90, QSizeF(600, 800),
                                         QSize(1600, 1200));
    QCOMPARE(rotated.widgetRect, QRectF(1360, 144, 48, 200));
}

QTEST_MAIN(TestPDFViewer)
#include "test_pdf_viewer.moc"
