#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariant>

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
    CachePriority priority;
    qint64 timestamp;
    qint64 accessCount;
    qint64 lastAccessed;
    QString key;
    qint64 memorySize;

    CacheItem()
        : priority(CachePriority::Normal),
          timestamp(QDateTime::currentMSecsSinceEpoch()),
          accessCount(0),
          lastAccessed(0),
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
          newestItemAge(0) {}
};

/**
 * Generic cache manager with intelligent caching strategies.
 *
 * Thread-safe KV cache with configurable eviction, memory thresholds,
 * statistics, and runtime item management.
 *
 * Signals are emitted outside the lock scope to prevent deadlock on
 * re-entrant calls from connected slots.  Cache state is fully committed
 * before any emission; itemEvicted is an at-most-once notification, not
 * a transactional guarantee (TOCTOU possible under concurrent access).
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

    // Core cache operations
    bool insert(const QString& key, const QVariant& data,
                CachePriority priority = CachePriority::Normal);
    QVariant get(const QString& key);
    bool contains(const QString& key) const;
    bool remove(const QString& key);
    void clear();

    // Cache management
    void optimizeCache();
    void cleanupExpiredItems();
    bool evictLeastUsedItems(int count);
    void compactCache();
    void defragmentCache();

    // Statistics
    CacheStatistics getStatistics() const;
    qint64 getCurrentMemoryUsage() const;
    double getHitRate() const;
    void resetStatistics();

    // Eviction policies
    void setEvictionPolicy(const QString& policy);
    QString getEvictionPolicy() const { return m_evictionPolicy; }

    void setPriorityWeights(double lowWeight, double normalWeight,
                            double highWeight);

    // Cache inspection
    QStringList getCacheKeys() const;
    QStringList getCacheKeysByPriority(CachePriority priority) const;

    // Runtime item management
    void setCachePriority(const QString& key, CachePriority priority);
    bool promoteToHighPriority(const QString& key);
    void refreshCacheItem(const QString& key);

    // Settings persistence
    void loadSettings();
    void saveSettings();

signals:
    void cacheHit(const QString& key, qint64 accessTime);
    void cacheMiss(const QString& key);
    void itemEvicted(const QString& key);
    void memoryThresholdExceeded(qint64 currentUsage, qint64 threshold);
    void cacheOptimized(int itemsRemoved, qint64 memoryFreed);
    void cacheDefragmented(int remainingItems);
    void cachePriorityChanged(const QString& key, CachePriority newPriority);
    void cacheItemRefreshed(const QString& key);

private slots:
    void performMaintenance();

private:
    void updateStatistics(bool hit);
    bool shouldEvict(const CacheItem& item) const;
    double calculateEvictionScore(const CacheItem& item) const;

    // Locked methods — caller must hold m_cacheMutex
    void cleanupExpiredLocked(QList<QString>& evicted);
    bool evictLeastUsedLocked(int count, QList<QString>& evicted);
    qint64 getCurrentMemoryUsageLocked() const;
    void enforceMemoryLimitLocked(QList<QString>& evicted);
    void enforceItemLimitLocked(QList<QString>& evicted);

    // Lock ordering: m_cacheMutex → m_statsMutex (never reverse)

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

    // Maintenance
    QTimer* m_maintenanceTimer;
    QElapsedTimer m_lastOptimization;

    // Settings
    QSettings* m_settings;
};
