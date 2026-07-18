#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSet>

#include <cstdint>

struct PDFContinuousRenderKey {
    int pageIndex = -1;
    double zoom = 1.0;
    int rotation = 0;
    qreal devicePixelRatio = 1.0;

    bool operator==(const PDFContinuousRenderKey& other) const;
};

struct PDFContinuousRenderRequest {
    PDFContinuousRenderKey key;
    QSize targetPixelSize;
    std::uint64_t generation = 0;
};

struct PDFContinuousRenderResult {
    PDFContinuousRenderRequest request;
    QImage image;
    bool success = false;
    QString error;
};

size_t qHash(const PDFContinuousRenderKey& key, size_t seed = 0);

struct PDFContinuousPendingKey {
    std::uint64_t generation = 0;
    PDFContinuousRenderKey renderKey;

    bool operator==(const PDFContinuousPendingKey& other) const;
};

size_t qHash(const PDFContinuousPendingKey& key, size_t seed = 0);

class PDFContinuousImageCache {
public:
    explicit PDFContinuousImageCache(qint64 maxCostBytes = 200LL * 1024 * 1024);

    void insert(const PDFContinuousRenderKey& key, const QImage& image);
    QImage get(const PDFContinuousRenderKey& key) const;
    bool contains(const PDFContinuousRenderKey& key) const;
    void clear();
    void setMaxCostBytes(qint64 maxCostBytes);
    qint64 maxCostBytes() const;
    qint64 currentCostBytes() const;

private:
    void evictIfNeeded();
    static qint64 imageCost(const QImage& image);

    mutable QMutex m_mutex;
    QHash<PDFContinuousRenderKey, QImage> m_images;
    mutable QList<PDFContinuousRenderKey> m_lruOrder;
    qint64 m_maxCostBytes;
    qint64 m_currentCostBytes = 0;
};
