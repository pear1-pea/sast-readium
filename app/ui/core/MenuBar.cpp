#include "MenuBar.h"
#include <QAction>
#include <QActionGroup>
#include <QDebug>
#include <QFileInfo>
#include <QMenu>
#include "managers/ShortcutManager.h"
#include "managers/StyleManager.h"

MenuBar::MenuBar(RecentFilesManager* recentFilesManager,
                 ShortcutManager* shortcutManager, QWidget* parent)
    : QMenuBar(parent),
      m_recentFilesManager(recentFilesManager),
      m_shortcutManager(shortcutManager),
      m_recentFilesMenu(nullptr),
      m_clearRecentFilesAction(nullptr) {
    createFileMenu();
    createTabMenu();
    createViewMenu();
    createThemeMenu();

    // 立即设置最近文件菜单连接
    if (m_recentFilesManager) {
        connect(m_recentFilesManager, &RecentFilesManager::recentFilesChanged,
                this, &MenuBar::updateRecentFilesMenu);
        updateRecentFilesMenu();
    }
}

void MenuBar::createFileMenu() {
    QMenu* fileMenu = new QMenu(tr("文件(F)"), this);
    addMenu(fileMenu);

    QAction* openAction = shortcutAction(ActionMap::openFile);
    QAction* openFolderAction = shortcutAction(ActionMap::openFolder);
    QAction* saveAction = shortcutAction(ActionMap::save);
    QAction* saveAsAction = shortcutAction(ActionMap::saveAs);
    QAction* documentPropertiesAction =
        shortcutAction(ActionMap::showDocumentMetadata);

    QAction* exitAction = new QAction(tr("退出"), this);
    exitAction->setShortcut(QKeySequence("Ctrl+Q"));

    fileMenu->addAction(openAction);
    fileMenu->addAction(openFolderAction);
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    fileMenu->addSeparator();

    // 添加最近文件菜单
    setupRecentFilesMenu();
    fileMenu->addMenu(m_recentFilesMenu);
    fileMenu->addSeparator();

    fileMenu->addAction(documentPropertiesAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);
}

void MenuBar::createTabMenu() {
    QMenu* tabMenu = new QMenu(tr("标签页(T)"), this);
    addMenu(tabMenu);

    QAction* newTabAction = shortcutAction(ActionMap::newTab);
    QAction* closeTabAction = shortcutAction(ActionMap::closeCurrentTab);
    QAction* closeAllTabsAction = shortcutAction(ActionMap::closeAllTabs);
    QAction* nextTabAction = shortcutAction(ActionMap::nextTab);
    QAction* prevTabAction = shortcutAction(ActionMap::prevTab);

    tabMenu->addAction(newTabAction);
    tabMenu->addSeparator();
    tabMenu->addAction(closeTabAction);
    tabMenu->addAction(closeAllTabsAction);
    tabMenu->addSeparator();
    tabMenu->addAction(nextTabAction);
    tabMenu->addAction(prevTabAction);
}

void MenuBar::createViewMenu() {
    QMenu* viewMenu = new QMenu(tr("视图(V)"), this);
    addMenu(viewMenu);

    // 欢迎界面控制
    m_welcomeScreenToggleAction = new QAction(tr("显示欢迎界面"), this);
    m_welcomeScreenToggleAction->setCheckable(true);
    m_welcomeScreenToggleAction->setChecked(true);  // 默认启用
    m_welcomeScreenToggleAction->setToolTip(tr("切换欢迎界面的显示"));

    // 侧边栏控制
    QAction* toggleSideBarAction = shortcutAction(ActionMap::toggleSideBar);
    toggleSideBarAction->setCheckable(true);
    toggleSideBarAction->setChecked(true);  // 默认显示

    QAction* showSideBarAction = new QAction(tr("显示侧边栏"), this);
    QAction* hideSideBarAction = new QAction(tr("隐藏侧边栏"), this);

    // 查看模式控制
    QAction* singlePageAction = shortcutAction(ActionMap::setSinglePageMode);
    singlePageAction->setCheckable(true);
    singlePageAction->setChecked(true);  // 默认单页视图

    QAction* continuousScrollAction =
        shortcutAction(ActionMap::setContinuousScrollMode);
    continuousScrollAction->setCheckable(true);

    // 创建查看模式动作组
    QActionGroup* viewModeGroup = new QActionGroup(this);
    viewModeGroup->addAction(singlePageAction);
    viewModeGroup->addAction(continuousScrollAction);

    // 视图控制
    QAction* fullScreenAction = shortcutAction(ActionMap::fullScreen);

    QAction* zoomInAction = shortcutAction(ActionMap::zoomIn);
    QAction* zoomOutAction = shortcutAction(ActionMap::zoomOut);

    // 调试面板控制
    m_debugPanelToggleAction = new QAction(tr("显示调试面板"), this);
    m_debugPanelToggleAction->setShortcut(QKeySequence("F12"));
    m_debugPanelToggleAction->setCheckable(true);
    m_debugPanelToggleAction->setChecked(true);  // 默认显示
    m_debugPanelToggleAction->setToolTip(tr("切换调试日志面板的显示"));

    m_debugPanelClearAction = new QAction(tr("清空调试日志"), this);
    m_debugPanelClearAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
    m_debugPanelClearAction->setToolTip(tr("清空调试面板中的所有日志"));

    m_debugPanelExportAction = new QAction(tr("导出调试日志"), this);
    m_debugPanelExportAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    m_debugPanelExportAction->setToolTip(tr("将调试日志导出到文件"));

    // 添加到菜单
    viewMenu->addAction(m_welcomeScreenToggleAction);
    viewMenu->addSeparator();
    viewMenu->addAction(toggleSideBarAction);
    viewMenu->addAction(showSideBarAction);
    viewMenu->addAction(hideSideBarAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_debugPanelToggleAction);
    viewMenu->addAction(m_debugPanelClearAction);
    viewMenu->addAction(m_debugPanelExportAction);
    viewMenu->addSeparator();
    viewMenu->addAction(singlePageAction);
    viewMenu->addAction(continuousScrollAction);
    viewMenu->addSeparator();
    viewMenu->addAction(fullScreenAction);
    viewMenu->addSeparator();
    viewMenu->addAction(zoomInAction);
    viewMenu->addAction(zoomOutAction);

    // 连接信号
    connect(m_welcomeScreenToggleAction, &QAction::triggered, this,
            [this]() { emit welcomeScreenToggleRequested(); });

    connect(showSideBarAction, &QAction::triggered, this,
            [this]() { emit onExecuted(ActionMap::showSideBar); });
    connect(hideSideBarAction, &QAction::triggered, this,
            [this]() { emit onExecuted(ActionMap::hideSideBar); });

    // 连接调试面板信号
    connect(m_debugPanelToggleAction, &QAction::triggered, this,
            [this]() { emit debugPanelToggleRequested(); });
    connect(m_debugPanelClearAction, &QAction::triggered, this,
            [this]() { emit debugPanelClearRequested(); });
    connect(m_debugPanelExportAction, &QAction::triggered, this,
            [this]() { emit debugPanelExportRequested(); });
}

