#include "AdvancedMemoryManager.h"
#include <QApplication>
#include <QDebug>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "utils/LoggingMacros.h"

// MemoryPool Implementation
MemoryPool::MemoryPool(MemoryPoolType type, size_t initialSize)
    : m_type(type),
      m_strategy(AllocationStrategy::FirstFit),
      m_poolMemory(nullptr),
      m_poolSize(initialSize),
      m_usedSize(0) {
    // Allocate initial pool memory
    m_poolMemory = std::malloc(m_poolSize);
    if (!m_poolMemory) {
        qCritical() << "MemoryPool: Failed to allocate initial pool memory";
        return;
    }

    // Create initial free block
    MemoryBlock initialBlock;
    initialBlock.ptr = m_poolMemory;
    initialBlock.size = m_poolSize;
    initialBlock.actualSize = m_poolSize;
    initialBlock.inUse = false;
    initialBlock.timestamp = QDateTime::currentMSecsSinceEpoch();
    initialBlock.poolType = m_type;
    initialBlock.refCount = 0;

    m_blocks.append(initialBlock);

    qDebug() << "MemoryPool: Created pool type" << static_cast<int>(type)
             << "with" << initialSize << "bytes";
}

MemoryPool::~MemoryPool() {
    if (m_poolMemory) {
        std::free(m_poolMemory);
    }
}

void* MemoryPool::allocate(size_t size) {
    QMutexLocker locker(&m_mutex);

    if (size == 0)
        return nullptr;

    // Align size to 8-byte boundary
    size = (size + 7) & ~7;

    MemoryBlock* block = findFreeBlock(size);
    if (!block) {
        // Try to expand pool
        expand(qMax(size, m_poolSize / 2));
        block = findFreeBlock(size);

        if (!block) {
            LOG_WARNING("MemoryPool: Failed to allocate {} bytes", size);
            return nullptr;
        }
    }

    // Split block if necessary
    if (block->size > size + 64) {  // Only split if remainder is significant
        splitBlock(block, size);
    }

    block->inUse = true;
    block->timestamp = QDateTime::currentMSecsSinceEpoch();
    block->refCount = 1;

    m_usedSize += block->size;

    return block->ptr;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr)
        return;

    QMutexLocker locker(&m_mutex);

    // Find the block
    for (auto& block : m_blocks) {
        if (block.ptr == ptr && block.inUse) {
            block.inUse = false;
            block.refCount = 0;
            m_usedSize -= block.size;

            // Try to merge with adjacent free blocks
            mergeAdjacentBlocks();
            return;
        }
    }

    qWarning() << "MemoryPool: Attempted to deallocate unknown pointer";
}

MemoryBlock* MemoryPool::findFreeBlock(size_t size) {
    MemoryBlock* bestBlock = nullptr;

    switch (m_strategy) {
        case AllocationStrategy::FirstFit:
            for (auto& block : m_blocks) {
                if (!block.inUse && block.size >= size) {
                    return &block;
                }
            }
            break;

        case AllocationStrategy::BestFit:
            for (auto& block : m_blocks) {
                if (!block.inUse && block.size >= size) {
                    if (!bestBlock || block.size < bestBlock->size) {
                        bestBlock = &block;
                    }
                }
            }
            break;

        case AllocationStrategy::WorstFit:
            for (auto& block : m_blocks) {
                if (!block.inUse && block.size >= size) {
                    if (!bestBlock || block.size > bestBlock->size) {
                        bestBlock = &block;
                    }
                }
            }
            break;

        default:
            // Default to first fit
            return findFreeBlock(size);
    }

    return bestBlock;
}

