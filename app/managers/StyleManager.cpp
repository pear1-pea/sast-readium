#include "StyleManager.h"
#include <QApplication>
#include <QFontDatabase>
#include <QWidget>
#include "utils/Logger.h"

StyleManager& StyleManager::instance() {
    static StyleManager instance;
    return instance;
}

StyleManager::StyleManager() : m_currentTheme(Theme::Light) {
    Logger::instance().info(
        "[managers] StyleManager initialized with Light theme");
    updateColors();

    // Auto-retheme all registered widgets on theme change
    connect(this, &StyleManager::themeChanged, this, &StyleManager::reThemeAll);
}

void StyleManager::setTheme(Theme theme) {
    if (m_currentTheme != theme) {
        Logger::instance().info("[managers] Changing theme from {} to {}",
                                static_cast<int>(m_currentTheme),
                                static_cast<int>(theme));
        m_currentTheme = theme;
        updateColors();
        emit themeChanged(theme);
        Logger::instance().debug(
            "[managers] Theme change completed and signal emitted");
    }
}

void StyleManager::toggleTheme() {
    Theme newTheme =
        (m_currentTheme == Theme::Light) ? Theme::Dark : Theme::Light;
    setTheme(newTheme);
}

void StyleManager::setLightTheme() { setTheme(Theme::Light); }

void StyleManager::setDarkTheme() { setTheme(Theme::Dark); }

void StyleManager::updateColors() {
    Logger::instance().debug("[managers] Updating colors for theme: {}",
                             m_currentTheme == Theme::Light ? "Light" : "Dark");
    if (m_currentTheme == Theme::Light) {
        // Light theme colors
        m_primaryColor = QColor(0, 120, 212);       // Blue
        m_secondaryColor = QColor(96, 94, 92);      // Gray
        m_backgroundColor = QColor(255, 255, 255);  // White
        m_surfaceColor = QColor(250, 250, 250);     // Light gray
        m_textColor = QColor(32, 31, 30);           // Dark gray
        m_textSecondaryColor = QColor(96, 94, 92);  // Medium gray
        m_borderColor = QColor(225, 223, 221);      // Border gray
        m_hoverColor = QColor(243, 242, 241);       // Hover gray
        m_pressedColor = QColor(237, 235, 233);     // Pressed gray
        m_accentColor = QColor(16, 110, 190);       // Accent blue
    } else {
        // Dark theme colors
        m_primaryColor = QColor(96, 205, 255);         // Bright blue
        m_secondaryColor = QColor(152, 151, 149);      // Light gray
        m_backgroundColor = QColor(32, 31, 30);        // Dark gray
        m_surfaceColor = QColor(40, 39, 38);           // Surface gray
        m_textColor = QColor(255, 255, 255);           // White
        m_textSecondaryColor = QColor(200, 198, 196);  // Light gray
        m_borderColor = QColor(72, 70, 68);            // Border dark gray
        m_hoverColor = QColor(50, 49, 48);             // Hover dark gray
        m_pressedColor = QColor(60, 58, 56);           // Pressed dark gray
        m_accentColor = QColor(118, 185, 237);         // Accent bright blue
    }
}

QString StyleManager::getApplicationStyleSheet() const {
    return QString(R"(
        QMainWindow {
            background-color: %1;
            color: %2;
        }
        QWidget {
            background-color: %1;
            color: %2;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: 9pt;
        }
        QGroupBox {
            font-weight: bold;
            border: 1px solid %3;
            border-radius: %4px;
            margin-top: 8px;
            padding-top: 4px;
            background-color: %5;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px 0 4px;
            color: %6;
        }
    )")
        .arg(backgroundColor().name())
        .arg(textColor().name())
        .arg(borderColor().name())
        .arg(borderRadius())
        .arg(surfaceColor().name())
        .arg(textSecondaryColor().name());
}

QString StyleManager::getToolbarStyleSheet() const {
    return QString(R"(
        QWidget#toolbar {
            background-color: %1;
            border-bottom: 1px solid %2;
            padding: %3px;
        }
    )")
        .arg(surfaceColor().name())
        .arg(borderColor().name())
        .arg(spacing());
}

QString StyleManager::getButtonStyleSheet() const {
    return createButtonStyle();
}

