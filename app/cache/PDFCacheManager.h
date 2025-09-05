#pragma once

#include <poppler-qt6.h>
#include <QCache>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QQueue>
#include <QRunnable>
#include <QSet>
#include <QSettings>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

/**
 * Cache item types
 */
enum class CacheItemType {
    RenderedPage,   // Rendered page pixmap
    Thumbnail,      // Page thumbnail
    TextContent,    // Extracted text content
    PageImage,      // Raw page image
    SearchResults,  // Search result data
    Annotations     // Page annotations
};

/**
 * Cache priority levels
 */
enum class CachePriority {
    Low,      // Can be evicted first
    Normal,   // Standard priority
    High,     // Keep longer
    Critical  // Never evict automatically
};

/**
 * Cached item wrapper
 */
struct CacheItem {
    QVariant data;
    CacheItemType type;
    CachePriority priority;
    qint64 timestamp;
    qint64 accessCount;
    qint64 lastAccessed;
    int pageNumber;
    QString key;
    qint64 memorySize;

    CacheItem()
        : type(CacheItemType::RenderedPage),
          priority(CachePriority::Normal),
          timestamp(QDateTime::currentMSecsSinceEpoch()),
          accessCount(0),
          lastAccessed(0),
          pageNumber(-1),
          memorySize(0) {}

    void updateAccess() {
        accessCount++;
        lastAccessed = QDateTime::currentMSecsSinceEpoch();
    }

    qint64 calculateSize() const;
    bool isExpired(qint64 maxAge) const;
};

/**
 * Cache statistics
 */
struct CacheStatistics {
    int totalItems;
    qint64 totalMemoryUsage;
    qint64 hitCount;
    qint64 missCount;
    double hitRate;
    int itemsByType[6];  // One for each CacheItemType
    qint64 averageAccessTime;
    qint64 oldestItemAge;
    qint64 newestItemAge;

    CacheStatistics()
        : totalItems(0),
          totalMemoryUsage(0),
          hitCount(0),
          missCount(0),
          hitRate(0.0),
          averageAccessTime(0),
          oldestItemAge(0),
          newestItemAge(0) {
        for (int i = 0; i < 6; ++i) {
            itemsByType[i] = 0;
        }
    }
};

/**
 * Preloading task for background cache population
 */
class PreloadTask : public QRunnable {
public:
    PreloadTask(Poppler::Document* document, int pageNumber, CacheItemType type,
                QObject* target);
    void run() override;

private:
    Poppler::Document* m_document;
    int m_pageNumber;
    CacheItemType m_type;
    QObject* m_target;
};

/**
 * PDF cache manager with intelligent caching strategies
 */
class PDFCacheManager : public QObject {
    Q_OBJECT

public:
    explicit PDFCacheManager(QObject* parent = nullptr);
    ~PDFCacheManager() = default;

    // Cache configuration
    void setMaxMemoryUsage(qint64 bytes);
    qint64 getMaxMemoryUsage() const { return m_maxMemoryUsage; }

    void setMaxItems(int count);
    int getMaxItems() const { return m_maxItems; }

    void setItemMaxAge(qint64 milliseconds);
    qint64 getItemMaxAge() const { return m_itemMaxAge; }

    // Cache operations
    bool insert(const QString& key, const QVariant& data, CacheItemType type,
                CachePriority priority = CachePriority::Normal,
                int pageNumber = -1);
    QVariant get(const QString& key);
    bool contains(const QString& key) const;
    bool remove(const QString& key);
    void clear();

    // Specialized cache operations
    bool cacheRenderedPage(int pageNumber, const QPixmap& pixmap,
                           double scaleFactor);
    QPixmap getRenderedPage(int pageNumber, double scaleFactor);

    bool cacheThumbnail(int pageNumber, const QPixmap& thumbnail);
    QPixmap getThumbnail(int pageNumber);