void MemoryPool::splitBlock(MemoryBlock* block, size_t size) {
    if (!block || block->size <= size)
        return;

    // Create new block for the remainder
    MemoryBlock newBlock;
    newBlock.ptr = static_cast<char*>(block->ptr) + size;
    newBlock.size = block->size - size;
    newBlock.actualSize = newBlock.size;
    newBlock.inUse = false;
    newBlock.timestamp = QDateTime::currentMSecsSinceEpoch();
    newBlock.poolType = m_type;
    newBlock.refCount = 0;

    // Update original block
    block->size = size;
    block->actualSize = size;

    // Insert new block after current one
    auto it =
        std::find_if(m_blocks.begin(), m_blocks.end(),
                     [block](const MemoryBlock& b) { return &b == block; });
    if (it != m_blocks.end()) {
        m_blocks.insert(it + 1, newBlock);
    }
}

void MemoryPool::mergeAdjacentBlocks() {
    bool merged = true;
    while (merged) {
        merged = false;

        for (int i = 0; i < m_blocks.size() - 1; ++i) {
            MemoryBlock& current = m_blocks[i];
            MemoryBlock& next = m_blocks[i + 1];

            if (!current.inUse && !next.inUse) {
                // Check if blocks are adjacent
                char* currentEnd =
                    static_cast<char*>(current.ptr) + current.size;
                if (currentEnd == next.ptr) {
                    // Merge blocks
                    current.size += next.size;
                    current.actualSize = current.size;
                    m_blocks.removeAt(i + 1);
                    merged = true;
                    break;
                }
            }
        }
    }
}

void MemoryPool::expand(size_t additionalSize) {
    size_t newSize = m_poolSize + additionalSize;
    void* newMemory = std::realloc(m_poolMemory, newSize);

    if (!newMemory) {
        qWarning() << "MemoryPool: Failed to expand pool";
        return;
    }

    // Update all block pointers if memory moved
    if (newMemory != m_poolMemory) {
        ptrdiff_t offset =
            static_cast<char*>(newMemory) - static_cast<char*>(m_poolMemory);
        for (auto& block : m_blocks) {
            block.ptr = static_cast<char*>(block.ptr) + offset;
        }
        m_poolMemory = newMemory;
    }

    // Add new free block for the expanded area
    MemoryBlock newBlock;
    newBlock.ptr = static_cast<char*>(m_poolMemory) + m_poolSize;
    newBlock.size = additionalSize;
    newBlock.actualSize = additionalSize;
    newBlock.inUse = false;
    newBlock.timestamp = QDateTime::currentMSecsSinceEpoch();
    newBlock.poolType = m_type;
    newBlock.refCount = 0;

    m_blocks.append(newBlock);
    m_poolSize = newSize;

    // Try to merge with adjacent blocks
    mergeAdjacentBlocks();

    qDebug() << "MemoryPool: Expanded to" << newSize << "bytes";
}

MemoryPoolStats MemoryPool::getStats() const {
    QMutexLocker locker(&m_mutex);

    MemoryPoolStats stats;
    stats.totalAllocated = m_poolSize;
    stats.totalUsed = m_usedSize;
    stats.totalFree = m_poolSize - m_usedSize;
    stats.blockCount = m_blocks.size();

    for (const auto& block : m_blocks) {
        if (!block.inUse) {
            stats.freeBlocks++;
        }
    }

    stats.fragmentation = getFragmentation();

    return stats;
}

double MemoryPool::getFragmentation() const {
    if (m_blocks.isEmpty())
        return 0.0;

    size_t largestFreeBlock = 0;
    size_t totalFreeSpace = 0;

    for (const auto& block : m_blocks) {
        if (!block.inUse) {
            totalFreeSpace += block.size;
            largestFreeBlock = qMax(largestFreeBlock, block.size);
        }
    }

    if (totalFreeSpace == 0)
        return 0.0;

    return 1.0 - (static_cast<double>(largestFreeBlock) / totalFreeSpace);
}

// CompressionManager Implementation
QByteArray CompressionManager::compress(const QByteArray& data,
                                        CompressionType type) {
    switch (type) {
        case LZ4:
        case Zlib:
            return qCompress(data, 6);  // Use Qt's built-in compression
        case Zstd:
            // Would implement Zstd compression here
            return qCompress(data, 9);
        default:
            return data;
    }
}

