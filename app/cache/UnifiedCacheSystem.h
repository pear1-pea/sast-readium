#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QtGlobal>

/**
 * Cache item types for unified system
 */
enum class UnifiedCacheType {
    RenderedPage,   // Full rendered page
    Thumbnail,      // Page thumbnail
    TextContent,    // Extracted text
    Outline,        // Document outline
    Metadata,       // Document metadata
    SearchResults,  // Search results
    Annotations,    // Page annotations
    CompressedPage  // Compressed page data
};

/**
 * Cache priority levels
 */
enum class UnifiedCachePriority {
    Critical = 10,  // Must keep in cache
    High = 8,       // Important items
    Normal = 5,     // Standard priority
    Low = 2,        // Can be evicted easily
    Background = 0  // Preloaded items
};

/**
 * Cache compression levels
 */
enum class CompressionLevel {
    None,      // No compression
    Fast,      // Fast compression (LZ4)
    Balanced,  // Balanced compression
    Maximum    // Maximum compression (slower)
};

/**
 * Unified cache item
 */
struct UnifiedCacheItem {
    QVariant data;
    UnifiedCacheType type;
    UnifiedCachePriority priority;
    qint64 size;
    qint64 timestamp;
    qint64 lastAccessed;
    int accessCount;
    int pageNumber;
    QString key;
    bool isCompressed;
    CompressionLevel compressionLevel;

    UnifiedCacheItem()
        : type(UnifiedCacheType::RenderedPage),
          priority(UnifiedCachePriority::Normal),
          size(0),
          timestamp(0),
          lastAccessed(0),
          accessCount(0),
          pageNumber(-1),
          isCompressed(false),
          compressionLevel(CompressionLevel::None) {}

    qint64 calculateMemorySize() const;
    bool shouldCompress() const;
    void updateAccess();
    double getAccessScore() const;
};

/**
 * Cache statistics
 */
struct UnifiedCacheStats {
    int totalItems;
    qint64 totalMemoryUsed;
    qint64 maxMemoryLimit;
    int hitCount;
    int missCount;
    double hitRate;
    qint64 compressionSaved;
    int compressedItems;

    // Per-type statistics
    QHash<UnifiedCacheType, int> itemsByType;
    QHash<UnifiedCacheType, qint64> memoryByType;

    UnifiedCacheStats()
        : totalItems(0),
          totalMemoryUsed(0),
          maxMemoryLimit(0),
          hitCount(0),
          missCount(0),
          hitRate(0.0),
          compressionSaved(0),
          compressedItems(0) {}
};

/**
 * Cache configuration
 */
struct UnifiedCacheConfig {
    qint64 maxMemoryUsage;
    int maxItems;
    bool enableCompression;
    CompressionLevel defaultCompressionLevel;
    bool enableAdaptiveCompression;
    double compressionThreshold;
    int cleanupInterval;
    bool enablePreloading;
    int preloadRadius;

    UnifiedCacheConfig()
        : maxMemoryUsage(512 * 1024 * 1024)  // 512MB
          ,
          maxItems(1000),
          enableCompression(true),
          defaultCompressionLevel(CompressionLevel::Fast),
          enableAdaptiveCompression(true),
          compressionThreshold(0.8),
          cleanupInterval(30000)  // 30 seconds
          ,
          enablePreloading(true),
          preloadRadius(2) {}
};

/**
 * Unified cache system that consolidates all caching needs
 */
class UnifiedCacheSystem : public QObject {
    Q_OBJECT

public:
    explicit UnifiedCacheSystem(QObject* parent = nullptr);
    ~UnifiedCacheSystem();

    // Configuration
    void setConfig(const UnifiedCacheConfig& config);
    UnifiedCacheConfig getConfig() const { return m_config; }

    void setMaxMemoryUsage(qint64 bytes);
    void setMaxItems(int maxItems);
    void enableCompression(bool enable);
    void setCompressionLevel(CompressionLevel level);

