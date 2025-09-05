#include "PerformanceMonitor.h"
#include <QDebug>
#include <QRandomGenerator>
#include <QObject>
#include <QTimer>
#include <QApplication>
#include <QProcess>
#include <QDir>
#include <QMutexLocker>
#include <algorithm>
#include <numeric>
#include "utils/LoggingMacros.h"

PerformanceMonitor* PerformanceMonitor::s_instance = nullptr;

PerformanceMonitor& PerformanceMonitor::instance() {
    if (!s_instance) {
        s_instance = new PerformanceMonitor(qApp);
    }
    return *s_instance;
}

PerformanceMonitor::PerformanceMonitor(QObject* parent)
    : QObject(parent)
    , m_isMonitoring(false)
    , m_realTimeEnabled(false)
    , m_updateTimer(new QTimer(this))
    , m_analysisTimer(new QTimer(this))
    , m_maxHistorySize(1000)
    , m_lastCpuTime(0)
{
    initializeThresholds();
    
    // Setup timers
    m_updateTimer->setInterval(1000); // Update every second
    connect(m_updateTimer, &QTimer::timeout, this, &PerformanceMonitor::updateSystemMetrics);
    
    m_analysisTimer->setInterval(5000); // Analyze every 5 seconds
    connect(m_analysisTimer, &QTimer::timeout, this, &PerformanceMonitor::analyzePerformance);
    
    m_cpuTimer.start();
}

void PerformanceMonitor::initializeThresholds() {
    m_renderTimeThreshold = 100;      // 100ms render time threshold
    m_memoryThreshold = 512 * 1024 * 1024; // 512MB memory threshold
    m_cacheHitThreshold = 80;         // 80% cache hit rate threshold
    m_responseTimeThreshold = 50;     // 50ms response time threshold
}

void PerformanceMonitor::startMonitoring() {
    if (!m_isMonitoring) {
        m_isMonitoring = true;
        m_updateTimer->start();
        m_analysisTimer->start();

        LOG_INFO("Performance monitoring started");
    }
}

void PerformanceMonitor::stopMonitoring() {
    if (m_isMonitoring) {
        m_isMonitoring = false;
        m_updateTimer->stop();
        m_analysisTimer->stop();

        LOG_INFO("Performance monitoring stopped");
    }
}

void PerformanceMonitor::recordRenderTime(int pageNumber, qint64 timeMs) {
    QMutexLocker locker(&m_metricsMutex);
    
    m_currentMetrics.renderTime = timeMs;
    m_currentMetrics.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    // Store page-specific render times
    m_pageRenderTimes[pageNumber].append(timeMs);
    if (m_pageRenderTimes[pageNumber].size() > 10) {
        m_pageRenderTimes[pageNumber].removeFirst();
    }
    
    // Store recent render times for analysis
    m_recentRenderTimes.append(timeMs);
    if (m_recentRenderTimes.size() > 50) {
        m_recentRenderTimes.removeFirst();
    }
    
    // Calculate FPS
    if (timeMs > 0) {
        m_currentMetrics.renderFPS = 1000.0 / timeMs;
    }
    
    if (m_realTimeEnabled) {
        emit metricsUpdated(m_currentMetrics);
    }
    
    // Check threshold
    if (timeMs > m_renderTimeThreshold) {
        emit thresholdExceeded("Render Time", timeMs, m_renderTimeThreshold);
    }
}

void PerformanceMonitor::recordCacheHit(qint64 timeMs) {
    QMutexLocker locker(&m_metricsMutex);
    
    m_currentMetrics.cacheHitTime = timeMs;
    m_recentCacheHits.append(timeMs);
    if (m_recentCacheHits.size() > 100) {
        m_recentCacheHits.removeFirst();
    }
}

void PerformanceMonitor::recordCacheMiss(qint64 timeMs) {
    QMutexLocker locker(&m_metricsMutex);
    
    m_currentMetrics.cacheMissTime = timeMs;
    m_recentCacheMisses.append(timeMs);
    if (m_recentCacheMisses.size() > 100) {
        m_recentCacheMisses.removeFirst();
    }
}

