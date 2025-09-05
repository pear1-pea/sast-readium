#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QElapsedTimer>
#include <QQueue>
#include <QHash>
#include <QThread>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QSettings>
#include <functional>

/**
 * Task priority levels for UI operations
 */
enum class UITaskPriority {
    Critical = 10,      // Immediate user interaction
    High = 8,           // Important UI updates
    Normal = 5,         // Standard operations
    Low = 2,            // Background UI tasks
    Deferred = 0        // Can be delayed
};

/**
 * UI performance metrics
 */
struct UIPerformanceMetrics {
    double averageFrameTime;
    double frameRate;
    int droppedFrames;
    qint64 totalRenderTime;
    qint64 totalUITime;
    int pendingTasks;
    double cpuUsage;
    double memoryUsage;
    
    UIPerformanceMetrics()
        : averageFrameTime(0.0), frameRate(0.0), droppedFrames(0)
        , totalRenderTime(0), totalUITime(0), pendingTasks(0)
        , cpuUsage(0.0), memoryUsage(0.0) {}
};

/**
 * Deferred UI task
 */
struct DeferredUITask {
    std::function<void()> task;
    UITaskPriority priority;
    qint64 timestamp;
    qint64 deadline;
    QString description;
    bool isRepeating;
    int interval;
    
    DeferredUITask()
        : priority(UITaskPriority::Normal), timestamp(0), deadline(0)
        , isRepeating(false), interval(0) {}
    
    bool operator<(const DeferredUITask& other) const {
        return priority < other.priority; // Higher priority first
    }
};

/**
 * Frame timing information
 */
struct FrameInfo {
    qint64 startTime;
    qint64 endTime;
    qint64 duration;
    bool wasDropped;
    
    FrameInfo() : startTime(0), endTime(0), duration(0), wasDropped(false) {}
};

/**
 * Adaptive performance configuration
 */
struct AdaptivePerformanceConfig {
    double targetFrameRate;
    qint64 maxFrameTime;
    int maxDeferredTasks;
    bool enableAdaptiveQuality;
    bool enableFrameSkipping;
    bool enableTaskBatching;
    double qualityReductionThreshold;
    int performanceWindow;
    
    AdaptivePerformanceConfig()
        : targetFrameRate(60.0)
        , maxFrameTime(16667) // 16.67ms for 60fps
        , maxDeferredTasks(100)
        , enableAdaptiveQuality(true)
        , enableFrameSkipping(false)
        , enableTaskBatching(true)
        , qualityReductionThreshold(0.8)
        , performanceWindow(30) {}
};

/**
 * Responsiveness optimizer for UI performance
 */
class ResponsivenessOptimizer : public QObject
{
    Q_OBJECT

public:
    explicit ResponsivenessOptimizer(QObject* parent = nullptr);
    ~ResponsivenessOptimizer();

    // Task scheduling
    void scheduleTask(const std::function<void()>& task, 
                     UITaskPriority priority = UITaskPriority::Normal,
                     const QString& description = QString());
    
    void scheduleDelayedTask(const std::function<void()>& task,
                           int delayMs,
                           UITaskPriority priority = UITaskPriority::Normal);
    
    void scheduleRepeatingTask(const std::function<void()>& task,
                             int intervalMs,
                             UITaskPriority priority = UITaskPriority::Normal);
    
    void cancelTasks(const QString& description);
    void clearAllTasks();
    
    // Frame management
    void beginFrame();
    void endFrame();
    void markFrameDropped();
    
    // Performance monitoring
    UIPerformanceMetrics getMetrics() const;
    double getCurrentFrameRate() const;
    bool isPerformanceGood() const;
    void resetMetrics();
    
    // Adaptive optimization
    void enableAdaptiveOptimization(bool enable);
    void setTargetFrameRate(double fps);
    void setQualityReductionThreshold(double threshold);
    
    // Configuration
    void setConfig(const AdaptivePerformanceConfig& config);
    AdaptivePerformanceConfig getConfig() const { return m_config; }
    
    // UI thread optimization
    void optimizeUIThread();
    void reduceUIComplexity();
    void enableFrameSkipping(bool enable);
    
    // Batch operations
    void beginBatch();
    void endBatch();
    bool isBatching() const { return m_batchMode; }
    
    // Settings
    void loadSettings();
    void saveSettings();

public slots:
    void onApplicationStateChanged(Qt::ApplicationState state);
    void onSystemPerformanceChanged();
    void pauseOptimization();
    void resumeOptimization();

signals:
    void performanceChanged(const UIPerformanceMetrics& metrics);
    void frameRateChanged(double fps);
    void qualityReduced(double newQuality);
    void optimizationStateChanged(bool enabled);
    void taskQueueFull();

private slots:
    void processTaskQueue();
    void updatePerformanceMetrics();
    void performAdaptiveOptimization();
    void onFrameTimer();

private:
    // Configuration
    AdaptivePerformanceConfig m_config;
    bool m_optimizationEnabled;
    bool m_adaptiveEnabled;
    bool m_batchMode;
    
    // Task management
    QQueue<DeferredUITask> m_taskQueue;
    QMutex m_taskMutex;
    QTimer* m_taskProcessor;
    int m_batchedTasks;
    
    // Frame timing
    QElapsedTimer m_frameTimer;
    QList<FrameInfo> m_frameHistory;
    qint64 m_currentFrameStart;
    bool m_frameInProgress;
    
    // Performance tracking
    mutable QMutex m_metricsMutex;
    UIPerformanceMetrics m_metrics;
    QTimer* m_metricsTimer;
    QTimer* m_adaptiveTimer;
    QTimer* m_frameRateTimer;
    
    // Adaptive optimization
    double m_currentQuality;
    int m_consecutiveSlowFrames;
    bool m_qualityReduced;
    
    // Settings
    QSettings* m_settings;
    
    // Helper methods
    void initializeTimers();
    void processHighPriorityTasks();
    void processBatchedTasks();
    void updateFrameRate();
    void analyzePerformance();
    void adjustQuality();
    void optimizeTaskScheduling();
    
    // Performance analysis
    bool isFrameRateStable() const;
    double calculateAverageFrameTime() const;
    int countDroppedFrames() const;
    bool shouldReduceQuality() const;
    bool shouldIncreaseQuality() const;
    
    // Task utilities
    void executeSafeTask(const std::function<void()>& task, const QString& description);
    bool hasHighPriorityTasks() const;
    void prioritizeTaskQueue();
    
    // System monitoring
    void monitorSystemResources();
    double getCurrentCPUUsage() const;
    double getCurrentMemoryUsage() const;
};
