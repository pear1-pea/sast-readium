#pragma once

#include <QCache>
#include <QMutex>
#include <QPixmap>

/**
 * @brief 线程安全的 PDF 页面渲染缓存
 *
 * 每个 PDFViewer 实例持有独立的缓存，避免多文档场景下的缓存污染。
 * 使用 LRU 策略自动淘汰旧缓存。
 */
class PDFRenderCache {
public:
    struct CacheKey {
        int pageNumber;
        double scaleFactor;
        int rotation;

        bool operator==(const CacheKey& other) const;
        bool operator<(const CacheKey& other) const;
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
