#include "PDFCacheManager.h"
#include <QApplication>
#include <QDateTime>
#include <QMutexLocker>
#include <QPixmap>
// #include <QtConcurrent> // Not available in this MSYS2 setup
#include "utils/LoggingMacros.h"

// CacheItem Implementation
qint64 CacheItem::calculateSize() const {
    qint64 size = sizeof(CacheItem);

    // Estimate data size based on QVariant content
    if (data.canConvert<QPixmap>()) {
        QPixmap pixmap = data.value<QPixmap>();
        size += pixmap.width() * pixmap.height() * 4;  // 32-bit ARGB
    } else if (data.canConvert<QString>()) {
        size += data.toString().size() * sizeof(QChar);
    } else {
        size += 1024;  // Conservative estimate for other types
    }

    return size;
}

bool CacheItem::isExpired(qint64 maxAge) const {
    if (maxAge <= 0)
        return false;
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    return (currentTime - timestamp) > maxAge;
}

// PDFCacheManager Implementation
PDFCacheManager::PDFCacheManager(QObject* parent)
    : QObject(parent),
      m_maxMemoryUsage(256 * 1024 * 1024)  // 256MB default
      ,
      m_maxItems(1000),
      m_itemMaxAge(30 * 60 * 1000)  // 30 minutes
      ,
      m_evictionPolicy("LRU"),
      m_lowPriorityWeight(0.1),
      m_normalPriorityWeight(1.0),
      m_highPriorityWeight(10.0),
      m_hitCount(0),
      m_missCount(0),
      m_totalAccessTime(0),
      m_accessCount(0),
      m_maintenanceTimer(new QTimer(this)),
      m_settings(new QSettings("SAST", "Readium-Cache", this)) {
    // Setup maintenance timer
    m_maintenanceTimer->setInterval(60000);  // 1 minute
    connect(m_maintenanceTimer, &QTimer::timeout, this,
            &PDFCacheManager::performMaintenance);
    m_maintenanceTimer->start();

    // Load settings
    loadSettings();

    LOG_DEBUG(
        "PDFCacheManager initialized with max memory: {} bytes, max items: {}",
        m_maxMemoryUsage, m_maxItems);
}

// Locked methods (caller must hold m_cacheMutex)

void PDFCacheManager::cleanupExpiredLocked(QList<QString>& evicted) {
    if (m_itemMaxAge <= 0)
        return;

    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        if (it->isExpired(m_itemMaxAge)) {
            evicted.append(it->key);
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

bool PDFCacheManager::evictLeastUsedLocked(int count, QList<QString>& evicted) {
    if (m_cache.isEmpty() || count <= 0)
        return false;

    // Build candidate list
    QList<QPair<double, QString>> candidates;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->priority != CachePriority::Critical) {
            candidates.append({calculateEvictionScore(*it), it->key});
        }
    }

    std::sort(candidates.begin(), candidates.end());

    int evictedCount = 0;
    for (const auto& candidate : candidates) {
        if (evictedCount >= count)
            break;

        auto it = m_cache.find(candidate.second);
        if (it != m_cache.end()) {
            evicted.append(it->key);
            m_cache.erase(it);
            evictedCount++;
        }
    }
    return evictedCount > 0;
}

qint64 PDFCacheManager::getCurrentMemoryUsageLocked() const {
    qint64 total = 0;
    for (const auto& item : m_cache) {
        total += item.memorySize;
    }
    return total;
}

void PDFCacheManager::enforceMemoryLimitLocked(QList<QString>& evicted) {
    while (getCurrentMemoryUsageLocked() > m_maxMemoryUsage &&
           !m_cache.isEmpty()) {
        if (!evictLeastUsedLocked(1, evicted))
            break;
    }
}

void PDFCacheManager::enforceItemLimitLocked(QList<QString>& evicted) {
    while (m_cache.size() > m_maxItems && !m_cache.isEmpty()) {
        if (!evictLeastUsedLocked(1, evicted))
            break;
    }
}

