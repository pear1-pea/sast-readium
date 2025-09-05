#pragma once

#include <QObject>
#include <QWidget>
#include <QApplication>
#include <QKeySequence>
#include <QShortcut>
#include <QAction>
#include <QSettings>
#include <QTimer>
// #include <QTextToSpeech> // Not available in this MSYS2 setup
#include <QAccessible>
#include <QAccessibleWidget>
#include <QFocusFrame>
#include <QStyleOption>

/**
 * Accessibility enhancement levels
 */
enum class AccessibilityLevel {
    Standard,    // Default accessibility
    Enhanced,    // Enhanced keyboard navigation and focus
    HighContrast,// High contrast mode
    ScreenReader // Screen reader optimized
};

/**
 * Focus navigation modes
 */
enum class FocusMode {
    Standard,    // Standard tab navigation
    Enhanced,    // Enhanced with visual indicators
    Spatial,     // Spatial navigation (arrow keys)
    Voice        // Voice-guided navigation
};

/**
 * Manages accessibility features and enhancements
 */
class AccessibilityManager : public QObject {
    Q_OBJECT

public:
    explicit AccessibilityManager(QWidget* mainWindow, QObject* parent = nullptr);
    ~AccessibilityManager() = default;

    // Accessibility level management
    AccessibilityLevel getCurrentLevel() const { return m_currentLevel; }
    void setAccessibilityLevel(AccessibilityLevel level);
    
    // Focus management
    FocusMode getFocusMode() const { return m_focusMode; }
    void setFocusMode(FocusMode mode);
    void enableEnhancedFocus(bool enabled);
    
    // High contrast mode
    bool isHighContrastEnabled() const { return m_highContrastEnabled; }
    void setHighContrastEnabled(bool enabled);
    
    // Screen reader support
    bool isScreenReaderEnabled() const { return m_screenReaderEnabled; }
    void setScreenReaderEnabled(bool enabled);
    void announceText(const QString& text);
    void announceAction(const QString& action);
    
    // Keyboard navigation
    void enableSpatialNavigation(bool enabled);
    void setTabOrder(const QList<QWidget*>& widgets);
    void focusNext();
    void focusPrevious();
    void focusUp();
    void focusDown();
    void focusLeft();
    void focusRight();
    
    // Visual enhancements
    void setFocusIndicatorSize(int size);
    void setFocusIndicatorColor(const QColor& color);
    void enableFocusSound(bool enabled);
    
    // Text scaling
    double getTextScaleFactor() const { return m_textScaleFactor; }
    void setTextScaleFactor(double factor);
    
    // Settings persistence
    void loadSettings();
    void saveSettings();
    void resetToDefaults();

signals:
    void accessibilityLevelChanged(AccessibilityLevel level);
    void focusModeChanged(FocusMode mode);
    void highContrastToggled(bool enabled);
    void screenReaderToggled(bool enabled);
    void textScaleChanged(double factor);
    void focusChanged(QWidget* widget);

public slots:
    void onFocusChanged(QWidget* old, QWidget* now);
    void onApplicationStateChanged(Qt::ApplicationState state);
    void toggleHighContrast();
    void toggleScreenReader();
    void increaseTextSize();
    void decreaseTextSize();
    void resetTextSize();

private slots:
    void onFocusTimer();
    void onAnnouncementFinished();

private:
    void setupKeyboardShortcuts();
    void setupFocusFrame();
    void updateFocusFrame();
    void applyHighContrastTheme();
    void applyStandardTheme();
    void updateTextScaling();
    void installAccessibilityInterface();
    void createSpatialNavigationMap();
    QWidget* findSpatialNeighbor(QWidget* current, Qt::Key direction);
    void playFocusSound();
    QString getWidgetDescription(QWidget* widget);
    void announceWidgetFocus(QWidget* widget);

    // Core components
    QWidget* m_mainWindow;
    QSettings* m_settings;
    
    // Accessibility state
    AccessibilityLevel m_currentLevel;
    FocusMode m_focusMode;
    bool m_highContrastEnabled;
    bool m_screenReaderEnabled;
    bool m_spatialNavigationEnabled;
    bool m_focusSoundEnabled;
    double m_textScaleFactor;
    
    // Focus management
    QFocusFrame* m_focusFrame;
    QTimer* m_focusTimer;
    QWidget* m_currentFocusWidget;
    QColor m_focusIndicatorColor;
    int m_focusIndicatorSize;
    
    // Screen reader (QTextToSpeech not available in this setup)
    // QTextToSpeech* m_textToSpeech;
    QStringList m_announcementQueue;
    bool m_isAnnouncing;
    
    // Keyboard shortcuts
    QShortcut* m_toggleHighContrastShortcut;
    QShortcut* m_toggleScreenReaderShortcut;
    QShortcut* m_increaseTextShortcut;
    QShortcut* m_decreaseTextShortcut;
    QShortcut* m_resetTextShortcut;
    QShortcut* m_spatialUpShortcut;
    QShortcut* m_spatialDownShortcut;
    QShortcut* m_spatialLeftShortcut;
    QShortcut* m_spatialRightShortcut;
    
    // Spatial navigation
    QMap<QWidget*, QMap<Qt::Key, QWidget*>> m_spatialMap;
    
    // Theme storage
    QString m_originalStyleSheet;
    QMap<QWidget*, QString> m_originalWidgetStyles;
};
