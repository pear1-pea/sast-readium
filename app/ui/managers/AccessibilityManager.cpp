#include "AccessibilityManager.h"
#include <QDebug>
#include <QApplication>
#include <QObject>
#include <QWidget>
#include <QSettings>
#include <QTimer>
#include <QLayout>
#include <QShortcut>
#include <QKeySequence>
#include <QLabel>
#include <QFocusFrame>
#include <QtCore/Qt>
#include <QtCore/QtGlobal>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QRadioButton>
#include <QTabWidget>
#include <QTreeView>
#include <QListView>
#include <QTableView>
#include <QScrollArea>
#include <QSplitter>
#include <QGroupBox>
#include <QFrame>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
// #include <QSound> // Not available in this MSYS2 setup
#include <QStandardPaths>
#include <QDir>

AccessibilityManager::AccessibilityManager(QWidget* mainWindow, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_settings(new QSettings("SAST", "Readium", this))
    , m_currentLevel(AccessibilityLevel::Standard)
    , m_focusMode(FocusMode::Standard)
    , m_highContrastEnabled(false)
    , m_screenReaderEnabled(false)
    , m_spatialNavigationEnabled(false)
    , m_focusSoundEnabled(false)
    , m_textScaleFactor(1.0)
    , m_focusFrame(nullptr)
    , m_focusTimer(new QTimer(this))
    , m_currentFocusWidget(nullptr)
    , m_focusIndicatorColor(Qt::blue)
    , m_focusIndicatorSize(2)
    // , m_textToSpeech(nullptr) // Not available in this setup
    , m_isAnnouncing(false)
{
    setupKeyboardShortcuts();
    setupFocusFrame();
    
    // Initialize text-to-speech if available (disabled - not available in this setup)
    /*
    if (QTextToSpeech::availableEngines().size() > 0) {
        m_textToSpeech = new QTextToSpeech(this);
        connect(m_textToSpeech, &QTextToSpeech::stateChanged,
                this, &AccessibilityManager::onAnnouncementFinished);
    }
    */
    
    // Connect to application focus changes
    connect(qApp, &QApplication::focusChanged,
            this, &AccessibilityManager::onFocusChanged);
    
    // Setup focus timer for enhanced focus indicators
    m_focusTimer->setSingleShot(true);
    m_focusTimer->setInterval(100);
    connect(m_focusTimer, &QTimer::timeout, this, &AccessibilityManager::onFocusTimer);
    
    loadSettings();
}

void AccessibilityManager::setupKeyboardShortcuts() {
    // High contrast toggle
    m_toggleHighContrastShortcut = new QShortcut(QKeySequence("Ctrl+Alt+H"), m_mainWindow);
    connect(m_toggleHighContrastShortcut, &QShortcut::activated,
            this, &AccessibilityManager::toggleHighContrast);
    
    // Screen reader toggle
    m_toggleScreenReaderShortcut = new QShortcut(QKeySequence("Ctrl+Alt+S"), m_mainWindow);
    connect(m_toggleScreenReaderShortcut, &QShortcut::activated,
            this, &AccessibilityManager::toggleScreenReader);
    
    // Text scaling shortcuts
    m_increaseTextShortcut = new QShortcut(QKeySequence("Ctrl+Plus"), m_mainWindow);
    connect(m_increaseTextShortcut, &QShortcut::activated,
            this, &AccessibilityManager::increaseTextSize);
    
    m_decreaseTextShortcut = new QShortcut(QKeySequence("Ctrl+Minus"), m_mainWindow);
    connect(m_decreaseTextShortcut, &QShortcut::activated,
            this, &AccessibilityManager::decreaseTextSize);
    
    m_resetTextShortcut = new QShortcut(QKeySequence("Ctrl+0"), m_mainWindow);
    connect(m_resetTextShortcut, &QShortcut::activated,
            this, &AccessibilityManager::resetTextSize);
    
    // Spatial navigation shortcuts
    m_spatialUpShortcut = new QShortcut(QKeySequence("Alt+Up"), m_mainWindow);
    connect(m_spatialUpShortcut, &QShortcut::activated,
            this, &AccessibilityManager::focusUp);
    
    m_spatialDownShortcut = new QShortcut(QKeySequence("Alt+Down"), m_mainWindow);
    connect(m_spatialDownShortcut, &QShortcut::activated,
            this, &AccessibilityManager::focusDown);
    
    m_spatialLeftShortcut = new QShortcut(QKeySequence("Alt+Left"), m_mainWindow);
    connect(m_spatialLeftShortcut, &QShortcut::activated,
            this, &AccessibilityManager::focusLeft);
    
    m_spatialRightShortcut = new QShortcut(QKeySequence("Alt+Right"), m_mainWindow);
    connect(m_spatialRightShortcut, &QShortcut::activated,
            this, &AccessibilityManager::focusRight);
}

void AccessibilityManager::setupFocusFrame() {
    m_focusFrame = new QFocusFrame(m_mainWindow);
    m_focusFrame->setVisible(false);
    
    // Style the focus frame
    QString focusStyle = QString(
        "QFocusFrame {"
        "    border: %1px solid %2;"
        "    border-radius: 3px;"
        "    background: transparent;"
        "}"
    ).arg(m_focusIndicatorSize).arg(m_focusIndicatorColor.name());
    
    m_focusFrame->setStyleSheet(focusStyle);
}

void AccessibilityManager::setAccessibilityLevel(AccessibilityLevel level) {
    if (m_currentLevel != level) {
        m_currentLevel = level;
        
        switch (level) {
            case AccessibilityLevel::Standard:
                setHighContrastEnabled(false);
                setScreenReaderEnabled(false);
                enableEnhancedFocus(false);
                break;
                
            case AccessibilityLevel::Enhanced:
                enableEnhancedFocus(true);
                setFocusMode(FocusMode::Enhanced);
                break;
                
            case AccessibilityLevel::HighContrast:
                setHighContrastEnabled(true);
                enableEnhancedFocus(true);
                break;
                
            case AccessibilityLevel::ScreenReader:
                setScreenReaderEnabled(true);
                enableEnhancedFocus(true);
                setFocusMode(FocusMode::Voice);
                break;
        }
        
        emit accessibilityLevelChanged(level);
        saveSettings();
    }
}

void AccessibilityManager::setFocusMode(FocusMode mode) {
    if (m_focusMode != mode) {
        m_focusMode = mode;
        
        switch (mode) {
            case FocusMode::Standard:
                enableEnhancedFocus(false);
                enableSpatialNavigation(false);
                break;
                
            case FocusMode::Enhanced:
                enableEnhancedFocus(true);
                enableSpatialNavigation(false);
                break;
                
            case FocusMode::Spatial:
                enableEnhancedFocus(true);
                enableSpatialNavigation(true);
                createSpatialNavigationMap();
                break;
                
            case FocusMode::Voice:
                enableEnhancedFocus(true);
                enableSpatialNavigation(false);
                setScreenReaderEnabled(true);
                break;
        }
        
        emit focusModeChanged(mode);
        saveSettings();
    }
}

void AccessibilityManager::enableEnhancedFocus(bool enabled) {
    if (enabled) {
        m_focusFrame->setVisible(true);
        updateFocusFrame();
    } else {
        m_focusFrame->setVisible(false);
    }
}

void AccessibilityManager::setHighContrastEnabled(bool enabled) {
    if (m_highContrastEnabled != enabled) {
        m_highContrastEnabled = enabled;
        
        if (enabled) {
            applyHighContrastTheme();
        } else {
            applyStandardTheme();
        }
        
        emit highContrastToggled(enabled);
        saveSettings();
    }
}

void AccessibilityManager::setScreenReaderEnabled(bool enabled) {
    if (m_screenReaderEnabled != enabled) {
        m_screenReaderEnabled = enabled;
        
        if (enabled) {
            // announceText("屏幕阅读器已启用"); // TTS not available
        }
        
        emit screenReaderToggled(enabled);
        saveSettings();
    }
}

void AccessibilityManager::announceText(const QString& text) {
    if (!m_screenReaderEnabled || text.isEmpty()) {
        return;
    }

    // Text-to-speech not available in this setup
    qDebug() << "TTS would announce:" << text;

    /*
    if (m_isAnnouncing) {
        m_announcementQueue.append(text);
    } else {
        m_isAnnouncing = true;
        m_textToSpeech->say(text);
    }
    */
}

void AccessibilityManager::announceAction(const QString& action) {
    if (m_screenReaderEnabled) {
        announceText(QString("执行操作: %1").arg(action));
    }
}

void AccessibilityManager::setTextScaleFactor(double factor) {
    if (m_textScaleFactor != factor && factor >= 0.5 && factor <= 3.0) {
        m_textScaleFactor = factor;
        updateTextScaling();
        emit textScaleChanged(factor);
        saveSettings();
    }
}

void AccessibilityManager::onFocusChanged(QWidget* old, QWidget* now) {
    Q_UNUSED(old)
    
    m_currentFocusWidget = now;
    
    if (m_focusMode != FocusMode::Standard) {
        m_focusTimer->start();
    }
    
    if (m_screenReaderEnabled && now) {
        announceWidgetFocus(now);
    }
    
    if (m_focusSoundEnabled) {
        playFocusSound();
    }
    
    emit focusChanged(now);
}

void AccessibilityManager::onFocusTimer() {
    updateFocusFrame();
}

void AccessibilityManager::updateFocusFrame() {
    if (!m_focusFrame || !m_currentFocusWidget) {
        return;
    }
    
    if (m_focusMode == FocusMode::Standard) {
        m_focusFrame->setVisible(false);
        return;
    }
    
    // Position focus frame around current widget
    QRect widgetRect = m_currentFocusWidget->geometry();
    QWidget* parent = m_currentFocusWidget->parentWidget();
    
    if (parent) {
        QPoint globalPos = parent->mapToGlobal(widgetRect.topLeft());
        QPoint localPos = m_mainWindow->mapFromGlobal(globalPos);
        
        QRect frameRect(localPos.x() - m_focusIndicatorSize,
                       localPos.y() - m_focusIndicatorSize,
                       widgetRect.width() + 2 * m_focusIndicatorSize,
                       widgetRect.height() + 2 * m_focusIndicatorSize);
        
        m_focusFrame->setGeometry(frameRect);
        m_focusFrame->setVisible(true);
        m_focusFrame->raise();
    }
}

void AccessibilityManager::applyHighContrastTheme() {
    // Store original styles
    m_originalStyleSheet = m_mainWindow->styleSheet();
    
    // Apply high contrast theme
    QString highContrastStyle = 
        "QWidget {"
        "    background-color: black;"
        "    color: white;"
        "    font-weight: bold;"
        "}"
        "QPushButton {"
        "    background-color: #333333;"
        "    border: 2px solid white;"
        "    color: white;"
        "    padding: 4px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background-color: #555555;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #777777;"
        "}"
        "QLineEdit, QTextEdit, QComboBox {"
        "    background-color: #222222;"
        "    border: 2px solid white;"
        "    color: white;"
        "    font-weight: bold;"
        "}"
        "QLabel {"
        "    color: white;"
        "    font-weight: bold;"
        "}"
        "QMenuBar {"
        "    background-color: black;"
        "    color: white;"
        "    font-weight: bold;"
        "}"
        "QMenuBar::item:selected {"
        "    background-color: #555555;"
        "}"
        "QMenu {"
        "    background-color: black;"
        "    color: white;"
        "    border: 2px solid white;"
        "}"
        "QMenu::item:selected {"
        "    background-color: #555555;"
        "}";
    
    m_mainWindow->setStyleSheet(highContrastStyle);
}

void AccessibilityManager::applyStandardTheme() {
    m_mainWindow->setStyleSheet(m_originalStyleSheet);
}

void AccessibilityManager::updateTextScaling() {
    // Apply text scaling to the main window and all child widgets
    QFont baseFont = m_mainWindow->font();
    int scaledSize = static_cast<int>(baseFont.pointSize() * m_textScaleFactor);
    baseFont.setPointSize(scaledSize);
    
    m_mainWindow->setFont(baseFont);
    
    // Recursively apply to all child widgets
    QList<QWidget*> widgets = m_mainWindow->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        QFont widgetFont = widget->font();
        int widgetScaledSize = static_cast<int>(widgetFont.pointSize() * m_textScaleFactor);
        widgetFont.setPointSize(widgetScaledSize);
        widget->setFont(widgetFont);
    }
}

