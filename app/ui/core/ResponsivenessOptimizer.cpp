#include "ResponsivenessOptimizer.h"
#include <QApplication>
#include <QDebug>
#include <QMutexLocker>
#include <QDateTime>
#include <QThread>
#include <algorithm>
#include <cmath>

ResponsivenessOptimizer::ResponsivenessOptimizer(QObject* parent)
    : QObject(parent)
    , m_optimizationEnabled(true)
    , m_adaptiveEnabled(true)
    , m_batchMode(false)
    , m_taskProcessor(nullptr)
    , m_batchedTasks(0)
    , m_frameInProgress(false)
    , m_currentFrameStart(0)
    , m_metricsTimer(nullptr)
    , m_adaptiveTimer(nullptr)
    , m_frameRateTimer(nullptr)
    , m_currentQuality(1.0)
    , m_consecutiveSlowFrames(0)
    , m_qualityReduced(false)
    , m_settings(nullptr)
{
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-ResponsivenessOptimizer", this);
    
    // Load configuration
    loadSettings();
    
    // Initialize timers
    initializeTimers();
    
    // Start frame timer
    m_frameTimer.start();
    
    qDebug() << "ResponsivenessOptimizer: Initialized with target FPS:" << m_config.targetFrameRate;
}

ResponsivenessOptimizer::~ResponsivenessOptimizer()
{
    saveSettings();
    clearAllTasks();
}

void ResponsivenessOptimizer::initializeTimers()
{
    // Task processor - processes deferred UI tasks
    m_taskProcessor = new QTimer(this);
    m_taskProcessor->setInterval(5); // 5ms intervals for responsive task processing
    connect(m_taskProcessor, &QTimer::timeout, this, &ResponsivenessOptimizer::processTaskQueue);
    m_taskProcessor->start();
    
    // Metrics timer - updates performance metrics
    m_metricsTimer = new QTimer(this);
    m_metricsTimer->setInterval(1000); // 1 second
    connect(m_metricsTimer, &QTimer::timeout, this, &ResponsivenessOptimizer::updatePerformanceMetrics);
    m_metricsTimer->start();
    
    // Adaptive optimization timer
    m_adaptiveTimer = new QTimer(this);
    m_adaptiveTimer->setInterval(100); // 100ms for quick adaptation
    connect(m_adaptiveTimer, &QTimer::timeout, this, &ResponsivenessOptimizer::performAdaptiveOptimization);
    if (m_adaptiveEnabled) {
        m_adaptiveTimer->start();
    }
    
    // Frame rate monitoring timer
    m_frameRateTimer = new QTimer(this);
    m_frameRateTimer->setInterval(16); // ~60fps monitoring
    connect(m_frameRateTimer, &QTimer::timeout, this, &ResponsivenessOptimizer::onFrameTimer);
    m_frameRateTimer->start();
}

void ResponsivenessOptimizer::scheduleTask(const std::function<void()>& task, 
                                          UITaskPriority priority, const QString& description)
{
    if (!m_optimizationEnabled) {
        // Execute immediately if optimization is disabled
        executeSafeTask(task, description);
        return;
    }
    
    DeferredUITask deferredTask;
    deferredTask.task = task;
    deferredTask.priority = priority;
    deferredTask.timestamp = QDateTime::currentMSecsSinceEpoch();
    deferredTask.description = description;
    deferredTask.isRepeating = false;
    
    // High priority tasks execute immediately
    if (priority >= UITaskPriority::High) {
        executeSafeTask(task, description);
        return;
    }
    
    QMutexLocker locker(&m_taskMutex);
    
    // Check queue size limit
    if (m_taskQueue.size() >= m_config.maxDeferredTasks) {
        emit taskQueueFull();
        
        // Remove lowest priority task to make room
        auto minIt = std::min_element(m_taskQueue.begin(), m_taskQueue.end());
        if (minIt != m_taskQueue.end() && minIt->priority < priority) {
            m_taskQueue.erase(minIt);
        } else {
            qWarning() << "ResponsivenessOptimizer: Task queue full, dropping task:" << description;
            return;
        }
    }
    
    m_taskQueue.enqueue(deferredTask);
    
    // Sort queue by priority if not batching
    if (!m_batchMode) {
        prioritizeTaskQueue();
    }
}

void ResponsivenessOptimizer::scheduleDelayedTask(const std::function<void()>& task,
                                                 int delayMs, UITaskPriority priority)
{
    QTimer::singleShot(delayMs, this, [this, task, priority]() {
        scheduleTask(task, priority, "Delayed Task");
    });
}

void ResponsivenessOptimizer::scheduleRepeatingTask(const std::function<void()>& task,
                                                   int intervalMs, UITaskPriority priority)
{
    DeferredUITask deferredTask;
    deferredTask.task = task;
    deferredTask.priority = priority;
    deferredTask.timestamp = QDateTime::currentMSecsSinceEpoch();
    deferredTask.description = "Repeating Task";
    deferredTask.isRepeating = true;
    deferredTask.interval = intervalMs;
    
    QMutexLocker locker(&m_taskMutex);
    m_taskQueue.enqueue(deferredTask);
}

