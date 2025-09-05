#pragma once

#include <QObject>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QEasingCurve>
#include <QTimer>
#include <QHash>
#include <QModelIndex>
#include <QPersistentModelIndex>

/**
 * @brief 缩略图动画管理器
 * 
 * 负责管理缩略图的各种动画效果：
 * - 悬停状态动画
 * - 选中状态动画
 * - 加载动画
 * - 淡入淡出效果
 * - 滚动动画
 */
class ThumbnailAnimations : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverOpacity READ hoverOpacity WRITE setHoverOpacity NOTIFY hoverOpacityChanged)
    Q_PROPERTY(qreal selectionOpacity READ selectionOpacity WRITE setSelectionOpacity NOTIFY selectionOpacityChanged)
    Q_PROPERTY(qreal fadeOpacity READ fadeOpacity WRITE setFadeOpacity NOTIFY fadeOpacityChanged)
    Q_PROPERTY(int loadingAngle READ loadingAngle WRITE setLoadingAngle NOTIFY loadingAngleChanged)

public:
    enum AnimationType {
        HoverAnimation,
        SelectionAnimation,
        LoadingAnimation,
        FadeInAnimation,
        FadeOutAnimation,
        ScrollAnimation
    };

    struct AnimationState {
        qreal hoverOpacity = 0.0;
        qreal selectionOpacity = 0.0;
        qreal fadeOpacity = 1.0;
        int loadingAngle = 0;
        bool isHovered = false;
        bool isSelected = false;
        bool isLoading = false;
        bool isFading = false;
        
        AnimationState() = default;
    };

    explicit ThumbnailAnimations(QObject* parent = nullptr);
    ~ThumbnailAnimations() override;

    // 动画属性
    qreal hoverOpacity() const { return m_hoverOpacity; }
    void setHoverOpacity(qreal opacity);
    
    qreal selectionOpacity() const { return m_selectionOpacity; }
    void setSelectionOpacity(qreal opacity);
    
    qreal fadeOpacity() const { return m_fadeOpacity; }
    void setFadeOpacity(qreal opacity);
    
    int loadingAngle() const { return m_loadingAngle; }
    void setLoadingAngle(int angle);

    // 动画控制
    void startHoverAnimation(const QModelIndex& index, bool hovered);
    void startSelectionAnimation(const QModelIndex& index, bool selected);
    void startLoadingAnimation(const QModelIndex& index, bool loading);
    void startFadeInAnimation(const QModelIndex& index);
    void startFadeOutAnimation(const QModelIndex& index);
    
    void stopAnimation(const QModelIndex& index, AnimationType type);
    void stopAllAnimations(const QModelIndex& index);
    void stopAllAnimations();
    
    // 状态查询
    AnimationState getAnimationState(const QModelIndex& index) const;
    bool isAnimating(const QModelIndex& index, AnimationType type) const;
    bool hasAnyAnimation(const QModelIndex& index) const;
    
    // 设置
    void setAnimationDuration(AnimationType type, int duration);
    int animationDuration(AnimationType type) const;
    
    void setEasingCurve(AnimationType type, const QEasingCurve& curve);
    QEasingCurve easingCurve(AnimationType type) const;
    
    void setAnimationEnabled(bool enabled);
    bool animationEnabled() const { return m_animationEnabled; }
    
    // 批量操作
    void pauseAllAnimations();
    void resumeAllAnimations();
    void clearAllAnimations();
    
    // 性能优化
    void setMaxConcurrentAnimations(int max);
    int maxConcurrentAnimations() const { return m_maxConcurrentAnimations; }
    
    int activeAnimationCount() const;

signals:
    void hoverOpacityChanged(qreal opacity);
    void selectionOpacityChanged(qreal opacity);
    void fadeOpacityChanged(qreal opacity);
    void loadingAngleChanged(int angle);
    void animationStarted(const QModelIndex& index, AnimationType type);
    void animationFinished(const QModelIndex& index, AnimationType type);
    void animationStateChanged(const QModelIndex& index);

private slots:
    void onHoverAnimationFinished();
    void onSelectionAnimationFinished();
    void onFadeAnimationFinished();
    void onLoadingTimer();
    void onAnimationValueChanged();

private:
    struct AnimationGroup {
        QPropertyAnimation* hoverAnimation = nullptr;
        QPropertyAnimation* selectionAnimation = nullptr;
        QPropertyAnimation* fadeAnimation = nullptr;
        QTimer* loadingTimer = nullptr;
        AnimationState state;
        
        AnimationGroup() = default;
        ~AnimationGroup();
    };

    void initializeAnimations();
    void cleanupAnimations();
    
    AnimationGroup* getAnimationGroup(const QModelIndex& index);
    void removeAnimationGroup(const QModelIndex& index);
    
    QPropertyAnimation* createPropertyAnimation(AnimationType type, QObject* target, const QByteArray& property);
    void configureAnimation(QPropertyAnimation* animation, AnimationType type);
    
    void updateLoadingAnimations();
    void cleanupFinishedAnimations();
    
    bool shouldCreateAnimation(const QModelIndex& index, AnimationType type) const;
    void limitConcurrentAnimations();

private:
    // 动画属性
    qreal m_hoverOpacity;
    qreal m_selectionOpacity;
    qreal m_fadeOpacity;
    int m_loadingAngle;
    
    // 动画组管理
    QHash<QPersistentModelIndex, AnimationGroup*> m_animationGroups;
    mutable QMutex m_animationsMutex;
    
    // 设置
    bool m_animationEnabled;
    int m_maxConcurrentAnimations;
    
    // 动画配置
    QHash<AnimationType, int> m_animationDurations;
    QHash<AnimationType, QEasingCurve> m_easingCurves;
    
    // 加载动画定时器
    QTimer* m_globalLoadingTimer;
    
    // 清理定时器
    QTimer* m_cleanupTimer;
    
    // 常量
    static constexpr int DEFAULT_HOVER_DURATION = 200; // ms
    static constexpr int DEFAULT_SELECTION_DURATION = 300; // ms
    static constexpr int DEFAULT_FADE_DURATION = 150; // ms
    static constexpr int DEFAULT_LOADING_INTERVAL = 50; // ms
    static constexpr int DEFAULT_MAX_CONCURRENT = 10;
    static constexpr int CLEANUP_INTERVAL = 5000; // ms
    static constexpr int LOADING_ANGLE_STEP = 15; // degrees
};
