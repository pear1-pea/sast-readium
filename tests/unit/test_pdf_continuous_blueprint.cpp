#include <QtTest/QtTest>

#include "../../app/ui/continuous/PDFContinuousBlueprint.h"

class TestPDFContinuousBlueprint : public QObject {
    Q_OBJECT

private slots:
    void testEmptyBlueprint();
    void testSinglePageLayout();
    void testMultiPageLayout();
    void testRotationSwapsDimensions();
    void testStrictHitTest();
    void testCurrentPageForViewport();
    void testScrollOffsetForPage();
    void testAnchorRestoreAfterZoom();
    void testSpreadLayoutPairsAndCentersPages();
    void testSpreadWidePageSpansRow();
    void testGridLayoutPositions();
    void testTwoColumnGridFormsTwoPageRows();
    void testCurrentPageForSpreadAndGrid();
    void testScrollOffsetForSpreadAndGrid();
    void testViewportDocumentOffsetTransformsBothAxes();
};

static PDFContinuousLayoutOptions defaultOptions() {
    PDFContinuousLayoutOptions options;
    options.zoom = 1.0;
    options.rotation = 0;
    options.pageSpacing = 20.0;
    options.documentMargins = QMarginsF(10.0, 10.0, 10.0, 10.0);
    options.viewportWidth = 500.0;
    return options;
}

void TestPDFContinuousBlueprint::testEmptyBlueprint() {
    PDFContinuousBlueprint blueprint;

    QVERIFY(blueprint.isEmpty());
    QCOMPARE(blueprint.pageCount(), 0);
    QCOMPARE(blueprint.pageAt(QPointF(10, 10)), -1);
    QVERIFY(!blueprint.hitTest(QPointF(10, 10)).has_value());
}

void TestPDFContinuousBlueprint::testSinglePageLayout() {
    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300)}, defaultOptions());

    QCOMPARE(blueprint.pageCount(), 1);
    QVERIFY(!blueprint.isEmpty());

    const PDFPageGeometry* geometry = blueprint.pageGeometry(0);
    QVERIFY(geometry != nullptr);
    QCOMPARE(geometry->originalSize, QSizeF(200, 300));
    QCOMPARE(geometry->contentRect.size(), QSizeF(200, 300));
    QCOMPARE(blueprint.documentHeight(), 320.0);
    QCOMPARE(geometry->contentRect.center().x(), 250.0);
}

void TestPDFContinuousBlueprint::testMultiPageLayout() {
    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300), QSizeF(300, 200)}, defaultOptions());

    const PDFPageGeometry* first = blueprint.pageGeometry(0);
    const PDFPageGeometry* second = blueprint.pageGeometry(1);
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);

    QCOMPARE(first->contentRect.top(), 10.0);
    QCOMPARE(second->contentRect.top(), 330.0);
    QCOMPARE(blueprint.documentHeight(), 540.0);
    QCOMPARE(first->contentRect.center().x(), second->contentRect.center().x());
}

void TestPDFContinuousBlueprint::testRotationSwapsDimensions() {
    PDFContinuousLayoutOptions options = defaultOptions();
    options.rotation = 90;

    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300)}, options);

    const PDFPageGeometry* geometry = blueprint.pageGeometry(0);
    QVERIFY(geometry != nullptr);
    QCOMPARE(geometry->contentRect.size(), QSizeF(300, 200));
}

void TestPDFContinuousBlueprint::testStrictHitTest() {
    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300), QSizeF(200, 300)}, defaultOptions());

    QCOMPARE(blueprint.pageAt(QPointF(250, 20)), 0);
    QCOMPARE(blueprint.pageAt(QPointF(250, 320)), -1);
    QCOMPARE(blueprint.pageAt(QPointF(5, 20)), -1);

    const auto hit = blueprint.hitTest(QPointF(250, 20));
    QVERIFY(hit.has_value());
    QCOMPARE(hit->pageIndex, 0);
    QCOMPARE(hit->insidePage, true);
}

