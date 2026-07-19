#include "ToolBar.h"
#include <QAction>
#include <QHBoxLayout>
#include <QWidget>
#include "../../managers/ShortcutManager.h"
#include "../../managers/StyleManager.h"

ToolBar::ToolBar(QWidget* parent)
    : QToolBar(parent), m_shortcutManager(&ShortcutManager::instance()) {
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
    // Open file
    openAction = new QAction("📁", this);
    applyShortcut(openAction, ActionMap::openFile, "打开PDF文件");
    addAction(openAction);

    // Open folder
    openFolderAction = new QAction("📂", this);
    applyShortcut(openFolderAction, ActionMap::openFolder, "打开文件夹");
    addAction(openFolderAction);

    // Save file
    saveAction = new QAction("💾", this);
    applyShortcut(saveAction, ActionMap::save, "保存文件");
    addAction(saveAction);

    connect(openAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::openFile); });
    connect(openFolderAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::openFolder); });
    connect(saveAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::save); });
}

void ToolBar::setupViewActions() {
    // Sidebar toggle
    toggleSidebarAction = new QAction("📋", this);
    applyShortcut(toggleSidebarAction, ActionMap::toggleSideBar, "切换侧边栏");
    toggleSidebarAction->setCheckable(true);
    toggleSidebarAction->setChecked(true);
    addAction(toggleSidebarAction);

    // View mode selector - wrapped in QWidgetAction for the toolbar
    viewModeCombo = STYLE.createComboBox(this);
    viewModeCombo->addItem("单页视图");
    viewModeCombo->addItem("连续滚动");
    viewModeCombo->setCurrentIndex(0);
    viewModeCombo->setToolTip("选择视图模式");
    viewModeCombo->setFixedWidth(100);

    QWidgetAction* viewAction = new QWidgetAction(this);
    viewAction->setDefaultWidget(viewModeCombo);
    addAction(viewAction);

    connect(toggleSidebarAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::toggleSideBar); });
    connect(viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolBar::onViewModeChanged);
}

void ToolBar::setupRotationActions() {
    // Rotate left
    rotateLeftAction = new QAction("↺", this);
    applyShortcut(rotateLeftAction, ActionMap::rotateLeft, "向左旋转90度");
    addAction(rotateLeftAction);

    // Rotate right
    rotateRightAction = new QAction("↻", this);
    applyShortcut(rotateRightAction, ActionMap::rotateRight, "向右旋转90度");
    addAction(rotateRightAction);

    connect(rotateLeftAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::rotateLeft); });
    connect(rotateRightAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::rotateRight); });
}

void ToolBar::setupThemeActions() {
    // Theme toggle
    themeToggleAction = new QAction("🌙", this);
    applyShortcut(themeToggleAction, ActionMap::toggleTheme, "切换主题");
    addAction(themeToggleAction);

    connect(themeToggleAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::toggleTheme); });
}

void ToolBar::createSeparator() { addSeparator(); }

void ToolBar::applyShortcut(QAction* action, ActionMap actionId,
                            const QString& tooltipPrefix) {
    const QList<QKeySequence> shortcuts =
        m_shortcutManager ? m_shortcutManager->shortcutsFor(actionId)
                          : QList<QKeySequence>{};
    if (!shortcuts.isEmpty()) {
        action->setToolTip(QString("%1 (%2)").arg(
            tooltipPrefix, shortcuts.first().toString()));
        return;
    }
    action->setToolTip(tooltipPrefix);
}

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

void ToolBar::setSidebarChecked(bool checked) {
    toggleSidebarAction->setChecked(checked);
}

void ToolBar::setViewModeIndex(int mode) {
    if (mode < 0 || mode >= viewModeCombo->count()) {
        return;
    }
    const QSignalBlocker blocker(viewModeCombo);
    viewModeCombo->setCurrentIndex(mode);
}

void ToolBar::onViewModeChanged() {
    int mode = viewModeCombo->currentIndex();
    if (mode == 0) {
        emit actionTriggered(ActionMap::setSinglePageMode);
    } else if (mode == 1) {
        emit actionTriggered(ActionMap::setContinuousScrollMode);
    }
}
