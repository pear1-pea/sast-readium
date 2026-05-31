#include "MainWindow.h"
#include <QApplication>
#include <QBoxLayout>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QLatin1String>
#include <QMainWindow>
#include <QStackedWidget>
#include <QStringList>
#include <QWidget>
#include "managers/FileTypeIconManager.h"
#include "managers/StyleManager.h"
#include "model/RenderModel.h"
#include "ui/managers/WelcomeScreenManager.h"
#include "ui/thumbnail/ThumbnailListView.h"
#include "ui/widgets/WelcomeWidget.h"
#include "utils/LoggingMacros.h"

MainWindow::MainWindow(const AppComponents& deps, QWidget* parent)
    : QMainWindow(parent),
      m_actionDispatcher(deps.actionDispatcher),
      m_documentOrchestrator(deps.documentOrchestrator),
      m_themeManager(deps.themeManager),
      documentModel(deps.documentModel),
      pageModel(deps.pageModel),
      renderModel(deps.renderModel),
      recentFilesManager(deps.recentFilesManager) {
    LOG_DEBUG("MainWindow: Starting initialization...");

    initWindow();
    LOG_DEBUG("MainWindow: Window initialized");

    // Models & controllers are assembled by AppBootstrap

    initWelcomeScreen();
    LOG_DEBUG("MainWindow: Welcome screen initialized");
    initContent();
    LOG_DEBUG("MainWindow: Content initialized");

    initConnection();
    LOG_DEBUG("MainWindow: Connections initialized");
    initWelcomeScreenConnections();
    LOG_DEBUG("MainWindow: Welcome screen connections initialized");

    // 在所有组件初始化完成后应用初始主题
    QString defaultTheme =
        (STYLE.currentTheme() == Theme::Light) ? "light" : "dark";

    LOG_DEBUG(
        "MainWindow: Constructor completed, scheduling initial theme "
        "application");

    // 延迟应用主题，确保窗口完全准备好，并强制应用到MainWindow
    QTimer::singleShot(0, this, [this, defaultTheme]() {
        LOG_DEBUG("MainWindow: Applying initial theme: {}",
                  defaultTheme.toStdString());

        // 应用主题
        m_themeManager->loadTheme(defaultTheme);

        // 额外检查：如果主窗口样式表仍然为空，使用备用方法
        if (this->styleSheet().isEmpty()) {
            LOG_WARNING(
                "MainWindow: StyleSheet is empty after ThemeManager, "
                "forcing fallback theme application");
            QString fallbackStyleSheet = STYLE.getApplicationStyleSheet();
            STYLE.forceApplyTheme(this, fallbackStyleSheet);
        }

        LOG_DEBUG(
            "MainWindow: Theme application completed ({}), stylesheet length: "
            "{}",
            defaultTheme.toStdString(), this->styleSheet().length());
    });

    // 启动异步初始化以避免阻塞UI
    if (recentFilesManager) {
        try {
            recentFilesManager->initializeAsync();
            LOG_DEBUG("MainWindow: Async initialization started");
        } catch (const std::exception& e) {
            LOG_WARNING("MainWindow: Failed to start async initialization: {}",
                        e.what());
        } catch (...) {
            LOG_WARNING(
                "MainWindow: Unknown error during async initialization "
                "startup");
        }
    } else {
        LOG_WARNING(
            "MainWindow: RecentFilesManager is null, skipping async "
            "initialization");
    }

    LOG_INFO("MainWindow: Initialization completed successfully");
}

MainWindow::~MainWindow() noexcept {}

// initialize
void MainWindow::initWindow() { resize(1280, 800); }