bool PDFCacheManager::insert(const QString& key, const QVariant& data,
                             CachePriority priority) {
    QList<QString> evicted;

    CacheItem item;
    item.data = data;
    item.priority = priority;
    item.key = key;
    item.memorySize = item.calculateSize();

    {
        QMutexLocker locker(&m_cacheMutex);

        // Check if we need to make room
        qint64 targetMemory = m_maxMemoryUsage - item.memorySize;
        while (m_cache.size() >= m_maxItems ||
               getCurrentMemoryUsageLocked() > targetMemory) {
            if (!evictLeastUsedLocked(1, evicted)) {
                LOG_WARNING(
                    "PDFCacheManager: Failed to evict items, cache full");
                evicted.clear();
                return false;
            }
        }

        m_cache[key] = item;
    }

    for (const auto& e : evicted)
        emit itemEvicted(e);

    LOG_DEBUG("PDFCacheManager: Cached item {} size: {} bytes",
              key.toStdString(), item.memorySize);

    return true;
}

QVariant PDFCacheManager::get(const QString& key) {
    bool hit = false;
    QVariant result;

    {
        QMutexLocker locker(&m_cacheMutex);

        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            it->updateAccess();
            result = it->data;
            hit = true;
        }
    }

    updateStatistics(hit);
    if (hit) {
        emit cacheHit(key, 0);  // TODO: measure actual access time
    } else {
        emit cacheMiss(key);
    }
    return result;
}

bool PDFCacheManager::contains(const QString& key) const {
    QMutexLocker locker(&m_cacheMutex);
    return m_cache.contains(key);
}

bool PDFCacheManager::remove(const QString& key) {
    bool found = false;

    {
        QMutexLocker locker(&m_cacheMutex);

        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            m_cache.erase(it);
            found = true;
        }
    }

    if (found)
        emit itemEvicted(key);
    return found;
}

void PDFCacheManager::clear() {
    QMutexLocker locker(&m_cacheMutex);
    m_cache.clear();
    LOG_DEBUG("PDFCacheManager: Cache cleared");
}

void PDFCacheManager::updateStatistics(bool hit) {
    QMutexLocker locker(&m_statsMutex);
    if (hit) {
        m_hitCount++;
    } else {
        m_missCount++;
    }
    m_accessCount++;
}

void PDFCacheManager::performMaintenance() {
    cleanupExpiredItems();

    // Perform optimization if needed
    if (m_lastOptimization.elapsed() > 300000) {  // 5 minutes
        optimizeCache();
        m_lastOptimization.restart();
    }
}

void PDFCacheManager::setMaxMemoryUsage(qint64 bytes) {
    QList<QString> evicted;

    {
        QMutexLocker locker(&m_cacheMutex);
        m_maxMemoryUsage = bytes;
        enforceMemoryLimitLocked(evicted);
    }

    for (const auto& e : evicted)
        emit itemEvicted(e);
}

void PDFCacheManager::setMaxItems(int count) {
    QList<QString> evicted;

    {
        QMutexLocker locker(&m_cacheMutex);
        m_maxItems = count;
        enforceItemLimitLocked(evicted);
    }

    for (const auto& e : evicted)
        emit itemEvicted(e);
}

void PDFCacheManager::setItemMaxAge(qint64 milliseconds) {
    m_itemMaxAge = milliseconds;
}

void PDFCacheManager::optimizeCache() {
    QList<QString> evicted;
    int itemsRemoved = 0;
    qint64 memoryFreed = 0;

    {
        QMutexLocker locker(&m_cacheMutex);

        int initialSize = m_cache.size();
        qint64 initialMemory = getCurrentMemoryUsageLocked();

        cleanupExpiredLocked(evicted);

        itemsRemoved = initialSize - m_cache.size();
        memoryFreed = initialMemory - getCurrentMemoryUsageLocked();
    }

    for (const auto& e : evicted)
        emit itemEvicted(e);
    if (itemsRemoved > 0 || memoryFreed > 0)
        emit cacheOptimized(itemsRemoved, memoryFreed);
}