void AccessibilityManager::toggleHighContrast() {
    setHighContrastEnabled(!m_highContrastEnabled);
    
    if (m_screenReaderEnabled) {
        announceText(m_highContrastEnabled ? "高对比度模式已启用" : "高对比度模式已禁用");
    }
}

void AccessibilityManager::toggleScreenReader() {
    setScreenReaderEnabled(!m_screenReaderEnabled);
}

void AccessibilityManager::increaseTextSize() {
    setTextScaleFactor(qMin(3.0, m_textScaleFactor + 0.1));
    
    if (m_screenReaderEnabled) {
        announceText(QString("文字大小增加到 %1%").arg(static_cast<int>(m_textScaleFactor * 100)));
    }
}

void AccessibilityManager::decreaseTextSize() {
    setTextScaleFactor(qMax(0.5, m_textScaleFactor - 0.1));
    
    if (m_screenReaderEnabled) {
        announceText(QString("文字大小减少到 %1%").arg(static_cast<int>(m_textScaleFactor * 100)));
    }
}

void AccessibilityManager::resetTextSize() {
    setTextScaleFactor(1.0);

    if (m_screenReaderEnabled) {
        announceText("文字大小已重置为默认");
    }
}

void AccessibilityManager::onApplicationStateChanged(Qt::ApplicationState state) {
    // Handle application state changes for accessibility
    switch (state) {
        case Qt::ApplicationActive:
            // Application became active - restore accessibility features if needed
            if (m_screenReaderEnabled) {
                announceText("应用程序已激活");
            }
            break;
        case Qt::ApplicationInactive:
            // Application became inactive - pause non-essential accessibility features
            break;
        case Qt::ApplicationSuspended:
            // Application suspended - save accessibility state
            saveSettings();
            break;
        case Qt::ApplicationHidden:
            // Application hidden - minimal accessibility processing
            break;
    }
}

