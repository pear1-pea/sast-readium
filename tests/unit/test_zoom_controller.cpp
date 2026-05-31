#include <QApplication>
#include <QSignalSpy>
#include <QtTest/QtTest>
#include "../../app/controller/ZoomController.h"

class TestZoomController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testDefaultState();
    void testSetZoom();
    void testZoomInOut();
    void testZoomClamping();
    void testZoomSignal();
    void testZoomType();
    void testFromPercentage();
    void testPersistence();
};

void TestZoomController::initTestCase() {
    QCoreApplication::setOrganizationName("SASTReadiumTest");
    QCoreApplication::setApplicationName("ZoomControllerTest");
}

void TestZoomController::testDefaultState() {
    ZoomController ctrl;
    QCOMPARE(ctrl.currentZoom(), 1.0);
    QCOMPARE(ctrl.zoomType(), ZoomType::FixedValue);
}

void TestZoomController::testSetZoom() {
    ZoomController ctrl;
    ctrl.setZoom(2.5);
    QCOMPARE(ctrl.currentZoom(), 2.5);

    ctrl.setZoom(0.5);
    QCOMPARE(ctrl.currentZoom(), 0.5);
}

void TestZoomController::testZoomInOut() {
    ZoomController ctrl;
    double start = ctrl.currentZoom();

    ctrl.zoomIn();
    QCOMPARE(ctrl.currentZoom(), start + 0.1);

    ctrl.zoomOut();
    QCOMPARE(ctrl.currentZoom(), start);  // back to original

    // zoom out below minimum
    for (int i = 0; i < 50; ++i)
        ctrl.zoomOut();
    QVERIFY(ctrl.currentZoom() >= 0.1);
}

void TestZoomController::testZoomClamping() {
    ZoomController ctrl;
    ctrl.setZoom(0.01);  // below min
    QCOMPARE(ctrl.currentZoom(), 0.1);

    ctrl.setZoom(10.0);  // above max
    QCOMPARE(ctrl.currentZoom(), 5.0);
}

void TestZoomController::testZoomSignal() {
    ZoomController ctrl;
    QSignalSpy spy(&ctrl, &ZoomController::zoomChanged);

    ctrl.setZoom(2.0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toDouble(), 2.0);

    // No signal for same value (change < 0.001)
    ctrl.setZoom(2.0005);
    QCOMPARE(spy.count(), 1);  // no new signal
}

void TestZoomController::testZoomType() {
    ZoomController ctrl;

    ctrl.setZoom(1.0, ZoomType::FitPage);
    QCOMPARE(ctrl.zoomType(), ZoomType::FitPage);

    ctrl.setZoom(2.0, ZoomType::FitWidth);
    QCOMPARE(ctrl.zoomType(), ZoomType::FitWidth);

    ctrl.setZoom(1.5, ZoomType::FitHeight);
    QCOMPARE(ctrl.zoomType(), ZoomType::FitHeight);
}

void TestZoomController::testFromPercentage() {
    ZoomController ctrl;
    ctrl.setFromPercentage(50);
    QCOMPARE(ctrl.currentZoom(), 0.5);

    ctrl.setFromPercentage(200);
    QCOMPARE(ctrl.currentZoom(), 2.0);
}

void TestZoomController::testPersistence() {
    ZoomController ctrl;
    ctrl.setZoom(2.5, ZoomType::FitWidth);

    ctrl.saveSettings();

    ZoomController restored;
    restored.loadSettings();
    QCOMPARE(restored.currentZoom(), 2.5);
    QCOMPARE(restored.zoomType(), ZoomType::FitWidth);
}

QTEST_MAIN(TestZoomController)
#include "test_zoom_controller.moc"
