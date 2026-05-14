#pragma once

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QWidget>

#include <functional>

class QMainWindow;

#include "common/Theme.h"

class StyleManager : public QObject {
    Q_OBJECT

public:
    static StyleManager& instance();

    // Theme management
    void setTheme(Theme theme);
    void toggleTheme();
    void setLightTheme();
    void setDarkTheme();
    Theme currentTheme() const { return m_currentTheme; }

    // Unified theme application
    void applyThemeStyleSheet(const QString& styleSheet);
    void forceApplyTheme(QWidget* widget, const QString& styleSheet);

    // Stylesheet getters
    QString getApplicationStyleSheet() const;
    QString getToolbarStyleSheet() const;
    QString getStatusBarStyleSheet() const;
    QString getPDFViewerStyleSheet() const;
    QString getButtonStyleSheet() const;
    QString getScrollBarStyleSheet() const;
    QString getSpinBoxStyleSheet() const;
    QString getComboBoxStyleSheet() const;
    QString getLineEditStyleSheet() const;
    QString getSliderStyleSheet() const;

    // Factory: create themed widgets (auto-register for theme switching)
    QSpinBox* createSpinBox(QWidget* parent = nullptr);
    QComboBox* createComboBox(QWidget* parent = nullptr);
    QSlider* createSlider(Qt::Orientation orientation,
                          QWidget* parent = nullptr);

    // Color getters
    QColor primaryColor() const;
    QColor secondaryColor() const;
    QColor backgroundColor() const;
    QColor surfaceColor() const;
    QColor textColor() const;
    QColor textSecondaryColor() const;
    QColor borderColor() const;
    QColor hoverColor() const;
    QColor pressedColor() const;
    QColor accentColor() const;

    // Font getters
    QFont defaultFont() const;
    QFont titleFont() const;
    QFont buttonFont() const;

    // Size constants
    int buttonHeight() const { return 32; }
    int buttonMinWidth() const { return 80; }
    int iconSize() const { return 16; }
    int spacing() const { return 8; }
    int margin() const { return 12; }
    int borderRadius() const { return 6; }

signals:
    void themeChanged(Theme theme);
    void styleSheetApplied();

private:
    StyleManager();
    ~StyleManager() = default;
    StyleManager(const StyleManager&) = delete;
    StyleManager& operator=(const StyleManager&) = delete;

    void updateColors();
    QString createButtonStyle() const;
    QString createScrollBarStyle() const;

    // Widget registration for auto re-theming
    struct WidgetEntry {
        QPointer<QWidget> widget;
        std::function<QString()> styleSheetFn;
    };
    void registerWidget(QWidget* widget, std::function<QString()> styleSheetFn);
    void reThemeAll();

    Theme m_currentTheme;
    QList<WidgetEntry> m_registeredWidgets;

    // Color definitions
    QColor m_primaryColor;
    QColor m_secondaryColor;
    QColor m_backgroundColor;
    QColor m_surfaceColor;
    QColor m_textColor;
    QColor m_textSecondaryColor;
    QColor m_borderColor;
    QColor m_hoverColor;
    QColor m_pressedColor;
    QColor m_accentColor;
};

// Convenience macro
#define STYLE StyleManager::instance()