void ResponsivenessOptimizer::beginFrame()
{
    if (m_frameInProgress) {
        qWarning() << "ResponsivenessOptimizer: Frame already in progress";
        return;
    }
    
    m_currentFrameStart = m_frameTimer.nsecsElapsed() / 1000000; // Convert to milliseconds
    m_frameInProgress = true;
}

void ResponsivenessOptimizer::endFrame()
{
    if (!m_frameInProgress) {
        return;
    }
    
    qint64 frameEnd = m_frameTimer.nsecsElapsed() / 1000000;
    qint64 frameDuration = frameEnd - m_currentFrameStart;
    
    FrameInfo frameInfo;
    frameInfo.startTime = m_currentFrameStart;
    frameInfo.endTime = frameEnd;
    frameInfo.duration = frameDuration;
    frameInfo.wasDropped = frameDuration > m_config.maxFrameTime;
    
    // Add to frame history
    m_frameHistory.append(frameInfo);
    
    // Keep only recent frames
    if (m_frameHistory.size() > m_config.performanceWindow) {
        m_frameHistory.removeFirst();
    }
    
    // Track slow frames for adaptive optimization
    if (frameInfo.wasDropped) {
        m_consecutiveSlowFrames++;
    } else {
        m_consecutiveSlowFrames = 0;
    }
    
    m_frameInProgress = false;
    
    // Update metrics
    QMutexLocker locker(&m_metricsMutex);
    m_metrics.totalRenderTime += frameDuration;
    if (frameInfo.wasDropped) {
        m_metrics.droppedFrames++;
    }
}

void ResponsivenessOptimizer::processTaskQueue()
{
    if (!m_optimizationEnabled || m_taskQueue.isEmpty()) {
        return;
    }
    
    QMutexLocker locker(&m_taskMutex);
    
    // Process high priority tasks first
    processHighPriorityTasks();
    
    // Process batched tasks if in batch mode
    if (m_batchMode) {
        processBatchedTasks();
        return;
    }
    
    // Process one task per cycle to maintain responsiveness
    if (!m_taskQueue.isEmpty()) {
        DeferredUITask task = m_taskQueue.dequeue();
        locker.unlock();
        
        executeSafeTask(task.task, task.description);
        
        // Reschedule repeating tasks
        if (task.isRepeating) {
            QTimer::singleShot(task.interval, this, [this, task]() {
                QMutexLocker locker(&m_taskMutex);
                DeferredUITask newTask = task;
                newTask.timestamp = QDateTime::currentMSecsSinceEpoch();
                m_taskQueue.enqueue(newTask);
            });
        }
    }
}

void ResponsivenessOptimizer::processHighPriorityTasks()
{
    // Process all critical and high priority tasks immediately
    auto it = m_taskQueue.begin();
    while (it != m_taskQueue.end()) {
        if (it->priority >= UITaskPriority::High) {
            DeferredUITask task = *it;
            it = m_taskQueue.erase(it);
            
            // Execute without lock to avoid deadlock
            m_taskMutex.unlock();
            executeSafeTask(task.task, task.description);
            m_taskMutex.lock();
            
            // Restart iteration as queue may have changed
            it = m_taskQueue.begin();
        } else {
            ++it;
        }
    }
}

void ResponsivenessOptimizer::processBatchedTasks()
{
    const int maxBatchSize = 5; // Process up to 5 tasks per batch
    int processed = 0;
    
    while (!m_taskQueue.isEmpty() && processed < maxBatchSize) {
        DeferredUITask task = m_taskQueue.dequeue();
        
        m_taskMutex.unlock();
        executeSafeTask(task.task, task.description);
        m_taskMutex.lock();
        
        processed++;
        m_batchedTasks++;
    }
}

void ResponsivenessOptimizer::executeSafeTask(const std::function<void()>& task, const QString& description)
{
    try {
        QElapsedTimer taskTimer;
        taskTimer.start();
        
        task();
        
        qint64 taskTime = taskTimer.elapsed();
        
        // Update metrics
        QMutexLocker locker(&m_metricsMutex);
        m_metrics.totalUITime += taskTime;
        m_metrics.pendingTasks = m_taskQueue.size();
        
        if (taskTime > 10) { // Log slow tasks
            qDebug() << "ResponsivenessOptimizer: Slow task executed:" << description << "took" << taskTime << "ms";
        }
        
    } catch (const std::exception& e) {
        qWarning() << "ResponsivenessOptimizer: Exception in task" << description << ":" << e.what();
    } catch (...) {
        qWarning() << "ResponsivenessOptimizer: Unknown exception in task" << description;
    }
}

