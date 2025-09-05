#include "UnifiedCacheSystem.h"
#include <QApplication>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QMutexLocker>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include "utils/LoggingMacros.h"

// UnifiedCacheItem Implementation
qint64 UnifiedCacheItem::calculateMemorySize() const {
    qint64 baseSize = sizeof(UnifiedCacheItem);

    if (data.canConvert<QPixmap>()) {
        QPixmap pixmap = data.value<QPixmap>();
        return baseSize + pixmap.width() * pixmap.height() * pixmap.depth() / 8;
    } else if (data.canConvert<QString>()) {
        QString text = data.toString();
        return baseSize + text.size() * sizeof(QChar);
    } else if (data.canConvert<QByteArray>()) {
        QByteArray bytes = data.toByteArray();
        return baseSize + bytes.size();
    }

    return baseSize + 1024;  // Default estimate
}

bool UnifiedCacheItem::shouldCompress() const {
    // Compress large items or specific types
    return size > 100 * 1024 ||  // > 100KB
           type == UnifiedCacheType::RenderedPage ||
           type == UnifiedCacheType::TextContent;
}

void UnifiedCacheItem::updateAccess() {
    lastAccessed = QDateTime::currentMSecsSinceEpoch();
    accessCount++;
}

double UnifiedCacheItem::getAccessScore() const {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 age = now - lastAccessed;

    // Score based on access frequency, recency, and priority
    double frequencyScore = std::log(accessCount + 1);
    double recencyScore = 1.0 / (1.0 + age / 3600000.0);  // Age in hours
    double priorityScore = static_cast<double>(priority) / 10.0;

    return frequencyScore * recencyScore * priorityScore;
}

// UnifiedCacheSystem Implementation
UnifiedCacheSystem::UnifiedCacheSystem(QObject* parent)
    : QObject(parent),
      m_cleanupTimer(nullptr),
      m_statsTimer(nullptr),
      m_settings(nullptr),
      m_persistenceEnabled(false) {
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-UnifiedCache", this);

    // Load configuration
    loadSettings();

    // Initialize timers
    initializeTimers();

    // Setup cache directory
    m_cacheDirectory =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        "/readium";
    QDir().mkpath(m_cacheDirectory);

    LOG_DEBUG("UnifiedCacheSystem: Initialized with {} bytes limit", m_config.maxMemoryUsage);
}

UnifiedCacheSystem::~UnifiedCacheSystem() {
    saveSettings();

    if (m_persistenceEnabled) {
        saveToDisk(m_cacheDirectory + "/cache.dat");
    }
}

void UnifiedCacheSystem::initializeTimers() {
    // Cleanup timer
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(m_config.cleanupInterval);
    connect(m_cleanupTimer, &QTimer::timeout, this,
            &UnifiedCacheSystem::onCleanupTimer);
    m_cleanupTimer->start();

    // Statistics timer
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(5000);  // 5 seconds
    connect(m_statsTimer, &QTimer::timeout, this,
            &UnifiedCacheSystem::onStatsTimer);
    m_statsTimer->start();
}

bool UnifiedCacheSystem::insert(const QString& key, const QVariant& data,
                                UnifiedCacheType type,
                                UnifiedCachePriority priority, int pageNumber) {
    QMutexLocker locker(&m_cacheMutex);

    // Check if already exists
    if (m_cache.contains(key)) {
        // Update existing item
        UnifiedCacheItem& item = m_cache[key];
        item.data = data;
        item.priority = priority;
        item.updateAccess();
        item.size = item.calculateMemorySize();

        updateStatistics(item, true);
        return true;
    }

    // Create new item
    UnifiedCacheItem item;
    item.data = data;
    item.type = type;
    item.priority = priority;
    item.pageNumber = pageNumber;
    item.key = key;
    item.timestamp = QDateTime::currentMSecsSinceEpoch();
    item.updateAccess();
    item.size = item.calculateMemorySize();

    // Check if compression is needed
    if (m_config.enableCompression && item.shouldCompress()) {
        compressItem(item);
    }

    // Ensure we have space
    while ((getCurrentMemoryUsage() + item.size) > m_config.maxMemoryUsage ||
           m_cache.size() >= m_config.maxItems) {
        if (!evictLRUItems(1)) {
            LOG_WARNING("UnifiedCacheSystem: Failed to make space for new item");
            return false;
        }
    }

    // Insert item
    m_cache[key] = item;

    updateStatistics(item, false);

    LOG_DEBUG("UnifiedCacheSystem: Cached {} type: {} size: {} compressed: {}",
              key.toStdString(), static_cast<int>(type), item.size, item.isCompressed);

    return true;
}

