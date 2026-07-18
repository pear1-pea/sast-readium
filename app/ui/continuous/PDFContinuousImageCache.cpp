#include "PDFContinuousImageCache.h"

#include <QMutexLocker>
#include <QtGlobal>

bool PDFContinuousRenderKey::operator==(
    const PDFContinuousRenderKey& other) const {
    return pageIndex == other.pageIndex && qAbs(zoom - other.zoom) < 0.0001 &&
           rotation == other.rotation &&
           qAbs(devicePixelRatio - other.devicePixelRatio) < 0.0001;
}

size_t qHash(const PDFContinuousRenderKey& key, size_t seed) {
    return qHashMulti(seed, key.pageIndex, static_cast<int>(key.zoom * 10000),
                      key.rotation,
                      static_cast<int>(key.devicePixelRatio * 10000));
}

bool PDFContinuousPendingKey::operator==(
    const PDFContinuousPendingKey& other) const {
    return generation == other.generation && renderKey == other.renderKey;
}

size_t qHash(const PDFContinuousPendingKey& key, size_t seed) {
    return qHashMulti(seed, key.generation, key.renderKey);
}

PDFContinuousImageCache::PDFContinuousImageCache(qint64 maxCostBytes)
    : m_maxCostBytes(maxCostBytes) {}

void PDFContinuousImageCache::insert(const PDFContinuousRenderKey& key,
                                     const QImage& image) {
    if (image.isNull()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    if (m_images.contains(key)) {
        m_currentCostBytes -= imageCost(m_images.value(key));
        m_lruOrder.removeAll(key);
    }

    m_images.insert(key, image);
    m_lruOrder.push_back(key);
    m_currentCostBytes += imageCost(image);
    evictIfNeeded();
}

QImage PDFContinuousImageCache::get(const PDFContinuousRenderKey& key) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_images.find(key);
    if (it == m_images.end()) {
        return QImage();
    }

    m_lruOrder.removeAll(key);
    m_lruOrder.push_back(key);
    return it.value();
}

bool PDFContinuousImageCache::contains(
    const PDFContinuousRenderKey& key) const {
    QMutexLocker locker(&m_mutex);
    return m_images.contains(key);
}

void PDFContinuousImageCache::clear() {
    QMutexLocker locker(&m_mutex);
    m_images.clear();
    m_lruOrder.clear();
    m_currentCostBytes = 0;
}

void PDFContinuousImageCache::setMaxCostBytes(qint64 maxCostBytes) {
    QMutexLocker locker(&m_mutex);
    m_maxCostBytes = maxCostBytes;
    evictIfNeeded();
}

qint64 PDFContinuousImageCache::maxCostBytes() const {
    QMutexLocker locker(&m_mutex);
    return m_maxCostBytes;
}

qint64 PDFContinuousImageCache::currentCostBytes() const {
    QMutexLocker locker(&m_mutex);
    return m_currentCostBytes;
}

void PDFContinuousImageCache::evictIfNeeded() {
    while (m_currentCostBytes > m_maxCostBytes && !m_lruOrder.isEmpty()) {
        const PDFContinuousRenderKey oldest = m_lruOrder.takeFirst();
        auto it = m_images.find(oldest);
        if (it == m_images.end()) {
            continue;
        }
        m_currentCostBytes -= imageCost(it.value());
        m_images.erase(it);
    }
}

qint64 PDFContinuousImageCache::imageCost(const QImage& image) {
    return image.sizeInBytes();
}