void MainWindow::initContent() {
    menuBar = new MenuBar(recentFilesManager, this);
    toolBar = new ToolBar(this);
    sideBar = new SideBar(this);
    rightSideBar = new RightSideBar(this);
    statusBar = new StatusBar(this);
    viewWidget = new ViewWidget(m_actionDispatcher, documentModel, this);

    setMenuBar(menuBar);
    addToolBar(toolBar);
    setStatusBar(statusBar);

    // 创建主内容区域的QStackedWidget
    m_contentStack = new QStackedWidget(this);

    // 使用 LayoutManager 创建主查看器布局
    m_layoutManager =
        new LayoutManager(sideBar, rightSideBar, viewWidget, this, this);
    QWidget* mainViewerWidget = m_layoutManager->createLayout();

    // 添加页面到堆叠组件
    m_contentStack->addWidget(m_welcomeWidget);   // 索引 0: 欢迎界面
    m_contentStack->addWidget(mainViewerWidget);  // 索引 1: 主查看器

    // 设置中央组件
    setCentralWidget(m_contentStack);

    // 初始显示欢迎界面（如果启用）
    if (m_welcomeScreenManager &&
        m_welcomeScreenManager->shouldShowWelcomeScreen()) {
        m_contentStack->setCurrentIndex(0);
    } else {
        m_contentStack->setCurrentIndex(1);
    }
}

void MainWindow::initWelcomeScreen() {
    LOG_DEBUG("MainWindow: Initializing welcome screen...");

    // 初始化文件类型图标管理器
    FILE_ICON_MANAGER.preloadIcons();

    // 创建欢迎界面组件
    m_welcomeWidget = new WelcomeWidget(this);
    m_welcomeWidget->setRecentFilesManager(recentFilesManager);

    // 创建欢迎界面管理器，一次性传入所有依赖
    m_welcomeScreenManager =
        new WelcomeScreenManager(m_welcomeWidget, documentModel, this);

    // 设置管理器到欢迎界面
    m_welcomeWidget->setWelcomeScreenManager(m_welcomeScreenManager);

    LOG_DEBUG("MainWindow: Welcome screen initialized successfully");
}

