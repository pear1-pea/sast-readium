#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QQueue>
#include <QMutex>
#include <QThread>
#include <QSystemSemaphore>

class ThumbnailModel;
class ThumbnailGenerator;
class ThumbnailListView;

/**
 * @brief 缩略图性能优化器
 * 
 * 特性：
 * - 自适应性能调优
 * - 内存使用监控和优化
 * - 渲染质量动态调整
 * - 并发控制优化
 * - 系统资源监控
 */
class ThumbnailPerformanceOptimizer : public QObject
{
    Q_OBJECT

public:
    enum PerformanceLevel {
        Low,        // 低性能模式：最小内存使用，基础质量
        Medium,     // 中等性能模式：平衡内存和质量
        High,       // 高性能模式：优先质量和流畅度
        Adaptive    // 自适应模式：根据系统状态动态调整
    };

    struct PerformanceMetrics {
        qint64 averageRenderTime = 0;
        qint64 memoryUsage = 0;
        double cacheHitRate = 0.0;
        int concurrentJobs = 0;
        int queueLength = 0;
        double cpuUsage = 0.0;
        qint64 availableMemory = 0;
        
        PerformanceMetrics() = default;
    };

    struct OptimizationSettings {
        int maxConcurrentJobs = 3;
        int maxCacheSize = 100;
        qint64 maxMemoryUsage = 256 * 1024 * 1024; // 256MB
        double thumbnailQuality = 1.0;
        int preloadRange = 5;
        bool adaptiveQuality = true;
        bool memoryPressureHandling = true;
        
        OptimizationSettings() = default;
    };

    explicit ThumbnailPerformanceOptimizer(QObject* parent = nullptr);
    ~ThumbnailPerformanceOptimizer() override;

    // 组件设置
    void setThumbnailModel(ThumbnailModel* model);
    void setThumbnailGenerator(ThumbnailGenerator* generator);
    void setThumbnailView(ThumbnailListView* view);
    
    // 性能级别
    void setPerformanceLevel(PerformanceLevel level);
    PerformanceLevel performanceLevel() const { return m_performanceLevel; }
    
    // 优化设置
    void setOptimizationSettings(const OptimizationSettings& settings);
    OptimizationSettings optimizationSettings() const { return m_settings; }
    
    // 监控控制
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return m_monitoring; }
    
    void setMonitoringInterval(int interval);
    int monitoringInterval() const { return m_monitoringInterval; }
    
    // 性能数据
    PerformanceMetrics currentMetrics() const;
    PerformanceMetrics averageMetrics() const;
    
    // 手动优化
    void optimizeNow();
    void clearPerformanceHistory();
    
    // 内存管理
    void handleMemoryPressure();
    void optimizeMemoryUsage();
    qint64 getSystemAvailableMemory() const;
    
    // 质量调整
    void adjustQualityBasedOnPerformance();
    void setAdaptiveQualityEnabled(bool enabled);
    bool adaptiveQualityEnabled() const { return m_adaptiveQuality; }

signals:
    void performanceMetricsUpdated(const PerformanceMetrics& metrics);
    void optimizationApplied(const OptimizationSettings& settings);
    void memoryPressureDetected(qint64 usage, qint64 available);
    void performanceWarning(const QString& message);
    void qualityAdjusted(double newQuality, const QString& reason);

private slots:
    void onMonitoringTimer();
    void onOptimizationTimer();
    void onMemoryCheckTimer();
    void onPerformanceAnalysis();

private:
    void initializeOptimizer();
    void setupTimers();
    void connectSignals();
    
    // 性能监控
    void collectMetrics();
    void analyzePerformance();
    void updatePerformanceHistory();
    
    // 优化策略
    void applyOptimizations();
    void optimizeConcurrency();
    void optimizeCacheSize();
    void optimizeQuality();
    void optimizePreloading();
    
    // 自适应调整
    void adaptToSystemConditions();
    void adaptToUsagePatterns();
    void adaptToDocumentCharacteristics();
    
    // 内存优化
    void monitorMemoryUsage();
    void handleLowMemory();
    void balanceMemoryDistribution();
    
    // 系统监控
    double getCurrentCPUUsage() const;
    qint64 getCurrentMemoryUsage() const;
    bool isSystemUnderPressure() const;
    
    // 性能分析
    void analyzeRenderingPerformance();
    void analyzeCacheEfficiency();
    void analyzeUserBehavior();
    
    // 配置应用
    void applyPerformanceLevel();
    void applyOptimizationSettings();
    
    // 日志和调试
    void logOptimization(const QString& action, const QString& reason);
    void logPerformanceWarning(const QString& warning);

private:
    // 核心组件
    ThumbnailModel* m_thumbnailModel;
    ThumbnailGenerator* m_thumbnailGenerator;
    ThumbnailListView* m_thumbnailView;
    PDFPrerenderer* m_prerenderer;
    std::shared_ptr<Poppler::Document> m_document;
    
    // 性能设置
    PerformanceLevel m_performanceLevel;
    OptimizationSettings m_settings;
    bool m_adaptiveQuality;
    
    // 监控状态
    bool m_monitoring;
    int m_monitoringInterval;
    
    // 定时器
    QTimer* m_monitoringTimer;
    QTimer* m_optimizationTimer;
    QTimer* m_memoryCheckTimer;
    QTimer* m_analysisTimer;
    
    // 性能数据
    PerformanceMetrics m_currentMetrics;
    QQueue<PerformanceMetrics> m_metricsHistory;
    mutable QMutex m_metricsMutex;
    
    // 优化历史
    QQueue<OptimizationSettings> m_settingsHistory;
    QElapsedTimer m_performanceTimer;
    
    // 系统监控
    qint64 m_systemTotalMemory;
    qint64 m_processBaseMemory;
    QElapsedTimer m_systemTimer;
    
    // 统计计数器
    int m_optimizationCount;
    int m_memoryWarningCount;
    int m_qualityAdjustmentCount;
    
    // 常量
    static constexpr int DEFAULT_MONITORING_INTERVAL = 2000; // ms
    static constexpr int DEFAULT_OPTIMIZATION_INTERVAL = 10000; // ms
    static constexpr int DEFAULT_MEMORY_CHECK_INTERVAL = 5000; // ms
    static constexpr int DEFAULT_ANALYSIS_INTERVAL = 30000; // ms
    static constexpr int MAX_METRICS_HISTORY = 50;
    static constexpr int MAX_SETTINGS_HISTORY = 20;
    static constexpr double LOW_MEMORY_THRESHOLD = 0.1; // 10% 可用内存
    static constexpr double HIGH_CPU_THRESHOLD = 0.8; // 80% CPU使用率
    static constexpr double CACHE_HIT_RATE_THRESHOLD = 0.7; // 70% 缓存命中率
    static constexpr qint64 MEMORY_WARNING_THRESHOLD = 1024 * 1024 * 1024; // 1GB
};
