#include "UnifiedRenderManager.h"
#include "../cache/PDFCacheManager.h"
#include "PerformanceMonitor.h"
#include <QApplication>
#include <QThread>
#include <QThreadPool>
#include <QDebug>
#include <QDateTime>
#include <QUuid>
#include <QMutexLocker>
#include <QPixmap>
#include <QList>
#include <QTimer>
#include <QFutureWatcher>
#include <QSettings>
#include <QtGlobal>
#include <QObject>
#include <QRunnable>
#include <QMetaObject>
#include <QMetaType>
#include <poppler-qt6.h>
#include <algorithm>
#include <cmath>

// Custom render task for thread pool
class RenderTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    RenderTask(UnifiedRenderManager* manager, const RenderRequest& request)
        : m_manager(manager), m_request(request)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (!m_manager) return;

        QPixmap result = m_manager->renderPageImmediate(
            m_request.pageNumber, m_request.scaleFactor,
            m_request.rotation, m_request.quality);

        RenderResult renderResult;
        renderResult.requestId = m_request.requestId;
        renderResult.pageNumber = m_request.pageNumber;
        renderResult.pixmap = result;
        renderResult.quality = m_request.quality;
        renderResult.success = !result.isNull();
        renderResult.renderTime = QDateTime::currentMSecsSinceEpoch() - m_request.timestamp;

        // Emit result on main thread
        QMetaObject::invokeMethod(m_manager, "onRenderTaskCompleted",
                                 Qt::QueuedConnection,
                                 Q_ARG(RenderResult, renderResult));
    }

signals:
    void finished(const RenderResult& result);

private:
    UnifiedRenderManager* m_manager;
    RenderRequest m_request;
};

// Register custom types for Qt's meta-object system
Q_DECLARE_METATYPE(RenderResult)

// Include the MOC file
#include "UnifiedRenderManager.moc"

// RenderRequest Implementation
QString RenderRequest::getCacheKey() const
{
    return QString("page_%1_scale_%2_rot_%3_qual_%4")
           .arg(pageNumber)
           .arg(scaleFactor, 0, 'f', 2)
           .arg(rotation)
           .arg(static_cast<int>(quality));
}

bool RenderRequest::isValid() const
{
    return pageNumber >= 0 && scaleFactor > 0.0 && !requestId.isEmpty();
}

// UnifiedRenderManager Implementation
UnifiedRenderManager::UnifiedRenderManager(QObject* parent)
    : QObject(parent)
    , m_document(nullptr)
    , m_cacheManager(nullptr)
    , m_performanceMonitor(nullptr)
    , m_renderThreadPool(nullptr)
    , m_defaultQuality(RenderQuality::Normal)
    , m_renderingEnabled(true)
    , m_isPaused(false)
    , m_queueProcessor(nullptr)
    , m_adaptiveTimer(nullptr)
    , m_memoryMonitor(nullptr)
    , m_memoryLimit(512 * 1024 * 1024) // 512MB
    , m_settings(nullptr)
{
    initializeComponents();
    setupTimers();
    loadSettings();
    
    qDebug() << "UnifiedRenderManager: Initialized with memory limit:" << m_memoryLimit;
}

UnifiedRenderManager::~UnifiedRenderManager()
{
    saveSettings();
    
    if (m_renderThreadPool) {
        m_renderThreadPool->waitForDone(5000);
    }
    
    cancelAllRequests();
}

void UnifiedRenderManager::initializeComponents()
{
    // Initialize cache manager
    m_cacheManager = new PDFCacheManager(this);
    m_cacheManager->setMaxMemoryUsage(m_memoryLimit * 0.7); // 70% for cache
    
    // Initialize performance monitor
    m_performanceMonitor = &PerformanceMonitor::instance();
    
    // Initialize thread pool
    m_renderThreadPool = new QThreadPool(this);
    m_renderThreadPool->setMaxThreadCount(m_adaptiveConfig.maxConcurrentRenders);
    
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-RenderManager", this);
    
    // Connect signals
    connect(m_cacheManager, &PDFCacheManager::memoryThresholdExceeded,
            this, &UnifiedRenderManager::onMemoryPressure);
}

