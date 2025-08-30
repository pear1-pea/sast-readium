#include "MenuBar.h"
#include <QAction>
#include <QActionGroup>
#include <QMenu>

MenuBar::MenuBar(QWidget* parent) : QMenuBar(parent) {
    createFileMenu();
    createViewMenu();
    createThemeMenu();
}

void MenuBar::createFileMenu() {
    QMenu* fileMenu = new QMenu(tr("文件(F)"), this);
    addMenu(fileMenu);

    QAction* openAction = new QAction(tr("打开"), this);
    openAction->setShortcut(QKeySequence("Ctrl+O"));

    QAction* saveAction = new QAction(tr("保存"), this);
    saveAction->setShortcut(QKeySequence("Ctrl+S"));

    QAction* closeAction = new QAction(tr("关闭"), this);
    closeAction->setShortcut(QKeySequence("Ctrl+Q"));

    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    fileMenu->addSeparator();
    fileMenu->addAction(closeAction);

    connect(openAction, &QAction::triggered, this,
            [this]() { emit onExecuted(ActionMap::openFile); });
}

void MenuBar::createViewMenu() {
    QMenu* viewMenu = new QMenu(tr("视图(V)"), this);
    addMenu(viewMenu);

    QAction* fullScreenAction = new QAction(tr("全屏"), this);
    fullScreenAction->setShortcut(QKeySequence("Ctrl+Shift+F"));
    fullScreenAction->setCheckable(true);

    QAction* zoomInAction = new QAction(tr("放大"), this);
    zoomInAction->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});

    QAction* zoomOutAction = new QAction(tr("缩小"), this);
    zoomOutAction->setShortcuts({QKeySequence("Ctrl+-"), QKeySequence("Ctrl+_")});

    viewMenu->addAction(fullScreenAction);
    viewMenu->addSeparator();
    viewMenu->addAction(zoomInAction);
    viewMenu->addAction(zoomOutAction);

    connect(fullScreenAction, &QAction::triggered, this, 
        [this]() { emit onExecuted(ActionMap::fullScreen); });
    connect(zoomInAction, &QAction::triggered, this, 
        [this]() { emit onExecuted(ActionMap::zoomIn); });
    connect(zoomOutAction, &QAction::triggered, this, 
        [this]() { emit onExecuted(ActionMap::zoomOut); });
}

void MenuBar::createThemeMenu() {
    QMenu* themeMenu = new QMenu(tr("主题(T)"), this);
    addMenu(themeMenu);

    QAction* lightThemeAction = new QAction(tr("浅色"), this);
    lightThemeAction->setCheckable(true);

    QAction* darkThemeAction = new QAction(tr("深色"), this);
    darkThemeAction->setCheckable(true);

    QActionGroup* themeGroup = new QActionGroup(this);
    themeGroup->addAction(lightThemeAction);
    themeGroup->addAction(darkThemeAction);

    connect(lightThemeAction, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            emit themeChanged("light");
        }
    });

    connect(darkThemeAction, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            emit themeChanged("dark");
        }
    });

    themeMenu->addAction(lightThemeAction);
    themeMenu->addAction(darkThemeAction);

    // init
    darkThemeAction->setChecked(true);
}