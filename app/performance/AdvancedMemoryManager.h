#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QtGlobal>

/**
 * Memory pool types for different allocation patterns
 */
enum class MemoryPoolType {
    SmallObjects,   // < 1KB
    MediumObjects,  // 1KB - 100KB
    LargeObjects,   // 100KB - 10MB
    HugeObjects,    // > 10MB
    PixmapPool,     // Specialized for QPixmap data
    StringPool      // Specialized for string data
};

/**
 * Memory allocation strategy
 */
enum class AllocationStrategy {
    FirstFit,  // First available block
    BestFit,   // Best size match
    WorstFit,  // Largest available block
    NextFit,   // Next available after last allocation
    Buddy      // Buddy system allocation
};

/**
 * Memory pressure levels
 */
enum class MemoryPressure {
    None,     // < 50% usage
    Low,      // 50-70% usage
    Medium,   // 70-85% usage
    High,     // 85-95% usage
    Critical  // > 95% usage
};

/**
 * Memory block descriptor
 */
struct MemoryBlock {
    void* ptr;
    size_t size;
    size_t actualSize;
    bool inUse;
    qint64 timestamp;
    MemoryPoolType poolType;
    int refCount;

    MemoryBlock()
        : ptr(nullptr),
          size(0),
          actualSize(0),
          inUse(false),
          timestamp(0),
          poolType(MemoryPoolType::SmallObjects),
          refCount(0) {}
};

/**
 * Memory pool statistics
 */
struct MemoryPoolStats {
    size_t totalAllocated;
    size_t totalUsed;
    size_t totalFree;
    int blockCount;
    int freeBlocks;
    double fragmentation;
    int allocations;
    int deallocations;

    MemoryPoolStats()
        : totalAllocated(0),
          totalUsed(0),
          totalFree(0),
          blockCount(0),
          freeBlocks(0),
          fragmentation(0.0),
          allocations(0),
          deallocations(0) {}
};

/**
 * Memory pool implementation
 */
class MemoryPool {
public:
    explicit MemoryPool(MemoryPoolType type, size_t initialSize = 1024 * 1024);
    ~MemoryPool();

    void* allocate(size_t size);
    void deallocate(void* ptr);
    bool contains(void* ptr) const;

    void defragment();
    void shrink();
    void expand(size_t additionalSize);

    MemoryPoolStats getStats() const;
    double getFragmentation() const;

    void setAllocationStrategy(AllocationStrategy strategy);
    AllocationStrategy getAllocationStrategy() const { return m_strategy; }

private:
    MemoryPoolType m_type;
    AllocationStrategy m_strategy;

    QList<MemoryBlock> m_blocks;
    void* m_poolMemory;
    size_t m_poolSize;
    size_t m_usedSize;

    mutable QMutex m_mutex;

    MemoryBlock* findFreeBlock(size_t size);
    void splitBlock(MemoryBlock* block, size_t size);
    void mergeAdjacentBlocks();
    void updateStats();
};

/**
 * Compression manager for memory optimization
 */
class CompressionManager {
public:
    enum CompressionType { None, LZ4, Zlib, Zstd };

    static QByteArray compress(const QByteArray& data,
                               CompressionType type = LZ4);
    static QByteArray decompress(const QByteArray& compressedData,
                                 CompressionType type = LZ4);
    static double getCompressionRatio(const QByteArray& original,
                                      const QByteArray& compressed);
    static bool shouldCompress(const QByteArray& data, size_t threshold = 1024);
};

/**
 * Advanced memory manager with pools and compression
 */
class AdvancedMemoryManager : public QObject {
    Q_OBJECT

public:
    explicit AdvancedMemoryManager(QObject* parent = nullptr);
    ~AdvancedMemoryManager();

    // Memory allocation
    void* allocate(size_t size,
                   MemoryPoolType poolType = MemoryPoolType::MediumObjects);
    void deallocate(void* ptr);
    void* reallocate(void* ptr, size_t newSize);

    // Specialized allocators
    void* allocatePixmapData(size_t width, size_t height, int depth);
    void* allocateStringData(size_t length);
    void deallocatePixmapData(void* ptr);
    void deallocateStringData(void* ptr);

    // Memory pressure management
    MemoryPressure getCurrentPressure() const;
    void handleMemoryPressure(MemoryPressure level);
    void setMemoryLimit(size_t limit);
    size_t getMemoryLimit() const { return m_memoryLimit; }

    // Statistics and monitoring
    size_t getTotalAllocated() const;
    size_t getTotalUsed() const;
    size_t getTotalFree() const;
    double getFragmentation() const;
    QHash<MemoryPoolType, MemoryPoolStats> getPoolStats() const;

    // Optimization operations
    void defragmentAll();
    void compressUnusedBlocks();
    void shrinkPools();
    void optimizeForPattern(const QString& pattern);

    // Configuration
    void setAllocationStrategy(AllocationStrategy strategy);
    void enableCompression(bool enable);
    void setCompressionThreshold(size_t threshold);
    void setDefragmentationInterval(int seconds);

    // Garbage collection
    void collectGarbage();
    void setGCEnabled(bool enabled);
    void setGCThreshold(double threshold);

    // Settings persistence
    void loadSettings();
    void saveSettings();

public slots:
    void onMemoryWarning();
    void onLowMemory();
    void performMaintenance();

signals:
    void memoryPressureChanged(MemoryPressure level);
    void memoryLimitExceeded(size_t current, size_t limit);
    void fragmentationHigh(double fragmentation);
    void poolExpanded(MemoryPoolType type, size_t newSize);
    void garbageCollected(size_t freedBytes);

private slots:
    void onMaintenanceTimer();
    void onGCTimer();
    void onStatsTimer();

private:
    // Memory pools
    QHash<MemoryPoolType, MemoryPool*> m_pools;

    // Configuration
    size_t m_memoryLimit;
    AllocationStrategy m_defaultStrategy;
    bool m_compressionEnabled;
    size_t m_compressionThreshold;
    bool m_gcEnabled;
    double m_gcThreshold;

    // Monitoring
    mutable QMutex m_statsMutex;
    MemoryPressure m_currentPressure;

    // Timers
    QTimer* m_maintenanceTimer;
    QTimer* m_gcTimer;
    QTimer* m_statsTimer;

    // Settings
    QSettings* m_settings;

    // Allocation tracking
    QHash<void*, MemoryBlock*> m_allocations;
    mutable QMutex m_allocationMutex;

    // Helper methods
    void initializePools();
    void initializeTimers();
    MemoryPoolType determinePoolType(size_t size) const;
    void updateMemoryPressure();
    void enforceMemoryLimit();
    void performDefragmentation();
    void performGarbageCollection();
    void optimizePoolSizes();

    // Statistics helpers
    void updateStatistics();
    size_t calculateTotalMemoryUsage() const;
    double calculateOverallFragmentation() const;

    // Emergency procedures
    void emergencyCleanup();
    void forceGarbageCollection();
    void compressAllPools();
};