void AccessibilityManager::onAnnouncementFinished() {
    // Text-to-speech not available in this setup
    /*
    if (m_textToSpeech && m_textToSpeech->state() == QTextToSpeech::Ready) {
        m_isAnnouncing = false;

        if (!m_announcementQueue.isEmpty()) {
            QString nextText = m_announcementQueue.takeFirst();
            m_isAnnouncing = true;
            m_textToSpeech->say(nextText);
        }
    }
    */
}

QString AccessibilityManager::getWidgetDescription(QWidget* widget) {
    if (!widget) {
        return QString();
    }
    
    QString description;
    
    // Get widget type and text content
    if (auto* button = qobject_cast<QPushButton*>(widget)) {
        description = QString("按钮: %1").arg(button->text());
    } else if (auto* label = qobject_cast<QLabel*>(widget)) {
        description = QString("标签: %1").arg(label->text());
    } else if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        description = QString("输入框: %1").arg(lineEdit->placeholderText());
    } else if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
        description = QString("下拉框: %1").arg(comboBox->currentText());
    } else if (auto* checkBox = qobject_cast<QCheckBox*>(widget)) {
        description = QString("复选框: %1 %2").arg(checkBox->text())
                     .arg(checkBox->isChecked() ? "已选中" : "未选中");
    } else {
        description = QString("控件: %1").arg(widget->metaObject()->className());
    }
    
    // Add tooltip if available
    if (!widget->toolTip().isEmpty()) {
        description += QString(" - %1").arg(widget->toolTip());
    }
    
    return description;
}

