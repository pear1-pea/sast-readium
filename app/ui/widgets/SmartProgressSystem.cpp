#include "SmartProgressSystem.h"
#include <QApplication>
#include <QDesktopWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QDebug>
#include <QUuid>
#include <QMutexLocker>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QEasingCurve>
#include <QSound>
#include <cmath>

// SmartProgressWidget Implementation
SmartProgressWidget::SmartProgressWidget(QWidget* parent)
    : QWidget(parent)
    , m_layout(nullptr)
    , m_titleLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_progressBar(nullptr)
    , m_cancelButton(nullptr)
    , m_timeLabel(nullptr)
    , m_fadeAnimation(nullptr)
    , m_opacityEffect(nullptr)
    , m_animationTimer(nullptr)
    , m_updateTimer(nullptr)
    , m_isAnimating(false)
    , m_animationFrame(0)
{
    setupUI();
    
    // Setup animations
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacityEffect);
    
    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setDuration(300);
    m_fadeAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    
    connect(m_fadeAnimation, &QPropertyAnimation::finished, 
            this, &SmartProgressWidget::onFadeInFinished);
    
    // Animation timer for indeterminate progress
    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(50); // 20 FPS
    connect(m_animationTimer, &QTimer::timeout, this, &SmartProgressWidget::onAnimationTimer);
    
    // Update timer for time display
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(1000); // 1 second
    connect(m_updateTimer, &QTimer::timeout, this, &SmartProgressWidget::updateTimeDisplay);
}

SmartProgressWidget::~SmartProgressWidget()
{
}

void SmartProgressWidget::setupUI()
{
    setFixedSize(300, 120);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(15, 10, 15, 10);
    m_layout->setSpacing(8);
    
    // Title label
    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 12px; color: #333; }");
    m_titleLabel->setWordWrap(true);
    m_layout->addWidget(m_titleLabel);
    
    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setStyleSheet(
        "QProgressBar {"
        "    border: 1px solid #ccc;"
        "    border-radius: 3px;"
        "    text-align: center;"
        "    font-size: 10px;"
        "}"
        "QProgressBar::chunk {"
        "    background-color: #4CAF50;"
        "    border-radius: 2px;"
        "}"
    );
    m_progressBar->setFixedHeight(20);
    m_layout->addWidget(m_progressBar);
    
    // Status and time layout
    QHBoxLayout* statusLayout = new QHBoxLayout();
    
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("QLabel { font-size: 10px; color: #666; }");
    m_statusLabel->setWordWrap(true);
    statusLayout->addWidget(m_statusLabel, 1);
    
    m_timeLabel = new QLabel(this);
    m_timeLabel->setStyleSheet("QLabel { font-size: 10px; color: #666; }");
    m_timeLabel->setAlignment(Qt::AlignRight);
    statusLayout->addWidget(m_timeLabel);
    
    m_layout->addLayout(statusLayout);
    
    // Cancel button
    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #f44336;"
        "    color: white;"
        "    border: none;"
        "    border-radius: 3px;"
        "    padding: 4px 12px;"
        "    font-size: 10px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #d32f2f;"
        "}"
    );
    m_cancelButton->setFixedHeight(24);
    m_cancelButton->hide();
    connect(m_cancelButton, &QPushButton::clicked, this, &SmartProgressWidget::cancelled);
    m_layout->addWidget(m_cancelButton);
}

void SmartProgressWidget::setOperation(const ProgressOperation& operation)
{
    m_operation = operation;
    
    m_titleLabel->setText(operation.title);
    m_statusLabel->setText(operation.description);
    m_progressBar->setMaximum(operation.maxValue);
    m_progressBar->setValue(operation.currentValue);
    
    setIndeterminate(operation.isIndeterminate);
    showCancelButton(operation.isCancellable);
    
    if (operation.startTime > 0) {
        m_updateTimer->start();
    }
    
    updateProgressText();
}

void SmartProgressWidget::updateProgress(int value)
{
    m_operation.currentValue = value;
    m_progressBar->setValue(value);
    updateProgressText();
    updateTimeDisplay();
}

void SmartProgressWidget::updateStatus(const QString& status)
{
    m_operation.statusText = status;
    m_statusLabel->setText(status);
}

void SmartProgressWidget::setIndeterminate(bool indeterminate)
{
    m_operation.isIndeterminate = indeterminate;
    
    if (indeterminate) {
        m_progressBar->setRange(0, 0);
        startAnimation();
    } else {
        m_progressBar->setRange(0, m_operation.maxValue);
        stopAnimation();
    }
}

void SmartProgressWidget::showCancelButton(bool show)
{
    m_operation.isCancellable = show;
    m_cancelButton->setVisible(show);
}

void SmartProgressWidget::startAnimation()
{
    if (!m_isAnimating) {
        m_isAnimating = true;
        m_animationFrame = 0;
        m_animationTimer->start();
    }
}

