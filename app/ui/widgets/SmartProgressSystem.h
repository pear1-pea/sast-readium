#pragma once

#include <QObject>
#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QElapsedTimer>
#include <QQueue>
#include <QMutex>
#include <QSettings>

/**
 * Progress operation types
 */
enum class ProgressType {
    DocumentLoading,
    PageRendering,
    SearchOperation,
    CacheOptimization,
    FileExport,
    ThumbnailGeneration,
    AnnotationProcessing,
    CustomOperation
};

/**
 * Progress notification levels
 */
enum class NotificationLevel {
    Info,
    Success,
    Warning,
    Error,
    Critical
};

/**
 * Progress operation data
 */
struct ProgressOperation {
    QString id;
    QString title;
    QString description;
    ProgressType type;
    int currentValue;
    int maxValue;
    qint64 startTime;
    qint64 estimatedDuration;
    bool isCancellable;
    bool isIndeterminate;
    QString statusText;
    
    ProgressOperation()
        : type(ProgressType::CustomOperation), currentValue(0), maxValue(100)
        , startTime(0), estimatedDuration(0), isCancellable(false)
        , isIndeterminate(false) {}
    
    double getProgress() const {
        if (maxValue == 0 || isIndeterminate) return 0.0;
        return static_cast<double>(currentValue) / maxValue;
    }
    
    qint64 getElapsedTime() const {
        return QDateTime::currentMSecsSinceEpoch() - startTime;
    }
    
    qint64 getEstimatedTimeRemaining() const {
        if (isIndeterminate || currentValue == 0) return -1;
        
        qint64 elapsed = getElapsedTime();
        double progress = getProgress();
        
        if (progress > 0.0) {
            return static_cast<qint64>(elapsed * (1.0 - progress) / progress);
        }
        
        return estimatedDuration > 0 ? estimatedDuration - elapsed : -1;
    }
};

/**
 * Notification message
 */
struct NotificationMessage {
    QString id;
    QString title;
    QString message;
    NotificationLevel level;
    qint64 timestamp;
    int duration;
    bool isAutoHide;
    QString actionText;
    std::function<void()> actionCallback;
    
    NotificationMessage()
        : level(NotificationLevel::Info), timestamp(0), duration(3000)
        , isAutoHide(true) {}
};

/**
 * Smart progress widget with animations
 */
class SmartProgressWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SmartProgressWidget(QWidget* parent = nullptr);
    ~SmartProgressWidget();

    void setOperation(const ProgressOperation& operation);
    void updateProgress(int value);
    void updateStatus(const QString& status);
    void setIndeterminate(bool indeterminate);
    void showCancelButton(bool show);
    
    void startAnimation();
    void stopAnimation();
    void fadeIn();
    void fadeOut();

signals:
    void cancelled();
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onAnimationTimer();
    void onFadeInFinished();
    void onFadeOutFinished();

private:
    QVBoxLayout* m_layout;
    QLabel* m_titleLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QPushButton* m_cancelButton;
    QLabel* m_timeLabel;
    
    QPropertyAnimation* m_fadeAnimation;
    QGraphicsOpacityEffect* m_opacityEffect;
    QTimer* m_animationTimer;
    QTimer* m_updateTimer;
    
    ProgressOperation m_operation;
    bool m_isAnimating;
    int m_animationFrame;
    
    void setupUI();
    void updateTimeDisplay();
    void updateProgressText();
    QString formatTime(qint64 milliseconds) const;
    QString formatProgressText() const;
};

/**
 * Notification widget with auto-hide
 */
class NotificationWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NotificationWidget(const NotificationMessage& message, QWidget* parent = nullptr);
    ~NotificationWidget();

    void show();
    void hide();
    const NotificationMessage& getMessage() const { return m_message; }

signals:
    void actionTriggered();
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onAutoHideTimer();
    void onActionClicked();

private:
    NotificationMessage m_message;
    QHBoxLayout* m_layout;
    QLabel* m_iconLabel;
    QLabel* m_titleLabel;
    QLabel* m_messageLabel;
    QPushButton* m_actionButton;
    QPushButton* m_closeButton;
    
    QPropertyAnimation* m_slideAnimation;
    QTimer* m_autoHideTimer;
    
    void setupUI();
    void setupAnimation();
    QPixmap getIconForLevel(NotificationLevel level) const;
    QColor getColorForLevel(NotificationLevel level) const;
};

