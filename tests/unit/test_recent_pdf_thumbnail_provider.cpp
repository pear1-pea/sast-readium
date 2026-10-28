#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include "ui/thumbnail/RecentPdfThumbnailProvider.h"

class RecentPdfThumbnailProviderTest : public QObject {
    Q_OBJECT

private slots:
    void cacheKeyIsStableForSamePathAndSize();
    void cacheKeyChangesWhenSizeChanges();
    void diskCacheFreshnessUsesSourceMetadata();
    void memoryCacheInvalidatesWhenSourceChanges();
    void providerStatisticsStartAtZero();
};

void RecentPdfThumbnailProviderTest::cacheKeyIsStableForSamePathAndSize() {
    const QString first = RecentPdfThumbnailProvider::cacheKeyForTesting(
        "/tmp/sample.pdf", QSize(72, 72));
    const QString second = RecentPdfThumbnailProvider::cacheKeyForTesting(
        "/tmp/sample.pdf", QSize(72, 72));

    QVERIFY(!first.isEmpty());
    QCOMPARE(first, second);
}

void RecentPdfThumbnailProviderTest::memoryCacheInvalidatesWhenSourceChanges() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString sourcePath = tempDir.filePath("source.pdf");
    {
        QPdfWriter writer(sourcePath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.setFont(QFont(QStringLiteral("Helvetica"), 14));
        painter.drawText(50, 50, QStringLiteral("thumbnail source"));
        painter.end();
    }

    RecentPdfThumbnailProvider provider;
    QSignalSpy ready(&provider, &RecentPdfThumbnailProvider::thumbnailReady);
    provider.requestThumbnail(sourcePath, QSize(64, 64));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QVERIFY(!provider.cachedThumbnail(sourcePath, QSize(64, 64)).isNull());

    QFile file(sourcePath);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(5),
                             QFileDevice::FileModificationTime));
    file.close();

    QVERIFY(provider.cachedThumbnail(sourcePath, QSize(64, 64)).isNull());
}

void RecentPdfThumbnailProviderTest::providerStatisticsStartAtZero() {
    RecentPdfThumbnailProvider provider;
    QCOMPARE(provider.memoryHitCount(), quint64(0));
    QCOMPARE(provider.diskHitCount(), quint64(0));
    provider.resetStatistics();
    QCOMPARE(provider.memoryHitCount(), quint64(0));
    QCOMPARE(provider.diskHitCount(), quint64(0));
}

void RecentPdfThumbnailProviderTest::cacheKeyChangesWhenSizeChanges() {
    const QString small = RecentPdfThumbnailProvider::cacheKeyForTesting(
        "/tmp/sample.pdf", QSize(72, 72));
    const QString large = RecentPdfThumbnailProvider::cacheKeyForTesting(
        "/tmp/sample.pdf", QSize(96, 96));

    QVERIFY(small != large);
}

void RecentPdfThumbnailProviderTest::diskCacheFreshnessUsesSourceMetadata() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString sourcePath = tempDir.filePath("source.pdf");
    const QString cachePath = tempDir.filePath("cache.png");

    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QVERIFY(source.write("pdf") > 0);
    source.close();

    QFile sourceFile(sourcePath);
    QVERIFY(sourceFile.open(QIODevice::ReadWrite));
    const QDateTime modified = QDateTime::currentDateTimeUtc().addSecs(-10);
    QVERIFY(
        sourceFile.setFileTime(modified, QFileDevice::FileModificationTime));
    sourceFile.close();

    const QFileInfo sourceInfo(sourcePath);
    const qint64 mtime = sourceInfo.lastModified().toMSecsSinceEpoch();
    const qint64 size = sourceInfo.size();

    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setText("Thumb::MTime", QString::number(mtime));
    image.setText("Thumb::Size", QString::number(size));
    QVERIFY(image.save(cachePath, "PNG"));
    QVERIFY(RecentPdfThumbnailProvider::isDiskCacheFreshForTesting(sourcePath,
                                                                   cachePath));

    QFile changedSource(sourcePath);
    QVERIFY(changedSource.open(QIODevice::ReadWrite));
    QVERIFY(changedSource.setFileTime(modified.addSecs(10),
                                      QFileDevice::FileModificationTime));
    changedSource.close();
    QVERIFY(!RecentPdfThumbnailProvider::isDiskCacheFreshForTesting(sourcePath,
                                                                    cachePath));

    QByteArray larger("pdf with more bytes");
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(source.write(larger) > 0);
    source.close();
    QVERIFY(sourceFile.open(QIODevice::ReadWrite));
    QVERIFY(
        sourceFile.setFileTime(modified, QFileDevice::FileModificationTime));
    sourceFile.close();
    QVERIFY(!RecentPdfThumbnailProvider::isDiskCacheFreshForTesting(sourcePath,
                                                                    cachePath));

    QImage missingMetadata(2, 2, QImage::Format_RGB32);
    missingMetadata.fill(Qt::red);
    QVERIFY(missingMetadata.save(cachePath, "PNG"));
    QVERIFY(!RecentPdfThumbnailProvider::isDiskCacheFreshForTesting(sourcePath,
                                                                    cachePath));
}

QTEST_MAIN(RecentPdfThumbnailProviderTest)
#include "test_recent_pdf_thumbnail_provider.moc"
