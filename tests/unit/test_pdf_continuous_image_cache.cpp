#include <QImage>
#include <QtTest/QtTest>

#include "../../app/ui/continuous/PDFContinuousImageCache.h"

class TestPDFContinuousImageCache : public QObject {
    Q_OBJECT

private slots:
    void testCostBoundedEviction();
    void testHitMissStatistics();
    void testEvictionFollowsAccessOrder();
    void testReplacingKeyDoesNotDoubleCountCost();
    void testOversizedImageIsRejected();
    void testLoweringBudgetEvictsLeastRecent();
    void testEqualZoomKeysShareOneEntry();
    void testQuantizedKeysAgreeWithHash();
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

void TestPDFContinuousImageCache::testEvictionFollowsAccessOrder() {
    QImage image(10, 10, QImage::Format_RGBA8888);
    PDFContinuousImageCache cache(image.sizeInBytes() * 2);

    cache.insert(key(0), image);
    cache.insert(key(1), image);
    QVERIFY(!cache.get(key(0)).isNull());

    // key(0) was just read, so key(1) is the least recently used entry.
    cache.insert(key(2), image);
    QVERIFY(!cache.contains(key(1)));
    QVERIFY(cache.contains(key(0)));
    QVERIFY(cache.contains(key(2)));
    QCOMPARE(cache.currentCostBytes(), qint64(image.sizeInBytes() * 2));
}

void TestPDFContinuousImageCache::testReplacingKeyDoesNotDoubleCountCost() {
    QImage image(10, 10, QImage::Format_RGBA8888);
    PDFContinuousImageCache cache(image.sizeInBytes() * 2);

    cache.insert(key(0), image);
    cache.insert(key(0), image);
    QCOMPARE(cache.currentCostBytes(), qint64(image.sizeInBytes()));

    cache.insert(key(1), image);
    QVERIFY(cache.contains(key(0)));
    QVERIFY(cache.contains(key(1)));
    QCOMPARE(cache.currentCostBytes(), qint64(image.sizeInBytes() * 2));
}

void TestPDFContinuousImageCache::testOversizedImageIsRejected() {
    QImage image(10, 10, QImage::Format_RGBA8888);
    QImage oversized(20, 10, QImage::Format_RGBA8888);
    PDFContinuousImageCache cache(image.sizeInBytes());

    cache.insert(key(0), image);
    cache.insert(key(1), oversized);

    QVERIFY(!cache.contains(key(1)));
    QVERIFY(cache.contains(key(0)));
    QCOMPARE(cache.currentCostBytes(), qint64(image.sizeInBytes()));
}

void TestPDFContinuousImageCache::testLoweringBudgetEvictsLeastRecent() {
    QImage image(10, 10, QImage::Format_RGBA8888);
    PDFContinuousImageCache cache(image.sizeInBytes() * 3);

    cache.insert(key(0), image);
    cache.insert(key(1), image);
    cache.insert(key(2), image);

    cache.setMaxCostBytes(image.sizeInBytes());
    QCOMPARE(cache.maxCostBytes(), qint64(image.sizeInBytes()));
    QVERIFY(cache.contains(key(2)));
    QVERIFY(!cache.contains(key(0)));
    QVERIFY(!cache.contains(key(1)));
    QCOMPARE(cache.currentCostBytes(), qint64(image.sizeInBytes()));
}

void TestPDFContinuousImageCache::testEqualZoomKeysShareOneEntry() {
    QImage image(10, 10, QImage::Format_RGBA8888);
    PDFContinuousImageCache cache(image.sizeInBytes() * 2);

    PDFContinuousRenderKey first;
    first.documentToken = 1;
    first.pageIndex = 0;
    first.zoom = 1.00007;

    PDFContinuousRenderKey second = first;
    second.zoom = 1.00013;

    QVERIFY(first == second);
    QCOMPARE(qHash(first), qHash(second));

    cache.insert(first, image);
    // The second key quantizes to the same slot, so it must hit and must not
    // add a duplicate entry.
    QVERIFY(!cache.get(second).isNull());
    QCOMPARE(cache.missCount(), quint64(0));
    QCOMPARE(cache.currentCostBytes(), qint64(image.sizeInBytes()));
}

void TestPDFContinuousImageCache::testQuantizedKeysAgreeWithHash() {
    PDFContinuousRenderKey reference;
    reference.documentToken = 3;
    reference.pageIndex = 7;
    reference.zoom = 1.00007;

    PDFContinuousRenderKey sameZoom = reference;
    sameZoom.zoom = 1.00013;
    QVERIFY(reference == sameZoom);
    QCOMPARE(qHash(reference), qHash(sameZoom));

    PDFContinuousRenderKey otherZoom = reference;
    otherZoom.zoom = 1.00022;
    QVERIFY(!(reference == otherZoom));

    PDFContinuousRenderKey dprA;
    dprA.documentToken = 3;
    dprA.pageIndex = 7;
    dprA.devicePixelRatio = 2.00004;
    PDFContinuousRenderKey dprB = dprA;
    dprB.devicePixelRatio = 2.00006;
    QVERIFY(dprA == dprB);
    QCOMPARE(qHash(dprA), qHash(dprB));
}

QTEST_MAIN(TestPDFContinuousImageCache)
#include "test_pdf_continuous_image_cache.moc"
