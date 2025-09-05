#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QThread>
#include <QMap>
#include <QQueue>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QStandardPaths>

/**
 * Performance metrics structure
 */
struct PerformanceMetrics {
    // Rendering metrics
    qint64 renderTime;          // Time to render a page (ms)
    qint64 cacheHitTime;        // Time for cache hit (ms)
    qint64 cacheMissTime;       // Time for cache miss (ms)
    double renderFPS;           // Rendering frames per second
    
    // Memory metrics
    qint64 memoryUsage;         // Current memory usage (bytes)
    qint64 cacheMemoryUsage;    // Cache memory usage (bytes)
    int cacheHitRate;           // Cache hit rate (percentage)
    int activeCacheItems;       // Number of items in cache
    
    // I/O metrics
    qint64 fileLoadTime;        // Time to load document (ms)
    qint64 pageLoadTime;        // Time to load page data (ms)
    qint64 thumbnailGenTime;    // Time to generate thumbnail (ms)
    
    // User interaction metrics
    qint64 scrollResponseTime;  // Scroll response time (ms)
    qint64 zoomResponseTime;    // Zoom response time (ms)
    qint64 searchTime;          // Search operation time (ms)
    
    // System metrics
    double cpuUsage;            // CPU usage percentage
    qint64 timestamp;           // Timestamp of measurement
    
    PerformanceMetrics() 
        : renderTime(0), cacheHitTime(0), cacheMissTime(0), renderFPS(0.0)
        , memoryUsage(0), cacheMemoryUsage(0), cacheHitRate(0), activeCacheItems(0)
        , fileLoadTime(0), pageLoadTime(0), thumbnailGenTime(0)
        , scrollResponseTime(0), zoomResponseTime(0), searchTime(0)
        , cpuUsage(0.0), timestamp(QDateTime::currentMSecsSinceEpoch())
    {}
};

/**
 * Performance optimization recommendations
 */
enum class OptimizationRecommendation {
    None,
    IncreaseCacheSize,
    DecreaseCacheSize,
    EnablePrerendering,
    DisablePrerendering,
    ReduceRenderQuality,
    IncreaseRenderQuality,
    OptimizeMemoryUsage,
    EnableAsyncLoading,
    ReduceAnimations
};

/**
 * Monitors and analyzes application performance
 */
class PerformanceMonitor : public QObject {
    Q_OBJECT

public:
    static PerformanceMonitor& instance();
    ~PerformanceMonitor() = default;

    // Monitoring control
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return m_isMonitoring; }
    
    // Metrics recording
    void recordRenderTime(int pageNumber, qint64 timeMs);
    void recordCacheHit(qint64 timeMs);
    void recordCacheMiss(qint64 timeMs);
    void recordMemoryUsage(qint64 bytes);
    void recordCacheStats(int hitRate, int activeItems, qint64 memoryUsage);
    void recordFileLoadTime(qint64 timeMs);
    void recordPageLoadTime(int pageNumber, qint64 timeMs);
    void recordThumbnailGenTime(qint64 timeMs);
    void recordScrollResponse(qint64 timeMs);
    void recordZoomResponse(qint64 timeMs);
    void recordSearchTime(qint64 timeMs);
    
    // Performance analysis
    PerformanceMetrics getCurrentMetrics() const;
    PerformanceMetrics getAverageMetrics(int periodMinutes = 5) const;
    QList<PerformanceMetrics> getMetricsHistory(int count = 100) const;
    
    // Optimization recommendations
    QList<OptimizationRecommendation> getRecommendations() const;
    QString getRecommendationText(OptimizationRecommendation recommendation) const;
    
    // Performance thresholds
    void setRenderTimeThreshold(qint64 ms) { m_renderTimeThreshold = ms; }
    void setMemoryUsageThreshold(qint64 bytes) { m_memoryThreshold = bytes; }
    void setCacheHitRateThreshold(int percentage) { m_cacheHitThreshold = percentage; }
    
    // Reporting
    QString generatePerformanceReport() const;
    bool exportMetricsToFile(const QString& filePath) const;
    void clearMetricsHistory();
    
    // Real-time monitoring
    void enableRealTimeMonitoring(bool enabled);
    bool isRealTimeMonitoringEnabled() const { return m_realTimeEnabled; }

signals:
    void metricsUpdated(const PerformanceMetrics& metrics);
    void performanceWarning(const QString& warning);
    void optimizationRecommended(OptimizationRecommendation recommendation);
    void thresholdExceeded(const QString& metric, qint64 value, qint64 threshold);

private slots:
    void updateSystemMetrics();
    void analyzePerformance();

private:
    explicit PerformanceMonitor(QObject* parent = nullptr);
    Q_DISABLE_COPY(PerformanceMonitor)
    
    void initializeThresholds();
    void checkThresholds(const PerformanceMetrics& metrics);
    double calculateCPUUsage();
    qint64 getCurrentMemoryUsage();
    void pruneOldMetrics();
    
    // Monitoring state
    bool m_isMonitoring;
    bool m_realTimeEnabled;
    QTimer* m_updateTimer;
    QTimer* m_analysisTimer;
    
    // Metrics storage
    mutable QMutex m_metricsMutex;
    QQueue<PerformanceMetrics> m_metricsHistory;
    PerformanceMetrics m_currentMetrics;
    int m_maxHistorySize;
    
    // Performance thresholds
    qint64 m_renderTimeThreshold;
    qint64 m_memoryThreshold;
    int m_cacheHitThreshold;
    qint64 m_responseTimeThreshold;
    
    // Analysis data
    QMap<int, QList<qint64>> m_pageRenderTimes;
    QList<qint64> m_recentRenderTimes;
    QList<qint64> m_recentCacheHits;
    QList<qint64> m_recentCacheMisses;
    
    // System monitoring
    QElapsedTimer m_cpuTimer;
    qint64 m_lastCpuTime;
    
    static PerformanceMonitor* s_instance;
};