void AccessibilityManager::announceWidgetFocus(QWidget* widget) {
    QString description = getWidgetDescription(widget);
    if (!description.isEmpty()) {
        announceText(description);
    }
}

void AccessibilityManager::playFocusSound() {
    // Play a subtle focus sound if enabled
    // This would require a sound file in the resources
    // For now, just log the event
    qDebug() << "Focus sound would play here";
}

void AccessibilityManager::loadSettings() {
    m_currentLevel = static_cast<AccessibilityLevel>(
        m_settings->value("accessibility/level", static_cast<int>(AccessibilityLevel::Standard)).toInt());
    m_focusMode = static_cast<FocusMode>(
        m_settings->value("accessibility/focusMode", static_cast<int>(FocusMode::Standard)).toInt());
    m_highContrastEnabled = m_settings->value("accessibility/highContrast", false).toBool();
    m_screenReaderEnabled = m_settings->value("accessibility/screenReader", false).toBool();
    m_textScaleFactor = m_settings->value("accessibility/textScale", 1.0).toDouble();
    m_focusSoundEnabled = m_settings->value("accessibility/focusSound", false).toBool();
    
    // Apply loaded settings
    setAccessibilityLevel(m_currentLevel);
}

void AccessibilityManager::saveSettings() {
    m_settings->setValue("accessibility/level", static_cast<int>(m_currentLevel));
    m_settings->setValue("accessibility/focusMode", static_cast<int>(m_focusMode));
    m_settings->setValue("accessibility/highContrast", m_highContrastEnabled);
    m_settings->setValue("accessibility/screenReader", m_screenReaderEnabled);
    m_settings->setValue("accessibility/textScale", m_textScaleFactor);
    m_settings->setValue("accessibility/focusSound", m_focusSoundEnabled);
    m_settings->sync();
}