void MainWindow::initConnection() {
    // 监听 StyleManager 的主题变更信号，确保从任何地方切换主题都能立即生效
    connect(&StyleManager::instance(), &StyleManager::themeChanged, this,
            [this](Theme theme) {
                QString themeStr = (theme == Theme::Dark) ? "dark" : "light";
                m_themeManager->loadTheme(themeStr);
            });

    connect(menuBar, &MenuBar::onExecuted, m_actionDispatcher,
            &ActionDispatcher::execute);
    connect(menuBar, &MenuBar::onExecuted, this,
            &MainWindow::handleActionExecuted);

    // 连接最近文件信号
    connect(menuBar, &MenuBar::openRecentFileRequested, this,
            &MainWindow::onOpenRecentFileRequested);

    // 连接工具栏信号
    connect(toolBar, &ToolBar::actionTriggered, this, [this](ActionMap action) {
        m_actionDispatcher->execute(action, this);
    });

    // Connect action dispatcher operation completed signal
    connect(m_actionDispatcher, &ActionDispatcher::documentOperationCompleted,
            this, &MainWindow::onDocumentOperationCompleted);

    // 连接侧边栏信号
    connect(sideBar, &SideBar::visibilityChanged, this,
            &MainWindow::onSideBarVisibilityChanged);

    // 连接缩略图点击信号
    connect(sideBar, &SideBar::pageClicked, this,
            &MainWindow::onThumbnailPageClicked);
    connect(sideBar, &SideBar::pageDoubleClicked, this,
            &MainWindow::onThumbnailPageDoubleClicked);

    // 连接文档模型信号以同步目录
    connect(documentModel, &DocumentModel::currentDocumentChanged, this,
            &MainWindow::onCurrentDocumentChangedForOutline);

    // 连接ViewWidget的目录模型变化信号
    connect(viewWidget, &ViewWidget::currentOutlineModelChanged, this,
            &MainWindow::onOutlineModelChanged);

    // 连接页面变化信号以更新目录高亮
    connect(viewWidget, &ViewWidget::currentViewerPageChanged, this,
            &MainWindow::onPageChangedForOutlineHighlight);

    // 连接文档模型信号以更新状态栏
    connect(documentModel, &DocumentModel::documentOpened, this,
            [this](int index, const QString& fileName) {
                statusBar->hideLoadingProgress();
                updateStatusBarInfo();
            });
    connect(documentModel, &DocumentModel::currentDocumentChanged, this,
            &MainWindow::updateStatusBarInfo);
    connect(documentModel, &DocumentModel::allDocumentsClosed, this, [this]() {
        statusBar->clearDocumentInfo();
        toolBar->setActionsEnabled(false);
    });

    // 连接异步加载进度信号
    connect(documentModel, &DocumentModel::loadingStarted, this,
            [this](const QString& filePath) {
                QFileInfo fileInfo(filePath);
                statusBar->showLoadingProgress(
                    QString("正在加载 %1...").arg(fileInfo.fileName()));
            });
    connect(documentModel, &DocumentModel::loadingProgressChanged, statusBar,
            &StatusBar::updateLoadingProgress);
    connect(documentModel, &DocumentModel::loadingMessageChanged, statusBar,
            &StatusBar::setLoadingMessage);
    connect(documentModel, &DocumentModel::loadingFailed, this,
            [this](const QString& error, const QString& filePath) {
                statusBar->hideLoadingProgress();
                statusBar->setMessage(QString("加载失败: %1").arg(error));
            });

    // 连接文档打开/关闭状态变化
    connect(documentModel, &DocumentModel::documentOpened, this,
            [this](int, const QString&) {
                toolBar->setActionsEnabled(true);
                // 通知欢迎界面管理器文档已打开
                if (m_welcomeScreenManager) {
                    m_welcomeScreenManager->onDocumentOpened();
                }
            });
    connect(documentModel, &DocumentModel::documentClosed, this, [this](int) {
        if (documentModel->isEmpty()) {
            toolBar->setActionsEnabled(false);
            // 通知欢迎界面管理器所有文档已关闭
            if (m_welcomeScreenManager) {
                m_welcomeScreenManager->onAllDocumentsClosed();
            }
        } else {
            // 通知欢迎界面管理器文档已关闭
            if (m_welcomeScreenManager) {
                m_welcomeScreenManager->onDocumentClosed();
            }
        }
    });

    // 连接ViewWidget的PDF查看器状态信号
    connect(viewWidget, &ViewWidget::currentViewerPageChanged, this,
            [this](int pageNumber, int totalPages) {
                statusBar->setPageInfo(pageNumber, totalPages);
            });

    // 连接页面变化信号以同步缩略图高光
    connect(viewWidget, &ViewWidget::currentViewerPageChanged, this,
            &MainWindow::onPageChangedForThumbnailSync);
    connect(viewWidget, &ViewWidget::currentViewerZoomChanged, this,
            [this](double zoomFactor) { statusBar->setZoomLevel(zoomFactor); });

    // 连接PDF操作信号
    connect(m_actionDispatcher, &ActionDispatcher::pdfActionRequested, this,
            &MainWindow::onPDFActionRequested);

    // 连接MainWindow的PDF操作信号到ViewWidget
    connect(this, &MainWindow::pdfViewerActionRequested, viewWidget,
            &ViewWidget::executePDFAction);

    // 连接状态栏缩放控制信号
    connect(statusBar, &StatusBar::zoomChanged, viewWidget,
            &ViewWidget::setCurrentZoom);
    connect(statusBar, &StatusBar::zoomInClicked, this,
            [this]() { emit pdfViewerActionRequested(ActionMap::zoomIn); });
    connect(statusBar, &StatusBar::zoomOutClicked, this,
            [this]() { emit pdfViewerActionRequested(ActionMap::zoomOut); });

    // 连接状态栏页码跳转信号
    connect(statusBar, &StatusBar::pageJumpRequested, this,
            &MainWindow::onPageJumpRequested);

    // 页面/状态栏信号（模型→视图的连接，后续移入 Assembly 层）
    connect(pageModel, &PageModel::pageUpdate, statusBar,
            &StatusBar::setPageInfo);
    connect(documentModel, &DocumentModel::pageUpdate, statusBar,
            &StatusBar::setPageInfo);
    // connect(viewWidget, &ViewWidget::scaleChanged, statusBar,
    // &StatusBar::setZoomInfo);
}

