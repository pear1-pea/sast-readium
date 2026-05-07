#include "ToolBar.h"
#include <QAction>
#include <QHBoxLayout>
#include <QWidget>
#include "../../managers/StyleManager.h"

ToolBar::ToolBar(QWidget* parent) : QToolBar(parent) {
    setMovable(true);
    setObjectName("MainToolBar");
    setToolButtonStyle(Qt::ToolButtonIconOnly);

    // Initialize all controls
    setupFileActions();
    createSeparator();
    setupViewActions();
    createSeparator();
    setupRotationActions();
    createSeparator();
    setupThemeActions();

    // Apply initial style
    applyToolBarStyle();

    // Listen to theme changes and reapply styles
    connect(&STYLE, &StyleManager::themeChanged, this,
            &ToolBar::applyToolBarStyle);

    // Initial state: disable all actions when no document
    setActionsEnabled(false);
}

void ToolBar::setupFileActions() {
    // 打开文件
    openAction = new QAction("📁", this);
    openAction->setToolTip("打开PDF文件 (Ctrl+O)");
    openAction->setShortcut(QKeySequence("Ctrl+O"));
    addAction(openAction);

    // 打开文件夹
    openFolderAction = new QAction("📂", this);
    openFolderAction->setToolTip("打开文件夹 (Ctrl+Shift+O)");
    openFolderAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
    addAction(openFolderAction);

    // 保存文件
    saveAction = new QAction("💾", this);
    saveAction->setToolTip("保存文件 (Ctrl+S)");
    saveAction->setShortcut(QKeySequence("Ctrl+S"));
    addAction(saveAction);

    // 连接信号
    connect(openAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::openFile); });
    connect(openFolderAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::openFolder); });
    connect(saveAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::save); });
}

void ToolBar::setupViewActions() {
    // 侧边栏切换
    toggleSidebarAction = new QAction("📋", this);
    toggleSidebarAction->setToolTip("切换侧边栏 (F9)");
    toggleSidebarAction->setCheckable(true);
    toggleSidebarAction->setChecked(true);
    addAction(toggleSidebarAction);

    // 视图模式选择 - 使用 QWidgetAction
    viewModeCombo = new QComboBox(this);
    viewModeCombo->addItem("单页视图");
    viewModeCombo->addItem("连续滚动");
    viewModeCombo->setCurrentIndex(0);
    viewModeCombo->setToolTip("选择视图模式");
    viewModeCombo->setFixedWidth(100);
    viewModeCombo->setStyleSheet(STYLE.getComboBoxStyleSheet());

    QWidgetAction* viewAction = new QWidgetAction(this);
    viewAction->setDefaultWidget(viewModeCombo);
    addAction(viewAction);

    // 连接信号
    connect(toggleSidebarAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::toggleSideBar); });
    connect(viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolBar::onViewModeChanged);
}

void ToolBar::setupRotationActions() {
    // 向左旋转
    rotateLeftAction = new QAction("↺", this);
    rotateLeftAction->setToolTip("向左旋转90度 (Ctrl+L)");
    addAction(rotateLeftAction);

    // 向右旋转
    rotateRightAction = new QAction("↻", this);
    rotateRightAction->setToolTip("向右旋转90度 (Ctrl+R)");
    addAction(rotateRightAction);

    // 连接信号
    connect(rotateLeftAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::rotateLeft); });
    connect(rotateRightAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::rotateRight); });
}

void ToolBar::setupThemeActions() {
    // 主题切换
    themeToggleAction = new QAction("🌙", this);
    themeToggleAction->setToolTip("切换主题 (Ctrl+Shift+T)");
    addAction(themeToggleAction);

    // 连接信号
    connect(themeToggleAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::toggleTheme); });
}

void ToolBar::createSeparator() { addSeparator(); }

void ToolBar::applyToolBarStyle() {
    // Apply toolbar style
    setStyleSheet(STYLE.getToolbarStyleSheet());

    // Apply button style to tool buttons only
    QList<QAction*> actions = this->actions();
    for (QAction* action : actions) {
        if (!action->isSeparator()) {
            QWidget* widget = widgetForAction(action);
            if (widget) {
                // Only apply button style to QToolButton, skip other widgets
                if (qobject_cast<QToolButton*>(widget)) {
                    widget->setStyleSheet(STYLE.getButtonStyleSheet());
                }
            }
        }
    }

    // Reapply styles to themed controls
    if (viewModeCombo) {
        viewModeCombo->setStyleSheet(STYLE.getComboBoxStyleSheet());
    }
}

void ToolBar::setActionsEnabled(bool enabled) {
    // File operations always available
    openAction->setEnabled(true);
    openFolderAction->setEnabled(true);
    saveAction->setEnabled(enabled);

    // Document-related operations only available when document is loaded
    viewModeCombo->setEnabled(enabled);

    rotateLeftAction->setEnabled(enabled);
    rotateRightAction->setEnabled(enabled);

    // Sidebar and theme toggle always available
    toggleSidebarAction->setEnabled(true);
    themeToggleAction->setEnabled(true);
}

void ToolBar::onViewModeChanged() {
    int mode = viewModeCombo->currentIndex();
    if (mode == 0) {
        emit actionTriggered(ActionMap::setSinglePageMode);
    } else if (mode == 1) {
        emit actionTriggered(ActionMap::setContinuousScrollMode);
    }
}