QVariant UnifiedCacheSystem::get(const QString& key) {
    QMutexLocker locker(&m_cacheMutex);

    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        QMutexLocker statsLocker(&m_statsMutex);
        m_stats.missCount++;
        m_stats.hitRate = static_cast<double>(m_stats.hitCount) /
                          (m_stats.hitCount + m_stats.missCount);
        return QVariant();
    }

    UnifiedCacheItem& item = it.value();
    updateItemAccess(item);

    // Decompress if needed
    if (item.isCompressed) {
        decompressItem(item);
    }

    updateStatistics(item, true);

    return item.data;
}

bool UnifiedCacheSystem::contains(const QString& key) const {
    QMutexLocker locker(&m_cacheMutex);
    return m_cache.contains(key);
}

bool UnifiedCacheSystem::remove(const QString& key) {
    QMutexLocker locker(&m_cacheMutex);

    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        return false;
    }

    UnifiedCacheType type = it.value().type;
    m_cache.erase(it);

    emit itemEvicted(key, type);
    return true;
}

void UnifiedCacheSystem::clear() {
    QMutexLocker locker(&m_cacheMutex);
    m_cache.clear();

    QMutexLocker statsLocker(&m_statsMutex);
    m_stats = UnifiedCacheStats();

    LOG_DEBUG("UnifiedCacheSystem: Cache cleared");
}