void MainWindow::onDocumentOperationCompleted(ActionMap action, bool success) {
    QString message;
    switch (action) {
        case ActionMap::openFile:
        case ActionMap::newTab:
            message = success ? "文档打开成功" : "文档打开失败";
            break;
        case ActionMap::closeTab:
        case ActionMap::closeCurrentTab:
            message = success ? "文档关闭成功" : "文档关闭失败";
            break;
        case ActionMap::closeAllTabs:
            message = success ? "所有文档已关闭" : "关闭文档时出错";
            break;
        default:
            return;
    }

    statusBar->setMessage(message);
}

void MainWindow::onSideBarVisibilityChanged(bool visible) {
    // 可以在这里添加状态栏消息或其他UI反馈
    QString message = visible ? "侧边栏已显示" : "侧边栏已隐藏";
    statusBar->setMessage(message);
}

void MainWindow::onCurrentDocumentChangedForOutline(int index) {
    // 设置缩略图文档
    if (documentModel && index >= 0) {
        std::shared_ptr<Poppler::Document> sharedDoc =
            documentModel->getDocument(index);
        if (sharedDoc) {
            sideBar->setDocument(sharedDoc);
            renderModel->setDocument(sharedDoc);
        }
    }
}

void MainWindow::onThumbnailPageClicked(int pageNumber) {
    // 缩略图单击跳转到对应页面
    if (viewWidget) {
        viewWidget->goToPage(pageNumber);
    }

    // 可选：显示状态消息
    if (statusBar) {
        statusBar->setMessage(QString("跳转到第 %1 页").arg(pageNumber + 1));
    }
}

void MainWindow::onThumbnailPageDoubleClicked(int pageNumber) {
    // 缩略图双击可以有不同的行为，比如放大显示
    onThumbnailPageClicked(pageNumber);

    // 可选：触发特殊操作，如适合页面宽度
    // 这里可以添加更多功能
}

void MainWindow::updateStatusBarInfo() {
    if (!documentModel || documentModel->isEmpty()) {
        statusBar->clearDocumentInfo();
        return;
    }

    // 获取当前文档信息
    QString fileName = documentModel->getCurrentFileName();

    // 获取当前PDF查看器的页面和缩放信息
    int currentPage = viewWidget->getCurrentPage();
    int totalPages = viewWidget->getCurrentPageCount();
    double zoomLevel = viewWidget->getCurrentZoom();
    int zoomPercent = static_cast<int>(zoomLevel * 100 + 0.5);

    statusBar->setDocumentInfo(fileName, currentPage, totalPages, zoomPercent);
}

void MainWindow::onPageJumpRequested(int pageNumber) {
    // 将页码跳转请求传递给当前的PDF查看器
    viewWidget->goToPage(pageNumber);
}

void MainWindow::onPDFActionRequested(ActionMap action) {
    // 获取当前活动的PDF查看器并执行相应操作
    if (!viewWidget->hasDocuments()) {
        return;  // 没有文档时不执行操作
    }

    int currentIndex = viewWidget->getCurrentDocumentIndex();
    if (currentIndex < 0)
        return;

    // 通过ViewWidget路由到当前PDFViewer
    switch (action) {
        // --- PDF viewer actions ---
        case ActionMap::firstPage:
        case ActionMap::previousPage:
        case ActionMap::nextPage:
        case ActionMap::lastPage:
        case ActionMap::zoomIn:
        case ActionMap::zoomOut:
        case ActionMap::fitToWidth:
        case ActionMap::fitToPage:
        case ActionMap::fitToHeight:
        case ActionMap::rotateLeft:
        case ActionMap::rotateRight:
            emit pdfViewerActionRequested(action);
            break;
        // --- Sidebar actions ---
        case ActionMap::toggleSideBar:
            m_layoutManager->toggleSideBar();
            break;
        case ActionMap::showSideBar:
            m_layoutManager->showSideBar();
            break;
        case ActionMap::hideSideBar:
            m_layoutManager->hideSideBar();
            break;
        // --- View mode ---
        case ActionMap::setSinglePageMode:
            viewWidget->setCurrentViewMode(0);
            break;
        case ActionMap::setContinuousScrollMode:
            viewWidget->setCurrentViewMode(1);
            break;
        // --- Theme ---
        case ActionMap::toggleTheme:
            STYLE.toggleTheme();
            break;
        default:
            LOG_WARNING("Unhandled PDF action: {}", static_cast<int>(action));
            break;
    }
}

