#include <QImage>
#include <QtTest/QtTest>

#include "../../app/ui/continuous/PDFContinuousImageCache.h"

class TestPDFContinuousImageCache : public QObject {
    Q_OBJECT

private slots:
    void testCostBoundedEviction();
    void testHitMissStatistics();
};

static PDFContinuousRenderKey key(int page) {
    PDFContinuousRenderKey result;
    result.documentToken = 1;
    result.pageIndex = page;
    return result;
}

void TestPDFContinuousImageCache::testCostBoundedEviction() {
    QImage image(10, 10, QImage::Format_RGBA8888);
    PDFContinuousImageCache cache(image.sizeInBytes() * 2);

    cache.insert(key(0), image);
    cache.insert(key(1), image);
    QVERIFY(cache.contains(key(0)));
    QVERIFY(cache.contains(key(1)));

    cache.insert(key(2), image);
    QVERIFY(!cache.contains(key(0)));
    QVERIFY(cache.contains(key(1)));
    QVERIFY(cache.contains(key(2)));
    QVERIFY(cache.currentCostBytes() <= cache.maxCostBytes());
}

void TestPDFContinuousImageCache::testHitMissStatistics() {
    QImage image(4, 4, QImage::Format_RGBA8888);
    PDFContinuousImageCache cache(image.sizeInBytes() * 2);

    QVERIFY(cache.get(key(0)).isNull());
    cache.insert(key(0), image);
    QVERIFY(!cache.get(key(0)).isNull());
    QCOMPARE(cache.hitCount(), quint64(1));
    QCOMPARE(cache.missCount(), quint64(1));

    cache.resetStatistics();
    QCOMPARE(cache.hitCount(), quint64(0));
    QCOMPARE(cache.missCount(), quint64(0));
}

QTEST_MAIN(TestPDFContinuousImageCache)
#include "test_pdf_continuous_image_cache.moc"