void SmartProgressWidget::stopAnimation()
{
    if (m_isAnimating) {
        m_isAnimating = false;
        m_animationTimer->stop();
        update();
    }
}

void SmartProgressWidget::fadeIn()
{
    m_fadeAnimation->setStartValue(0.0);
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
    show();
}

void SmartProgressWidget::fadeOut()
{
    m_fadeAnimation->setStartValue(1.0);
    m_fadeAnimation->setEndValue(0.0);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, &QWidget::hide);
    m_fadeAnimation->start();
}

void SmartProgressWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background with shadow
    QRect bgRect = rect().adjusted(5, 5, -5, -5);
    
    // Shadow
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bgRect.adjusted(2, 2, 2, 2), 8, 8);
    
    // Background
    painter.setBrush(QColor(255, 255, 255, 240));
    painter.setPen(QPen(QColor(200, 200, 200), 1));
    painter.drawRoundedRect(bgRect, 8, 8);
    
    QWidget::paintEvent(event);
}

void SmartProgressWidget::updateTimeDisplay()
{
    if (m_operation.startTime == 0) return;
    
    qint64 elapsed = m_operation.getElapsedTime();
    qint64 remaining = m_operation.getEstimatedTimeRemaining();
    
    QString timeText;
    if (remaining > 0) {
        timeText = QString("%1 / %2").arg(formatTime(elapsed)).arg(formatTime(remaining));
    } else {
        timeText = formatTime(elapsed);
    }
    
    m_timeLabel->setText(timeText);
}

QString SmartProgressWidget::formatTime(qint64 milliseconds) const
{
    int seconds = milliseconds / 1000;
    int minutes = seconds / 60;
    seconds %= 60;
    
    if (minutes > 0) {
        return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
    } else {
        return QString("%1s").arg(seconds);
    }
}

void SmartProgressWidget::updateProgressText()
{
    QString progressText = formatProgressText();
    m_progressBar->setFormat(progressText);
}

QString SmartProgressWidget::formatProgressText() const
{
    if (m_operation.isIndeterminate) {
        return "Processing...";
    }
    
    double progress = m_operation.getProgress() * 100;
    return QString("%1%").arg(static_cast<int>(progress));
}

void SmartProgressWidget::onAnimationTimer()
{
    m_animationFrame = (m_animationFrame + 1) % 100;
    update();
}

// SmartProgressSystem Implementation
SmartProgressSystem::SmartProgressSystem(QWidget* parentWidget, QObject* parent)
    : QObject(parent)
    , m_parentWidget(parentWidget)
    , m_maxVisibleNotifications(5)
    , m_defaultNotificationDuration(3000)
    , m_progressPosition(Qt::TopRightCorner)
    , m_soundsEnabled(true)
    , m_isPaused(false)
    , m_cleanupTimer(nullptr)
    , m_updateTimer(nullptr)
    , m_settings(nullptr)
{
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-ProgressSystem", this);
    loadSettings();
    
    // Setup timers
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(5000); // 5 seconds
    connect(m_cleanupTimer, &QTimer::timeout, this, &SmartProgressSystem::onCleanupTimer);
    m_cleanupTimer->start();
    
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(100); // 100ms
    connect(m_updateTimer, &QTimer::timeout, this, &SmartProgressSystem::updateProgressWidgets);
    m_updateTimer->start();
    
    qDebug() << "SmartProgressSystem: Initialized";
}

SmartProgressSystem::~SmartProgressSystem()
{
    saveSettings();
    cancelAllOperations();
    clearAllNotifications();
}

QString SmartProgressSystem::startOperation(const QString& title, ProgressType type)
{
    return startOperation(title, QString(), 100, type);
}

QString SmartProgressSystem::startOperation(const QString& title, const QString& description, 
                                           int maxValue, ProgressType type)
{
    QString id = generateOperationId();
    
    ProgressOperation operation;
    operation.id = id;
    operation.title = title;
    operation.description = description;
    operation.type = type;
    operation.maxValue = maxValue;
    operation.startTime = QDateTime::currentMSecsSinceEpoch();
    
    QMutexLocker locker(&m_operationsMutex);
    m_operations[id] = operation;
    locker.unlock();
    
    createProgressWidget(id, operation);
    
    emit operationStarted(id, operation);
    
    qDebug() << "SmartProgressSystem: Started operation" << id << title;
    return id;
}

void SmartProgressSystem::updateOperation(const QString& id, int value)
{
    updateOperation(id, value, QString());
}