/**
 * Smart progress and notification system
 */
class SmartProgressSystem : public QObject
{
    Q_OBJECT

public:
    explicit SmartProgressSystem(QWidget* parentWidget, QObject* parent = nullptr);
    ~SmartProgressSystem();

    // Progress operations
    QString startOperation(const QString& title, ProgressType type = ProgressType::CustomOperation);
    QString startOperation(const QString& title, const QString& description, 
                          int maxValue, ProgressType type = ProgressType::CustomOperation);
    
    void updateOperation(const QString& id, int value);
    void updateOperation(const QString& id, int value, const QString& status);
    void setOperationIndeterminate(const QString& id, bool indeterminate);
    void setOperationCancellable(const QString& id, bool cancellable);
    void finishOperation(const QString& id);
    void cancelOperation(const QString& id);
    
    // Notifications
    void showNotification(const QString& title, const QString& message, 
                         NotificationLevel level = NotificationLevel::Info);
    void showNotification(const NotificationMessage& message);
    void hideNotification(const QString& id);
    void clearAllNotifications();
    
    // Quick notifications
    void showInfo(const QString& message);
    void showSuccess(const QString& message);
    void showWarning(const QString& message);
    void showError(const QString& message);
    void showCritical(const QString& message);
    
    // Configuration
    void setMaxVisibleNotifications(int max);
    void setDefaultNotificationDuration(int milliseconds);
    void setProgressWidgetPosition(Qt::Corner corner);
    void enableSounds(bool enable);
    
    // State management
    bool hasActiveOperations() const;
    QStringList getActiveOperationIds() const;
    ProgressOperation getOperation(const QString& id) const;
    
    // Settings
    void loadSettings();
    void saveSettings();

public slots:
    void pauseAllOperations();
    void resumeAllOperations();
    void cancelAllOperations();

signals:
    void operationStarted(const QString& id, const ProgressOperation& operation);
    void operationUpdated(const QString& id, const ProgressOperation& operation);
    void operationFinished(const QString& id);
    void operationCancelled(const QString& id);
    void notificationShown(const QString& id, const NotificationMessage& message);
    void notificationDismissed(const QString& id);

private slots:
    void onOperationCancelled();
    void onNotificationDismissed();
    void onCleanupTimer();
    void updateProgressWidgets();

private:
    QWidget* m_parentWidget;
    
    // Progress management
    QHash<QString, ProgressOperation> m_operations;
    QHash<QString, SmartProgressWidget*> m_progressWidgets;
    mutable QMutex m_operationsMutex;
    
    // Notification management
    QQueue<NotificationMessage> m_notificationQueue;
    QList<NotificationWidget*> m_visibleNotifications;
    QHash<QString, NotificationWidget*> m_notificationWidgets;
    mutable QMutex m_notificationsMutex;
    
    // Configuration
    int m_maxVisibleNotifications;
    int m_defaultNotificationDuration;
    Qt::Corner m_progressPosition;
    bool m_soundsEnabled;
    bool m_isPaused;
    
    // Timers
    QTimer* m_cleanupTimer;
    QTimer* m_updateTimer;
    
    // Settings
    QSettings* m_settings;
    
    // Helper methods
    QString generateOperationId() const;
    QString generateNotificationId() const;
    void positionProgressWidget(SmartProgressWidget* widget);
    void positionNotificationWidget(NotificationWidget* widget, int index);
    void repositionNotifications();
    void processNotificationQueue();
    void cleanupFinishedOperations();
    void playNotificationSound(NotificationLevel level);
    
    // Widget management
    void createProgressWidget(const QString& id, const ProgressOperation& operation);
    void removeProgressWidget(const QString& id);
    void createNotificationWidget(const NotificationMessage& message);
    void removeNotificationWidget(const QString& id);
    
    // Animation helpers
    void animateWidgetIn(QWidget* widget);
    void animateWidgetOut(QWidget* widget);
};
