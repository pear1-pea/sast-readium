#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QtTest/QtTest>

#include "../../app/managers/ReadingProgressManager.h"

class ReadingProgressManagerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void testMissingFileDefaultsToFirstPage();
    void testPagePersistsAcrossInstances();
    void testEmptyPathIsIgnored();
};

void ReadingProgressManagerTest::initTestCase() {
    QCoreApplication::setOrganizationName("SASTReadiumTest");
    QCoreApplication::setApplicationName("ReadingProgressManagerTest");
}

void ReadingProgressManagerTest::cleanup() {
    QSettings settings;
    settings.clear();
    settings.sync();
}

void ReadingProgressManagerTest::testMissingFileDefaultsToFirstPage() {
    ReadingProgressManager manager;
    QCOMPARE(manager.pageForFile("/tmp/missing-document.pdf"), 0);
}

void ReadingProgressManagerTest::testPagePersistsAcrossInstances() {
    const QString filePath = QDir::temp().filePath("reading-progress.pdf");

    ReadingProgressManager writer;
    writer.savePageForFile(filePath, 12);

    ReadingProgressManager reader;
    QCOMPARE(reader.pageForFile(filePath), 12);

    writer.savePageForFile(filePath, -1);
    QCOMPARE(reader.pageForFile(filePath), 0);
}

void ReadingProgressManagerTest::testEmptyPathIsIgnored() {
    ReadingProgressManager manager;
    manager.savePageForFile(QString(), 5);
    QCOMPARE(manager.pageForFile(QString()), 0);
}

QTEST_MAIN(ReadingProgressManagerTest)
#include "test_reading_progress_manager.moc"