void UnifiedRenderManager::setupTimers()
{
    // Queue processor timer
    m_queueProcessor = new QTimer(this);
    m_queueProcessor->setInterval(10); // Process queue every 10ms
    connect(m_queueProcessor, &QTimer::timeout, this, &UnifiedRenderManager::processRenderQueue);
    m_queueProcessor->start();
    
    // Adaptive analysis timer
    m_adaptiveTimer = new QTimer(this);
    m_adaptiveTimer->setInterval(30000); // 30 seconds
    connect(m_adaptiveTimer, &QTimer::timeout, this, &UnifiedRenderManager::onAdaptiveAnalysis);
    m_adaptiveTimer->start();
    
    // Memory monitor timer
    m_memoryMonitor = new QTimer(this);
    m_memoryMonitor->setInterval(5000); // 5 seconds
    connect(m_memoryMonitor, &QTimer::timeout, this, &UnifiedRenderManager::enforceMemoryLimit);
    m_memoryMonitor->start();
}

void UnifiedRenderManager::setDocument(Poppler::Document* document)
{
    if (m_document == document) {
        return;
    }
    
    // Cancel all pending requests for old document
    cancelAllRequests();
    
    m_document = document;
    
    if (m_document) {
        // Configure document for optimal rendering
        configureDocument(m_document, m_defaultQuality);
        
        // Clear cache for new document
        m_cacheManager->clear();
        
        qDebug() << "UnifiedRenderManager: Document set with" << m_document->numPages() << "pages";
    }
    
    // Reset statistics
    resetStatistics();
}

QString UnifiedRenderManager::requestRender(int pageNumber, double scaleFactor, 
                                           int rotation, RenderQuality quality,
                                           RenderPriority priority, const QSize& targetSize)
{
    if (!m_document || pageNumber < 0 || pageNumber >= m_document->numPages()) {
        return QString();
    }
    
    // Check cache first
    QString cacheKey = QString("page_%1_scale_%2_rot_%3_qual_%4")
                      .arg(pageNumber)
                      .arg(scaleFactor, 0, 'f', 2)
                      .arg(rotation)
                      .arg(static_cast<int>(quality));
    
    if (m_cacheManager->contains(cacheKey)) {
        // Cache hit - return immediately
        QMutexLocker locker(&m_statsMutex);
        m_statistics.cacheHits++;
        m_statistics.cacheHitRate = static_cast<double>(m_statistics.cacheHits) / 
                                   (m_statistics.cacheHits + m_statistics.cacheMisses);
        
        // Emit result immediately
        RenderResult result;
        result.requestId = generateRequestId();
        result.pageNumber = pageNumber;
        result.pixmap = m_cacheManager->get(cacheKey).value<QPixmap>();
        result.quality = quality;
        result.renderTime = 0;
        result.success = true;
        
        QTimer::singleShot(0, this, [this, result]() {
            emit renderCompleted(result);
        });
        
        return result.requestId;
    }
    
    // Cache miss - queue for rendering
    QMutexLocker locker(&m_statsMutex);
    m_statistics.cacheMisses++;
    m_statistics.cacheHitRate = static_cast<double>(m_statistics.cacheHits) / 
                               (m_statistics.cacheHits + m_statistics.cacheMisses);
    locker.unlock();
    
    // Create render request
    RenderRequest request;
    request.pageNumber = pageNumber;
    request.scaleFactor = scaleFactor;
    request.rotation = rotation;
    request.quality = quality;
    request.priority = priority;
    request.targetSize = targetSize;
    request.requestId = generateRequestId();
    request.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    // Add to queue
    QMutexLocker queueLocker(&m_queueMutex);
    m_renderQueue.enqueue(request);
    m_activeRequests[request.requestId] = request;
    
    // Sort queue by priority
    std::sort(m_renderQueue.begin(), m_renderQueue.end(), 
              [](const RenderRequest& a, const RenderRequest& b) {
                  return a.priority > b.priority; // Higher priority first
              });
    
    queueLocker.unlock();
    
    // Update statistics
    QMutexLocker statsLocker(&m_statsMutex);
    m_statistics.totalRequests++;
    
    qDebug() << "UnifiedRenderManager: Queued render request" << request.requestId 
             << "for page" << pageNumber << "priority" << static_cast<int>(priority);
    
    return request.requestId;
}