void PerformanceMonitor::recordMemoryUsage(qint64 bytes) {
    QMutexLocker locker(&m_metricsMutex);
    
    m_currentMetrics.memoryUsage = bytes;
    
    if (bytes > m_memoryThreshold) {
        emit thresholdExceeded("Memory Usage", bytes, m_memoryThreshold);
    }
}

void PerformanceMonitor::recordCacheStats(int hitRate, int activeItems, qint64 memoryUsage) {
    QMutexLocker locker(&m_metricsMutex);
    
    m_currentMetrics.cacheHitRate = hitRate;
    m_currentMetrics.activeCacheItems = activeItems;
    m_currentMetrics.cacheMemoryUsage = memoryUsage;
    
    if (hitRate < m_cacheHitThreshold) {
        emit performanceWarning(QString("Cache hit rate is low: %1%").arg(hitRate));
    }
}

void PerformanceMonitor::recordFileLoadTime(qint64 timeMs) {
    QMutexLocker locker(&m_metricsMutex);
    m_currentMetrics.fileLoadTime = timeMs;
}

void PerformanceMonitor::recordPageLoadTime(int pageNumber, qint64 timeMs) {
    Q_UNUSED(pageNumber)
    QMutexLocker locker(&m_metricsMutex);
    m_currentMetrics.pageLoadTime = timeMs;
}

void PerformanceMonitor::recordThumbnailGenTime(qint64 timeMs) {
    QMutexLocker locker(&m_metricsMutex);
    m_currentMetrics.thumbnailGenTime = timeMs;
}

void PerformanceMonitor::recordScrollResponse(qint64 timeMs) {
    QMutexLocker locker(&m_metricsMutex);
    m_currentMetrics.scrollResponseTime = timeMs;
    
    if (timeMs > m_responseTimeThreshold) {
        emit performanceWarning(QString("Slow scroll response: %1ms").arg(timeMs));
    }
}

void PerformanceMonitor::recordZoomResponse(qint64 timeMs) {
    QMutexLocker locker(&m_metricsMutex);
    m_currentMetrics.zoomResponseTime = timeMs;
    
    if (timeMs > m_responseTimeThreshold) {
        emit performanceWarning(QString("Slow zoom response: %1ms").arg(timeMs));
    }
}

void PerformanceMonitor::recordSearchTime(qint64 timeMs) {
    QMutexLocker locker(&m_metricsMutex);
    m_currentMetrics.searchTime = timeMs;
}

PerformanceMetrics PerformanceMonitor::getCurrentMetrics() const {
    QMutexLocker locker(&m_metricsMutex);
    return m_currentMetrics;
}

PerformanceMetrics PerformanceMonitor::getAverageMetrics(int periodMinutes) const {
    QMutexLocker locker(&m_metricsMutex);
    
    PerformanceMetrics average;
    qint64 cutoffTime = QDateTime::currentMSecsSinceEpoch() - (periodMinutes * 60 * 1000);
    
    QList<PerformanceMetrics> recentMetrics;
    for (const PerformanceMetrics& metrics : m_metricsHistory) {
        if (metrics.timestamp >= cutoffTime) {
            recentMetrics.append(metrics);
        }
    }
    
    if (recentMetrics.isEmpty()) {
        return average;
    }
    
    // Calculate averages
    qint64 totalRenderTime = 0, totalMemory = 0, totalCacheMemory = 0;
    int totalHitRate = 0, totalCacheItems = 0;
    double totalFPS = 0.0, totalCPU = 0.0;
    
    for (const PerformanceMetrics& metrics : recentMetrics) {
        totalRenderTime += metrics.renderTime;
        totalMemory += metrics.memoryUsage;
        totalCacheMemory += metrics.cacheMemoryUsage;
        totalHitRate += metrics.cacheHitRate;
        totalCacheItems += metrics.activeCacheItems;
        totalFPS += metrics.renderFPS;
        totalCPU += metrics.cpuUsage;
    }
    
    int count = recentMetrics.size();
    average.renderTime = totalRenderTime / count;
    average.memoryUsage = totalMemory / count;
    average.cacheMemoryUsage = totalCacheMemory / count;
    average.cacheHitRate = totalHitRate / count;
    average.activeCacheItems = totalCacheItems / count;
    average.renderFPS = totalFPS / count;
    average.cpuUsage = totalCPU / count;
    average.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    return average;
}