QByteArray CompressionManager::decompress(const QByteArray& compressedData,
                                          CompressionType type) {
    switch (type) {
        case LZ4:
        case Zlib:
        case Zstd:
            return qUncompress(compressedData);
        default:
            return compressedData;
    }
}

double CompressionManager::getCompressionRatio(const QByteArray& original,
                                               const QByteArray& compressed) {
    if (original.isEmpty())
        return 1.0;
    return static_cast<double>(compressed.size()) / original.size();
}

bool CompressionManager::shouldCompress(const QByteArray& data,
                                        size_t threshold) {
    return data.size() >= threshold;
}

// AdvancedMemoryManager Implementation
AdvancedMemoryManager::AdvancedMemoryManager(QObject* parent)
    : QObject(parent),
      m_memoryLimit(1024 * 1024 * 1024)  // 1GB default
      ,
      m_defaultStrategy(AllocationStrategy::FirstFit),
      m_compressionEnabled(true),
      m_compressionThreshold(4096)  // 4KB
      ,
      m_gcEnabled(true),
      m_gcThreshold(0.8)  // 80%
      ,
      m_currentPressure(MemoryPressure::None),
      m_maintenanceTimer(nullptr),
      m_gcTimer(nullptr),
      m_statsTimer(nullptr),
      m_settings(nullptr) {
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-MemoryManager", this);

    // Load configuration
    loadSettings();

    // Initialize memory pools
    initializePools();

    // Initialize timers
    initializeTimers();

    qDebug() << "AdvancedMemoryManager: Initialized with" << m_memoryLimit
             << "bytes limit";
}

AdvancedMemoryManager::~AdvancedMemoryManager() {
    saveSettings();

    // Clean up pools
    for (auto pool : m_pools) {
        delete pool;
    }
}

void AdvancedMemoryManager::initializePools() {
    // Create pools for different object sizes
    m_pools[MemoryPoolType::SmallObjects] =
        new MemoryPool(MemoryPoolType::SmallObjects, 1024 * 1024);  // 1MB
    m_pools[MemoryPoolType::MediumObjects] = new MemoryPool(
        MemoryPoolType::MediumObjects, 16 * 1024 * 1024);  // 16MB
    m_pools[MemoryPoolType::LargeObjects] =
        new MemoryPool(MemoryPoolType::LargeObjects, 64 * 1024 * 1024);  // 64MB
    m_pools[MemoryPoolType::HugeObjects] = new MemoryPool(
        MemoryPoolType::HugeObjects, 128 * 1024 * 1024);  // 128MB
    m_pools[MemoryPoolType::PixmapPool] =
        new MemoryPool(MemoryPoolType::PixmapPool, 32 * 1024 * 1024);  // 32MB
    m_pools[MemoryPoolType::StringPool] =
        new MemoryPool(MemoryPoolType::StringPool, 4 * 1024 * 1024);  // 4MB

    // Set allocation strategies
    for (auto pool : m_pools) {
        pool->setAllocationStrategy(m_defaultStrategy);
    }
}

void AdvancedMemoryManager::initializeTimers() {
    // Maintenance timer - performs regular cleanup and optimization
    m_maintenanceTimer = new QTimer(this);
    m_maintenanceTimer->setInterval(30000);  // 30 seconds
    connect(m_maintenanceTimer, &QTimer::timeout, this,
            &AdvancedMemoryManager::onMaintenanceTimer);
    m_maintenanceTimer->start();

    // Garbage collection timer
    m_gcTimer = new QTimer(this);
    m_gcTimer->setInterval(60000);  // 1 minute
    connect(m_gcTimer, &QTimer::timeout, this,
            &AdvancedMemoryManager::onGCTimer);
    if (m_gcEnabled) {
        m_gcTimer->start();
    }

    // Statistics timer
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(5000);  // 5 seconds
    connect(m_statsTimer, &QTimer::timeout, this,
            &AdvancedMemoryManager::onStatsTimer);
    m_statsTimer->start();
}

