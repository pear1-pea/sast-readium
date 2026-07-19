#include "RecentPdfThumbnailProvider.h"

#include <poppler/qt6/poppler-qt6.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>
#include <memory>
#include "utils/LoggingMacros.h"

const int RecentPdfThumbnailProvider::MAX_MEMORY_CACHE_ITEMS;
const int RecentPdfThumbnailProvider::MAX_DISK_CACHE_ITEMS;

RecentPdfThumbnailProvider::RecentPdfThumbnailProvider(QObject* parent)
    : QObject(parent) {
    m_cacheDirectory =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        "/recent-thumbnails";
    ensureCacheDirectory();
    trimDiskCache();
}

QPixmap RecentPdfThumbnailProvider::cachedThumbnail(const QString& filePath,
                                                    const QSize& size) const {
    if (filePath.isEmpty() || !size.isValid() || !isPdfPath(filePath)) {
        return {};
    }

    const QString key = cacheKey(filePath, size);
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_memoryCache.constFind(key);
        if (it != m_memoryCache.constEnd()) {
            m_cacheAccessTimes[key] = QDateTime::currentDateTimeUtc();
            return it.value();
        }
    }

    const QString cachePath = cacheFilePath(key);
    if (!isDiskCacheFresh(filePath, cachePath)) {
        return {};
    }

    QPixmap pixmap(cachePath);
    if (pixmap.isNull()) {
        return {};
    }

    const_cast<RecentPdfThumbnailProvider*>(this)->insertMemoryCache(key,
                                                                     pixmap);
    return pixmap;
}

void RecentPdfThumbnailProvider::requestThumbnail(const QString& filePath,
                                                  const QSize& size) {
    if (filePath.isEmpty() || !size.isValid() || !isPdfPath(filePath)) {
        emit thumbnailFailed(filePath);
        return;
    }

    const QString key = cacheKey(filePath, size);
    const QString cachePath = cacheFilePath(key);

    QPixmap cached = cachedThumbnail(filePath, size);
    if (!cached.isNull()) {
        emit thumbnailReady(filePath, cached);
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingRequests.contains(key)) {
            return;
        }
        m_pendingRequests.insert(key);
    }

    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, filePath, key, cachePath]() {
                QImage image = watcher->result();
                watcher->deleteLater();

                {
                    QMutexLocker locker(&m_mutex);
                    m_pendingRequests.remove(key);
                }

                if (image.isNull()) {
                    LOG_DEBUG("RecentPdfThumbnailProvider: failed to render {}",
                              filePath.toStdString());
                    emit thumbnailFailed(filePath);
                    return;
                }

                QPixmap pixmap = QPixmap::fromImage(image);
                if (pixmap.isNull()) {
                    emit thumbnailFailed(filePath);
                    return;
                }

                insertMemoryCache(key, pixmap);
                ensureCacheDirectory();
                pixmap.save(cachePath, "PNG");
                trimDiskCache();
                emit thumbnailReady(filePath, pixmap);
            });

    watcher->setFuture(QtConcurrent::run([filePath, size]() {
        return renderFirstPageTopSquare(filePath, size);
    }));
}

QString RecentPdfThumbnailProvider::cacheKeyForTesting(const QString& filePath,
                                                       const QSize& size) {
    return cacheKey(filePath, size);
}

bool RecentPdfThumbnailProvider::isDiskCacheFreshForTesting(
    const QString& sourcePath, const QString& cachePath) {
    return isDiskCacheFresh(sourcePath, cachePath);
}

QString RecentPdfThumbnailProvider::cacheKey(const QString& filePath,
                                             const QSize& size) {
    const QString input = QString("%1|%2x%3")
                              .arg(normalizedPath(filePath))
                              .arg(size.width())
                              .arg(size.height());
    return QString::fromLatin1(
        QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Sha1)
            .toHex());
}

QString RecentPdfThumbnailProvider::normalizedPath(const QString& filePath) {
    QFileInfo info(filePath);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool RecentPdfThumbnailProvider::isPdfPath(const QString& filePath) {
    return QFileInfo(filePath).suffix().compare("pdf", Qt::CaseInsensitive) ==
           0;
}

bool RecentPdfThumbnailProvider::isDiskCacheFresh(const QString& sourcePath,
                                                  const QString& cachePath) {
    QFileInfo sourceInfo(sourcePath);
    QFileInfo cacheInfo(cachePath);

    if (!sourceInfo.exists() || !sourceInfo.isFile() || !cacheInfo.exists() ||
        !cacheInfo.isFile()) {
        return false;
    }

    return cacheInfo.lastModified() >= sourceInfo.lastModified();
}

QImage RecentPdfThumbnailProvider::renderFirstPageTopSquare(
    const QString& filePath, const QSize& targetSize) {
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile() || !isPdfPath(filePath)) {
        return {};
    }

    std::unique_ptr<Poppler::Document> document(
        Poppler::Document::load(filePath));
    if (!document || document->isLocked() || document->numPages() <= 0) {
        return {};
    }

    std::unique_ptr<Poppler::Page> page(document->page(0));
    if (!page) {
        return {};
    }

    const double dpi = calculateDpiForTarget(page->pageSizeF(), targetSize);
    QImage image = page->renderToImage(dpi, dpi);
    if (image.isNull()) {
        return {};
    }

    const int side = qMin(image.width(), image.height());
    if (side <= 0) {
        return {};
    }

    const int x = (image.width() - side) / 2;
    const QImage square = image.copy(x, 0, side, side);
    return square.scaled(targetSize, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
}

double RecentPdfThumbnailProvider::calculateDpiForTarget(
    const QSizeF& pageSize, const QSize& targetSize) {
    if (pageSize.isEmpty() || !targetSize.isValid()) {
        return 72.0;
    }

    const double scale = qMax(targetSize.width() / pageSize.width(),
                              targetSize.height() / pageSize.height());
    return qBound(72.0, scale * 72.0 * 2.0, 180.0);
}

QString RecentPdfThumbnailProvider::cacheFilePath(const QString& key) const {
    return QDir(m_cacheDirectory).filePath(key + ".png");
}

void RecentPdfThumbnailProvider::ensureCacheDirectory() const {
    QDir dir(m_cacheDirectory);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}

void RecentPdfThumbnailProvider::insertMemoryCache(const QString& key,
                                                   const QPixmap& pixmap) {
    QMutexLocker locker(&m_mutex);
    m_memoryCache.insert(key, pixmap);
    m_cacheAccessTimes.insert(key, QDateTime::currentDateTimeUtc());
    trimMemoryCache();
}

void RecentPdfThumbnailProvider::trimMemoryCache() {
    while (m_memoryCache.size() > MAX_MEMORY_CACHE_ITEMS) {
        auto oldestIt = m_cacheAccessTimes.constBegin();
        for (auto it = m_cacheAccessTimes.constBegin();
             it != m_cacheAccessTimes.constEnd(); ++it) {
            if (it.value() < oldestIt.value()) {
                oldestIt = it;
            }
        }

        const QString key = oldestIt.key();
        m_cacheAccessTimes.remove(key);
        m_memoryCache.remove(key);
    }
}

void RecentPdfThumbnailProvider::trimDiskCache() const {
    QDir dir(m_cacheDirectory);
    const QFileInfoList files =
        dir.entryInfoList({"*.png"}, QDir::Files, QDir::Time | QDir::Reversed);
    if (files.size() <= MAX_DISK_CACHE_ITEMS) {
        return;
    }

    for (int i = 0; i < files.size() - MAX_DISK_CACHE_ITEMS; ++i) {
        QFile::remove(files[i].absoluteFilePath());
    }
}
