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
#include <QtEndian>
#include <algorithm>
#include <memory>
#include "utils/LoggingMacros.h"

namespace {
constexpr const char* kThumbMTimeKey = "Thumb::MTime";
constexpr const char* kThumbSizeKey = "Thumb::Size";

bool readPngTextMetadata(const QString& path, QString* mtime, QString* size) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    const QByteArray signature("\x89PNG\r\n\x1a\n", 8);
    if (!data.startsWith(signature)) {
        return false;
    }

    int offset = signature.size();
    while (offset + 8 <= data.size()) {
        const quint32 length =
            qFromBigEndian<quint32>(data.constData() + offset);
        const QByteArray type = data.mid(offset + 4, 4);
        offset += 8;
        const qint64 chunkEnd =
            static_cast<qint64>(offset) + length + 4;  // data + CRC
        if (chunkEnd > data.size()) {
            return false;
        }
        const QByteArray chunk = data.mid(offset, static_cast<int>(length));
        offset += static_cast<int>(length) + 4;  // data + CRC

        if (type == "tEXt") {
            const int separator = chunk.indexOf('\0');
            if (separator > 0) {
                const QString key = QString::fromLatin1(chunk.left(separator));
                const QString value =
                    QString::fromLatin1(chunk.mid(separator + 1));
                if (key == QString::fromLatin1(kThumbMTimeKey)) {
                    *mtime = value;
                } else if (key == QString::fromLatin1(kThumbSizeKey)) {
                    *size = value;
                }
            }
        }
        if (type == "IEND") {
            break;
        }
    }
    return !mtime->isEmpty() && !size->isEmpty();
}
}  // namespace

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

    const QFileInfo sourceInfo(filePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return {};
    }
    return cachedThumbnailWithMetadata(
        filePath, size, sourceInfo.lastModified().toMSecsSinceEpoch(),
        sourceInfo.size());
}

QPixmap RecentPdfThumbnailProvider::cachedThumbnailWithMetadata(
    const QString& filePath, const QSize& size, qint64 sourceModifiedTimeMs,
    qint64 sourceSize) const {
    const QString key = cacheKey(filePath, size);
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_memoryCache.find(key);
        if (it != m_memoryCache.end()) {
            if (it->sourceModifiedTimeMs == sourceModifiedTimeMs &&
                it->sourceSize == sourceSize) {
                m_cacheAccessTimes[key] = QDateTime::currentDateTimeUtc();
                ++m_memoryHitCount;
                return it->pixmap;
            }
            m_memoryCache.erase(it);
            m_cacheAccessTimes.remove(key);
        }
    }

    const QString cachePath = cacheFilePath(key);
    QImage image(cachePath);
    if (image.isNull() ||
        !imageMatchesSource(image, sourceModifiedTimeMs, sourceSize)) {
        return {};
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    if (pixmap.isNull()) {
        return {};
    }

    {
        QMutexLocker locker(&m_mutex);
        ++m_diskHitCount;
    }

    const_cast<RecentPdfThumbnailProvider*>(this)->insertMemoryCache(
        key, pixmap, sourceModifiedTimeMs, sourceSize);
    return pixmap;
}

quint64 RecentPdfThumbnailProvider::memoryHitCount() const {
    QMutexLocker locker(&m_mutex);
    return m_memoryHitCount;
}

quint64 RecentPdfThumbnailProvider::diskHitCount() const {
    QMutexLocker locker(&m_mutex);
    return m_diskHitCount;
}

void RecentPdfThumbnailProvider::resetStatistics() {
    QMutexLocker locker(&m_mutex);
    m_memoryHitCount = 0;
    m_diskHitCount = 0;
}

void RecentPdfThumbnailProvider::requestThumbnail(const QString& filePath,
                                                  const QSize& size) {
    if (filePath.isEmpty() || !size.isValid() || !isPdfPath(filePath)) {
        emit thumbnailFailed(filePath);
        return;
    }

    const QFileInfo sourceInfo(filePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        emit thumbnailFailed(filePath);
        return;
    }
    const qint64 sourceModifiedTimeMs =
        sourceInfo.lastModified().toMSecsSinceEpoch();
    const qint64 sourceSize = sourceInfo.size();

    const QString key = cacheKey(filePath, size);
    const QString cachePath = cacheFilePath(key);

    QPixmap cached = cachedThumbnailWithMetadata(
        filePath, size, sourceModifiedTimeMs, sourceSize);
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
            [this, watcher, filePath, key, cachePath, sourceModifiedTimeMs,
             sourceSize]() {
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

                const QFileInfo currentInfo(filePath);
                if (!currentInfo.exists() || !currentInfo.isFile() ||
                    currentInfo.size() != sourceSize ||
                    currentInfo.lastModified().toMSecsSinceEpoch() !=
                        sourceModifiedTimeMs) {
                    LOG_DEBUG(
                        "RecentPdfThumbnailProvider: source changed during "
                        "render: {}",
                        filePath.toStdString());
                    emit thumbnailFailed(filePath);
                    return;
                }

                image.setText(kThumbMTimeKey,
                              QString::number(sourceModifiedTimeMs));
                image.setText(kThumbSizeKey, QString::number(sourceSize));

                QPixmap pixmap = QPixmap::fromImage(image);
                if (pixmap.isNull()) {
                    emit thumbnailFailed(filePath);
                    return;
                }

                insertMemoryCache(key, pixmap, sourceModifiedTimeMs,
                                  sourceSize);
                ensureCacheDirectory();
                // QPixmap::save() drops QImage text metadata. Save the QImage
                // so the PNG keeps the Thumb::MTime/Thumb::Size fields.
                image.save(cachePath, "PNG");
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
    const QFileInfo sourceInfo(sourcePath);
    const QFileInfo cacheInfo(cachePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile() || !cacheInfo.exists() ||
        !cacheInfo.isFile()) {
        return false;
    }

    QString cachedMTimeText;
    QString cachedSizeText;
    if (!readPngTextMetadata(cachePath, &cachedMTimeText, &cachedSizeText)) {
        return false;
    }
    bool mtimeOk = false;
    bool sizeOk = false;
    const qint64 cachedMTime = cachedMTimeText.toLongLong(&mtimeOk);
    const qint64 cachedSize = cachedSizeText.toLongLong(&sizeOk);
    return mtimeOk && sizeOk &&
           cachedMTime == sourceInfo.lastModified().toMSecsSinceEpoch() &&
           cachedSize == sourceInfo.size();
}

bool RecentPdfThumbnailProvider::imageMatchesSource(const QImage& image,
                                                    qint64 sourceModifiedTimeMs,
                                                    qint64 sourceSize) {
    bool mtimeOk = false;
    bool sizeOk = false;
    const qint64 cachedMTime = image.text(kThumbMTimeKey).toLongLong(&mtimeOk);
    const qint64 cachedSize = image.text(kThumbSizeKey).toLongLong(&sizeOk);
    return mtimeOk && sizeOk && cachedMTime == sourceModifiedTimeMs &&
           cachedSize == sourceSize;
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
                                                   const QPixmap& pixmap,
                                                   qint64 sourceModifiedTimeMs,
                                                   qint64 sourceSize) {
    QMutexLocker locker(&m_mutex);
    m_memoryCache.insert(
        key, MemoryCacheEntry{pixmap, sourceModifiedTimeMs, sourceSize});
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
