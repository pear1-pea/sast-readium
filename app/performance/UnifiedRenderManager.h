#pragma once

#include <QObject>
#include <QPixmap>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QThreadPool>
#include <QQueue>
#include <QHash>
#include <QElapsedTimer>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QList>
#include <QtGlobal>
#include <poppler-qt6.h>

// Forward declarations
class PDFCacheManager;
class PDFPrerenderer;
class PerformanceMonitor;

/**
 * Render quality levels
 */
enum class RenderQuality {
    Draft,      // Fast rendering for previews
    Normal,     // Standard quality
    High,       // High quality for final display
    Print       // Maximum quality for printing
};

/**
 * Render priority levels
 */
enum class RenderPriority {
    Background = 0,  // Background prerendering
    Low = 1,         // Non-critical renders
    Normal = 5,      // Standard priority
    High = 8,        // User-initiated actions
    Critical = 10    // Immediate display needs
};

/**
 * Render request structure
 */
struct RenderRequest {
    int pageNumber;
    double scaleFactor;
    int rotation;
    RenderQuality quality;
    RenderPriority priority;
    QSize targetSize;
    QString requestId;
    qint64 timestamp;
    
    RenderRequest() 
        : pageNumber(-1), scaleFactor(1.0), rotation(0)
        , quality(RenderQuality::Normal), priority(RenderPriority::Normal)
        , timestamp(0) {}
    
    bool operator<(const RenderRequest& other) const {
        return priority < other.priority;
    }
    
    QString getCacheKey() const;
    bool isValid() const;
};

/**
 * Render result structure
 */
struct RenderResult {
    QString requestId;
    int pageNumber;
    QPixmap pixmap;
    RenderQuality quality;
    qint64 renderTime;
    bool success;
    QString errorMessage;
    
    RenderResult() 
        : pageNumber(-1), renderTime(0), success(false) {}
};

/**
 * Render statistics
 */
struct RenderStatistics {
    int totalRequests;
    int completedRequests;
    int failedRequests;
    int cacheHits;
    int cacheMisses;
    double averageRenderTime;
    double cacheHitRate;
    qint64 totalMemoryUsed;
    
    RenderStatistics() 
        : totalRequests(0), completedRequests(0), failedRequests(0)
        , cacheHits(0), cacheMisses(0), averageRenderTime(0.0)
        , cacheHitRate(0.0), totalMemoryUsed(0) {}
};

/**
 * Adaptive rendering configuration
 */
struct AdaptiveConfig {
    bool enableAdaptiveQuality;
    bool enablePredictivePrerendering;
    bool enableMemoryOptimization;
    double qualityThreshold;
    int maxConcurrentRenders;
    qint64 memoryLimit;
    
    AdaptiveConfig()
        : enableAdaptiveQuality(true)
        , enablePredictivePrerendering(true)
        , enableMemoryOptimization(true)
        , qualityThreshold(0.8)
        , maxConcurrentRenders(4)
        , memoryLimit(512 * 1024 * 1024) {} // 512MB
};

/**
 * Unified render manager that coordinates all PDF rendering operations
 * Integrates caching, prerendering, and performance optimization
 */
class UnifiedRenderManager : public QObject
{
    Q_OBJECT

public:
    explicit UnifiedRenderManager(QObject* parent = nullptr);
    ~UnifiedRenderManager();

    // Document management
    void setDocument(Poppler::Document* document);
    Poppler::Document* document() const { return m_document; }
    
    // Configuration
    void setAdaptiveConfig(const AdaptiveConfig& config);
    AdaptiveConfig getAdaptiveConfig() const { return m_adaptiveConfig; }
    
    void setMaxConcurrentRenders(int maxRenders);
    void setMemoryLimit(qint64 bytes);
    void setDefaultQuality(RenderQuality quality);
    