QString StyleManager::applyStyleTokens(
    QString style, const QVector<QPair<QString, QString>>& tokens) const {
    for (const auto& token : tokens) {
        style.replace(token.first, token.second);
    }

    for (const auto& token : tokens) {
        Q_ASSERT_X(!style.contains(token.first),
                   "StyleManager::applyStyleTokens",
                   "QSS template still contains an unreplaced style token");
    }

    return style;
}

QString StyleManager::createButtonStyle() const {
    return applyStyleTokens(
        QString(R"(
        QPushButton {
            background-color: @surface@;
            border: 1px solid @border@;
            border-radius: @radius@px;
            color: @text@;
            font-weight: 500;
            padding: 6px 12px;
        }
        QPushButton:hover {
            background-color: @hover@;
            border-color: @accent@;
        }
        QPushButton:pressed {
            background-color: @pressed@;
            border-color: @accent@;
        }
        QPushButton:disabled {
            background-color: @surface@;
            border-color: @border@;
            color: @textSecondary@;
        }
        QPushButton:focus {
            border: 2px solid @accent@;
        }
    )"),
        {{QStringLiteral("@surface@"), surfaceColor().name()},
         {QStringLiteral("@border@"), borderColor().name()},
         {QStringLiteral("@radius@"), QString::number(borderRadius())},
         {QStringLiteral("@text@"), textColor().name()},
         {QStringLiteral("@hover@"), hoverColor().name()},
         {QStringLiteral("@accent@"), accentColor().name()},
         {QStringLiteral("@pressed@"), pressedColor().name()},
         {QStringLiteral("@textSecondary@"), textSecondaryColor().name()}});
}

QColor StyleManager::primaryColor() const { return m_primaryColor; }
QColor StyleManager::secondaryColor() const { return m_secondaryColor; }
QColor StyleManager::backgroundColor() const { return m_backgroundColor; }
QColor StyleManager::surfaceColor() const { return m_surfaceColor; }
QColor StyleManager::textColor() const { return m_textColor; }
QColor StyleManager::textSecondaryColor() const { return m_textSecondaryColor; }
QColor StyleManager::borderColor() const { return m_borderColor; }
QColor StyleManager::hoverColor() const { return m_hoverColor; }
QColor StyleManager::pressedColor() const { return m_pressedColor; }
QColor StyleManager::accentColor() const { return m_accentColor; }

QFont StyleManager::defaultFont() const {
    QFont font("Segoe UI", 9);
    return font;
}

QFont StyleManager::titleFont() const {
    QFont font("Segoe UI", 10);
    font.setBold(true);
    return font;
}

QFont StyleManager::buttonFont() const {
    QFont font("Segoe UI", 9);
    font.setWeight(QFont::Medium);
    return font;
}

QString StyleManager::getSpinBoxStyleSheet() const {
    return QString(R"(
        QSpinBox {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 3px;
            color: %3;
            padding: 2px 4px;
            selection-background-color: %4;
        }
        QSpinBox:hover {
            border-color: %5;
        }
        QSpinBox:focus {
            border: 2px solid %4;
        }
        QSpinBox:disabled {
            background-color: %6;
            color: %7;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            background-color: %1;
            border: none;
            width: 16px;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: %8;
        }
        QSpinBox::up-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-bottom: 4px solid %3;
            width: 0;
            height: 0;
        }
        QSpinBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 4px solid %3;
            width: 0;
            height: 0;
        }
    )")
        .arg(surfaceColor().name())
        .arg(borderColor().name())
        .arg(textColor().name())
        .arg(accentColor().name())
        .arg(primaryColor().name())
        .arg(backgroundColor().name())
        .arg(textSecondaryColor().name())
        .arg(hoverColor().name());
}

QString StyleManager::getComboBoxStyleSheet() const {
    return QString(R"(
        QComboBox {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 3px;
            color: %3;
            padding: 2px 4px 2px 8px;
            selection-background-color: %4;
        }
        QComboBox:hover {
            border-color: %5;
        }
        QComboBox:focus {
            border: 2px solid %4;
        }
        QComboBox:disabled {
            background-color: %6;
            color: %7;
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 4px solid %3;
            width: 0;
            height: 0;
        }
        QComboBox QAbstractItemView {
            background-color: %1;
            border: 1px solid %2;
            color: %3;
            selection-background-color: %4;
            selection-color: %8;
        }
    )")
        .arg(surfaceColor().name())
        .arg(borderColor().name())
        .arg(textColor().name())
        .arg(accentColor().name())
        .arg(primaryColor().name())
        .arg(backgroundColor().name())
        .arg(textSecondaryColor().name())
        .arg(surfaceColor().name());
}