QPixmap UnifiedRenderManager::renderPageImmediate(int pageNumber, double scaleFactor,
                                                 int rotation, RenderQuality quality)
{
    if (!m_document || pageNumber < 0 || pageNumber >= m_document->numPages()) {
        return QPixmap();
    }
    
    // Check cache first
    QString cacheKey = QString("page_%1_scale_%2_rot_%3_qual_%4")
                      .arg(pageNumber)
                      .arg(scaleFactor, 0, 'f', 2)
                      .arg(rotation)
                      .arg(static_cast<int>(quality));
    
    QPixmap cached = m_cacheManager->get(cacheKey).value<QPixmap>();
    if (!cached.isNull()) {
        return cached;
    }
    
    // Render immediately
    QElapsedTimer timer;
    timer.start();
    
    try {
        std::unique_ptr<Poppler::Page> page(m_document->page(pageNumber));
        if (!page) {
            return QPixmap();
        }
        
        double dpi = calculateDPI(scaleFactor, quality);
        
        QImage image = page->renderToImage(dpi, dpi, -1, -1, -1, -1,
                                          static_cast<Poppler::Page::Rotation>(rotation / 90));
        
        if (image.isNull()) {
            return QPixmap();
        }
        
        QPixmap pixmap = QPixmap::fromImage(image);
        
        // Cache the result
        m_cacheManager->insert(cacheKey, pixmap,
                              CacheItemType::RenderedPage,
                              CachePriority::Normal, pageNumber);
        
        // Update performance metrics
        qint64 renderTime = timer.elapsed();
        m_performanceMonitor->recordRenderTime(pageNumber, renderTime);
        
        return pixmap;
        
    } catch (const std::exception& e) {
        qWarning() << "UnifiedRenderManager: Exception in immediate render:" << e.what();
        return QPixmap();
    }
}

void UnifiedRenderManager::processRenderQueue()
{
    if (!m_renderingEnabled || m_isPaused || !m_document) {
        return;
    }
    
    QMutexLocker locker(&m_queueMutex);
    
    if (m_renderQueue.isEmpty()) {
        return;
    }
    
    // Check if we have available threads
    if (m_renderThreadPool->activeThreadCount() >= m_adaptiveConfig.maxConcurrentRenders) {
        return;
    }
    
    // Get next request
    RenderRequest request = m_renderQueue.dequeue();
    locker.unlock();
    
    // Process the request asynchronously using custom task
    RenderTask* task = new RenderTask(this, request);
    m_renderThreadPool->start(task);
}

void UnifiedRenderManager::onRenderTaskCompleted(const RenderResult& result)
{
    if (result.success) {
        updateStatistics(result);
        emit renderCompleted(result);
    } else {
        emit renderFailed(result.requestId, "Render failed");
    }

    // Remove from active requests
    QMutexLocker locker(&m_queueMutex);
    m_activeRequests.remove(result.requestId);
}

void UnifiedRenderManager::cancelRequest(const QString& requestId)
{
    QMutexLocker locker(&m_queueMutex);

    // Remove from active requests
    m_activeRequests.remove(requestId);

    // Remove from queue if not yet processed
    for (auto it = m_renderQueue.begin(); it != m_renderQueue.end(); ++it) {
        if (it->requestId == requestId) {
            m_renderQueue.erase(it);
            break;
        }
    }
}

void UnifiedRenderManager::cancelAllRequests()
{
    QMutexLocker locker(&m_queueMutex);
    m_renderQueue.clear();
    m_activeRequests.clear();
}

QPixmap UnifiedRenderManager::getCachedPage(int pageNumber, double scaleFactor, int rotation)
{
    QString cacheKey = QString("page_%1_scale_%2_rot_%3_qual_%4")
                      .arg(pageNumber)
                      .arg(scaleFactor, 0, 'f', 2)
                      .arg(rotation)
                      .arg(static_cast<int>(m_defaultQuality));

    return m_cacheManager->get(cacheKey).value<QPixmap>();
}