void TestPDFContinuousBlueprint::testCurrentPageForViewport() {
    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300), QSizeF(200, 300)}, defaultOptions());

    QCOMPARE(blueprint.currentPageForViewport(QRectF(0, 0, 500, 200)), 0);
    QCOMPARE(blueprint.currentPageForViewport(QRectF(0, 260, 500, 100)), 0);
    QCOMPARE(blueprint.currentPageForViewport(QRectF(0, 330, 500, 200)), 1);
    QCOMPARE(blueprint.currentPageForViewport(QRectF(0, 312, 500, 20), 0, 24),
             0);
}

void TestPDFContinuousBlueprint::testScrollOffsetForPage() {
    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300), QSizeF(200, 300)}, defaultOptions());

    QCOMPARE(blueprint.scrollOffsetForPage(0, 200, PageScrollAlignment::Top),
             10.0);
    QCOMPARE(blueprint.scrollOffsetForPage(1, 200, PageScrollAlignment::Top),
             330.0);
    QCOMPARE(blueprint.scrollOffsetForPage(1, 200, PageScrollAlignment::Center),
             380.0);
}

void TestPDFContinuousBlueprint::testAnchorRestoreAfterZoom() {
    QVector<QSizeF> pages{QSizeF(200, 300), QSizeF(200, 300)};

    PDFContinuousBlueprint before;
    before.rebuild(pages, defaultOptions());
    const PDFViewportAnchor anchor =
        before.captureAnchor(QRectF(0, 100, 500, 200));
    QCOMPARE(anchor.pageIndex, 0);

    PDFContinuousLayoutOptions zoomed = defaultOptions();
    zoomed.zoom = 2.0;
    PDFContinuousBlueprint after;
    after.rebuild(pages, zoomed);

    const qreal restored = after.restoreAnchorOffset(anchor, 200);
    QCOMPARE(restored, 290.0);
}

void TestPDFContinuousBlueprint::testSpreadLayoutPairsAndCentersPages() {
    // Pages of differing heights so AlignCenter produces distinct tops.
    PDFContinuousLayoutOptions options = defaultOptions();
    options.layoutType = PDFLayoutType::Spread;
    options.firstPageAlone = true;
    options.verticalAlignment = PDFVerticalAlignment::AlignCenter;
    options.columnSpacing = 20.0;

    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300), QSizeF(200, 200), QSizeF(200, 300)},
                      options);

    const PDFPageGeometry* p0 = blueprint.pageGeometry(0);
    const PDFPageGeometry* p1 = blueprint.pageGeometry(1);
    const PDFPageGeometry* p2 = blueprint.pageGeometry(2);
    QVERIFY(p0 && p1 && p2);

    // Row 0: page 0 alone, centered horizontally in the 480 layout width.
    QCOMPARE(p0->contentRect.top(), 10.0);
    QCOMPARE(p0->contentRect.left(), 150.0);

    // Row 1: pages 1 and 2 paired side by side, sharing the same row band.
    QCOMPARE(p1->contentRect.left(), 40.0);
    QCOMPARE(p2->contentRect.left(), 260.0);

    // AlignCenter lifts the shorter page 1 within the 300-tall band, so the
    // paired pages have DIFFERENT tops — this breaks top-monotonicity.
    QCOMPARE(p2->contentRect.top(), 330.0);
    QCOMPARE(p1->contentRect.top(), 380.0);

    QCOMPARE(blueprint.documentHeight(), 640.0);

    // pageAt must handle the non-monotonic tops via linear scan.
    QCOMPARE(blueprint.pageAt(QPointF(100, 400)), 1);
    QCOMPARE(blueprint.pageAt(QPointF(300, 400)), 2);
    // The gap above page 1 (top-aligned position) belongs to no page.
    QCOMPARE(blueprint.pageAt(QPointF(100, 340)), -1);
}