QString StyleManager::getLineEditStyleSheet() const {
    return QString(R"(
        QLineEdit {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 3px;
            color: %3;
            padding: 2px 4px;
            selection-background-color: %4;
        }
        QLineEdit:hover {
            border-color: %5;
        }
        QLineEdit:focus {
            border: 2px solid %4;
        }
        QLineEdit:disabled {
            background-color: %6;
            color: %7;
        }
    )")
        .arg(surfaceColor().name())
        .arg(borderColor().name())
        .arg(textColor().name())
        .arg(accentColor().name())
        .arg(primaryColor().name())
        .arg(backgroundColor().name())
        .arg(textSecondaryColor().name());
}

QString StyleManager::getSliderStyleSheet() const {
    return QString(R"(
        QSlider::groove:horizontal {
            background-color: %1;
            border: 1px solid %2;
            height: 4px;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background-color: %3;
            border: 1px solid %4;
            width: 14px;
            height: 14px;
            margin: -6px 0;
            border-radius: 7px;
        }
        QSlider::handle:horizontal:hover {
            background-color: %5;
        }
        QSlider::handle:horizontal:pressed {
            background-color: %6;
        }
        QSlider::sub-page:horizontal {
            background-color: %3;
            border-radius: 2px;
        }
    )")
        .arg(backgroundColor().name())
        .arg(borderColor().name())
        .arg(accentColor().name())
        .arg(primaryColor().name())
        .arg(hoverColor().name())
        .arg(pressedColor().name());
}

QString StyleManager::getStatusBarStyleSheet() const {
    return QString(R"(
        QStatusBar {
            background-color: %1;
            border-top: 1px solid %2;
            color: %3;
            padding: 4px;
        }
        QStatusBar::item {
            border: none;
        }
        QStatusBar QLabel {
            color: %4;
            padding: 2px 8px;
        }
        QStatusBar QLineEdit {
            background-color: %5;
            border: 1px solid %2;
            border-radius: 3px;
            padding: 2px 6px;
            color: %3;
        }
        QStatusBar QLineEdit:focus {
            border-color: %6;
        }
    )")
        .arg(surfaceColor().name())
        .arg(borderColor().name())
        .arg(textColor().name())
        .arg(textSecondaryColor().name())
        .arg(backgroundColor().name())
        .arg(accentColor().name());
}

QString StyleManager::getPDFViewerStyleSheet() const {
    // Set PDF viewer background based on current theme
    QColor pdfBackgroundColor =
        (m_currentTheme == Theme::Light)
            ? QColor(245, 245, 245)  // Light gray for light theme
            : QColor(30, 30, 30);    // Dark gray for dark theme

    QColor pageBackgroundColor =
        (m_currentTheme == Theme::Light)
            ? QColor(255, 255, 255)  // White page in light theme
            : QColor(45, 45, 48);    // Dark page in dark theme

    return QString(R"(
        QScrollArea#singlePageScrollArea {
            background-color: %1;
            border: none;
        }
        QScrollArea#continuousScrollArea {
            background-color: %1;
            border: none;
        }
        QScrollArea > QWidget > QWidget {
            background-color: %1;
        }
        QLabel#pdfPage {
            background-color: %2;
            border: 1px solid %3;
            border-radius: 4px;
            margin: 8px;
        }
        PDFPageWidget {
            background-color: %2;
            border: none;
        }
    )")
        .arg(pdfBackgroundColor.name())
        .arg(pageBackgroundColor.name())
        .arg(borderColor().name());
}

QString StyleManager::getScrollBarStyleSheet() const {
    return createScrollBarStyle();
}