    // Render requests
    QString requestRender(int pageNumber, double scaleFactor = 1.0, 
                         int rotation = 0, RenderQuality quality = RenderQuality::Normal,
                         RenderPriority priority = RenderPriority::Normal,
                         const QSize& targetSize = QSize());
    
    void cancelRequest(const QString& requestId);
    void cancelAllRequests();
    
    // Immediate rendering (synchronous)
    QPixmap renderPageImmediate(int pageNumber, double scaleFactor = 1.0,
                               int rotation = 0, RenderQuality quality = RenderQuality::Normal);
    
    // Cache access
    QPixmap getCachedPage(int pageNumber, double scaleFactor = 1.0, int rotation = 0);
    bool hasPageInCache(int pageNumber, double scaleFactor = 1.0, int rotation = 0);
    void preloadPages(const QList<int>& pageNumbers, double scaleFactor = 1.0);
    
    // Performance and statistics
    RenderStatistics getStatistics() const;
    void resetStatistics();
    bool isRenderingActive() const;
    int getQueueSize() const;
    
    // Adaptive features
    void enableAdaptiveQuality(bool enable);
    void enablePredictivePrerendering(bool enable);
    void recordPageView(int pageNumber, qint64 viewDuration);
    void recordNavigation(int fromPage, int toPage);
    
    // Memory management
    void optimizeMemoryUsage();
    void clearCache();
    qint64 getCurrentMemoryUsage() const;
    
    // Settings persistence
    void loadSettings();
    void saveSettings();

public slots:
    void pauseRendering();
    void resumeRendering();
    void setRenderingEnabled(bool enabled);

signals:
    void renderCompleted(const RenderResult& result);
    void renderFailed(const QString& requestId, const QString& error);
    void renderProgress(const QString& requestId, int percentage);
    void cacheUpdated();
    void memoryUsageChanged(qint64 bytes);
    void statisticsUpdated(const RenderStatistics& stats);
    void adaptiveConfigChanged(const AdaptiveConfig& config);

private slots:
    void processRenderQueue();
    void onRenderWorkerFinished();
    void onMemoryPressure();
    void onAdaptiveAnalysis();
    void onRenderTaskCompleted(const RenderResult& result);

private:
    // Core components
    Poppler::Document* m_document;
    PDFCacheManager* m_cacheManager;
    PerformanceMonitor* m_performanceMonitor;
    QThreadPool* m_renderThreadPool;
    
    // Configuration
    AdaptiveConfig m_adaptiveConfig;
    RenderQuality m_defaultQuality;
    bool m_renderingEnabled;
    bool m_isPaused;
    
    // Request management
    QQueue<RenderRequest> m_renderQueue;
    QHash<QString, RenderRequest> m_activeRequests;
    QMutex m_queueMutex;
    QWaitCondition m_queueCondition;
    QTimer* m_queueProcessor;
    
    // Statistics and monitoring
    mutable QMutex m_statsMutex;
    RenderStatistics m_statistics;
    QElapsedTimer m_performanceTimer;
    
    // Adaptive learning
    QHash<int, QList<qint64>> m_pageViewTimes;
    QHash<int, QHash<int, int>> m_navigationPatterns;
    QTimer* m_adaptiveTimer;
    QList<int> m_recentPages;
    
    // Memory management
    QTimer* m_memoryMonitor;
    qint64 m_memoryLimit;
    
    // Settings
    QSettings* m_settings;
    
    // Helper methods
    void initializeComponents();
    void setupTimers();
    void processNextRequest();
    RenderQuality determineOptimalQuality(int pageNumber, double scaleFactor);
    QList<int> predictNextPages(int currentPage, int count = 3);
    void schedulePrerendering(const QList<int>& pages, double scaleFactor);
    void updateStatistics(const RenderResult& result);
    void enforceMemoryLimit();
    QString generateRequestId() const;
    double calculateDPI(double scaleFactor, RenderQuality quality) const;
    void configureDocument(Poppler::Document* doc, RenderQuality quality) const;
};
