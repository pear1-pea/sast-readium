#pragma once

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QString>

class RecentPdfThumbnailProvider : public QObject {
    Q_OBJECT

public:
    struct MemoryCacheEntry {
        QPixmap pixmap;
        qint64 sourceModifiedTimeMs = 0;
        qint64 sourceSize = 0;
    };

    explicit RecentPdfThumbnailProvider(QObject* parent = nullptr);

    QPixmap cachedThumbnail(const QString& filePath, const QSize& size) const;
    void requestThumbnail(const QString& filePath, const QSize& size);

    static QString cacheKeyForTesting(const QString& filePath,
                                      const QSize& size);
    static bool isDiskCacheFreshForTesting(const QString& sourcePath,
                                           const QString& cachePath);
    quint64 memoryHitCount() const;
    quint64 diskHitCount() const;
    void resetStatistics();

signals:
    void thumbnailReady(const QString& filePath, const QPixmap& pixmap);
    void thumbnailFailed(const QString& filePath);

private:
    static QString cacheKey(const QString& filePath, const QSize& size);
    static QString normalizedPath(const QString& filePath);
    static bool isPdfPath(const QString& filePath);
    static bool isDiskCacheFresh(const QString& sourcePath,
                                 const QString& cachePath);
    static bool imageMatchesSource(const QImage& image,
                                   qint64 sourceModifiedTimeMs,
                                   qint64 sourceSize);
    static QImage renderFirstPageTopSquare(const QString& filePath,
                                           const QSize& targetSize);
    static double calculateDpiForTarget(const QSizeF& pageSize,
                                        const QSize& targetSize);

    QString cacheFilePath(const QString& key) const;
    QPixmap cachedThumbnailWithMetadata(const QString& filePath,
                                        const QSize& size,
                                        qint64 sourceModifiedTimeMs,
                                        qint64 sourceSize) const;
    void ensureCacheDirectory() const;
    void insertMemoryCache(const QString& key, const QPixmap& pixmap,
                           qint64 sourceModifiedTimeMs, qint64 sourceSize);
    void trimMemoryCache();
    void trimDiskCache() const;

    mutable QMutex m_mutex;
    mutable QHash<QString, MemoryCacheEntry> m_memoryCache;
    mutable QHash<QString, QDateTime> m_cacheAccessTimes;
    QSet<QString> m_pendingRequests;
    mutable quint64 m_memoryHitCount = 0;
    mutable quint64 m_diskHitCount = 0;
    QString m_cacheDirectory;

    static const int MAX_MEMORY_CACHE_ITEMS = 50;
    static const int MAX_DISK_CACHE_ITEMS = 100;
};