void MainWindow::onOpenRecentFileRequested(const QString& filePath) {
    // 通过ActionDispatcher打开最近文件
    if (m_actionDispatcher) {
        bool success = m_actionDispatcher->openDocument(filePath);
        if (!success) {
            LOG_WARNING("Failed to open recent file: {}",
                        filePath.toStdString());
        }
    }
}

void MainWindow::initWelcomeScreenConnections() {
    if (!m_welcomeScreenManager || !m_welcomeWidget)
        return;

    LOG_DEBUG("MainWindow: Setting up welcome screen connections...");

    // 连接欢迎界面管理器信号
    connect(m_welcomeScreenManager,
            &WelcomeScreenManager::showWelcomeScreenRequested, this,
            &MainWindow::onWelcomeScreenShowRequested);
    connect(m_welcomeScreenManager,
            &WelcomeScreenManager::hideWelcomeScreenRequested, this,
            &MainWindow::onWelcomeScreenHideRequested);
    connect(m_welcomeScreenManager,
            &WelcomeScreenManager::welcomeScreenEnabledChanged, menuBar,
            &MenuBar::setWelcomeScreenEnabled);

    // 连接菜单栏欢迎界面切换信号
    connect(menuBar, &MenuBar::welcomeScreenToggleRequested,
            m_welcomeScreenManager,
            &WelcomeScreenManager::onWelcomeScreenToggleRequested);

    // 连接欢迎界面组件信号
    connect(m_welcomeWidget, &WelcomeWidget::fileOpenRequested, this,
            &MainWindow::onWelcomeFileOpenRequested);
    connect(m_welcomeWidget, &WelcomeWidget::newFileRequested, this,
            &MainWindow::onWelcomeNewFileRequested);
    connect(m_welcomeWidget, &WelcomeWidget::openFileRequested, this,
            &MainWindow::onWelcomeOpenFileRequested);

    // 初始化菜单状态
    menuBar->setWelcomeScreenEnabled(
        m_welcomeScreenManager->isWelcomeScreenEnabled());

    // 启动欢迎界面管理器
    m_welcomeScreenManager->onApplicationStartup();

    LOG_DEBUG("MainWindow: Welcome screen connections established");
}

// Welcome screen slot implementations
void MainWindow::onWelcomeScreenShowRequested() {
    LOG_DEBUG("MainWindow: Showing welcome screen");
    if (m_contentStack) {
        m_contentStack->setCurrentIndex(0);  // 欢迎界面
    }
}

void MainWindow::onWelcomeScreenHideRequested() {
    LOG_DEBUG("MainWindow: Hiding welcome screen");
    if (m_contentStack) {
        m_contentStack->setCurrentIndex(1);  // 主查看器
    }
}

void MainWindow::onWelcomeFileOpenRequested(const QString& filePath) {
    LOG_DEBUG("MainWindow: Opening file from welcome screen: {}",
              filePath.toStdString());

    // 使用现有的ActionDispatcher打开文件
    if (m_actionDispatcher) {
        m_actionDispatcher->openDocument(filePath);
    }
}

void MainWindow::onWelcomeNewFileRequested() {
    LOG_DEBUG("MainWindow: New file requested from welcome screen");

    // 这里可以实现新建文件的逻辑
    // 目前PDF阅读器可能不支持新建文件，所以可以显示打开文件对话框
    onWelcomeOpenFileRequested();
}

void MainWindow::onWelcomeOpenFileRequested() {
    LOG_DEBUG("MainWindow: Open file requested from welcome screen");

    // 使用现有的ActionDispatcher打开文件对话框
    if (m_actionDispatcher) {
        m_actionDispatcher->execute(ActionMap::openFile, this);
    }
}

