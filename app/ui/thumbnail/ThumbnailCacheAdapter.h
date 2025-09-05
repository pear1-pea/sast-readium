#pragma once

#include <QObject>
#include <QPixmap>
#include <QSize>
#include <QMutex>
#include <QTimer>
#include <memory>

// 前向声明
class PDFPrerenderer;
class ThumbnailModel;
class ThumbnailGenerator;
namespace Poppler {
    class Document;
}

/**
 * @brief 缩略图缓存适配器
 * 
 * 将新的缩略图系统与现有的PDFPrerenderer缓存系统集成：
 * - 复用现有的渲染缓存
 * - 智能缓存策略协调
 * - 内存使用优化
 * - 避免重复渲染
 */
class ThumbnailCacheAdapter : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailCacheAdapter(QObject* parent = nullptr);
    ~ThumbnailCacheAdapter() override;

    // 组件设置
    void setPDFPrerenderer(PDFPrerenderer* prerenderer);
    void setThumbnailModel(ThumbnailModel* model);
    void setThumbnailGenerator(ThumbnailGenerator* generator);
    
    // 文档管理
    void setDocument(std::shared_ptr<Poppler::Document> document);
    
    // 缓存策略
    void setSharedCacheEnabled(bool enabled);
    bool sharedCacheEnabled() const { return m_sharedCacheEnabled; }
    
    void setCacheCoordinationEnabled(bool enabled);
    bool cacheCoordinationEnabled() const { return m_cacheCoordinationEnabled; }
    
    void setMemoryBalancingEnabled(bool enabled);
    bool memoryBalancingEnabled() const { return m_memoryBalancingEnabled; }
    
    // 缓存查询
    bool hasCachedThumbnail(int pageNumber, const QSize& size) const;
    QPixmap getCachedThumbnail(int pageNumber, const QSize& size) const;
    
    // 缓存管理
    void requestThumbnailFromCache(int pageNumber, const QSize& size, double quality);
    void storeThumbnailToCache(int pageNumber, const QPixmap& pixmap, const QSize& size);
    
    void invalidateCache(int pageNumber);
    void clearAllCaches();
    
    // 统计信息
    int totalCacheHits() const;
    int totalCacheMisses() const;
    qint64 totalMemoryUsage() const;
    
    // 性能优化
    void optimizeCacheUsage();
    void balanceMemoryUsage();

signals:
    void thumbnailCacheHit(int pageNumber, const QPixmap& pixmap);
    void thumbnailCacheMiss(int pageNumber);
    void cacheMemoryWarning(qint64 usage, qint64 limit);
    void cacheOptimized();

private slots:
    void onPrerenderPageReady(int pageNumber, double scaleFactor, int rotation);
    void onThumbnailGenerated(int pageNumber, const QPixmap& pixmap);
    void onMemoryCheckTimer();
    void onCacheOptimizationTimer();

private:
    struct CacheMapping {
        int pageNumber;
        QSize thumbnailSize;
        double scaleFactor;
        int rotation;
        qint64 timestamp;
        
        CacheMapping() : pageNumber(-1), scaleFactor(1.0), rotation(0), timestamp(0) {}
        CacheMapping(int page, const QSize& size, double scale, int rot)
            : pageNumber(page), thumbnailSize(size), scaleFactor(scale), rotation(rot),
              timestamp(QDateTime::currentMSecsSinceEpoch()) {}
    };

    void initializeAdapter();
    void connectSignals();
    void disconnectSignals();
    
    // 缓存协调
    void coordinateCaches();
    void syncCacheStates();
    void transferCacheData();
    
    // 内存管理
    void checkMemoryUsage();
    void evictLeastUsefulItems();
    qint64 calculateTotalMemoryUsage() const;
    
    // 缓存转换
    QPixmap convertFromPrerendererCache(int pageNumber, const QSize& targetSize) const;
    bool canConvertFromPrerendererCache(int pageNumber, const QSize& targetSize) const;
    
    // 策略优化
    void updateCacheStrategies();
    void adjustCacheSizes();
    
    // 性能监控
    void logCachePerformance();
    void updateStatistics();

private:
    // 核心组件
    PDFPrerenderer* m_prerenderer;
    ThumbnailModel* m_thumbnailModel;
    ThumbnailGenerator* m_thumbnailGenerator;
    std::shared_ptr<Poppler::Document> m_document;
    
    // 缓存设置
    bool m_sharedCacheEnabled;
    bool m_cacheCoordinationEnabled;
    bool m_memoryBalancingEnabled;
    
    // 缓存映射
    QHash<QString, CacheMapping> m_cacheMappings;
    mutable QMutex m_mappingsMutex;
    
    // 内存管理
    QTimer* m_memoryCheckTimer;
    QTimer* m_optimizationTimer;
    qint64 m_memoryLimit;
    qint64 m_lastMemoryUsage;
    
    // 统计信息
    int m_cacheHits;
    int m_cacheMisses;
    int m_conversions;
    int m_evictions;
    
    // 常量
    static constexpr int MEMORY_CHECK_INTERVAL = 5000; // ms
    static constexpr int OPTIMIZATION_INTERVAL = 30000; // ms
    static constexpr qint64 DEFAULT_MEMORY_LIMIT = 512 * 1024 * 1024; // 512MB
    static constexpr double CACHE_SIZE_RATIO = 0.7; // 缩略图缓存占总缓存的比例
    static constexpr double CONVERSION_QUALITY_THRESHOLD = 0.8; // 转换质量阈值
};