void SmartProgressSystem::updateOperation(const QString& id, int value, const QString& status)
{
    QMutexLocker locker(&m_operationsMutex);
    
    auto it = m_operations.find(id);
    if (it == m_operations.end()) {
        qWarning() << "SmartProgressSystem: Unknown operation" << id;
        return;
    }
    
    it.value().currentValue = value;
    if (!status.isEmpty()) {
        it.value().statusText = status;
    }
    
    ProgressOperation operation = it.value();
    locker.unlock();
    
    // Update widget
    SmartProgressWidget* widget = m_progressWidgets.value(id, nullptr);
    if (widget) {
        widget->updateProgress(value);
        if (!status.isEmpty()) {
            widget->updateStatus(status);
        }
    }
    
    emit operationUpdated(id, operation);
}

void SmartProgressSystem::finishOperation(const QString& id)
{
    QMutexLocker locker(&m_operationsMutex);
    
    if (!m_operations.contains(id)) {
        qWarning() << "SmartProgressSystem: Unknown operation" << id;
        return;
    }
    
    m_operations.remove(id);
    locker.unlock();
    
    removeProgressWidget(id);
    
    emit operationFinished(id);
    
    qDebug() << "SmartProgressSystem: Finished operation" << id;
}

void SmartProgressSystem::showNotification(const QString& title, const QString& message, 
                                          NotificationLevel level)
{
    NotificationMessage notification;
    notification.id = generateNotificationId();
    notification.title = title;
    notification.message = message;
    notification.level = level;
    notification.timestamp = QDateTime::currentMSecsSinceEpoch();
    notification.duration = m_defaultNotificationDuration;
    
    showNotification(notification);
}

void SmartProgressSystem::showInfo(const QString& message)
{
    showNotification("Information", message, NotificationLevel::Info);
}

void SmartProgressSystem::showSuccess(const QString& message)
{
    showNotification("Success", message, NotificationLevel::Success);
}

void SmartProgressSystem::showWarning(const QString& message)
{
    showNotification("Warning", message, NotificationLevel::Warning);
}

void SmartProgressSystem::showError(const QString& message)
{
    showNotification("Error", message, NotificationLevel::Error);
}

QString SmartProgressSystem::generateOperationId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString SmartProgressSystem::generateNotificationId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void SmartProgressSystem::createProgressWidget(const QString& id, const ProgressOperation& operation)
{
    SmartProgressWidget* widget = new SmartProgressWidget(m_parentWidget);
    widget->setOperation(operation);
    
    connect(widget, &SmartProgressWidget::cancelled, this, [this, id]() {
        cancelOperation(id);
    });
    
    m_progressWidgets[id] = widget;
    positionProgressWidget(widget);
    widget->fadeIn();
}

void SmartProgressSystem::removeProgressWidget(const QString& id)
{
    SmartProgressWidget* widget = m_progressWidgets.value(id, nullptr);
    if (widget) {
        m_progressWidgets.remove(id);
        widget->fadeOut();
        widget->deleteLater();
    }
}

void SmartProgressSystem::positionProgressWidget(SmartProgressWidget* widget)
{
    if (!m_parentWidget || !widget) return;
    
    QRect parentRect = m_parentWidget->rect();
    QSize widgetSize = widget->size();
    
    int x, y;
    int margin = 20;
    int offset = m_progressWidgets.size() * (widgetSize.height() + 10);
    
    switch (m_progressPosition) {
        case Qt::TopRightCorner:
            x = parentRect.right() - widgetSize.width() - margin;
            y = parentRect.top() + margin + offset;
            break;
        case Qt::TopLeftCorner:
            x = parentRect.left() + margin;
            y = parentRect.top() + margin + offset;
            break;
        case Qt::BottomRightCorner:
            x = parentRect.right() - widgetSize.width() - margin;
            y = parentRect.bottom() - widgetSize.height() - margin - offset;
            break;
        case Qt::BottomLeftCorner:
            x = parentRect.left() + margin;
            y = parentRect.bottom() - widgetSize.height() - margin - offset;
            break;
    }
    
    widget->move(x, y);
}

void SmartProgressSystem::loadSettings()
{
    if (!m_settings) return;
    
    m_maxVisibleNotifications = m_settings->value("progress/maxNotifications", 5).toInt();
    m_defaultNotificationDuration = m_settings->value("progress/notificationDuration", 3000).toInt();
    m_soundsEnabled = m_settings->value("progress/soundsEnabled", true).toBool();
    
    int position = m_settings->value("progress/position", static_cast<int>(Qt::TopRightCorner)).toInt();
    m_progressPosition = static_cast<Qt::Corner>(position);
}

void SmartProgressSystem::saveSettings()
{
    if (!m_settings) return;
    
    m_settings->setValue("progress/maxNotifications", m_maxVisibleNotifications);
    m_settings->setValue("progress/notificationDuration", m_defaultNotificationDuration);
    m_settings->setValue("progress/soundsEnabled", m_soundsEnabled);
    m_settings->setValue("progress/position", static_cast<int>(m_progressPosition));
    
    m_settings->sync();
}