void TestPDFContinuousBlueprint::testSpreadWidePageSpansRow() {
    // A landscape page wider than half the row must occupy its own row.
    PDFContinuousLayoutOptions options = defaultOptions();
    options.layoutType = PDFLayoutType::Spread;
    options.firstPageAlone = false;
    options.verticalAlignment = PDFVerticalAlignment::AlignTop;
    options.columnSpacing = 20.0;

    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300), QSizeF(200, 300), QSizeF(500, 300)},
                      options);

    const PDFPageGeometry* p0 = blueprint.pageGeometry(0);
    const PDFPageGeometry* p1 = blueprint.pageGeometry(1);
    const PDFPageGeometry* p2 = blueprint.pageGeometry(2);
    QVERIFY(p0 && p1 && p2);

    // Row 0: portrait pages 0 and 1 pair together.
    QCOMPARE(p0->contentRect.top(), 10.0);
    QCOMPARE(p1->contentRect.top(), 10.0);
    QVERIFY(p0->contentRect.left() < p1->contentRect.left());

    // Row 1: wide page 2 spans its own row below.
    QCOMPARE(p2->contentRect.top(), 330.0);
    QCOMPARE(p2->contentRect.width(), 500.0);

    QCOMPARE(blueprint.documentHeight(), 640.0);
}

void TestPDFContinuousBlueprint::testGridLayoutPositions() {
    // 5 uniform pages in a 3-column grid → rows {0,1,2} and {3,4}.
    PDFContinuousLayoutOptions options = defaultOptions();
    options.layoutType = PDFLayoutType::Grid;
    options.gridColumns = 3;
    options.verticalAlignment = PDFVerticalAlignment::AlignTop;
    options.columnSpacing = 20.0;

    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(100, 150), QSizeF(100, 150), QSizeF(100, 150),
                       QSizeF(100, 150), QSizeF(100, 150)},
                      options);

    QCOMPARE(blueprint.pageCount(), 5);

    const PDFPageGeometry* p0 = blueprint.pageGeometry(0);
    const PDFPageGeometry* p1 = blueprint.pageGeometry(1);
    const PDFPageGeometry* p2 = blueprint.pageGeometry(2);
    const PDFPageGeometry* p3 = blueprint.pageGeometry(3);
    QVERIFY(p0 && p1 && p2 && p3);

    // First row: three pages share a top, spaced by columnSpacing (20).
    QCOMPARE(p0->contentRect.top(), 10.0);
    QCOMPARE(p1->contentRect.top(), 10.0);
    QCOMPARE(p2->contentRect.top(), 10.0);
    QCOMPARE(p0->contentRect.left(), 80.0);
    QCOMPARE(p1->contentRect.left(), 200.0);
    QCOMPARE(p2->contentRect.left(), 320.0);

    // Second row starts below the first (150 tall + 20 spacing).
    QCOMPARE(p3->contentRect.top(), 180.0);
    QCOMPARE(blueprint.documentHeight(), 340.0);

    // pageAt across grid cells uses the linear-scan path.
    QCOMPARE(blueprint.pageAt(QPointF(250, 100)), 1);
    QCOMPARE(blueprint.pageAt(QPointF(150, 200)), 3);
}

void TestPDFContinuousBlueprint::testTwoColumnGridFormsTwoPageRows() {
    PDFContinuousLayoutOptions options = defaultOptions();
    options.layoutType = PDFLayoutType::Grid;
    options.gridColumns = 2;
    options.columnSpacing = 20.0;

    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(100, 150), QSizeF(100, 150), QSizeF(100, 150),
                       QSizeF(100, 150)},
                      options);

    const PDFPageGeometry* p0 = blueprint.pageGeometry(0);
    const PDFPageGeometry* p1 = blueprint.pageGeometry(1);
    const PDFPageGeometry* p2 = blueprint.pageGeometry(2);
    const PDFPageGeometry* p3 = blueprint.pageGeometry(3);
    QVERIFY(p0 && p1 && p2 && p3);

    // Two consecutive rows make the 2x2 multi-page arrangement.
    QCOMPARE(p0->contentRect.top(), 10.0);
    QCOMPARE(p1->contentRect.top(), 10.0);
    QCOMPARE(p2->contentRect.top(), 180.0);
    QCOMPARE(p3->contentRect.top(), 180.0);
    QVERIFY(p0->contentRect.left() < p1->contentRect.left());
    QVERIFY(p2->contentRect.left() < p3->contentRect.left());
}