void PDFCacheManager::cleanupExpiredItems() {
    QList<QString> evicted;

    {
        QMutexLocker locker(&m_cacheMutex);
        cleanupExpiredLocked(evicted);
    }

    for (const auto& e : evicted)
        emit itemEvicted(e);
}

bool PDFCacheManager::evictLeastUsedItems(int count) {
    QList<QString> evicted;
    bool ok = false;

    {
        QMutexLocker locker(&m_cacheMutex);
        ok = evictLeastUsedLocked(count, evicted);
    }

    for (const auto& e : evicted)
        emit itemEvicted(e);
    return ok;
}

double PDFCacheManager::calculateEvictionScore(const CacheItem& item) const {
    double score = 0.0;

    // Priority weight
    switch (item.priority) {
        case CachePriority::Low:
            score += m_lowPriorityWeight;
            break;
        case CachePriority::Normal:
            score += m_normalPriorityWeight;
            break;
        case CachePriority::High:
            score += m_highPriorityWeight;
            break;
        case CachePriority::Critical:
            score += 1000.0;
            break;  // Should not be evicted
    }

    // Age factor (older items have lower scores)
    qint64 age = QDateTime::currentMSecsSinceEpoch() - item.timestamp;
    score -= age / 1000.0;  // Convert to seconds

    // Access frequency factor
    score += item.accessCount * 10.0;

    // Recent access factor
    qint64 timeSinceLastAccess =
        QDateTime::currentMSecsSinceEpoch() - item.lastAccessed;
    score -= timeSinceLastAccess / 1000.0;

    return score;
}

qint64 PDFCacheManager::getCurrentMemoryUsage() const {
    QMutexLocker locker(&m_cacheMutex);
    return getCurrentMemoryUsageLocked();
}

CacheStatistics PDFCacheManager::getStatistics() const {
    QMutexLocker locker(&m_cacheMutex);
    QMutexLocker statsLocker(&m_statsMutex);

    CacheStatistics stats;
    stats.totalItems = m_cache.size();
    stats.totalMemoryUsage = getCurrentMemoryUsageLocked();
    stats.hitCount = m_hitCount;
    stats.missCount = m_missCount;
    stats.hitRate =
        (m_hitCount + m_missCount > 0)
            ? static_cast<double>(m_hitCount) / (m_hitCount + m_missCount)
            : 0.0;

    return stats;
}

double PDFCacheManager::getHitRate() const {
    QMutexLocker locker(&m_statsMutex);
    return (m_hitCount + m_missCount > 0)
               ? static_cast<double>(m_hitCount) / (m_hitCount + m_missCount)
               : 0.0;
}

void PDFCacheManager::resetStatistics() {
    QMutexLocker locker(&m_statsMutex);
    m_hitCount = 0;
    m_missCount = 0;
    m_totalAccessTime = 0;
    m_accessCount = 0;
}

void PDFCacheManager::loadSettings() {
    m_maxMemoryUsage =
        m_settings->value("maxMemoryUsage", m_maxMemoryUsage).toLongLong();
    m_maxItems = m_settings->value("maxItems", m_maxItems).toInt();
    m_itemMaxAge = m_settings->value("itemMaxAge", m_itemMaxAge).toLongLong();
    m_evictionPolicy =
        m_settings->value("evictionPolicy", m_evictionPolicy).toString();
}

void PDFCacheManager::saveSettings() {
    m_settings->setValue("maxMemoryUsage", m_maxMemoryUsage);
    m_settings->setValue("maxItems", m_maxItems);
    m_settings->setValue("itemMaxAge", m_itemMaxAge);
    m_settings->setValue("evictionPolicy", m_evictionPolicy);
}