void UnifiedCacheSystem::clearType(UnifiedCacheType type) {
    QMutexLocker locker(&m_cacheMutex);

    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        if (it.value().type == type) {
            emit itemEvicted(it.key(), type);
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

bool UnifiedCacheSystem::cacheRenderedPage(int pageNumber,
                                           const QPixmap& pixmap,
                                           double scaleFactor, int rotation) {
    QString key = generateKey(UnifiedCacheType::RenderedPage, pageNumber,
                              QVariantList() << scaleFactor << rotation);
    return insert(key, pixmap, UnifiedCacheType::RenderedPage,
                  UnifiedCachePriority::High, pageNumber);
}

QPixmap UnifiedCacheSystem::getRenderedPage(int pageNumber, double scaleFactor,
                                            int rotation) {
    QString key = generateKey(UnifiedCacheType::RenderedPage, pageNumber,
                              QVariantList() << scaleFactor << rotation);
    QVariant result = get(key);
    return result.canConvert<QPixmap>() ? result.value<QPixmap>() : QPixmap();
}

bool UnifiedCacheSystem::cacheThumbnail(int pageNumber,
                                        const QPixmap& thumbnail,
                                        const QSize& size) {
    QString key = generateKey(UnifiedCacheType::Thumbnail, pageNumber, size);
    return insert(key, thumbnail, UnifiedCacheType::Thumbnail,
                  UnifiedCachePriority::Normal, pageNumber);
}

QPixmap UnifiedCacheSystem::getThumbnail(int pageNumber, const QSize& size) {
    QString key = generateKey(UnifiedCacheType::Thumbnail, pageNumber, size);
    QVariant result = get(key);
    return result.canConvert<QPixmap>() ? result.value<QPixmap>() : QPixmap();
}

bool UnifiedCacheSystem::cacheTextContent(int pageNumber, const QString& text) {
    QString key = generateKey(UnifiedCacheType::TextContent, pageNumber);
    return insert(key, text, UnifiedCacheType::TextContent,
                  UnifiedCachePriority::Normal, pageNumber);
}

QString UnifiedCacheSystem::getTextContent(int pageNumber) {
    QString key = generateKey(UnifiedCacheType::TextContent, pageNumber);
    QVariant result = get(key);
    return result.canConvert<QString>() ? result.toString() : QString();
}

qint64 UnifiedCacheSystem::getCurrentMemoryUsage() const {
    QMutexLocker locker(&m_cacheMutex);

    qint64 total = 0;
    for (const auto& item : m_cache) {
        total += item.size;
    }

    return total;
}

void UnifiedCacheSystem::optimizeMemory() {
    QMutexLocker locker(&m_cacheMutex);

    // Compress uncompressed items if memory pressure
    if (isMemoryPressure()) {
        for (auto& item : m_cache) {
            if (!item.isCompressed && item.shouldCompress()) {
                compressItem(item);
            }
        }
    }

    // Evict low-priority items
    evictByPriority(UnifiedCachePriority::Low);

    LOG_DEBUG("UnifiedCacheSystem: Memory optimized, usage: {}", getCurrentMemoryUsage());
}

void UnifiedCacheSystem::enforceMemoryLimit() {
    while (getCurrentMemoryUsage() > m_config.maxMemoryUsage) {
        if (!evictLRUItems(1)) {
            break;  // No more items to evict
        }
    }
}

bool UnifiedCacheSystem::evictLRUItems(int count) {
    if (m_cache.isEmpty()) {
        return false;
    }

    // Find items with lowest access scores
    QList<QString> candidates = selectItemsForEviction(count);

    for (const QString& key : candidates) {
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            emit itemEvicted(key, it.value().type);
            m_cache.erase(it);
        }
    }

    return !candidates.isEmpty();
}

void UnifiedCacheSystem::evictByPriority(UnifiedCachePriority maxPriority) {
    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        if (it.value().priority <= maxPriority) {
            emit itemEvicted(it.key(), it.value().type);
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

QList<QString> UnifiedCacheSystem::selectItemsForEviction(int count) const {
    QList<QPair<double, QString>> candidates;

    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        double score = it.value().getAccessScore();
        candidates.append(qMakePair(score, it.key()));
    }

    // Sort by score (lowest first)
    std::sort(candidates.begin(), candidates.end());

    QList<QString> result;
    for (int i = 0; i < qMin(count, candidates.size()); ++i) {
        result.append(candidates[i].second);
    }

    return result;
}

void UnifiedCacheSystem::compressItem(UnifiedCacheItem& item) {
    if (item.isCompressed || !m_config.enableCompression) {
        return;
    }

    QByteArray originalData;
    if (item.data.canConvert<QPixmap>()) {
        originalData = pixmapToByteArray(item.data.value<QPixmap>());
    } else if (item.data.canConvert<QString>()) {
        originalData = item.data.toString().toUtf8();
    } else {
        return;  // Cannot compress this type
    }

    QByteArray compressed =
        compressData(originalData, m_config.defaultCompressionLevel);
    if (compressed.size() <
        originalData.size() * 0.9) {  // Only if significant savings
        item.data = compressed;
        item.isCompressed = true;
        item.compressionLevel = m_config.defaultCompressionLevel;

        qint64 saved = originalData.size() - compressed.size();
        item.size = item.calculateMemorySize();

        QMutexLocker statsLocker(&m_statsMutex);
        m_stats.compressionSaved += saved;
        m_stats.compressedItems++;

        emit compressionCompleted(item.key, saved);
    }
}

void UnifiedCacheSystem::decompressItem(UnifiedCacheItem& item) {
    if (!item.isCompressed) {
        return;
    }

    QByteArray compressedData = item.data.toByteArray();
    QByteArray decompressed = decompressData(compressedData);

    if (item.type == UnifiedCacheType::RenderedPage) {
        item.data = byteArrayToPixmap(decompressed);
    } else if (item.type == UnifiedCacheType::TextContent) {
        item.data = QString::fromUtf8(decompressed);
    }

    item.isCompressed = false;
    item.size = item.calculateMemorySize();
}

QByteArray UnifiedCacheSystem::compressData(const QByteArray& data,
                                            CompressionLevel level) const {
    // Simple compression using Qt's built-in qCompress
    // In a real implementation, you might use LZ4, Zstd, or other algorithms
    int compressionLevel = 6;  // Default

    switch (level) {
        case CompressionLevel::Fast:
            compressionLevel = 1;
            break;
        case CompressionLevel::Balanced:
            compressionLevel = 6;
            break;
        case CompressionLevel::Maximum:
            compressionLevel = 9;
            break;
        default:
            return data;
    }

    return qCompress(data, compressionLevel);
}

QByteArray UnifiedCacheSystem::decompressData(
    const QByteArray& compressedData) const {
    return qUncompress(compressedData);
}

QByteArray UnifiedCacheSystem::pixmapToByteArray(const QPixmap& pixmap) const {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return data;
}

QPixmap UnifiedCacheSystem::byteArrayToPixmap(const QByteArray& data) const {
    QPixmap pixmap;
    pixmap.loadFromData(data);
    return pixmap;
}

QString UnifiedCacheSystem::generateKey(UnifiedCacheType type, int pageNumber,
                                        const QVariant& params) const {
    QString baseKey =
        QString("type_%1_page_%2").arg(static_cast<int>(type)).arg(pageNumber);

    if (params.isValid()) {
        if (params.canConvert<QVariantList>()) {
            QVariantList list = params.toList();
            for (const QVariant& param : list) {
                baseKey += QString("_%1").arg(param.toString());
            }
        } else if (params.canConvert<QSize>()) {
            QSize size = params.value<QSize>();
            baseKey += QString("_%1x%2").arg(size.width()).arg(size.height());
        } else {
            baseKey += QString("_%1").arg(params.toString());
        }
    }

    return baseKey;
}

void UnifiedCacheSystem::updateItemAccess(UnifiedCacheItem& item) {
    item.updateAccess();
}

void UnifiedCacheSystem::updateStatistics(const UnifiedCacheItem& item,
                                          bool isHit) {
    QMutexLocker locker(&m_statsMutex);

    if (isHit) {
        m_stats.hitCount++;
    } else {
        m_stats.totalItems = m_cache.size();
        m_stats.itemsByType[item.type]++;
        m_stats.memoryByType[item.type] += item.size;
    }

    m_stats.hitRate = static_cast<double>(m_stats.hitCount) /
                      (m_stats.hitCount + m_stats.missCount);
    m_stats.totalMemoryUsed = getCurrentMemoryUsage();
}

bool UnifiedCacheSystem::isMemoryPressure() const {
    return getCurrentMemoryUsage() > (m_config.maxMemoryUsage * 0.8);
}

void UnifiedCacheSystem::onCleanupTimer() { performCleanup(); }

void UnifiedCacheSystem::onStatsTimer() { updateStatistics(); }

void UnifiedCacheSystem::performCleanup() {
    QMutexLocker locker(&m_cacheMutex);

    // Remove expired items (if any expiration logic is needed)
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 maxAge = 24 * 3600 * 1000;  // 24 hours

    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        if ((now - it.value().timestamp) > maxAge &&
            it.value().priority <= UnifiedCachePriority::Low) {
            emit itemEvicted(it.key(), it.value().type);
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }

    // Enforce memory limits
    enforceMemoryLimit();
}

void UnifiedCacheSystem::updateStatistics() {
    QMutexLocker locker(&m_statsMutex);
    m_stats.totalItems = m_cache.size();
    m_stats.totalMemoryUsed = getCurrentMemoryUsage();
    m_stats.maxMemoryLimit = m_config.maxMemoryUsage;

    emit statisticsUpdated(m_stats);

    if (m_stats.totalMemoryUsed > m_stats.maxMemoryLimit) {
        emit memoryThresholdExceeded(m_stats.totalMemoryUsed,
                                     m_stats.maxMemoryLimit);
    }
}

UnifiedCacheStats UnifiedCacheSystem::getStatistics() const {
    QMutexLocker locker(&m_statsMutex);
    return m_stats;
}

void UnifiedCacheSystem::loadSettings() {
    if (!m_settings)
        return;

    m_config.maxMemoryUsage =
        m_settings->value("cache/maxMemoryUsage", 512 * 1024 * 1024)
            .toLongLong();
    m_config.maxItems = m_settings->value("cache/maxItems", 1000).toInt();
    m_config.enableCompression =
        m_settings->value("cache/enableCompression", true).toBool();
    m_config.defaultCompressionLevel = static_cast<CompressionLevel>(
        m_settings
            ->value("cache/compressionLevel",
                    static_cast<int>(CompressionLevel::Fast))
            .toInt());
    m_config.cleanupInterval =
        m_settings->value("cache/cleanupInterval", 30000).toInt();
}

void UnifiedCacheSystem::saveSettings() {
    if (!m_settings)
        return;

    m_settings->setValue("cache/maxMemoryUsage", m_config.maxMemoryUsage);
    m_settings->setValue("cache/maxItems", m_config.maxItems);
    m_settings->setValue("cache/enableCompression", m_config.enableCompression);
    m_settings->setValue("cache/compressionLevel",
                         static_cast<int>(m_config.defaultCompressionLevel));
    m_settings->setValue("cache/cleanupInterval", m_config.cleanupInterval);

    m_settings->sync();
}
