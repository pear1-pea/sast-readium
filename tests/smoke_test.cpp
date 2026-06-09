#include <QApplication>
#include <QDebug>
#include <QtTest/QtTest>

/**
 * Simple smoke test to verify basic compilation.
 */
class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testBasicQtFunctionality();
    void cleanupTestCase();
};

void SmokeTest::initTestCase() {
    qDebug() << "=== SAST Readium Smoke Test ===";
    qDebug() << "Qt version:" << QT_VERSION_STR;
}

void SmokeTest::testBasicQtFunctionality() {
    // Test basic Qt functionality
    QString testString = "SAST Readium Test";
    QCOMPARE(testString.length(), 17);

    QStringList testList;
    testList << "PDF"
             << "Readium";
    QCOMPARE(testList.size(), 2);

    qDebug() << "Basic Qt functionality works";
}

void SmokeTest::cleanupTestCase() {
    qDebug() << "=== Smoke Test Completed Successfully ===";
}

// Simple main function for standalone execution
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Set up for headless testing
    app.setAttribute(Qt::AA_Use96Dpi, true);

    SmokeTest test;
    int result = QTest::qExec(&test, argc, argv);

    if (result == 0) {
        qDebug() << "SUCCESS: Smoke test passed";
    } else {
        qDebug() << "FAILURE: Smoke test failed";
    }

    return result;
}

#include "smoke_test.moc"