    // Core cache operations
    bool insert(const QString& key, const QVariant& data, UnifiedCacheType type,
                UnifiedCachePriority priority = UnifiedCachePriority::Normal,
                int pageNumber = -1);

    QVariant get(const QString& key);
    bool contains(const QString& key) const;
    bool remove(const QString& key);
    void clear();
    void clearType(UnifiedCacheType type);

    // Specialized operations
    bool cacheRenderedPage(int pageNumber, const QPixmap& pixmap,
                           double scaleFactor = 1.0, int rotation = 0);
    QPixmap getRenderedPage(int pageNumber, double scaleFactor = 1.0,
                            int rotation = 0);

    bool cacheThumbnail(int pageNumber, const QPixmap& thumbnail,
                        const QSize& size = QSize());
    QPixmap getThumbnail(int pageNumber, const QSize& size = QSize());

    bool cacheTextContent(int pageNumber, const QString& text);
    QString getTextContent(int pageNumber);

    bool cacheSearchResults(const QString& query, const QVariant& results);
    QVariant getSearchResults(const QString& query);

    // Memory management
    void optimizeMemory();
    void enforceMemoryLimit();
    qint64 getCurrentMemoryUsage() const;
    double getMemoryUsagePercentage() const;

    // Statistics and monitoring
    UnifiedCacheStats getStatistics() const;
    void resetStatistics();
    QList<QString> getKeysForType(UnifiedCacheType type) const;
    QList<int> getCachedPages() const;

    // Preloading
    void preloadAroundPage(int centerPage, int radius = -1);
    void preloadPages(const QList<int>& pageNumbers, UnifiedCacheType type);
    void cancelPreloading();

    // Compression
    QByteArray compressData(const QByteArray& data,
                            CompressionLevel level) const;
    QByteArray decompressData(const QByteArray& compressedData) const;
    bool shouldCompressItem(const UnifiedCacheItem& item) const;

    // Persistence
    void saveToDisk(const QString& filePath);
    void loadFromDisk(const QString& filePath);
    void enablePersistence(bool enable, const QString& cacheDir = QString());

    // Settings
    void loadSettings();
    void saveSettings();

public slots:
    void onMemoryPressure();
    void performCleanup();
    void updateStatistics();

signals:
    void memoryUsageChanged(qint64 bytes);
    void statisticsUpdated(const UnifiedCacheStats& stats);
    void memoryThresholdExceeded(qint64 current, qint64 limit);
    void itemEvicted(const QString& key, UnifiedCacheType type);
    void compressionCompleted(const QString& key, qint64 savedBytes);

private slots:
    void onCleanupTimer();
    void onStatsTimer();

private:
    // Core data
    mutable QMutex m_cacheMutex;
    QHash<QString, UnifiedCacheItem> m_cache;
    UnifiedCacheConfig m_config;

    // Statistics
    mutable QMutex m_statsMutex;
    UnifiedCacheStats m_stats;

    // Timers
    QTimer* m_cleanupTimer;
    QTimer* m_statsTimer;

    // Settings
    QSettings* m_settings;

    // Persistence
    bool m_persistenceEnabled;
    QString m_cacheDirectory;

    // Helper methods
    void initializeTimers();
    void evictLRUItems(int count = 1);
    void evictByPriority(UnifiedCachePriority maxPriority);
    void compressItem(UnifiedCacheItem& item);
    void decompressItem(UnifiedCacheItem& item);
    QString generateKey(UnifiedCacheType type, int pageNumber,
                        const QVariant& params = QVariant()) const;
    void updateItemAccess(UnifiedCacheItem& item);
    void updateStatistics(const UnifiedCacheItem& item, bool isHit);
    bool isMemoryPressure() const;
    void enforceItemLimit();
    QList<QString> selectItemsForEviction(int count) const;

    // Compression helpers
    QByteArray pixmapToByteArray(const QPixmap& pixmap) const;
    QPixmap byteArrayToPixmap(const QByteArray& data) const;
    qint64 calculateCompressionSavings(const QByteArray& original,
                                       const QByteArray& compressed) const;
};
