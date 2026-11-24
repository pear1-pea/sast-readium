#include <QApplication>
#include <QPixmap>
#include <QtTest/QtTest>

#include "ui/viewer/PDFRenderCache.h"

class TestPDFRenderCache : public QObject {
    Q_OBJECT

private slots:
    void equalKeysShareHash();
    void quantizedKeysCollapse();
    void tokenAndDprDistinguishKeys();
    void insertAndGet();
    void nullPixmapIsNotStored();
    void oversizedFrameIsNotCached();
    void evictsWithinBudget();
};

void TestPDFRenderCache::equalKeysShareHash() {
    const auto first = PDFRenderCache::CacheKey::make(7, 3, 1.5, 90, 2.0);
    const auto second = PDFRenderCache::CacheKey::make(7, 3, 1.5, 90, 2.0);
    QVERIFY(first == second);
    QCOMPARE(qHash(first), qHash(second));
    QCOMPARE(qHash(first, 42), qHash(second, 42));
}

void TestPDFRenderCache::quantizedKeysCollapse() {
    // Equality is defined by the same integer quantization used for hashing,
    // so two keys that compare equal always hash equal.
    const auto first = PDFRenderCache::CacheKey::make(0, 0, 1.00001, 0, 2.0);
    const auto second = PDFRenderCache::CacheKey::make(0, 0, 1.00002, 0, 2.0);
    QVERIFY(first == second);
    QCOMPARE(first.zoomKey, second.zoomKey);
    QCOMPARE(qHash(first), qHash(second));

    const auto different = PDFRenderCache::CacheKey::make(0, 0, 1.01, 0, 2.0);
    QVERIFY(!(first == different));
}

void TestPDFRenderCache::tokenAndDprDistinguishKeys() {
    const auto base = PDFRenderCache::CacheKey::make(1, 0, 1.0, 0, 1.0);
    const auto otherToken = PDFRenderCache::CacheKey::make(2, 0, 1.0, 0, 1.0);
    const auto otherDpr = PDFRenderCache::CacheKey::make(1, 0, 1.0, 0, 2.0);
    const auto otherRotation =
        PDFRenderCache::CacheKey::make(1, 0, 1.0, 90, 1.0);

    QVERIFY(!(base == otherToken));
    QVERIFY(!(base == otherDpr));
    QVERIFY(!(base == otherRotation));
    QVERIFY(qHash(base) != qHash(otherToken));
    QVERIFY(qHash(base) != qHash(otherDpr));
    QVERIFY(qHash(base) != qHash(otherRotation));
}

void TestPDFRenderCache::insertAndGet() {
    PDFRenderCache cache(4);
    const auto key = PDFRenderCache::CacheKey::make(1, 0, 1.0, 0, 1.0);
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::white);

    QVERIFY(!cache.contains(key));
    cache.insert(key, pixmap);
    QVERIFY(cache.contains(key));
    QCOMPARE(cache.get(key).size(), pixmap.size());

    cache.clear();
    QVERIFY(!cache.contains(key));
}

void TestPDFRenderCache::nullPixmapIsNotStored() {
    PDFRenderCache cache(4);
    const auto key = PDFRenderCache::CacheKey::make(1, 0, 1.0, 0, 1.0);
    cache.insert(key, QPixmap());
    QVERIFY(!cache.contains(key));
}

void TestPDFRenderCache::oversizedFrameIsNotCached() {
    PDFRenderCache cache(1);
    const auto key = PDFRenderCache::CacheKey::make(1, 0, 1.0, 0, 1.0);
    QPixmap huge(2048, 2048);  // 16 MB of RGBA, larger than the 1 MB budget
    huge.fill(Qt::white);

    cache.insert(key, huge);
    QVERIFY(!cache.contains(key));
}

void TestPDFRenderCache::evictsWithinBudget() {
    PDFRenderCache cache(1);
    QList<PDFRenderCache::CacheKey> keys;
    for (int i = 0; i < 6; ++i) {
        const auto key = PDFRenderCache::CacheKey::make(1, i, 1.0, 0, 1.0);
        keys.append(key);
        QPixmap pixmap(256, 256);  // 256 KB each
        pixmap.fill(Qt::white);
        cache.insert(key, pixmap);
    }

    QVERIFY(cache.size() > 0);
    QVERIFY(cache.size() <= 4);  // 1 MB / 256 KB
    // The most recently inserted key must still be present.
    QVERIFY(cache.contains(keys.last()));
}

QTEST_MAIN(TestPDFRenderCache)
#include "test_pdf_render_cache.moc"
