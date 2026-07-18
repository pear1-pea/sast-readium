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

QTEST_MAIN(TestPDFContinuousBlueprint)
#include "test_pdf_continuous_blueprint.moc"
