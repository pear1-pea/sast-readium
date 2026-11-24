#include "PDFRenderCache.h"
#include <QMutexLocker>
#include <QtGlobal>
#include <cmath>
#include <limits>

namespace {

constexpr int kZoomQuantize = 10000;
constexpr int kDprQuantize = 100;

}  // namespace

PDFRenderCache::CacheKey PDFRenderCache::CacheKey::make(
    quint64 documentTokenValue, int pageNumberValue, double scaleFactor,
    int rotationValue, double devicePixelRatio) {
    CacheKey key;
    key.documentToken = documentTokenValue;
    key.pageNumber = pageNumberValue;
    key.zoomKey = std::isfinite(scaleFactor)
                      ? qMax(1, qRound(scaleFactor * kZoomQuantize))
                      : 1;
    key.rotation = rotationValue;
    key.dprKey = std::isfinite(devicePixelRatio)
                     ? qMax(1, qRound(devicePixelRatio * kDprQuantize))
                     : kDprQuantize;
    return key;
}

bool PDFRenderCache::CacheKey::operator==(const CacheKey& other) const {
    return documentToken == other.documentToken &&
           pageNumber == other.pageNumber && zoomKey == other.zoomKey &&
           rotation == other.rotation && dprKey == other.dprKey;
}

size_t qHash(const PDFRenderCache::CacheKey& key, size_t seed) {
    return qHashMulti(seed, key.documentToken, key.pageNumber, key.zoomKey,
                      key.rotation, key.dprKey);
}

PDFRenderCache::PDFRenderCache(int maxCostMB)
    : m_cache(qMax(0, maxCostMB) * 1024 * 1024)  // 转换为字节
{}

void PDFRenderCache::insert(const CacheKey& key, const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        return;
    }
    QMutexLocker locker(&m_mutex);
    const qint64 costBytes = qint64(pixmap.width()) * pixmap.height() * 4;
    // A frame larger than the whole budget would evict everything else and
    // still not fit; keep it out of the cache instead.
    if (costBytes <= 0 || costBytes > m_cache.maxCost() ||
        costBytes > std::numeric_limits<int>::max()) {
        return;
    }
    m_cache.insert(key, new QPixmap(pixmap), int(costBytes));
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
    m_cache.setMaxCost(qMax(0, maxCostMB) * 1024 * 1024);
}

int PDFRenderCache::size() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}

int PDFRenderCache::maxCost() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.maxCost() / (1024 * 1024);  // 转换为 MB
}