bool UnifiedRenderManager::hasPageInCache(int pageNumber, double scaleFactor, int rotation)
{
    QString cacheKey = QString("page_%1_scale_%2_rot_%3_qual_%4")
                      .arg(pageNumber)
                      .arg(scaleFactor, 0, 'f', 2)
                      .arg(rotation)
                      .arg(static_cast<int>(m_defaultQuality));

    return m_cacheManager->contains(cacheKey);
}

void UnifiedRenderManager::preloadPages(const QList<int>& pageNumbers, double scaleFactor)
{
    for (int pageNumber : pageNumbers) {
        requestRender(pageNumber, scaleFactor, 0, m_defaultQuality, RenderPriority::Background);
    }
}

RenderStatistics UnifiedRenderManager::getStatistics() const
{
    QMutexLocker locker(&m_statsMutex);
    return m_statistics;
}

void UnifiedRenderManager::resetStatistics()
{
    QMutexLocker locker(&m_statsMutex);
    m_statistics = RenderStatistics();
}

bool UnifiedRenderManager::isRenderingActive() const
{
    return m_renderThreadPool->activeThreadCount() > 0;
}

int UnifiedRenderManager::getQueueSize() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_queueMutex));
    return m_renderQueue.size();
}

void UnifiedRenderManager::recordPageView(int pageNumber, qint64 viewDuration)
{
    if (pageNumber < 0) return;

    m_pageViewTimes[pageNumber].append(viewDuration);

    // Keep only recent view times
    if (m_pageViewTimes[pageNumber].size() > 10) {
        m_pageViewTimes[pageNumber].removeFirst();
    }

    // Update recent pages for prediction
    m_recentPages.append(pageNumber);
    if (m_recentPages.size() > 20) {
        m_recentPages.removeFirst();
    }

    // Trigger predictive prerendering if enabled
    if (m_adaptiveConfig.enablePredictivePrerendering) {
        QList<int> predictedPages = predictNextPages(pageNumber);
        schedulePrerendering(predictedPages, 1.0);
    }
}

void UnifiedRenderManager::recordNavigation(int fromPage, int toPage)
{
    if (fromPage < 0 || toPage < 0) return;

    m_navigationPatterns[fromPage][toPage]++;
}

void UnifiedRenderManager::optimizeMemoryUsage()
{
    m_cacheManager->optimizeCache();
    enforceMemoryLimit();
}

void UnifiedRenderManager::clearCache()
{
    m_cacheManager->clear();

    QMutexLocker locker(&m_statsMutex);
    m_statistics.cacheHits = 0;
    m_statistics.cacheMisses = 0;
    m_statistics.cacheHitRate = 0.0;
}

qint64 UnifiedRenderManager::getCurrentMemoryUsage() const
{
    return m_cacheManager->getCurrentMemoryUsage();
}

void UnifiedRenderManager::pauseRendering()
{
    m_isPaused = true;
    qDebug() << "UnifiedRenderManager: Rendering paused";
}

void UnifiedRenderManager::resumeRendering()
{
    m_isPaused = false;
    qDebug() << "UnifiedRenderManager: Rendering resumed";
}

void UnifiedRenderManager::setRenderingEnabled(bool enabled)
{
    m_renderingEnabled = enabled;
    qDebug() << "UnifiedRenderManager: Rendering" << (enabled ? "enabled" : "disabled");
}

void UnifiedRenderManager::onMemoryPressure()
{
    qDebug() << "UnifiedRenderManager: Memory pressure detected, optimizing...";

    // Reduce cache size temporarily
    qint64 currentLimit = m_cacheManager->getMaxMemoryUsage();
    m_cacheManager->setMaxMemoryUsage(currentLimit * 0.8);

    // Clear low-priority cache items
    m_cacheManager->optimizeCache();

    // Pause background rendering temporarily
    bool wasPaused = m_isPaused;
    pauseRendering();

    QTimer::singleShot(5000, this, [this, wasPaused, currentLimit]() {
        // Restore settings after 5 seconds
        m_cacheManager->setMaxMemoryUsage(currentLimit);
        if (!wasPaused) {
            resumeRendering();
        }
    });
}