void* AdvancedMemoryManager::allocate(size_t size, MemoryPoolType poolType) {
    if (size == 0)
        return nullptr;

    // Determine appropriate pool if not specified
    if (poolType == MemoryPoolType::MediumObjects) {
        poolType = determinePoolType(size);
    }

    MemoryPool* pool = m_pools.value(poolType, nullptr);
    if (!pool) {
        qWarning() << "AdvancedMemoryManager: Invalid pool type"
                   << static_cast<int>(poolType);
        return nullptr;
    }

    void* ptr = pool->allocate(size);
    if (ptr) {
        QMutexLocker locker(&m_allocationMutex);

        // Track allocation
        MemoryBlock* block = new MemoryBlock();
        block->ptr = ptr;
        block->size = size;
        block->inUse = true;
        block->timestamp = QDateTime::currentMSecsSinceEpoch();
        block->poolType = poolType;
        block->refCount = 1;

        m_allocations[ptr] = block;

        // Check memory pressure
        updateMemoryPressure();

        qDebug() << "AdvancedMemoryManager: Allocated" << size
                 << "bytes from pool" << static_cast<int>(poolType);
    }

    return ptr;
}

void AdvancedMemoryManager::deallocate(void* ptr) {
    if (!ptr)
        return;

    QMutexLocker locker(&m_allocationMutex);

    auto it = m_allocations.find(ptr);
    if (it == m_allocations.end()) {
        qWarning()
            << "AdvancedMemoryManager: Attempted to deallocate unknown pointer";
        return;
    }

    MemoryBlock* block = it.value();
    MemoryPool* pool = m_pools.value(block->poolType, nullptr);

    if (pool) {
        pool->deallocate(ptr);
    }

    delete block;
    m_allocations.erase(it);

    updateMemoryPressure();
}

void* AdvancedMemoryManager::allocatePixmapData(size_t width, size_t height,
                                                int depth) {
    size_t size = width * height * (depth / 8);
    return allocate(size, MemoryPoolType::PixmapPool);
}

void* AdvancedMemoryManager::allocateStringData(size_t length) {
    return allocate(length, MemoryPoolType::StringPool);
}

MemoryPoolType AdvancedMemoryManager::determinePoolType(size_t size) const {
    if (size < 1024) {
        return MemoryPoolType::SmallObjects;
    } else if (size < 100 * 1024) {
        return MemoryPoolType::MediumObjects;
    } else if (size < 10 * 1024 * 1024) {
        return MemoryPoolType::LargeObjects;
    } else {
        return MemoryPoolType::HugeObjects;
    }
}

MemoryPressure AdvancedMemoryManager::getCurrentPressure() const {
    return m_currentPressure;
}

void AdvancedMemoryManager::updateMemoryPressure() {
    size_t totalUsed = getTotalUsed();
    double usage = static_cast<double>(totalUsed) / m_memoryLimit;

    MemoryPressure newPressure;
    if (usage < 0.5) {
        newPressure = MemoryPressure::None;
    } else if (usage < 0.7) {
        newPressure = MemoryPressure::Low;
    } else if (usage < 0.85) {
        newPressure = MemoryPressure::Medium;
    } else if (usage < 0.95) {
        newPressure = MemoryPressure::High;
    } else {
        newPressure = MemoryPressure::Critical;
    }

    if (newPressure != m_currentPressure) {
        m_currentPressure = newPressure;
        emit memoryPressureChanged(newPressure);

        // Handle pressure automatically
        handleMemoryPressure(newPressure);
    }
}

void AdvancedMemoryManager::handleMemoryPressure(MemoryPressure level) {
    switch (level) {
        case MemoryPressure::None:
        case MemoryPressure::Low:
            // Normal operation
            break;

        case MemoryPressure::Medium:
            // Start compression and light cleanup
            if (m_compressionEnabled) {
                compressUnusedBlocks();
            }
            break;

        case MemoryPressure::High:
            // Aggressive cleanup
            performGarbageCollection();
            defragmentAll();
            break;

        case MemoryPressure::Critical:
            // Emergency procedures
            emergencyCleanup();
            break;
    }
}

size_t AdvancedMemoryManager::getTotalUsed() const {
    size_t total = 0;
    for (auto pool : m_pools) {
        total += pool->getStats().totalUsed;
    }
    return total;
}