QList<PerformanceMetrics> PerformanceMonitor::getMetricsHistory(int count) const {
    QMutexLocker locker(&m_metricsMutex);
    
    QList<PerformanceMetrics> result;
    int startIndex = qMax(0, m_metricsHistory.size() - count);
    
    for (int i = startIndex; i < m_metricsHistory.size(); ++i) {
        result.append(m_metricsHistory.at(i));
    }
    
    return result;
}

QList<OptimizationRecommendation> PerformanceMonitor::getRecommendations() const {
    QList<OptimizationRecommendation> recommendations;
    
    PerformanceMetrics current = getCurrentMetrics();
    PerformanceMetrics average = getAverageMetrics(5);
    
    // Analyze render performance
    if (average.renderTime > m_renderTimeThreshold) {
        if (average.cacheHitRate < 70) {
            recommendations.append(OptimizationRecommendation::IncreaseCacheSize);
        }
        if (average.renderFPS < 30) {
            recommendations.append(OptimizationRecommendation::ReduceRenderQuality);
        }
        recommendations.append(OptimizationRecommendation::EnablePrerendering);
    }
    
    // Analyze memory usage
    if (average.memoryUsage > m_memoryThreshold) {
        recommendations.append(OptimizationRecommendation::OptimizeMemoryUsage);
        if (average.cacheMemoryUsage > average.memoryUsage * 0.5) {
            recommendations.append(OptimizationRecommendation::DecreaseCacheSize);
        }
    }
    
    // Analyze response times
    if (average.scrollResponseTime > m_responseTimeThreshold ||
        average.zoomResponseTime > m_responseTimeThreshold) {
        recommendations.append(OptimizationRecommendation::EnableAsyncLoading);
        recommendations.append(OptimizationRecommendation::ReduceAnimations);
    }
    
    // Analyze CPU usage
    if (average.cpuUsage > 80.0) {
        recommendations.append(OptimizationRecommendation::ReduceRenderQuality);
        recommendations.append(OptimizationRecommendation::OptimizeMemoryUsage);
    }
    
    return recommendations;
}

QString PerformanceMonitor::getRecommendationText(OptimizationRecommendation recommendation) const {
    switch (recommendation) {
        case OptimizationRecommendation::IncreaseCacheSize:
            return "增加缓存大小以提高缓存命中率";
        case OptimizationRecommendation::DecreaseCacheSize:
            return "减少缓存大小以降低内存使用";
        case OptimizationRecommendation::EnablePrerendering:
            return "启用预渲染以提高响应速度";
        case OptimizationRecommendation::DisablePrerendering:
            return "禁用预渲染以降低CPU使用";
        case OptimizationRecommendation::ReduceRenderQuality:
            return "降低渲染质量以提高性能";
        case OptimizationRecommendation::IncreaseRenderQuality:
            return "提高渲染质量以改善视觉效果";
        case OptimizationRecommendation::OptimizeMemoryUsage:
            return "优化内存使用以提高整体性能";
        case OptimizationRecommendation::EnableAsyncLoading:
            return "启用异步加载以提高响应性";
        case OptimizationRecommendation::ReduceAnimations:
            return "减少动画效果以提高性能";
        default:
            return "无特定建议";
    }
}

void PerformanceMonitor::updateSystemMetrics() {
    if (!m_isMonitoring) {
        return;
    }
    
    QMutexLocker locker(&m_metricsMutex);
    
    // Update CPU usage
    m_currentMetrics.cpuUsage = calculateCPUUsage();
    
    // Update memory usage
    m_currentMetrics.memoryUsage = getCurrentMemoryUsage();
    
    // Update timestamp
    m_currentMetrics.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    // Store in history
    m_metricsHistory.enqueue(m_currentMetrics);
    pruneOldMetrics();
    
    if (m_realTimeEnabled) {
        emit metricsUpdated(m_currentMetrics);
    }
}

void PerformanceMonitor::analyzePerformance() {
    if (!m_isMonitoring) {
        return;
    }
    
    checkThresholds(getCurrentMetrics());
    
    // Generate recommendations
    QList<OptimizationRecommendation> recommendations = getRecommendations();
    for (OptimizationRecommendation rec : recommendations) {
        emit optimizationRecommended(rec);
    }
}