void UnifiedRenderManager::onAdaptiveAnalysis()
{
    if (!m_adaptiveConfig.enableAdaptiveQuality && !m_adaptiveConfig.enablePredictivePrerendering) {
        return;
    }

    // Analyze performance and adjust settings
    RenderStatistics stats = getStatistics();

    if (stats.averageRenderTime > 1000) { // > 1 second
        // Rendering is slow, reduce quality for background renders
        if (m_adaptiveConfig.enableAdaptiveQuality) {
            // Implementation would adjust quality based on performance
            qDebug() << "UnifiedRenderManager: Adaptive quality adjustment triggered";
        }
    }

    if (stats.cacheHitRate < 0.7) { // < 70% hit rate
        // Low cache hit rate, increase prerendering
        if (m_adaptiveConfig.enablePredictivePrerendering && !m_recentPages.isEmpty()) {
            int currentPage = m_recentPages.last();
            QList<int> predictedPages = predictNextPages(currentPage, 5);
            schedulePrerendering(predictedPages, 1.0);
        }
    }
}

void UnifiedRenderManager::enforceMemoryLimit()
{
    qint64 currentUsage = getCurrentMemoryUsage();
    if (currentUsage > m_memoryLimit) {
        qDebug() << "UnifiedRenderManager: Memory limit exceeded, optimizing...";
        optimizeMemoryUsage();
    }
}

QList<int> UnifiedRenderManager::predictNextPages(int currentPage, int count)
{
    QList<int> predicted;

    if (!m_document || currentPage < 0) {
        return predicted;
    }

    // Simple prediction: next few pages
    for (int i = 1; i <= count && (currentPage + i) < m_document->numPages(); ++i) {
        predicted.append(currentPage + i);
    }

    // Add pages based on navigation patterns
    if (m_navigationPatterns.contains(currentPage)) {
        auto patterns = m_navigationPatterns[currentPage];
        auto it = patterns.begin();
        while (it != patterns.end() && predicted.size() < count) {
            if (!predicted.contains(it.key())) {
                predicted.append(it.key());
            }
            ++it;
        }
    }

    return predicted;
}

void UnifiedRenderManager::schedulePrerendering(const QList<int>& pages, double scaleFactor)
{
    for (int page : pages) {
        if (!hasPageInCache(page, scaleFactor, 0)) {
            requestRender(page, scaleFactor, 0, RenderQuality::Normal, RenderPriority::Background);
        }
    }
}

void UnifiedRenderManager::updateStatistics(const RenderResult& result)
{
    QMutexLocker locker(&m_statsMutex);

    if (result.success) {
        m_statistics.completedRequests++;

        // Update average render time
        double totalTime = m_statistics.averageRenderTime * (m_statistics.completedRequests - 1);
        totalTime += result.renderTime;
        m_statistics.averageRenderTime = totalTime / m_statistics.completedRequests;
    } else {
        m_statistics.failedRequests++;
    }

    // Update cache hit rate
    m_statistics.cacheHitRate = static_cast<double>(m_statistics.cacheHits) /
                               (m_statistics.cacheHits + m_statistics.cacheMisses);

    // Update memory usage
    m_statistics.totalMemoryUsed = getCurrentMemoryUsage();

    emit statisticsUpdated(m_statistics);
}

QString UnifiedRenderManager::generateRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

double UnifiedRenderManager::calculateDPI(double scaleFactor, RenderQuality quality) const
{
    double baseDpi = 72.0;
    double qualityMultiplier = 1.0;

    switch (quality) {
        case RenderQuality::Draft:
            qualityMultiplier = 0.5;
            break;
        case RenderQuality::Normal:
            qualityMultiplier = 1.0;
            break;
        case RenderQuality::High:
            qualityMultiplier = 1.5;
            break;
        case RenderQuality::Print:
            qualityMultiplier = 2.0;
            break;
    }

    double deviceRatio = qApp->devicePixelRatio();
    return baseDpi * scaleFactor * qualityMultiplier * deviceRatio;
}

