#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include "ui/thumbnail/RecentPdfThumbnailProvider.h"

class RecentPdfThumbnailProviderTest : public QObject {
    Q_OBJECT

private slots:
    void cacheKeyIsStableForSamePathAndSize();
    void cacheKeyChangesWhenSizeChanges();
    void diskCacheFreshnessUsesFileModificationTimes();
};

void RecentPdfThumbnailProviderTest::cacheKeyIsStableForSamePathAndSize() {
    const QString first = RecentPdfThumbnailProvider::cacheKeyForTesting(
        "/tmp/sample.pdf", QSize(72, 72));
    const QString second = RecentPdfThumbnailProvider::cacheKeyForTesting(
        "/tmp/sample.pdf", QSize(72, 72));

    QVERIFY(!first.isEmpty());
    QCOMPARE(first, second);
}

void RecentPdfThumbnailProviderTest::cacheKeyChangesWhenSizeChanges() {
    const QString small = RecentPdfThumbnailProvider::cacheKeyForTesting(
        "/tmp/sample.pdf", QSize(72, 72));
    const QString large = RecentPdfThumbnailProvider::cacheKeyForTesting(
        "/tmp/sample.pdf", QSize(96, 96));

    QVERIFY(small != large);
}

void RecentPdfThumbnailProviderTest::
    diskCacheFreshnessUsesFileModificationTimes() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString sourcePath = tempDir.filePath("source.pdf");
    const QString cachePath = tempDir.filePath("cache.png");

    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QVERIFY(source.write("pdf") > 0);
    source.close();

    QFile cache(cachePath);
    QVERIFY(cache.open(QIODevice::WriteOnly));
    QVERIFY(cache.write("png") > 0);
    cache.close();

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QFile sourceFile(sourcePath);
    QFile cacheFile(cachePath);
    QVERIFY(sourceFile.open(QIODevice::ReadWrite));
    QVERIFY(cacheFile.open(QIODevice::ReadWrite));

    QVERIFY(sourceFile.setFileTime(now.addSecs(-10),
                                   QFileDevice::FileModificationTime));
    QVERIFY(cacheFile.setFileTime(now, QFileDevice::FileModificationTime));
    QVERIFY(RecentPdfThumbnailProvider::isDiskCacheFreshForTesting(sourcePath,
                                                                   cachePath));

    QVERIFY(sourceFile.setFileTime(now.addSecs(10),
                                   QFileDevice::FileModificationTime));
    QVERIFY(!RecentPdfThumbnailProvider::isDiskCacheFreshForTesting(sourcePath,
                                                                    cachePath));
}

QTEST_MAIN(RecentPdfThumbnailProviderTest)
#include "test_recent_pdf_thumbnail_provider.moc"