    bool cacheTextContent(int pageNumber, const QString& text);
    QString getTextContent(int pageNumber);

    // Preloading and background operations
    void enablePreloading(bool enabled);
    bool isPreloadingEnabled() const { return m_preloadingEnabled; }

    void preloadPages(const QList<int>& pageNumbers, CacheItemType type);
    void preloadAroundPage(int centerPage, int radius = 2);
    void setPreloadingStrategy(const QString& strategy);

    // Cache management
    void optimizeCache();
    void cleanupExpiredItems();
    bool evictLeastUsedItems(int count);
    void compactCache();

    // Statistics and monitoring
    CacheStatistics getStatistics() const;
    qint64 getCurrentMemoryUsage() const;
    double getHitRate() const;
    void resetStatistics();

    // Cache policies
    void setEvictionPolicy(const QString& policy);
    QString getEvictionPolicy() const { return m_evictionPolicy; }

    void setPriorityWeights(double lowWeight, double normalWeight,
                            double highWeight);

    // Settings persistence
    void loadSettings();
    void saveSettings();

    // Additional utility functions
    bool exportCacheToFile(const QString& filePath) const;
    bool importCacheFromFile(const QString& filePath);
    void defragmentCache();

    // Cache inspection functions
    QStringList getCacheKeys() const;
    QStringList getCacheKeysByType(CacheItemType type) const;
    QStringList getCacheKeysByPriority(CachePriority priority) const;
    int getCacheItemCount(CacheItemType type) const;
    qint64 getCacheMemoryUsage(CacheItemType type) const;

    // Cache management functions
    void setCachePriority(const QString& key, CachePriority priority);
    bool promoteToHighPriority(const QString& key);
    void refreshCacheItem(const QString& key);

signals:
    void cacheHit(const QString& key, qint64 accessTime);
    void cacheMiss(const QString& key);
    void itemEvicted(const QString& key, CacheItemType type);
    void memoryThresholdExceeded(qint64 currentUsage, qint64 threshold);
    void preloadCompleted(int pageNumber, CacheItemType type);
    void cacheOptimized(int itemsRemoved, qint64 memoryFreed);
    void cacheDefragmented(int remainingItems);
    void cachePriorityChanged(const QString& key, CachePriority newPriority);
    void cacheItemRefreshed(const QString& key);
    void cacheExported(const QString& filePath, bool success);
    void cacheImported(const QString& filePath, bool success);

private slots:
    void performMaintenance();
    void onPreloadTaskCompleted();

private:
    QString generateKey(int pageNumber, CacheItemType type,
                        const QVariant& extra = QVariant()) const;
    void updateStatistics(bool hit);
    void enforceMemoryLimit();
    void enforceItemLimit();
    bool shouldEvict(const CacheItem& item) const;
    double calculateEvictionScore(const CacheItem& item) const;
    void schedulePreload(int pageNumber, CacheItemType type);

    // Cache storage
    mutable QMutex m_cacheMutex;
    QHash<QString, CacheItem> m_cache;

    // Configuration
    qint64 m_maxMemoryUsage;
    int m_maxItems;
    qint64 m_itemMaxAge;
    QString m_evictionPolicy;

    // Priority weights for eviction scoring
    double m_lowPriorityWeight;
    double m_normalPriorityWeight;
    double m_highPriorityWeight;

    // Statistics
    mutable QMutex m_statsMutex;
    qint64 m_hitCount;
    qint64 m_missCount;
    qint64 m_totalAccessTime;
    qint64 m_accessCount;

    // Preloading
    bool m_preloadingEnabled;
    QString m_preloadingStrategy;
    QThreadPool* m_preloadThreadPool;
    QQueue<QPair<int, CacheItemType>> m_preloadQueue;
    QSet<QString> m_preloadingItems;

    // Maintenance
    QTimer* m_maintenanceTimer;
    QElapsedTimer m_lastOptimization;

    // Settings
    QSettings* m_settings;
};