void TestPDFContinuousBlueprint::testCurrentPageForSpreadAndGrid() {
    PDFContinuousLayoutOptions spreadOptions = defaultOptions();
    spreadOptions.layoutType = PDFLayoutType::Spread;
    spreadOptions.columnSpacing = 20.0;

    PDFContinuousBlueprint spread;
    spread.rebuild({QSizeF(200, 300), QSizeF(200, 300), QSizeF(200, 300),
                    QSizeF(200, 300)},
                   spreadOptions);

    // The viewport center is inside the left and right pages respectively.
    QCOMPARE(spread.currentPageForViewport(QRectF(0, 0, 200, 300)), 0);
    QCOMPARE(spread.currentPageForViewport(QRectF(200, 0, 200, 300)), 1);
    QCOMPARE(spread.currentPageForViewport(QRectF(0, 320, 200, 300)), 2);

    PDFContinuousLayoutOptions gridOptions = defaultOptions();
    gridOptions.layoutType = PDFLayoutType::Grid;
    gridOptions.gridColumns = 2;
    gridOptions.columnSpacing = 20.0;

    PDFContinuousBlueprint grid;
    grid.rebuild({QSizeF(100, 150), QSizeF(100, 150), QSizeF(100, 150),
                  QSizeF(100, 150)},
                 gridOptions);

    QCOMPARE(grid.currentPageForViewport(QRectF(0, 0, 200, 150)), 0);
    QCOMPARE(grid.currentPageForViewport(QRectF(200, 0, 200, 150)), 1);
    QCOMPARE(grid.currentPageForViewport(QRectF(0, 170, 200, 150)), 2);
}

void TestPDFContinuousBlueprint::testScrollOffsetForSpreadAndGrid() {
    PDFContinuousLayoutOptions spreadOptions = defaultOptions();
    spreadOptions.layoutType = PDFLayoutType::Spread;
    spreadOptions.columnSpacing = 20.0;

    PDFContinuousBlueprint spread;
    spread.rebuild({QSizeF(200, 300), QSizeF(200, 300), QSizeF(200, 300),
                    QSizeF(200, 300)},
                   spreadOptions);

    QCOMPARE(spread.scrollOffsetForPage(1, 200, PageScrollAlignment::Top),
             10.0);
    QCOMPARE(spread.scrollOffsetForPage(2, 200, PageScrollAlignment::Top),
             330.0);
    QCOMPARE(spread.scrollOffsetForPage(2, 200, PageScrollAlignment::Center),
             380.0);

    PDFContinuousLayoutOptions gridOptions = defaultOptions();
    gridOptions.layoutType = PDFLayoutType::Grid;
    gridOptions.gridColumns = 2;
    gridOptions.columnSpacing = 20.0;

    PDFContinuousBlueprint grid;
    grid.rebuild({QSizeF(100, 150), QSizeF(100, 150), QSizeF(100, 150),
                  QSizeF(100, 150), QSizeF(100, 150), QSizeF(100, 150)},
                 gridOptions);

    QCOMPARE(grid.scrollOffsetForPage(4, 200, PageScrollAlignment::Top), 310.0);
    QCOMPARE(grid.scrollOffsetForPage(4, 200, PageScrollAlignment::Center),
             310.0);
}

void TestPDFContinuousBlueprint::
    testViewportDocumentOffsetTransformsBothAxes() {
    PDFContinuousBlueprint blueprint;
    blueprint.rebuild({QSizeF(200, 300)}, defaultOptions());

    const QPointF offset(80.0, 120.0);
    const QPointF viewportPoint(25.0, 35.0);
    const QPointF documentPoint =
        blueprint.viewportToDocument(viewportPoint, offset);
    QCOMPARE(documentPoint, QPointF(105.0, 155.0));
    QCOMPARE(blueprint.documentToViewport(documentPoint, offset),
             viewportPoint);
}

QTEST_MAIN(TestPDFContinuousBlueprint)
#include "test_pdf_continuous_blueprint.moc"