void MainWindow::handleActionExecuted(ActionMap id) {
    switch (id) {
        case ActionMap::fullScreen:
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
            break;
        case ActionMap::zoomIn:
            // 通过现有的PDF操作信号处理缩放
            emit pdfViewerActionRequested(ActionMap::zoomIn);
            break;
        case ActionMap::zoomOut:
            // 通过现有的PDF操作信号处理缩放
            emit pdfViewerActionRequested(ActionMap::zoomOut);
            break;
        default:
            // 其他操作通过ActionDispatcher处理
            break;
    }
}

// 目录相关的新增函数实现
void MainWindow::onOutlineModelChanged(PDFOutlineModel* model) {
    // 当ViewWidget发出目录模型变化信号时，更新侧边栏的目录
    if (sideBar) {
        sideBar->setOutlineModel(model);

        // 重新建立连接
        setupOutlineConnections();

        // 更新目录高亮（如果有当前页面信息）
        if (viewWidget && viewWidget->hasDocuments()) {
            int currentPage = viewWidget->getCurrentPage();
            updateOutlineHighlight(currentPage);
        }

        qDebug() << "Outline model changed and updated, model:" << model;
    }
}

void MainWindow::onPageChangedForOutlineHighlight(int pageNumber,
                                                  int totalPages) {
    // 当页面变化时，更新目录高亮
    Q_UNUSED(totalPages)
    updateOutlineHighlight(pageNumber);
}

void MainWindow::setupOutlineConnections() {
    // 重新建立目录点击跳转信号连接
    if (sideBar && sideBar->getOutlineWidget()) {
        qDebug() << "Setting up outline connections - sidebar and outline "
                    "widget exist";

        // 断开之前的连接，避免重复连接
        disconnect(sideBar->getOutlineWidget(),
                   &PDFOutlineWidget::pageNavigationRequested, nullptr,
                   nullptr);

        // 连接到当前PDF查看器的页面跳转
        connect(
            sideBar->getOutlineWidget(),
            &PDFOutlineWidget::pageNavigationRequested, this,
            [this](int pageNumber) {
                qDebug() << "Outline navigation requested to page:"
                         << pageNumber + 1;

                // 通过ViewWidget获取当前的PDF查看器并跳转页面
                if (viewWidget) {
                    viewWidget->goToPage(pageNumber);

                    // 显示状态消息
                    if (statusBar) {
                        statusBar->setMessage(QString("从目录跳转到第 %1 页")
                                                  .arg(pageNumber + 1));
                    }
                }
            });

        qDebug() << "Outline navigation connections established";
    } else {
        qDebug() << "Cannot setup outline connections - sidebar:"
                 << (sideBar != nullptr) << ", outline widget:"
                 << (sideBar ? (sideBar->getOutlineWidget() != nullptr)
                             : false);
    }
}

void MainWindow::updateOutlineHighlight(int pageNumber) {
    // 更新目录中对应页面的高亮显示
    if (sideBar && sideBar->getOutlineWidget()) {
        sideBar->getOutlineWidget()->highlightPageItem(pageNumber);

        qDebug() << "Updated outline highlight for page" << pageNumber + 1
                 << "(0-based:" << pageNumber << ")";
    } else {
        qDebug() << "Cannot update outline highlight: sidebar or outline "
                    "widget is null";
    }
}

void MainWindow::onPageChangedForThumbnailSync(int pageNumber, int totalPages) {
    // 同步缩略图的当前页面高光和滚动位置
    if (sideBar && sideBar->getThumbnailView()) {
        ThumbnailListView* thumbnailView = sideBar->getThumbnailView();

        // 设置当前页面（这会自动更新高光并滚动到当前页面）
        thumbnailView->setCurrentPage(pageNumber, true);  // true表示使用动画

        LOG_DEBUG("MainWindow: Synchronized thumbnail highlight to page {}",
                  pageNumber + 1);
    } else {
        LOG_DEBUG(
            "MainWindow: Cannot sync thumbnail highlight: sidebar or thumbnail "
            "view is null");
    }

    Q_UNUSED(totalPages)  // 避免未使用参数的警告
}
