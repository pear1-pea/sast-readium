#include "PDFRenderCache.h"
#include <QMutexLocker>
#include <QtGlobal>

bool PDFRenderCache::CacheKey::operator==(const CacheKey& other) const {
    return pageNumber == other.pageNumber &&
           qAbs(scaleFactor - other.scaleFactor) < 0.01 &&
           rotation == other.rotation;
}

bool PDFRenderCache::CacheKey::operator<(const CacheKey& other) const {
    if (pageNumber != other.pageNumber)
        return pageNumber < other.pageNumber;
    if (qAbs(scaleFactor - other.scaleFactor) >= 0.01)
        return scaleFactor < other.scaleFactor;
    return rotation < other.rotation;
}

size_t qHash(const PDFRenderCache::CacheKey& key, size_t seed) {
    return qHashMulti(seed, key.pageNumber,
                      static_cast<int>(key.scaleFactor * 100), key.rotation);
}

PDFRenderCache::PDFRenderCache(int maxCostMB)
    : m_cache(maxCostMB * 1024 * 1024)  // 转换为字节
{}

void PDFRenderCache::insert(const CacheKey& key, const QPixmap& pixmap) {
    QMutexLocker locker(&m_mutex);
    int cost = pixmap.width() * pixmap.height() * 4;  // RGBA
    m_cache.insert(key, new QPixmap(pixmap), cost);
}

QPixmap PDFRenderCache::get(const CacheKey& key) {
    QMutexLocker locker(&m_mutex);
    QPixmap* pixmap = m_cache.object(key);
    return pixmap ? *pixmap : QPixmap();
}

bool PDFRenderCache::contains(const CacheKey& key) const {
    QMutexLocker locker(&m_mutex);
    return m_cache.contains(key);
}

void PDFRenderCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
}

void PDFRenderCache::setMaxCost(int maxCostMB) {
    QMutexLocker locker(&m_mutex);
    m_cache.setMaxCost(maxCostMB * 1024 * 1024);
}

int PDFRenderCache::size() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}

int PDFRenderCache::maxCost() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.maxCost() / (1024 * 1024);  // 转换为 MB
}