size_t AdvancedMemoryManager::getTotalAllocated() const {
    size_t total = 0;
    for (auto pool : m_pools) {
        total += pool->getStats().totalAllocated;
    }
    return total;
}

void AdvancedMemoryManager::defragmentAll() {
    qDebug() << "AdvancedMemoryManager: Starting defragmentation";

    for (auto pool : m_pools) {
        pool->defragment();
    }

    updateMemoryPressure();
}

void AdvancedMemoryManager::performGarbageCollection() {
    if (!m_gcEnabled)
        return;

    qDebug() << "AdvancedMemoryManager: Starting garbage collection";

    size_t freedBytes = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 threshold = 300000;  // 5 minutes

    QMutexLocker locker(&m_allocationMutex);

    auto it = m_allocations.begin();
    while (it != m_allocations.end()) {
        MemoryBlock* block = it.value();

        // Check if block is old and unused
        if (block->refCount == 0 && (now - block->timestamp) > threshold) {
            MemoryPool* pool = m_pools.value(block->poolType, nullptr);
            if (pool) {
                pool->deallocate(block->ptr);
                freedBytes += block->size;
            }

            delete block;
            it = m_allocations.erase(it);
        } else {
            ++it;
        }
    }

    emit garbageCollected(freedBytes);
    qDebug() << "AdvancedMemoryManager: GC freed" << freedBytes << "bytes";
}

void AdvancedMemoryManager::emergencyCleanup() {
    qWarning() << "AdvancedMemoryManager: Emergency cleanup triggered";

    // Force garbage collection
    forceGarbageCollection();

    // Compress all pools
    compressAllPools();

    // Shrink pools
    shrinkPools();

    // Defragment
    defragmentAll();

    updateMemoryPressure();
}

void AdvancedMemoryManager::onMaintenanceTimer() { performMaintenance(); }

void AdvancedMemoryManager::onGCTimer() {
    if (m_gcEnabled && m_currentPressure >= MemoryPressure::Medium) {
        performGarbageCollection();
    }
}

void AdvancedMemoryManager::onStatsTimer() { updateStatistics(); }

void AdvancedMemoryManager::performMaintenance() {
    // Regular maintenance tasks
    updateMemoryPressure();

    if (m_currentPressure >= MemoryPressure::Medium) {
        performDefragmentation();
    }

    if (getFragmentation() > 0.3) {  // 30% fragmentation
        emit fragmentationHigh(getFragmentation());
    }
}

void AdvancedMemoryManager::loadSettings() {
    if (!m_settings)
        return;

    m_memoryLimit =
        m_settings->value("memory/limit", 1024 * 1024 * 1024).toLongLong();
    m_compressionEnabled =
        m_settings->value("memory/compressionEnabled", true).toBool();
    m_compressionThreshold =
        m_settings->value("memory/compressionThreshold", 4096).toULongLong();
    m_gcEnabled = m_settings->value("memory/gcEnabled", true).toBool();
    m_gcThreshold = m_settings->value("memory/gcThreshold", 0.8).toDouble();

    int strategy = m_settings
                       ->value("memory/allocationStrategy",
                               static_cast<int>(AllocationStrategy::FirstFit))
                       .toInt();
    m_defaultStrategy = static_cast<AllocationStrategy>(strategy);
}

void AdvancedMemoryManager::saveSettings() {
    if (!m_settings)
        return;

    m_settings->setValue("memory/limit", static_cast<qint64>(m_memoryLimit));
    m_settings->setValue("memory/compressionEnabled", m_compressionEnabled);
    m_settings->setValue("memory/compressionThreshold",
                         static_cast<qint64>(m_compressionThreshold));
    m_settings->setValue("memory/gcEnabled", m_gcEnabled);
    m_settings->setValue("memory/gcThreshold", m_gcThreshold);
    m_settings->setValue("memory/allocationStrategy",
                         static_cast<int>(m_defaultStrategy));

    m_settings->sync();
}
