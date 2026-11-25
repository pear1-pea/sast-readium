#include "PDFContinuousImageCache.h"

#include <QMutexLocker>
#include <QtGlobal>

#include <cmath>

namespace {

constexpr int kZoomQuantize = 10000;
constexpr int kDprQuantize = 100;

// Equality and hashing must agree on the same integer quantization.
// Floating-point tolerances are not transitive and break hash lookup.
int quantizedZoom(double zoom) {
    return std::isfinite(zoom) ? qMax(1, qRound(zoom * kZoomQuantize)) : 1;
}

int quantizedDpr(double devicePixelRatio) {
    return std::isfinite(devicePixelRatio)
               ? qMax(1, qRound(devicePixelRatio * kDprQuantize))
               : kDprQuantize;
}

}  // namespace

bool PDFContinuousRenderKey::operator==(
    const PDFContinuousRenderKey& other) const {
    return documentToken == other.documentToken &&
           pageIndex == other.pageIndex &&
           quantizedZoom(zoom) == quantizedZoom(other.zoom) &&
           rotation == other.rotation &&
           quantizedDpr(devicePixelRatio) ==
               quantizedDpr(other.devicePixelRatio);
}

size_t qHash(const PDFContinuousRenderKey& key, size_t seed) {
    return qHashMulti(seed, key.documentToken, key.pageIndex,
                      quantizedZoom(key.zoom), key.rotation,
                      quantizedDpr(key.devicePixelRatio));
}

bool PDFContinuousPendingKey::operator==(
    const PDFContinuousPendingKey& other) const {
    return generation == other.generation && renderKey == other.renderKey;
}

size_t qHash(const PDFContinuousPendingKey& key, size_t seed) {
    return qHashMulti(seed, key.generation, key.renderKey);
}

PDFContinuousImageCache::PDFContinuousImageCache(qint64 maxCostBytes)
    : m_cache(qMax<qint64>(0, maxCostBytes)) {}

void PDFContinuousImageCache::insert(const PDFContinuousRenderKey& key,
                                     const QImage& image) {
    if (image.isNull()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    const qint64 cost = imageCost(image);
    // An entry larger than the whole budget would evict every other frame and
    // still not fit; keep it out of the cache instead.
    if (cost <= 0 || cost > m_cache.maxCost()) {
        return;
    }
    m_cache.insert(key, new QImage(image), cost);
}

QImage PDFContinuousImageCache::get(const PDFContinuousRenderKey& key) const {
    QMutexLocker locker(&m_mutex);
    // object() relinks the entry to the front, which makes it the most
    // recently used one.
    QImage* image = m_cache.object(key);
    if (!image) {
        ++m_missCount;
        return QImage();
    }

    ++m_hitCount;
    return *image;
}

bool PDFContinuousImageCache::contains(
    const PDFContinuousRenderKey& key) const {
    QMutexLocker locker(&m_mutex);
    return m_cache.contains(key);
}

void PDFContinuousImageCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
}

void PDFContinuousImageCache::setMaxCostBytes(qint64 maxCostBytes) {
    QMutexLocker locker(&m_mutex);
    m_cache.setMaxCost(qMax<qint64>(0, maxCostBytes));
}

qint64 PDFContinuousImageCache::maxCostBytes() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.maxCost();
}

qint64 PDFContinuousImageCache::currentCostBytes() const {
    QMutexLocker locker(&m_mutex);
    return m_cache.totalCost();
}

quint64 PDFContinuousImageCache::hitCount() const {
    QMutexLocker locker(&m_mutex);
    return m_hitCount;
}

quint64 PDFContinuousImageCache::missCount() const {
    QMutexLocker locker(&m_mutex);
    return m_missCount;
}

void PDFContinuousImageCache::resetStatistics() {
    QMutexLocker locker(&m_mutex);
    m_hitCount = 0;
    m_missCount = 0;
}

qint64 PDFContinuousImageCache::imageCost(const QImage& image) {
    return image.sizeInBytes();
}