void StyleManager::applyThemeStyleSheet(const QString& styleSheet) {
    // Find the main window and apply the stylesheet
    QWidget* mainWindow = nullptr;

    // Try active window first
    mainWindow = QApplication::activeWindow();

    // Fall back to first QMainWindow among top-level widgets
    if (!mainWindow) {
        QWidgetList topLevelWidgets = QApplication::topLevelWidgets();
        for (QWidget* widget : topLevelWidgets) {
            if (widget && widget->inherits("QMainWindow")) {
                mainWindow = widget;
                break;
            }
        }
    }

    // Last resort: any top-level widget
    if (!mainWindow) {
        QWidgetList topLevelWidgets = QApplication::topLevelWidgets();
        if (!topLevelWidgets.isEmpty()) {
            mainWindow = topLevelWidgets.first();
        }
    }

    if (mainWindow) {
        mainWindow->setStyleSheet(styleSheet);
        Logger::instance().debug(
            "[StyleManager] Applied theme stylesheet to window: {}, stylesheet "
            "length: {}",
            mainWindow->metaObject()->className(), styleSheet.length());
    } else {
        Logger::instance().warning(
            "[StyleManager] No main window found to apply stylesheet");
    }

    emit styleSheetApplied();
}

void StyleManager::forceApplyTheme(QWidget* widget, const QString& styleSheet) {
    if (widget) {
        widget->setStyleSheet(styleSheet);
        Logger::instance().debug(
            "[StyleManager] Force applied theme to widget: {}",
            widget->metaObject()->className());
        emit styleSheetApplied();
    } else {
        Logger::instance().warning(
            "[StyleManager] Cannot force apply theme to null widget");
    }
}

void StyleManager::registerWidget(QWidget* widget,
                                  std::function<QString()> styleSheetFn) {
    // Apply initial stylesheet
    widget->setStyleSheet(styleSheetFn());

    // Track for future theme changes (QPointer handles widget deletion)
    m_registeredWidgets.append({QPointer<QWidget>(widget), styleSheetFn});
}

void StyleManager::reThemeAll() {
    Logger::instance().debug("[StyleManager] Re-theming {} registered widgets",
                             m_registeredWidgets.size());

    // Iterate in reverse so removal doesn't affect index
    for (int i = m_registeredWidgets.size() - 1; i >= 0; --i) {
        auto& entry = m_registeredWidgets[i];
        if (entry.widget) {
            entry.widget->setStyleSheet(entry.styleSheetFn());
        } else {
            // Widget was deleted, remove from tracking
            m_registeredWidgets.removeAt(i);
        }
    }
}

QSpinBox* StyleManager::createSpinBox(QWidget* parent) {
    QSpinBox* spinBox = new QSpinBox(parent);
    registerWidget(spinBox, [this]() { return getSpinBoxStyleSheet(); });
    return spinBox;
}

QComboBox* StyleManager::createComboBox(QWidget* parent) {
    QComboBox* comboBox = new QComboBox(parent);
    registerWidget(comboBox, [this]() { return getComboBoxStyleSheet(); });
    return comboBox;
}

QSlider* StyleManager::createSlider(Qt::Orientation orientation,
                                    QWidget* parent) {
    QSlider* slider = new QSlider(orientation, parent);
    registerWidget(slider, [this]() { return getSliderStyleSheet(); });
    return slider;
}

QString StyleManager::createScrollBarStyle() const {
    return QString(R"(
        QScrollBar:vertical {
            background-color: %1;
            width: 12px;
            border: none;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical {
            background-color: %2;
            border-radius: 6px;
            min-height: 20px;
            margin: 0px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: %3;
        }
        QScrollBar::handle:vertical:pressed {
            background-color: %4;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar:horizontal {
            background-color: %1;
            height: 12px;
            border: none;
            border-radius: 6px;
        }
        QScrollBar::handle:horizontal {
            background-color: %2;
            border-radius: 6px;
            min-width: 20px;
            margin: 0px;
        }
        QScrollBar::handle:horizontal:hover {
            background-color: %3;
        }
        QScrollBar::handle:horizontal:pressed {
            background-color: %4;
        }
        QScrollBar::add-line:horizontal,
        QScrollBar::sub-line:horizontal {
            width: 0px;
        }
    )")
        .arg(surfaceColor().name())
        .arg(borderColor().name())
        .arg(textSecondaryColor().name())
        .arg(secondaryColor().name());
}