void ResponsivenessOptimizer::updatePerformanceMetrics()
{
    QMutexLocker locker(&m_metricsMutex);
    
    // Calculate frame rate
    updateFrameRate();
    
    // Calculate average frame time
    m_metrics.averageFrameTime = calculateAverageFrameTime();
    
    // Update system resource usage
    m_metrics.cpuUsage = getCurrentCPUUsage();
    m_metrics.memoryUsage = getCurrentMemoryUsage();
    
    // Update pending tasks count
    m_metrics.pendingTasks = m_taskQueue.size();
    
    emit performanceChanged(m_metrics);
}

void ResponsivenessOptimizer::updateFrameRate()
{
    if (m_frameHistory.size() < 2) {
        m_metrics.frameRate = 0.0;
        return;
    }
    
    qint64 totalTime = m_frameHistory.last().endTime - m_frameHistory.first().startTime;
    if (totalTime > 0) {
        m_metrics.frameRate = (m_frameHistory.size() - 1) * 1000.0 / totalTime;
        emit frameRateChanged(m_metrics.frameRate);
    }
}

double ResponsivenessOptimizer::calculateAverageFrameTime() const
{
    if (m_frameHistory.isEmpty()) return 0.0;
    
    qint64 totalDuration = 0;
    for (const FrameInfo& frame : m_frameHistory) {
        totalDuration += frame.duration;
    }
    
    return static_cast<double>(totalDuration) / m_frameHistory.size();
}

void ResponsivenessOptimizer::performAdaptiveOptimization()
{
    if (!m_adaptiveEnabled) return;
    
    analyzePerformance();
    
    if (shouldReduceQuality()) {
        adjustQuality();
    } else if (shouldIncreaseQuality()) {
        adjustQuality();
    }
    
    optimizeTaskScheduling();
}

bool ResponsivenessOptimizer::shouldReduceQuality() const
{
    return m_consecutiveSlowFrames > 3 && 
           m_metrics.frameRate < m_config.targetFrameRate * m_config.qualityReductionThreshold &&
           m_currentQuality > 0.5;
}

bool ResponsivenessOptimizer::shouldIncreaseQuality() const
{
    return m_consecutiveSlowFrames == 0 && 
           m_metrics.frameRate > m_config.targetFrameRate * 0.95 &&
           m_currentQuality < 1.0;
}

void ResponsivenessOptimizer::adjustQuality()
{
    double oldQuality = m_currentQuality;
    
    if (shouldReduceQuality()) {
        m_currentQuality = qMax(0.5, m_currentQuality - 0.1);
        m_qualityReduced = true;
    } else if (shouldIncreaseQuality()) {
        m_currentQuality = qMin(1.0, m_currentQuality + 0.05);
        if (m_currentQuality >= 1.0) {
            m_qualityReduced = false;
        }
    }
    
    if (oldQuality != m_currentQuality) {
        emit qualityReduced(m_currentQuality);
        qDebug() << "ResponsivenessOptimizer: Quality adjusted to" << m_currentQuality;
    }
}

void ResponsivenessOptimizer::loadSettings()
{
    if (!m_settings) return;
    
    m_config.targetFrameRate = m_settings->value("ui/targetFrameRate", 60.0).toDouble();
    m_config.maxFrameTime = m_settings->value("ui/maxFrameTime", 16667).toLongLong();
    m_config.enableAdaptiveQuality = m_settings->value("ui/enableAdaptiveQuality", true).toBool();
    m_config.enableFrameSkipping = m_settings->value("ui/enableFrameSkipping", false).toBool();
    m_config.enableTaskBatching = m_settings->value("ui/enableTaskBatching", true).toBool();
    m_config.qualityReductionThreshold = m_settings->value("ui/qualityReductionThreshold", 0.8).toDouble();
    
    m_optimizationEnabled = m_settings->value("ui/optimizationEnabled", true).toBool();
    m_adaptiveEnabled = m_settings->value("ui/adaptiveEnabled", true).toBool();
}

void ResponsivenessOptimizer::saveSettings()
{
    if (!m_settings) return;
    
    m_settings->setValue("ui/targetFrameRate", m_config.targetFrameRate);
    m_settings->setValue("ui/maxFrameTime", m_config.maxFrameTime);
    m_settings->setValue("ui/enableAdaptiveQuality", m_config.enableAdaptiveQuality);
    m_settings->setValue("ui/enableFrameSkipping", m_config.enableFrameSkipping);
    m_settings->setValue("ui/enableTaskBatching", m_config.enableTaskBatching);
    m_settings->setValue("ui/qualityReductionThreshold", m_config.qualityReductionThreshold);
    
    m_settings->setValue("ui/optimizationEnabled", m_optimizationEnabled);
    m_settings->setValue("ui/adaptiveEnabled", m_adaptiveEnabled);
    
    m_settings->sync();
}
