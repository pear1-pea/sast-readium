#pragma once

#include <QMainWindow>

#include <QStackedWidget>
#include "controller/DocumentOrchestrator.h"
#include "controller/tool.hpp"
#include "core/AppComponents.h"
#include "managers/LayoutManager.h"
#include "managers/RecentFilesManager.h"
#include "managers/StyleManager.h"
#include "managers/ThemeManager.h"
#include "model/DocumentModel.h"
#include "model/PageModel.h"
#include "model/RenderModel.h"
#include "ui/core/MenuBar.h"
#include "ui/core/RightSideBar.h"
#include "ui/core/SideBar.h"
#include "ui/core/StatusBar.h"
#include "ui/core/ToolBar.h"
#include "ui/core/ViewWidget.h"
#include "ui/managers/WelcomeScreenManager.h"
#include "ui/widgets/WelcomeWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(const AppComponents& deps, QWidget* parent = nullptr);
    ~MainWindow() noexcept;

private slots:
    void onSideBarVisibilityChanged(bool visible);
    void onCurrentDocumentChangedForOutline(int index);
    void updateStatusBarInfo();
    void onPageJumpRequested(int pageNumber);
    void onThumbnailPageClicked(int pageNumber);
    void onThumbnailPageDoubleClicked(int pageNumber);
    void onOpenRecentFileRequested(const QString& filePath);

    // 目录相关的槽函数
    void onOutlineModelChanged(PDFOutlineModel* model);
    void onPageChangedForOutlineHighlight(int pageNumber, int totalPages);

    // 缩略图同步的槽函数
    void onPageChangedForThumbnailSync(int pageNumber, int totalPages);

    // Welcome screen slots
    void onWelcomeScreenShowRequested();
    void onWelcomeScreenHideRequested();
    void onWelcomeFileOpenRequested(const QString& filePath);
    void onWelcomeNewFileRequested();
    void onWelcomeOpenFileRequested();

private:
    void initWindow();
    void initContent();
    void initConnection();
    void initWelcomeScreen();
    void initWelcomeScreenConnections();

    // Route an ActionMap from MenuBar / ToolBar to the right component.
    void routeAction(ActionMap action);
    // Dialog-based file operations (L5 — needs QWidget* parent).
    void openFileDialog();
    void openFolderDialog();
    void saveDocumentAs();
    void showDocumentMetadata();

    // 目录相关的辅助函数
    void setupOutlineConnections();
    void updateOutlineHighlight(int pageNumber);

    MenuBar* menuBar;
    ToolBar* toolBar;
    SideBar* sideBar;
    RightSideBar* rightSideBar;
    StatusBar* statusBar;
    ViewWidget* viewWidget;

    LayoutManager* m_layoutManager;

    // Welcome screen components
    QStackedWidget* m_contentStack;
    WelcomeWidget* m_welcomeWidget;
    WelcomeScreenManager* m_welcomeScreenManager;

    DocumentOrchestrator* m_documentOrchestrator;

    DocumentModel* documentModel;
    PageModel* pageModel;
    RenderModel* renderModel;

    RecentFilesManager* recentFilesManager;

    ThemeManager* m_themeManager;

signals:
    void pdfViewerActionRequested(ActionMap action);
};