void PerformanceMonitor::checkThresholds(const PerformanceMetrics& metrics) {
    if (metrics.renderTime > m_renderTimeThreshold) {
        emit thresholdExceeded("Render Time", metrics.renderTime, m_renderTimeThreshold);
    }
    
    if (metrics.memoryUsage > m_memoryThreshold) {
        emit thresholdExceeded("Memory Usage", metrics.memoryUsage, m_memoryThreshold);
    }
    
    if (metrics.cacheHitRate < m_cacheHitThreshold) {
        emit thresholdExceeded("Cache Hit Rate", metrics.cacheHitRate, m_cacheHitThreshold);
    }
}

double PerformanceMonitor::calculateCPUUsage() {
    // Simplified CPU usage calculation
    // In a real implementation, this would use platform-specific APIs
    return QRandomGenerator::global()->bounded(100); // Placeholder
}

qint64 PerformanceMonitor::getCurrentMemoryUsage() {
    // Simplified memory usage calculation
    // In a real implementation, this would use platform-specific APIs
    return QApplication::instance()->property("memoryUsage").toLongLong();
}

void PerformanceMonitor::pruneOldMetrics() {
    while (m_metricsHistory.size() > m_maxHistorySize) {
        m_metricsHistory.dequeue();
    }
}

void PerformanceMonitor::enableRealTimeMonitoring(bool enabled) {
    m_realTimeEnabled = enabled;
}

QString PerformanceMonitor::generatePerformanceReport() const {
    PerformanceMetrics current = getCurrentMetrics();
    PerformanceMetrics average = getAverageMetrics(10);
    
    QString report;
    report += "=== 性能报告 ===\n\n";
    report += QString("生成时间: %1\n\n").arg(QDateTime::currentDateTime().toString());
    
    report += "当前性能指标:\n";
    report += QString("- 渲染时间: %1ms\n").arg(current.renderTime);
    report += QString("- 内存使用: %1MB\n").arg(current.memoryUsage / 1024 / 1024);
    report += QString("- 缓存命中率: %1%\n").arg(current.cacheHitRate);
    report += QString("- CPU使用率: %1%\n\n").arg(current.cpuUsage, 0, 'f', 1);
    
    report += "平均性能指标 (10分钟):\n";
    report += QString("- 平均渲染时间: %1ms\n").arg(average.renderTime);
    report += QString("- 平均内存使用: %1MB\n").arg(average.memoryUsage / 1024 / 1024);
    report += QString("- 平均缓存命中率: %1%\n").arg(average.cacheHitRate);
    report += QString("- 平均CPU使用率: %1%\n\n").arg(average.cpuUsage, 0, 'f', 1);
    
    QList<OptimizationRecommendation> recommendations = getRecommendations();
    if (!recommendations.isEmpty()) {
        report += "优化建议:\n";
        for (OptimizationRecommendation rec : recommendations) {
            report += QString("- %1\n").arg(getRecommendationText(rec));
        }
    }
    
    return report;
}

bool PerformanceMonitor::exportMetricsToFile(const QString& filePath) const {
    QJsonArray metricsArray;
    QList<PerformanceMetrics> history = getMetricsHistory();
    
    for (const PerformanceMetrics& metrics : history) {
        QJsonObject obj;
        obj["timestamp"] = metrics.timestamp;
        obj["renderTime"] = metrics.renderTime;
        obj["memoryUsage"] = metrics.memoryUsage;
        obj["cacheHitRate"] = metrics.cacheHitRate;
        obj["cpuUsage"] = metrics.cpuUsage;
        obj["renderFPS"] = metrics.renderFPS;
        metricsArray.append(obj);
    }
    
    QJsonObject root;
    root["metrics"] = metricsArray;
    root["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    QJsonDocument doc(root);
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        return true;
    }
    
    return false;
}

void PerformanceMonitor::clearMetricsHistory() {
    QMutexLocker locker(&m_metricsMutex);
    m_metricsHistory.clear();
    m_pageRenderTimes.clear();
    m_recentRenderTimes.clear();
    m_recentCacheHits.clear();
    m_recentCacheMisses.clear();
}