void AccessibilityManager::resetToDefaults() {
    setAccessibilityLevel(AccessibilityLevel::Standard);
    setFocusMode(FocusMode::Standard);
    setHighContrastEnabled(false);
    setScreenReaderEnabled(false);
    setTextScaleFactor(1.0);
    m_focusSoundEnabled = false;
    
    saveSettings();
    
    if (m_screenReaderEnabled) {
        announceText("辅助功能设置已重置为默认");
    }
}

// Placeholder implementations for spatial navigation
void AccessibilityManager::enableSpatialNavigation(bool enabled) {
    m_spatialNavigationEnabled = enabled;
}

void AccessibilityManager::createSpatialNavigationMap() {
    // This would create a spatial map of widgets for arrow key navigation
    // Implementation would analyze widget positions and create navigation relationships
}

void AccessibilityManager::focusNext() {
    // Use QApplication::focusWidget() and find next focusable widget
    QWidget* current = QApplication::focusWidget();
    if (current) {
        QWidget* next = current->nextInFocusChain();
        if (next) {
            next->setFocus();
        }
    }
}

void AccessibilityManager::focusPrevious() {
    // Use QApplication::focusWidget() and find previous focusable widget
    QWidget* current = QApplication::focusWidget();
    if (current) {
        QWidget* prev = current->previousInFocusChain();
        if (prev) {
            prev->setFocus();
        }
    }
}

void AccessibilityManager::focusUp() {
    if (m_spatialNavigationEnabled) {
        QWidget* neighbor = findSpatialNeighbor(m_currentFocusWidget, Qt::Key_Up);
        if (neighbor) {
            neighbor->setFocus();
        }
    }
}

void AccessibilityManager::focusDown() {
    if (m_spatialNavigationEnabled) {
        QWidget* neighbor = findSpatialNeighbor(m_currentFocusWidget, Qt::Key_Down);
        if (neighbor) {
            neighbor->setFocus();
        }
    }
}

void AccessibilityManager::focusLeft() {
    if (m_spatialNavigationEnabled) {
        QWidget* neighbor = findSpatialNeighbor(m_currentFocusWidget, Qt::Key_Left);
        if (neighbor) {
            neighbor->setFocus();
        }
    }
}

void AccessibilityManager::focusRight() {
    if (m_spatialNavigationEnabled) {
        QWidget* neighbor = findSpatialNeighbor(m_currentFocusWidget, Qt::Key_Right);
        if (neighbor) {
            neighbor->setFocus();
        }
    }
}

QWidget* AccessibilityManager::findSpatialNeighbor(QWidget* current, Qt::Key direction) {
    // Placeholder implementation for spatial navigation
    // Would find the nearest widget in the specified direction
    Q_UNUSED(current)
    Q_UNUSED(direction)
    return nullptr;
}