void UnifiedRenderManager::configureDocument(Poppler::Document* doc, RenderQuality quality) const
{
    if (!doc) return;

    // Configure rendering hints based on quality
    bool highQuality = (quality == RenderQuality::High || quality == RenderQuality::Print);

    doc->setRenderHint(Poppler::Document::Antialiasing, true);
    doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
    doc->setRenderHint(Poppler::Document::TextHinting, highQuality);
    doc->setRenderHint(Poppler::Document::TextSlightHinting, highQuality);
    doc->setRenderHint(Poppler::Document::ThinLineShape, highQuality);
}

void UnifiedRenderManager::loadSettings()
{
    if (!m_settings) return;

    m_adaptiveConfig.enableAdaptiveQuality = m_settings->value("adaptive/enableAdaptiveQuality", true).toBool();
    m_adaptiveConfig.enablePredictivePrerendering = m_settings->value("adaptive/enablePredictivePrerendering", true).toBool();
    m_adaptiveConfig.enableMemoryOptimization = m_settings->value("adaptive/enableMemoryOptimization", true).toBool();
    m_adaptiveConfig.qualityThreshold = m_settings->value("adaptive/qualityThreshold", 0.8).toDouble();
    m_adaptiveConfig.maxConcurrentRenders = m_settings->value("adaptive/maxConcurrentRenders", 4).toInt();
    m_adaptiveConfig.memoryLimit = m_settings->value("adaptive/memoryLimit", 512 * 1024 * 1024).toLongLong();

    m_memoryLimit = m_adaptiveConfig.memoryLimit;

    if (m_renderThreadPool) {
        m_renderThreadPool->setMaxThreadCount(m_adaptiveConfig.maxConcurrentRenders);
    }
}

void UnifiedRenderManager::saveSettings()
{
    if (!m_settings) return;

    m_settings->setValue("adaptive/enableAdaptiveQuality", m_adaptiveConfig.enableAdaptiveQuality);
    m_settings->setValue("adaptive/enablePredictivePrerendering", m_adaptiveConfig.enablePredictivePrerendering);
    m_settings->setValue("adaptive/enableMemoryOptimization", m_adaptiveConfig.enableMemoryOptimization);
    m_settings->setValue("adaptive/qualityThreshold", m_adaptiveConfig.qualityThreshold);
    m_settings->setValue("adaptive/maxConcurrentRenders", m_adaptiveConfig.maxConcurrentRenders);
    m_settings->setValue("adaptive/memoryLimit", m_adaptiveConfig.memoryLimit);

    m_settings->sync();
}

void UnifiedRenderManager::setAdaptiveConfig(const AdaptiveConfig& config)
{
    m_adaptiveConfig = config;
    m_memoryLimit = config.memoryLimit;

    if (m_renderThreadPool) {
        m_renderThreadPool->setMaxThreadCount(config.maxConcurrentRenders);
    }

    if (m_cacheManager) {
        m_cacheManager->setMaxMemoryUsage(m_memoryLimit * 0.7);
    }

    emit adaptiveConfigChanged(config);
}

void UnifiedRenderManager::setMaxConcurrentRenders(int maxRenders)
{
    m_adaptiveConfig.maxConcurrentRenders = maxRenders;
    if (m_renderThreadPool) {
        m_renderThreadPool->setMaxThreadCount(maxRenders);
    }
}

void UnifiedRenderManager::setMemoryLimit(qint64 bytes)
{
    m_memoryLimit = bytes;
    m_adaptiveConfig.memoryLimit = bytes;

    if (m_cacheManager) {
        m_cacheManager->setMaxMemoryUsage(bytes * 0.7);
    }
}

void UnifiedRenderManager::setDefaultQuality(RenderQuality quality)
{
    m_defaultQuality = quality;
}

void UnifiedRenderManager::enableAdaptiveQuality(bool enable)
{
    m_adaptiveConfig.enableAdaptiveQuality = enable;
}

void UnifiedRenderManager::enablePredictivePrerendering(bool enable)
{
    m_adaptiveConfig.enablePredictivePrerendering = enable;
}