void MenuBar::createThemeMenu() {
    QMenu* themeMenu = new QMenu(tr("主题(T)"), this);
    addMenu(themeMenu);

    QAction* lightThemeAction = new QAction(tr("浅色"), this);
    lightThemeAction->setCheckable(true);
    lightThemeAction->setChecked(STYLE.currentTheme() == Theme::Light);

    QAction* darkThemeAction = new QAction(tr("深色"), this);
    darkThemeAction->setCheckable(true);
    darkThemeAction->setChecked(STYLE.currentTheme() == Theme::Dark);

    QActionGroup* themeGroup = new QActionGroup(this);
    themeGroup->addAction(lightThemeAction);
    themeGroup->addAction(darkThemeAction);

    themeMenu->addAction(lightThemeAction);
    themeMenu->addAction(darkThemeAction);

    connect(lightThemeAction, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            STYLE.setLightTheme();
        }
    });

    connect(darkThemeAction, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            STYLE.setDarkTheme();
        }
    });
}

void MenuBar::setWelcomeScreenEnabled(bool enabled) {
    if (m_welcomeScreenToggleAction) {
        m_welcomeScreenToggleAction->setChecked(enabled);
    }
}

void MenuBar::setupRecentFilesMenu() {
    m_recentFilesMenu = new QMenu(tr("最近打开的文件"), this);
    m_recentFilesMenu->setEnabled(false);  // 初始状态禁用，直到有文件

    m_clearRecentFilesAction = new QAction(tr("清空最近文件"), this);
    connect(m_clearRecentFilesAction, &QAction::triggered, this,
            &MenuBar::onClearRecentFilesTriggered);
}

void MenuBar::updateRecentFilesMenu() {
    if (!m_recentFilesMenu || !m_recentFilesManager) {
        return;
    }

    // 清空现有菜单项
    m_recentFilesMenu->clear();

    QList<RecentFileInfo> recentFiles = m_recentFilesManager->getRecentFiles();

    if (recentFiles.isEmpty()) {
        m_recentFilesMenu->setEnabled(false);
        QAction* noFilesAction = m_recentFilesMenu->addAction(tr("无最近文件"));
        noFilesAction->setEnabled(false);
        return;
    }

    m_recentFilesMenu->setEnabled(true);

    // 添加最近文件项
    for (int i = 0; i < recentFiles.size(); ++i) {
        const RecentFileInfo& fileInfo = recentFiles[i];

        // 创建显示文本：序号 + 文件名 + 路径
        QString displayText =
            QString("&%1 %2").arg(i + 1).arg(fileInfo.fileName);
        if (displayText.length() > 50) {
            displayText = displayText.left(47) + "...";
        }

        QAction* fileAction = m_recentFilesMenu->addAction(displayText);
        fileAction->setToolTip(fileInfo.filePath);
        fileAction->setData(fileInfo.filePath);

        connect(fileAction, &QAction::triggered, this,
                &MenuBar::onRecentFileTriggered);
    }

    // 添加分隔符和清空选项
    m_recentFilesMenu->addSeparator();
    m_recentFilesMenu->addAction(m_clearRecentFilesAction);
}

void MenuBar::onRecentFileTriggered() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        QString filePath = action->data().toString();
        if (!filePath.isEmpty()) {
            // 检查文件是否仍然存在
            QFileInfo fileInfo(filePath);
            if (fileInfo.exists()) {
                emit openRecentFileRequested(filePath);
            } else {
                // 文件不存在，从列表中移除
                if (m_recentFilesManager) {
                    m_recentFilesManager->removeRecentFile(filePath);
                }
                qDebug() << "Recent file no longer exists:" << filePath;
            }
        }
    }
}

void MenuBar::onClearRecentFilesTriggered() {
    if (m_recentFilesManager) {
        m_recentFilesManager->clearRecentFiles();
    }
}

QAction* MenuBar::shortcutAction(ActionMap action) {
    QAction* registered =
        m_shortcutManager ? m_shortcutManager->actionFor(action) : nullptr;
    if (registered) {
        return registered;
    }
    return new QAction(this);
}
