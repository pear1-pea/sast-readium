#pragma once

#include <QObject>
#include <QTimer>
#include <QRect>
#include <QSize>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QAbstractItemView>

class ThumbnailModel;
class ThumbnailListView;

/**
 * @brief 缩略图虚拟滚动优化器
 * 
 * 特性：
 * - 智能可见区域检测
 * - 懒加载管理
 * - 内存使用优化
 * - 预加载策略
 * - 性能监控
 */
class ThumbnailVirtualizer : public QObject
{
    Q_OBJECT

public:
    struct VisibleRange {
        int firstVisible = -1;
        int lastVisible = -1;
        int totalItems = 0;
        QRect viewportRect;
        
        bool isValid() const {
            return firstVisible >= 0 && lastVisible >= firstVisible && totalItems > 0;
        }
        
        bool contains(int index) const {
            return index >= firstVisible && index <= lastVisible;
        }
        
        int count() const {
            return isValid() ? (lastVisible - firstVisible + 1) : 0;
        }
    };

    struct PreloadStrategy {
        int preloadBefore = 3;    // 向前预加载的项目数
        int preloadAfter = 5;     // 向后预加载的项目数
        int maxPreloadItems = 20; // 最大预加载项目数
        int preloadDelay = 200;   // 预加载延迟(ms)
        bool adaptivePreload = true; // 自适应预加载
        
        PreloadStrategy() = default;
    };

    explicit ThumbnailVirtualizer(ThumbnailListView* view, QObject* parent = nullptr);
    ~ThumbnailVirtualizer() override;

    // 视图管理
    void setView(ThumbnailListView* view);
    ThumbnailListView* view() const { return m_view; }
    
    void setModel(ThumbnailModel* model);
    ThumbnailModel* model() const { return m_model; }
    
    // 可见区域管理
    VisibleRange calculateVisibleRange() const;
    VisibleRange currentVisibleRange() const { return m_currentRange; }
    
    void updateVisibleRange();
    void forceUpdateVisibleRange();
    
    // 预加载策略
    void setPreloadStrategy(const PreloadStrategy& strategy);
    PreloadStrategy preloadStrategy() const { return m_preloadStrategy; }
    
    void setPreloadEnabled(bool enabled);
    bool preloadEnabled() const { return m_preloadEnabled; }
    
    // 懒加载控制
    void setLazyLoadingEnabled(bool enabled);
    bool lazyLoadingEnabled() const { return m_lazyLoadingEnabled; }
    
    void setUnloadInvisibleItems(bool enabled);
    bool unloadInvisibleItems() const { return m_unloadInvisibleItems; }
    
    // 性能设置
    void setUpdateThrottleInterval(int interval);
    int updateThrottleInterval() const { return m_updateThrottleInterval; }
    
    void setMemoryPressureThreshold(qint64 threshold);
    qint64 memoryPressureThreshold() const { return m_memoryPressureThreshold; }
    
    // 手动控制
    void requestLoad(int pageNumber);
    void requestLoadRange(int startPage, int endPage);
    void requestUnload(int pageNumber);
    void requestUnloadRange(int startPage, int endPage);
    
    // 状态查询
    bool isVisible(int pageNumber) const;
    bool isPreloaded(int pageNumber) const;
    QSet<int> visiblePages() const;
    QSet<int> preloadedPages() const;
    
    // 统计信息
    int visibleItemCount() const;
    int preloadedItemCount() const;
    int totalManagedItems() const;
    qint64 estimatedMemoryUsage() const;
    
    // 性能监控
    void enablePerformanceMonitoring(bool enabled);
    bool performanceMonitoringEnabled() const { return m_performanceMonitoring; }
    
    double averageUpdateTime() const { return m_averageUpdateTime; }
    int updateCount() const { return m_updateCount; }

signals:
    void visibleRangeChanged(const VisibleRange& range);
    void preloadRequested(int pageNumber);
    void unloadRequested(int pageNumber);
    void memoryPressureDetected(qint64 usage, qint64 threshold);
    void performanceWarning(const QString& message);

private slots:
    void onUpdateTimer();
    void onPreloadTimer();
    void onMemoryCheckTimer();
    void onViewScrolled();
    void onViewResized();
    void onModelChanged();

private:
    void initializeVirtualizer();
    void connectViewSignals();
    void disconnectViewSignals();
    
    void scheduleUpdate();
    void performUpdate();
    void updatePreloadQueue();
    void processPreloadQueue();
    void checkMemoryPressure();
    
    VisibleRange calculateVisibleRangeInternal() const;
    QRect getItemRect(int index) const;
    bool isItemVisible(int index, const QRect& viewportRect) const;
    
    void adaptPreloadStrategy();
    void updatePreloadCounts();
    
    void logPerformance(const QString& operation, qint64 duration);
    void updatePerformanceStats(qint64 duration);
    
    // 内存管理
    void unloadInvisibleItems();
    void prioritizeVisibleItems();
    qint64 calculateMemoryUsage() const;

private:
    // 核心组件
    ThumbnailListView* m_view;
    ThumbnailModel* m_model;
    
    // 可见区域状态
    VisibleRange m_currentRange;
    VisibleRange m_previousRange;
    QRect m_lastViewportRect;
    
    // 预加载管理
    PreloadStrategy m_preloadStrategy;
    bool m_preloadEnabled;
    QSet<int> m_preloadQueue;
    QSet<int> m_preloadedItems;
    QTimer* m_preloadTimer;
    
    // 懒加载设置
    bool m_lazyLoadingEnabled;
    bool m_unloadInvisibleItems;
    
    // 更新控制
    QTimer* m_updateTimer;
    int m_updateThrottleInterval;
    bool m_updateScheduled;
    
    // 内存管理
    qint64 m_memoryPressureThreshold;
    QTimer* m_memoryCheckTimer;
    
    // 性能监控
    bool m_performanceMonitoring;
    qint64 m_totalUpdateTime;
    int m_updateCount;
    double m_averageUpdateTime;
    QTimer* m_performanceTimer;
    
    // 常量
    static constexpr int DEFAULT_UPDATE_THROTTLE = 50; // ms
    static constexpr int DEFAULT_PRELOAD_DELAY = 200; // ms
    static constexpr int DEFAULT_MEMORY_CHECK_INTERVAL = 1000; // ms
    static constexpr qint64 DEFAULT_MEMORY_THRESHOLD = 256 * 1024 * 1024; // 256MB
    static constexpr int PERFORMANCE_LOG_INTERVAL = 10000; // ms
    static constexpr int MAX_PRELOAD_QUEUE_SIZE = 50;
    static constexpr double VIEWPORT_MARGIN_FACTOR = 0.5; // 视口边距因子
};
