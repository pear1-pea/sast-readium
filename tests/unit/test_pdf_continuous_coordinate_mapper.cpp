#include <QtTest/QtTest>

#include "../../app/ui/continuous/PDFContinuousCoordinateMapper.h"

class TestPDFContinuousCoordinateMapper : public QObject {
    Q_OBJECT

private slots:
    void testViewportDocumentRoundTrip();
    void testRotatedPageRoundTrip();
    void testNormalizedRectMapping();
    void testPdfRectToNormalizedMapping();
    void testStrictHitTestUsesViewportOffset();
};

static std::shared_ptr<const PDFContinuousBlueprint> makeBlueprint(
    int rotation = 0) {
    PDFContinuousLayoutOptions options;
    options.zoom = 2.0;
    options.rotation = rotation;
    options.viewportWidth = 600.0;
    options.documentMargins = QMarginsF(10.0, 10.0, 10.0, 10.0);
    auto blueprint = std::make_shared<PDFContinuousBlueprint>();
    blueprint->rebuild({QSizeF(200.0, 300.0)}, options);
    return blueprint;
}

void TestPDFContinuousCoordinateMapper::testViewportDocumentRoundTrip() {
    PDFContinuousCoordinateMapper mapper;
    mapper.setBlueprint(makeBlueprint());
    mapper.setContentOffset(QPointF(30.0, 40.0));

    const QPointF viewport(25.0, 35.0);
    QCOMPARE(mapper.documentToViewport(mapper.viewportToDocument(viewport)),
             viewport);
}

void TestPDFContinuousCoordinateMapper::testRotatedPageRoundTrip() {
    PDFContinuousCoordinateMapper mapper;
    mapper.setBlueprint(makeBlueprint(90));

    const QPointF original(40.0, 80.0);
    const QPointF page = mapper.originalTopLeftToPage(0, original);
    const QPointF restored = mapper.pageToOriginalTopLeft(0, page);
    QVERIFY((restored - original).manhattanLength() < 0.0001);
}

void TestPDFContinuousCoordinateMapper::testNormalizedRectMapping() {
    PDFContinuousCoordinateMapper mapper;
    mapper.setBlueprint(makeBlueprint(180));

    const QRectF normalized(0.2, 0.3, 0.25, 0.1);
    const QRectF restored = mapper.pageRectToNormalizedOriginal(
        0, mapper.normalizedOriginalRectToPage(0, normalized));
    QVERIFY((restored.topLeft() - normalized.topLeft()).manhattanLength() <
            0.0001);
    QVERIFY(qAbs(restored.width() - normalized.width()) < 0.0001);
    QVERIFY(qAbs(restored.height() - normalized.height()) < 0.0001);
}

void TestPDFContinuousCoordinateMapper::testPdfRectToNormalizedMapping() {
    PDFContinuousCoordinateMapper mapper;
    mapper.setBlueprint(makeBlueprint(0));
    const QRectF pdfRect(40.0, 80.0, 50.0, 30.0);
    const QRectF normalized = mapper.pdfRectToNormalizedOriginal(0, pdfRect);
    QCOMPARE(normalized, QRectF(0.2, 0.6333333333333333, 0.25, 0.1));
}

void TestPDFContinuousCoordinateMapper::testStrictHitTestUsesViewportOffset() {
    PDFContinuousCoordinateMapper mapper;
    mapper.setBlueprint(makeBlueprint());
    mapper.setContentOffset(QPointF(0.0, 100.0));

    QVERIFY(mapper.hitTestViewport(QPointF(300.0, 20.0)).has_value());
    QVERIFY(!mapper.hitTestViewport(QPointF(300.0, 590.0)).has_value());
}

QTEST_MAIN(TestPDFContinuousCoordinateMapper)
#include "test_pdf_continuous_coordinate_mapper.moc"
