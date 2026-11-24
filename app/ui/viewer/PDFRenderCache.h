#pragma once

#include <QCache>
#include <QMutex>
#include <QPixmap>
#include <QtGlobal>
#include <cstdint>

/**
 * @brief 线程安全的 PDF 页面渲染缓存
 *
 * 每个 PDFViewer 实例持有独立的缓存，避免多文档场景下的缓存污染。
 * 使用 LRU 策略自动淘汰旧缓存。
 *
 * The key is fully integer-quantized so equality and hashing use exactly the
 * same fields. Floating-point tolerances must never define key equality: they
 * are not transitive and can disagree with the hash.
 */
class PDFRenderCache {
public:
    struct CacheKey {
        quint64 documentToken = 0;
        int pageNumber = -1;
        int zoomKey = 10000;  // round(zoom * 10000)
        int rotation = 0;
        int dprKey = 100;  // round(devicePixelRatio * 100)

        static CacheKey make(quint64 documentTokenValue, int pageNumberValue,
                             double scaleFactor, int rotationValue,
                             double devicePixelRatio);

        bool operator==(const CacheKey& other) const;
        bool operator!=(const CacheKey& other) const {
            return !(*this == other);
        }
    };

    /**
     * @param maxCostMB 最大缓存大小（MB），默认 100MB
     */
    explicit PDFRenderCache(int maxCostMB = 100);

    void insert(const CacheKey& key, const QPixmap& pixmap);
    QPixmap get(const CacheKey& key);
    bool contains(const CacheKey& key) const;
    void clear();
    void setMaxCost(int maxCostMB);

    // 统计信息
    int size() const;
    int maxCost() const;

private:
    mutable QMutex m_mutex;
    QCache<CacheKey, QPixmap> m_cache;
};

// Hash 函数用于 QCache
size_t qHash(const PDFRenderCache::CacheKey& key, size_t seed = 0);
