#include "MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>

#include <limits>
#include "managers/FileTypeIconManager.h"
#include "managers/StyleManager.h"
#include "model/RenderModel.h"
#include "rag/domain/EvidenceHighlightRequest.h"
#include "rag/ui/AskPdfIndexDialog.h"
#include "rag/ui/AskPdfSettingsDialog.h"
#include "rag/ui/AskPdfWidget.h"
#include "ui/dialogs/DocumentMetadataDialog.h"
#include "ui/managers/WelcomeScreenManager.h"
#include "ui/thumbnail/ThumbnailListView.h"
#include "ui/widgets/WelcomeWidget.h"
#include "utils/DocumentCopy.h"
#include "utils/FileUtils.h"
#include "utils/LoggingMacros.h"

namespace {
QStringList pdfPathsFromMimeData(const QMimeData* mimeData) {
    QStringList paths;
    if (!mimeData || !mimeData->hasUrls()) {
        return paths;
    }

    for (const QUrl& url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isFile() &&
            path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            paths.append(path);
        }
    }
    return paths;
}

QString normalizedDocumentPath(const QString& path) {
    const QFileInfo fileInfo(path);
    return fileInfo.exists() ? fileInfo.canonicalFilePath()
                             : fileInfo.absoluteFilePath();
}

void setActionsEnabled(ShortcutManager* shortcutManager,
                       std::initializer_list<ActionMap> actions, bool enabled) {
    if (!shortcutManager) {
        return;
    }
    for (ActionMap action : actions) {
        shortcutManager->setActionEnabled(action, enabled);
    }
}
}  // namespace

MainWindow::MainWindow(const AppComponents& deps, QWidget* parent)
    : QMainWindow(parent),
      m_documentOrchestrator(deps.documentOrchestrator),
      m_themeManager(deps.themeManager),
      documentModel(deps.documentModel),
      pageModel(deps.pageModel),
      renderModel(deps.renderModel),
      recentFilesManager(deps.recentFilesManager),
      shortcutManager(&ShortcutManager::instance()),
      m_ragCoordinator(deps.ragDocumentCoordinator) {
    LOG_DEBUG("MainWindow: Starting initialization...");

    m_thumbnailLoadPool.setMaxThreadCount(1);
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

MainWindow::~MainWindow() noexcept { m_thumbnailLoadPool.clear(); }

void MainWindow::openFiles(const QStringList& filePaths) {
    if (m_documentOrchestrator) {
        m_documentOrchestrator->openDocuments(filePaths);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!pdfPathsFromMimeData(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QStringList paths = pdfPathsFromMimeData(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }

    openFiles(paths);
    event->acceptProposedAction();
}

// initialize
void MainWindow::initWindow() {
    resize(1280, 800);
    setAcceptDrops(true);
}

void MainWindow::initContent() {
    shortcutManager->registerDefaults();

    menuBar = new MenuBar(recentFilesManager, shortcutManager, this);
    toolBar = new ToolBar(this);
    sideBar = new SideBar(this);
    statusBar = new StatusBar(this);
    viewWidget = new ViewWidget(documentModel, this);

    setMenuBar(menuBar);
    addToolBar(toolBar);
    setStatusBar(statusBar);

    // 创建主内容区域的QStackedWidget
    m_contentStack = new QStackedWidget(this);

    // 使用 LayoutManager 创建主查看器布局
    m_layoutManager = new LayoutManager(sideBar, viewWidget, this, this);
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
    for (QAction* action : shortcutManager->actions()) {
        addAction(action);
    }
    connect(shortcutManager, &ShortcutManager::actionTriggered, this,
            [this](ActionMap action) { routeAction(action); });
    updateShortcutActionStates();

    // 监听 StyleManager 的主题变更信号
    connect(&StyleManager::instance(), &StyleManager::themeChanged, this,
            [this](Theme theme) {
                QString themeStr = (theme == Theme::Dark) ? "dark" : "light";
                m_themeManager->loadTheme(themeStr);
            });

    // MenuBar 动作路由
    connect(menuBar, &MenuBar::onExecuted, this,
            [this](ActionMap action) { routeAction(action); });

    // 连接最近文件信号
    connect(menuBar, &MenuBar::openRecentFileRequested, this,
            &MainWindow::onOpenRecentFileRequested);

    // ToolBar 动作路由
    connect(toolBar, &ToolBar::actionTriggered, this,
            [this](ActionMap action) { routeAction(action); });

    // StatusBar 导航按钮路由（上一页/下一页等）
    connect(statusBar, &StatusBar::actionTriggered, this,
            [this](ActionMap action) { routeAction(action); });

    // 侧边栏信号
    connect(sideBar, &SideBar::visibilityChanged, this,
            &MainWindow::onSideBarVisibilityChanged);
    connect(sideBar, &SideBar::pageClicked, this,
            &MainWindow::onThumbnailPageClicked);
    connect(sideBar, &SideBar::pageDoubleClicked, this,
            &MainWindow::onThumbnailPageDoubleClicked);

    // ViewWidget 标签信号 → DocumentOrchestrator
    connect(
        viewWidget, &ViewWidget::tabCloseRequested, this,
        [this](int index) { m_documentOrchestrator->closeDocument(index); });
    connect(viewWidget, &ViewWidget::tabSwitched, this, [this](int index) {
        m_documentOrchestrator->switchToDocument(index);
    });
    connect(viewWidget, &ViewWidget::newTabRequested, this,
            [this]() { routeAction(ActionMap::newTab); });
    connect(viewWidget, &ViewWidget::filesDropped, this,
            [this](const QStringList& filePaths) {
                m_documentOrchestrator->openDocuments(filePaths);
            });

    connect(viewWidget, &ViewWidget::bookmarkRequested, this,
            [this](const QString& documentPath, int pageNumber) {
                if (sideBar && sideBar->getBookmarkWidget()) {
                    sideBar->getBookmarkWidget()->addBookmark(documentPath,
                                                              pageNumber);
                }
            });
    connect(sideBar->getBookmarkWidget(), &BookmarkWidget::navigateToBookmark,
            this, &MainWindow::onBookmarkNavigationRequested);

    if (m_ragCoordinator && sideBar && sideBar->getAskPdfWidget()) {
        Rag::AskPdfWidget* askWidget = sideBar->getAskPdfWidget();
        connect(m_ragCoordinator,
                &Rag::RagDocumentCoordinator::currentDocumentStateChanged, this,
                [this, askWidget](const Rag::RagDocumentSnapshot& snapshot) {
                    const bool documentChanged =
                        snapshot.filePath != m_ragUiDocumentPath;
                    m_ragUiDocumentPath = snapshot.filePath;
                    if (viewWidget &&
                        (documentChanged ||
                         snapshot.state != Rag::RagIndexState::Ready)) {
                        viewWidget->clearEvidenceHighlights();
                    }
                    if (documentChanged) {
                        m_returnPage = -1;
                        m_returnOffsetY = 0.0;
                        askWidget->setReturnToPositionAvailable(false);
                    }
                    askWidget->setDocumentContext(snapshot.key);
                    askWidget->setIndexState(
                        snapshot.state, snapshot.chunkCount, snapshot.error,
                        snapshot.embeddingState, snapshot.embeddedChunkCount,
                        snapshot.embeddingError, snapshot.failureReason,
                        snapshot.processedPages, snapshot.totalPages);
                });
        connect(askWidget, &Rag::AskPdfWidget::questionSubmitted, this,
                [this](const QString& question) {
                    if (m_ragCoordinator) {
                        m_ragCoordinator->askQuestion(question);
                    }
                });
        connect(askWidget, &Rag::AskPdfWidget::rebuildIndexRequested, this,
                [this]() {
                    if (m_ragCoordinator) {
                        m_ragCoordinator->rebuildCurrentDocument();
                    }
                });
        connect(askWidget, &Rag::AskPdfWidget::cancelIndexRequested, this,
                [this]() {
                    if (m_ragCoordinator) {
                        m_ragCoordinator->cancelCurrentIndexing();
                    }
                });
        connect(askWidget, &Rag::AskPdfWidget::clearIndexRequested, this,
                [this]() {
                    if (!m_ragCoordinator) {
                        return;
                    }
                    if (QMessageBox::question(
                            this, tr("清空 Ask PDF 索引"),
                            tr("确定删除所有文档的本地索引吗？PDF 文件本身不会"
                               "被修改。")) != QMessageBox::Yes) {
                        return;
                    }
                    m_ragCoordinator->clearAllIndexes();
                });
        connect(m_ragCoordinator,
                &Rag::RagDocumentCoordinator::conversationModelChanged, this,
                [askWidget](Rag::RagConversationModel* model) {
                    askWidget->setConversationModel(model);
                });
        connect(m_ragCoordinator,
                &Rag::RagDocumentCoordinator::conversationBusyChanged,
                askWidget, &Rag::AskPdfWidget::setConversationBusy);
        connect(askWidget, &Rag::AskPdfWidget::regenerateTurnRequested,
                m_ragCoordinator, &Rag::RagDocumentCoordinator::regenerateTurn);
        connect(askWidget, &Rag::AskPdfWidget::stopGenerationRequested,
                m_ragCoordinator, &Rag::RagDocumentCoordinator::stopActiveTurn);
        connect(
            askWidget, &Rag::AskPdfWidget::clearConversationRequested, this,
            [this]() {
                if (!m_ragCoordinator) {
                    return;
                }
                if (QMessageBox::question(
                        this, tr("清空对话"),
                        tr("确定清空当前文档的对话吗？本地索引不会受影响。")) !=
                    QMessageBox::Yes) {
                    return;
                }
                m_ragCoordinator->clearConversation();
            });
        connect(askWidget, &Rag::AskPdfWidget::citationActivated,
                m_ragCoordinator,
                &Rag::RagDocumentCoordinator::activateCitation);
        connect(
            m_ragCoordinator, &Rag::RagDocumentCoordinator::citationReady, this,
            [this, askWidget](const Rag::RagEvidence& evidence) {
                if (viewWidget && m_returnPage < 0) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        const PDFViewerPosition position =
                            viewer->captureViewport();
                        m_returnPage = position.page;
                        m_returnOffsetY = position.offsetY;
                        askWidget->setReturnToPositionAvailable(true);
                    }
                }
                askWidget->evidenceActivated(evidence);
            });
        connect(m_ragCoordinator,
                &Rag::RagDocumentCoordinator::indexClearingChanged, askWidget,
                &Rag::AskPdfWidget::setClearing);
        connect(m_ragCoordinator, &Rag::RagDocumentCoordinator::indexCleared,
                this, [this]() {
                    if (statusBar) {
                        statusBar->setMessage(QStringLiteral("索引已清空"));
                    }
                });
        connect(
            m_ragCoordinator, &Rag::RagDocumentCoordinator::indexClearFailed,
            this, [this](const QString& error) {
                if (statusBar) {
                    statusBar->setMessage(
                        QStringLiteral("清空索引失败：%1").arg(error));
                }
                QMessageBox::warning(this, tr("清空 Ask PDF 索引失败"), error);
            });
        connect(m_ragCoordinator, &Rag::RagDocumentCoordinator::queryFailed,
                this, [this](const QString& error) {
                    if (statusBar) {
                        statusBar->setMessage(error);
                    }
                });
        connect(askWidget, &Rag::AskPdfWidget::retryAnswerRequested,
                m_ragCoordinator, &Rag::RagDocumentCoordinator::retryAnswer);
        connect(askWidget, &Rag::AskPdfWidget::retryQueryRequested,
                m_ragCoordinator, &Rag::RagDocumentCoordinator::retryQuery);
        connect(askWidget, &Rag::AskPdfWidget::returnToPositionRequested, this,
                [this, askWidget]() {
                    if (m_returnPage < 0 || !viewWidget) {
                        return;
                    }
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        PDFViewerPosition position;
                        position.page = m_returnPage;
                        position.offsetY = m_returnOffsetY;
                        viewer->restoreViewport(position);
                        viewer->clearEvidenceHighlights();
                    }
                    m_returnPage = -1;
                    m_returnOffsetY = 0.0;
                    askWidget->setReturnToPositionAvailable(false);
                });
        connect(
            askWidget, &Rag::AskPdfWidget::indexManagementRequested, this,
            [this]() {
                if (!m_ragCoordinator) {
                    return;
                }
                Rag::AskPdfIndexDialog dialog(this);
                const Rag::RagDocumentSnapshot snapshot =
                    m_ragCoordinator->currentSnapshot();
                dialog.setDocument(snapshot.key);
                dialog.setSnapshot(snapshot);
                connect(&dialog, &Rag::AskPdfIndexDialog::refreshRequested,
                        m_ragCoordinator,
                        &Rag::RagDocumentCoordinator::requestIndexStats);
                connect(&dialog, &Rag::AskPdfIndexDialog::rebuildRequested,
                        m_ragCoordinator,
                        &Rag::RagDocumentCoordinator::rebuildCurrentDocument);
                connect(&dialog, &Rag::AskPdfIndexDialog::deleteRequested, this,
                        [this, &dialog]() {
                            if (!m_ragCoordinator) {
                                return;
                            }
                            if (QMessageBox::question(
                                    this, tr("删除当前文档索引"),
                                    tr("只删除本地索引，不会修改 PDF。确定继续"
                                       "吗？")) != QMessageBox::Yes) {
                                return;
                            }
                            dialog.setBusy(true);
                            m_ragCoordinator->deleteCurrentDocumentIndex();
                        });
                connect(
                    &dialog, &Rag::AskPdfIndexDialog::clearAllRequested, this,
                    [this, &dialog]() {
                        if (!m_ragCoordinator) {
                            return;
                        }
                        if (QMessageBox::question(
                                this, tr("清空 Ask PDF 索引"),
                                tr("确定删除所有文档的本地索引吗？PDF 文件"
                                   "本身不会被修改。")) != QMessageBox::Yes) {
                            return;
                        }
                        dialog.setBusy(true);
                        m_ragCoordinator->clearAllIndexes();
                    });
                connect(m_ragCoordinator,
                        &Rag::RagDocumentCoordinator::indexStatsReady, &dialog,
                        [&dialog](const Rag::RagIndexStats& stats) {
                            dialog.setStats(stats);
                            dialog.setBusy(false);
                        });
                connect(
                    m_ragCoordinator,
                    &Rag::RagDocumentCoordinator::currentDocumentStateChanged,
                    &dialog,
                    [&dialog](const Rag::RagDocumentSnapshot& updated) {
                        dialog.setDocument(updated.key);
                        dialog.setSnapshot(updated);
                    });
                connect(m_ragCoordinator,
                        &Rag::RagDocumentCoordinator::documentIndexDeleted,
                        &dialog, [this, &dialog]() {
                            dialog.showInfo(tr("已删除当前文档索引。"));
                            dialog.setBusy(false);
                            if (m_ragCoordinator) {
                                m_ragCoordinator->requestIndexStats();
                            }
                        });
                connect(m_ragCoordinator,
                        &Rag::RagDocumentCoordinator::documentIndexDeleteFailed,
                        &dialog, [&dialog](const QString& error) {
                            dialog.setBusy(false);
                            dialog.showError(error);
                        });
                connect(m_ragCoordinator,
                        &Rag::RagDocumentCoordinator::indexClearingChanged,
                        &dialog,
                        [&dialog](bool clearing) { dialog.setBusy(clearing); });
                connect(m_ragCoordinator,
                        &Rag::RagDocumentCoordinator::indexCleared, &dialog,
                        [this, &dialog]() {
                            dialog.showInfo(tr("已清空全部索引。"));
                            dialog.setBusy(false);
                            if (m_ragCoordinator) {
                                m_ragCoordinator->requestIndexStats();
                            }
                        });
                connect(m_ragCoordinator,
                        &Rag::RagDocumentCoordinator::indexClearFailed, &dialog,
                        [&dialog](const QString& error) {
                            dialog.setBusy(false);
                            dialog.showError(error);
                        });
                dialog.exec();
            });
        connect(askWidget, &Rag::AskPdfWidget::evidenceActivated, this,
                [this](const Rag::RagEvidence& evidence) {
                    if (!m_ragCoordinator || !viewWidget) {
                        return;
                    }
                    const Rag::RagDocumentSnapshot snapshot =
                        m_ragCoordinator->currentSnapshot();
                    if (snapshot.key.contentHash.isEmpty() ||
                        snapshot.key.contentHash != evidence.documentHash) {
                        return;
                    }

                    QVector<Rag::EvidenceHighlightRequest> highlights;
                    for (const Rag::ChunkPageLocation& location :
                         evidence.locations) {
                        Rag::EvidenceHighlightRequest request;
                        request.pageIndex = location.pageIndex;
                        request.originalTopLeftRects = location.boundingRects;
                        request.focus =
                            location.pageIndex == evidence.pageStart;
                        highlights.append(std::move(request));
                    }
                    viewWidget->setEvidenceHighlights(highlights);
                    viewWidget->goToPage(evidence.pageStart);
                });

        const Rag::RagDocumentSnapshot snapshot =
            m_ragCoordinator->currentSnapshot();
        askWidget->setDocumentContext(snapshot.key);
        askWidget->setIndexState(
            snapshot.state, snapshot.chunkCount, snapshot.error,
            snapshot.embeddingState, snapshot.embeddedChunkCount,
            snapshot.embeddingError, snapshot.failureReason,
            snapshot.processedPages, snapshot.totalPages);
        askWidget->setAnswerEnabled(m_ragCoordinator->settings().chatEnabled());
        askWidget->setEmbeddingEnabled(
            m_ragCoordinator->settings().embeddingsEnabled());
        askWidget->setConversationModel(
            m_ragCoordinator->currentConversationModel());
        connect(
            askWidget, &Rag::AskPdfWidget::settingsRequested, this,
            [this, askWidget]() {
                if (!m_ragCoordinator) {
                    return;
                }
                Rag::AskPdfSettingsDialog dialog(m_ragCoordinator->settings(),
                                                 this);
                connect(
                    &dialog, &Rag::AskPdfSettingsDialog::settingsApplied, this,
                    [this, askWidget](const Rag::RagSettings& settings) {
                        if (!m_ragCoordinator) {
                            return;
                        }
                        const Rag::RagSettings previous =
                            m_ragCoordinator->settings();
                        m_ragCoordinator->applySettings(settings);
                        askWidget->setAnswerEnabled(settings.chatEnabled());
                        askWidget->setEmbeddingEnabled(
                            settings.embeddingsEnabled());
                        if (statusBar) {
                            statusBar->setMessage(
                                previous.chatModel != settings.chatModel
                                    ? QStringLiteral(
                                          "Ask PDF 设置已保存，更换 Chat 模型后"
                                          "后续回答不再使用此前对话作为上下文")
                                    : QStringLiteral(
                                          "Ask PDF 本地 AI 设置已保存"));
                        }
                    });
                dialog.exec();
            });
    }

    if (toolBar && toolBar->annotationToolbar()) {
        AnnotationToolbar* annotationToolbar = toolBar->annotationToolbar();
        connect(annotationToolbar, &AnnotationToolbar::toolChanged, this,
                [this](AnnotationType tool) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->setAnnotationTool(tool);
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::annotationModeChanged,
                this, [this](bool enabled) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->setAnnotationMode(enabled);
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::colorChanged, this,
                [this](const QColor& color) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->setAnnotationColor(color);
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::opacityChanged, this,
                [this](double opacity) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->setAnnotationOpacity(opacity);
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::lineWidthChanged, this,
                [this](double width) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->setAnnotationLineWidth(width);
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::fontSizeChanged, this,
                [this, annotationToolbar](int size) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->setAnnotationFont(
                            annotationToolbar->getCurrentFontFamily(), size);
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::fontFamilyChanged, this,
                [this, annotationToolbar](const QString& family) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->setAnnotationFont(
                            family, annotationToolbar->getCurrentFontSize());
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::clearAllAnnotations,
                this, [this]() {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->clearAnnotations();
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::saveAnnotations, this,
                [this]() {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->saveAnnotations();
                    }
                });
        connect(annotationToolbar, &AnnotationToolbar::loadAnnotations, this,
                [this]() {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->loadAnnotations();
                    }
                });
        annotationToolbar->setAnnotationMode(false);
        updateAnnotationToolbarState();
    }

    connect(
        documentModel, &DocumentModel::currentDocumentChanged, this,
        [this](int index) {
            if (sideBar && sideBar->getBookmarkWidget()) {
                sideBar->setBookmarkContext(
                    documentModel->getDocumentFilePath(index),
                    viewWidget->getCurrentPage());
            }
            if (toolBar && toolBar->annotationToolbar()) {
                auto* toolbar = toolBar->annotationToolbar();
                updateAnnotationToolbarState();
                if (index >= 0 && toolbar->isEnabled()) {
                    if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
                        viewer->setAnnotationTool(toolbar->getCurrentTool());
                        viewer->setAnnotationMode(toolbar->isAnnotationMode());
                    }
                }
            }
        });
    connect(documentModel, &DocumentModel::allDocumentsClosed, this, [this]() {
        if (sideBar && sideBar->getBookmarkWidget()) {
            sideBar->setBookmarkContext(QString(), -1);
        }
        sideBar->setDocument(nullptr, {});
        m_thumbnailDocumentCache.clear();
        m_thumbnailRetries.clear();
        m_thumbnailLoadsInFlight.clear();
        m_thumbnailCurrentPath.clear();
        if (m_welcomeScreenManager) {
            m_welcomeScreenManager->onAllDocumentsClosed();
        }
        if (toolBar && toolBar->annotationToolbar()) {
            updateAnnotationToolbarState();
        }
    });
    connect(
        documentModel, &DocumentModel::documentOpened, this,
        [this](int index, const QString&) {
            if (m_pendingBookmarkNavigation.first.isEmpty() ||
                normalizedDocumentPath(
                    documentModel->getDocumentFilePath(index)) !=
                    normalizedDocumentPath(m_pendingBookmarkNavigation.first)) {
                return;
            }

            const int pageNumber = m_pendingBookmarkNavigation.second;
            m_pendingBookmarkNavigation = {};
            QTimer::singleShot(0, this, [this, pageNumber]() {
                viewWidget->goToPage(pageNumber);
            });
        });

    // 文档模型信号以同步目录
    connect(documentModel, &DocumentModel::currentDocumentChanged, this,
            &MainWindow::onCurrentDocumentChangedForOutline);
    connect(viewWidget, &ViewWidget::currentOutlineModelChanged, this,
            &MainWindow::onOutlineModelChanged);
    connect(viewWidget, &ViewWidget::currentViewerPageChanged, this,
            &MainWindow::onPageChangedForOutlineHighlight);

    // 文档模型信号以更新状态栏
    connect(documentModel, &DocumentModel::documentOpened, this,
            [this](int index, const QString& fileName) {
                statusBar->hideLoadingProgress();
                updateStatusBarInfo();
            });
    connect(documentModel, &DocumentModel::currentDocumentChanged, this,
            &MainWindow::updateStatusBarInfo);
    connect(documentModel, &DocumentModel::currentDocumentChanged, this,
            [this](int) { updateShortcutActionStates(); });
    connect(documentModel, &DocumentModel::allDocumentsClosed, this, [this]() {
        statusBar->clearDocumentInfo();
        toolBar->setActionsEnabled(false);
        updateShortcutActionStates();
    });

    // 异步加载进度信号
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

    // 文档打开/关闭 — 工具栏状态 + 欢迎界面
    connect(documentModel, &DocumentModel::documentOpened, this,
            [this](int, const QString&) {
                toolBar->setActionsEnabled(true);
                updateShortcutActionStates();
                updateAnnotationToolbarState();
                if (m_welcomeScreenManager)
                    m_welcomeScreenManager->onDocumentOpened();
            });
    connect(documentModel, &DocumentModel::documentClosed, this,
            [this](int index) {
                const QString closedPath = normalizedDocumentPath(
                    documentModel->getDocumentFilePath(index));
                if (!closedPath.isEmpty()) {
                    m_thumbnailDocumentCache.remove(closedPath);
                }
                if (m_thumbnailCurrentPath == closedPath) {
                    m_thumbnailCurrentPath.clear();
                }
                m_thumbnailRetries.remove(closedPath);
                // documentClosed is emitted before DocumentModel removes the
                // document, so isEmpty() is false here even for the last tab.
                // The allDocumentsClosed handler performs the final cleanup.
                if (documentModel->getDocumentCount() > 1 &&
                    m_welcomeScreenManager) {
                    m_welcomeScreenManager->onDocumentClosed();
                }
                updateShortcutActionStates();
                updateAnnotationToolbarState();
            });

    // ViewWidget 状态信号 → 状态栏
    connect(viewWidget, &ViewWidget::currentViewerPageChanged, this,
            [this](int pageNumber, int totalPages) {
                statusBar->setPageInfo(pageNumber, totalPages);
                updatePageShortcutActions(pageNumber, totalPages);
            });
    connect(viewWidget, &ViewWidget::currentViewerPageChanged, this,
            &MainWindow::onPageChangedForThumbnailSync);
    connect(viewWidget, &ViewWidget::currentViewerZoomChanged, this,
            [this](double zoomFactor) { statusBar->setZoomLevel(zoomFactor); });
    connect(viewWidget, &ViewWidget::currentViewerViewModeChanged, this,
            &MainWindow::updateViewModeShortcutActions);

    // PDF 操作信号 → ViewWidget
    connect(this, &MainWindow::pdfViewerActionRequested, viewWidget,
            &ViewWidget::executePDFAction);

    // 状态栏缩放控制
    connect(statusBar, &StatusBar::zoomChanged, viewWidget,
            &ViewWidget::setCurrentZoom);
    connect(statusBar, &StatusBar::zoomInClicked, this,
            [this]() { emit pdfViewerActionRequested(ActionMap::zoomIn); });
    connect(statusBar, &StatusBar::zoomOutClicked, this,
            [this]() { emit pdfViewerActionRequested(ActionMap::zoomOut); });
    connect(statusBar, &StatusBar::pageJumpRequested, this,
            &MainWindow::onPageJumpRequested);

    // 页面/状态栏信号（模型→视图）
    connect(pageModel, &PageModel::pageUpdate, statusBar,
            &StatusBar::setPageInfo);
    connect(documentModel, &DocumentModel::pageUpdate, statusBar,
            &StatusBar::setPageInfo);
}

void MainWindow::onSideBarVisibilityChanged(bool visible) {
    // 可以在这里添加状态栏消息或其他UI反馈
    QString message = visible ? "侧边栏已显示" : "侧边栏已隐藏";
    statusBar->setMessage(message);
    shortcutManager->setActionChecked(ActionMap::toggleSideBar, visible);
    toolBar->setSidebarChecked(visible);
}

void MainWindow::onCurrentDocumentChangedForOutline(int index) {
    std::shared_ptr<Poppler::Document> viewerDocument;
    QString filePath;

    if (documentModel && index >= 0) {
        viewerDocument = documentModel->getDocument(index);
        filePath =
            normalizedDocumentPath(documentModel->getDocumentFilePath(index));
        const bool documentLocked =
            viewerDocument && viewerDocument->isLocked();
        if (documentLocked) {
            const QString message =
                QStringLiteral("文档已加密或受保护，当前版本无法显示页面内容");
            LOG_WARNING(
                "MainWindow: Locked PDF is not available without a "
                "password: {}",
                filePath.toStdString());
            statusBar->setMessage(message);
        }
    }

    // The viewer document is already loaded and safe to reuse. Thumbnails use
    // a separate Poppler document because the generator runs on worker
    // threads.
    renderModel->setDocument(viewerDocument);
    m_thumbnailCurrentPath = filePath;

    if (filePath.isEmpty()) {
        sideBar->setDocument(nullptr, {});
        return;
    }

    auto cached = m_thumbnailDocumentCache.find(filePath);
    if (cached != m_thumbnailDocumentCache.end() &&
        thumbnailDocumentIsFresh(cached.value(), filePath)) {
        cached.value().lastAccess = ++m_thumbnailCacheAccessCounter;
        applyThumbnailDocument(filePath, cached.value());
        return;
    }
    if (cached != m_thumbnailDocumentCache.end()) {
        m_thumbnailDocumentCache.erase(cached);
        m_thumbnailRetries.remove(filePath);
    }

    sideBar->setDocument(nullptr, {});
    requestThumbnailDocument(filePath);
}

bool MainWindow::thumbnailDocumentIsFresh(
    const ThumbnailDocumentCacheEntry& entry, const QString& filePath) const {
    const QFileInfo info(filePath);
    return info.exists() && info.isFile() && info.size() == entry.fileSize &&
           info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch() ==
               entry.modifiedTimeMs;
}

void MainWindow::applyThumbnailDocument(
    const QString& filePath, const ThumbnailDocumentCacheEntry& entry) {
    if (filePath != m_thumbnailCurrentPath || !entry.document) {
        return;
    }
    sideBar->setDocument(entry.document, entry.pageSizes);
}

void MainWindow::requestThumbnailDocument(const QString& filePath) {
    if (filePath.isEmpty() || m_thumbnailLoadsInFlight.contains(filePath)) {
        return;
    }

    m_thumbnailLoadsInFlight.insert(filePath);
    auto* watcher = new QFutureWatcher<ThumbnailDocumentCacheEntry>(this);
    connect(
        watcher, &QFutureWatcher<ThumbnailDocumentCacheEntry>::finished, this,
        [this, watcher, filePath]() {
            const ThumbnailDocumentCacheEntry entry = watcher->result();
            watcher->deleteLater();
            m_thumbnailLoadsInFlight.remove(filePath);

            bool documentStillOpen = false;
            if (documentModel) {
                for (int i = 0; i < documentModel->getDocumentCount(); ++i) {
                    if (normalizedDocumentPath(
                            documentModel->getDocumentFilePath(i)) ==
                        filePath) {
                        documentStillOpen = true;
                        break;
                    }
                }
            }

            const QFileInfo info(filePath);
            const bool stale =
                !info.exists() || !info.isFile() ||
                info.size() != entry.fileSize ||
                info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch() !=
                    entry.modifiedTimeMs;
            if (stale) {
                LOG_DEBUG(
                    "MainWindow: discarded stale thumbnail document for {}",
                    filePath.toStdString());
                if (filePath == m_thumbnailCurrentPath && documentStillOpen &&
                    m_thumbnailRetries.value(filePath) < 2) {
                    ++m_thumbnailRetries[filePath];
                    requestThumbnailDocument(filePath);
                }
                return;
            }
            if (!entry.document) {
                LOG_WARNING("MainWindow: Failed to load thumbnail document: {}",
                            filePath.toStdString());
                return;
            }
            if (!documentStillOpen) {
                return;
            }

            m_thumbnailRetries.remove(filePath);
            ThumbnailDocumentCacheEntry cachedEntry = entry;
            cachedEntry.lastAccess = ++m_thumbnailCacheAccessCounter;
            m_thumbnailDocumentCache.insert(filePath, cachedEntry);
            trimThumbnailDocumentCache();
            applyThumbnailDocument(filePath, cachedEntry);
        });

    watcher->setFuture(QtConcurrent::run(&m_thumbnailLoadPool, [filePath]() {
        ThumbnailDocumentCacheEntry entry;
        const QFileInfo info(filePath);
        if (!info.exists() || !info.isFile()) {
            return entry;
        }
        entry.fileSize = info.size();
        entry.modifiedTimeMs =
            info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();

        std::unique_ptr<Poppler::Document> document =
            Poppler::Document::load(filePath);
        if (!document || document->isLocked()) {
            return entry;
        }
        auto shared = std::shared_ptr<Poppler::Document>(document.release());
        entry.pageSizes.reserve(shared->numPages());
        for (int pageNumber = 0; pageNumber < shared->numPages();
             ++pageNumber) {
            std::unique_ptr<Poppler::Page> page(shared->page(pageNumber));
            entry.pageSizes.push_back(page ? page->pageSizeF().toSize()
                                           : QSize());
        }
        entry.document = std::move(shared);
        return entry;
    }));
}

void MainWindow::trimThumbnailDocumentCache() {
    while (m_thumbnailDocumentCache.size() > kMaxThumbnailDocuments) {
        QString oldestPath;
        quint64 oldestAccess = std::numeric_limits<quint64>::max();
        for (auto it = m_thumbnailDocumentCache.cbegin();
             it != m_thumbnailDocumentCache.cend(); ++it) {
            if (it.key() == m_thumbnailCurrentPath) {
                continue;
            }
            if (it->lastAccess < oldestAccess) {
                oldestAccess = it->lastAccess;
                oldestPath = it.key();
            }
        }
        if (oldestPath.isEmpty()) {
            break;
        }
        m_thumbnailDocumentCache.remove(oldestPath);
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

void MainWindow::onBookmarkNavigationRequested(const QString& documentPath,
                                               int pageNumber) {
    if (!documentModel || !m_documentOrchestrator || documentPath.isEmpty() ||
        pageNumber < 0) {
        return;
    }

    const QString normalizedPath = normalizedDocumentPath(documentPath);
    for (int index = 0; index < documentModel->getDocumentCount(); ++index) {
        if (normalizedDocumentPath(documentModel->getDocumentFilePath(index)) ==
            normalizedPath) {
            m_documentOrchestrator->switchToDocument(index);
            QTimer::singleShot(0, this, [this, pageNumber]() {
                viewWidget->goToPage(pageNumber);
            });
            return;
        }
    }

    m_pendingBookmarkNavigation = {documentPath, pageNumber};
    m_documentOrchestrator->openDocument(documentPath);
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

void MainWindow::onOpenRecentFileRequested(const QString& filePath) {
    if (m_documentOrchestrator) {
        bool success = m_documentOrchestrator->openDocument(filePath);
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

    if (m_documentOrchestrator) {
        m_documentOrchestrator->openDocument(filePath);
    }
}

void MainWindow::onWelcomeNewFileRequested() {
    LOG_DEBUG("MainWindow: New file requested from welcome screen");

    // 目前PDF阅读器不支持新建文件，显示打开文件对话框
    onWelcomeOpenFileRequested();
}

void MainWindow::onWelcomeOpenFileRequested() {
    LOG_DEBUG("MainWindow: Open file requested from welcome screen");

    openFileDialog();
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
        sideBar->setBookmarkContext(documentModel->getCurrentFilePath(),
                                    pageNumber);

        LOG_DEBUG("MainWindow: Synchronized thumbnail highlight to page {}",
                  pageNumber + 1);
    } else {
        LOG_DEBUG(
            "MainWindow: Cannot sync thumbnail highlight: sidebar or thumbnail "
            "view is null");
    }

    Q_UNUSED(totalPages)  // 避免未使用参数的警告
}

// ---------------------------------------------------------------------------
//  Action routing (replaces the former ActionDispatcher command-map)
// ---------------------------------------------------------------------------

void MainWindow::routeAction(ActionMap action) {
    switch (action) {
        // --- PDF viewer actions → ViewWidget ---
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
        case ActionMap::showSearch:
        case ActionMap::findNext:
        case ActionMap::findPrevious:
        case ActionMap::addBookmark:
            emit pdfViewerActionRequested(action);
            break;

        // --- Sidebar actions → LayoutManager ---
        case ActionMap::toggleSideBar:
            m_layoutManager->toggleSideBar();
            shortcutManager->setActionChecked(ActionMap::toggleSideBar,
                                              sideBar->isVisible());
            toolBar->setSidebarChecked(sideBar->isVisible());
            break;
        case ActionMap::showSideBar:
            m_layoutManager->showSideBar();
            shortcutManager->setActionChecked(ActionMap::toggleSideBar, true);
            toolBar->setSidebarChecked(true);
            break;
        case ActionMap::hideSideBar:
            m_layoutManager->hideSideBar();
            shortcutManager->setActionChecked(ActionMap::toggleSideBar, false);
            toolBar->setSidebarChecked(false);
            break;

        // --- View mode → ViewWidget (index matches PDFViewMode enum) ---
        case ActionMap::setSinglePageMode:
            viewWidget->setCurrentViewMode(0);
            break;
        case ActionMap::setContinuousScrollMode:
            viewWidget->setCurrentViewMode(1);
            break;
        case ActionMap::setDoublePageMode:
            viewWidget->setCurrentViewMode(2);
            break;
        case ActionMap::setGridViewMode:
            viewWidget->setCurrentViewMode(3);
            break;

        // --- Theme → StyleManager ---
        case ActionMap::toggleTheme:
            STYLE.toggleTheme();
            break;

        case ActionMap::fullScreen:
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
            shortcutManager->setActionChecked(ActionMap::fullScreen,
                                              isFullScreen());
            break;

        // --- File / tab operations (dialog + orchestrate) ---
        case ActionMap::openFile:
            openFileDialog();
            break;
        case ActionMap::openFolder:
            openFolderDialog();
            break;
        case ActionMap::exitApp:
            close();
            break;
        case ActionMap::save:
            saveDocumentAs();
            break;
        case ActionMap::newTab:
            openFileDialog();
            break;
        case ActionMap::closeTab:
        case ActionMap::closeCurrentTab:
            m_documentOrchestrator->closeCurrentDocument();
            break;
        case ActionMap::closeAllTabs: {
            bool success = true;
            while (!documentModel->isEmpty()) {
                if (!m_documentOrchestrator->closeDocument(0)) {
                    success = false;
                    break;
                }
            }
            break;
        }
        case ActionMap::nextTab: {
            int current = documentModel->getCurrentDocumentIndex();
            int count = documentModel->getDocumentCount();
            if (count > 1)
                m_documentOrchestrator->switchToDocument((current + 1) % count);
            break;
        }
        case ActionMap::prevTab: {
            int current = documentModel->getCurrentDocumentIndex();
            int count = documentModel->getDocumentCount();
            if (count > 1)
                m_documentOrchestrator->switchToDocument((current - 1 + count) %
                                                         count);
            break;
        }
        case ActionMap::saveAs:
            saveDocumentAs();
            break;
        case ActionMap::showDocumentMetadata:
            showDocumentMetadata();
            break;

        // --- Recent files ---
        case ActionMap::clearRecentFiles:
            if (recentFilesManager)
                recentFilesManager->clearRecentFiles();
            break;

        default:
            LOG_WARNING("Unhandled action in MainWindow routing: {}",
                        static_cast<int>(action));
            break;
    }
}

void MainWindow::updateShortcutActionStates() {
    const bool hasDocuments = viewWidget && viewWidget->hasDocuments();
    const int documentCount =
        documentModel ? documentModel->getDocumentCount() : 0;

    setActionsEnabled(
        shortcutManager,
        {ActionMap::save, ActionMap::saveAs, ActionMap::showDocumentMetadata,
         ActionMap::setSinglePageMode, ActionMap::setContinuousScrollMode,
         ActionMap::setDoublePageMode, ActionMap::setGridViewMode,
         ActionMap::zoomIn, ActionMap::zoomOut, ActionMap::fitToWidth,
         ActionMap::fitToPage, ActionMap::fitToHeight, ActionMap::rotateLeft,
         ActionMap::rotateRight, ActionMap::showSearch, ActionMap::findNext,
         ActionMap::findPrevious, ActionMap::addBookmark},
        hasDocuments);

    setActionsEnabled(shortcutManager,
                      {ActionMap::closeCurrentTab, ActionMap::closeAllTabs},
                      hasDocuments);
    setActionsEnabled(shortcutManager, {ActionMap::nextTab, ActionMap::prevTab},
                      documentCount > 1);

    if (hasDocuments) {
        updatePageShortcutActions(viewWidget->getCurrentPage(),
                                  viewWidget->getCurrentPageCount());
        updateViewModeShortcutActions(viewWidget->getCurrentViewMode());
    } else {
        updatePageShortcutActions(-1, 0);
        updateViewModeShortcutActions(PDFViewMode::SinglePage);
    }

    shortcutManager->setActionChecked(ActionMap::toggleSideBar,
                                      sideBar && sideBar->isVisible());
    shortcutManager->setActionChecked(ActionMap::fullScreen, isFullScreen());
    if (toolBar && sideBar) {
        toolBar->setSidebarChecked(sideBar->isVisible());
    }
}

void MainWindow::updatePageShortcutActions(int pageNumber, int totalPages) {
    const bool hasPages = totalPages > 0 && pageNumber >= 0;
    shortcutManager->setActionEnabled(ActionMap::firstPage,
                                      hasPages && pageNumber > 0);
    shortcutManager->setActionEnabled(ActionMap::previousPage,
                                      hasPages && pageNumber > 0);
    shortcutManager->setActionEnabled(ActionMap::nextPage,
                                      hasPages && pageNumber < totalPages - 1);
    shortcutManager->setActionEnabled(ActionMap::lastPage,
                                      hasPages && pageNumber < totalPages - 1);
}

void MainWindow::updateViewModeShortcutActions(PDFViewMode mode) {
    shortcutManager->setActionChecked(ActionMap::setSinglePageMode,
                                      mode == PDFViewMode::SinglePage);
    shortcutManager->setActionChecked(ActionMap::setContinuousScrollMode,
                                      mode == PDFViewMode::ContinuousScroll);
    shortcutManager->setActionChecked(ActionMap::setDoublePageMode,
                                      mode == PDFViewMode::DoublePage);
    shortcutManager->setActionChecked(ActionMap::setGridViewMode,
                                      mode == PDFViewMode::GridView);
    if (toolBar) {
        toolBar->setViewModeIndex(static_cast<int>(mode));
    }
    updateAnnotationToolbarState();
}

void MainWindow::updateAnnotationToolbarState() {
    if (!toolBar || !toolBar->annotationToolbar()) {
        return;
    }

    AnnotationToolbar* annotationToolbar = toolBar->annotationToolbar();
    const bool enabled =
        viewWidget && documentModel && !documentModel->isEmpty() &&
        viewWidget->hasDocuments() &&
        viewWidget->getCurrentViewMode() != PDFViewMode::SinglePage;
    annotationToolbar->setEnabled(enabled);
    annotationToolbar->setUnavailableReason(
        enabled ? QString() : QStringLiteral("单页模式不支持批注"));

    if (!enabled) {
        if (m_annotationToolbarEnabled && statusBar && viewWidget &&
            documentModel && !documentModel->isEmpty() &&
            viewWidget->hasDocuments() &&
            viewWidget->getCurrentViewMode() == PDFViewMode::SinglePage) {
            statusBar->setMessage(QStringLiteral("单页模式不支持批注"));
        }
        // Also turn off the canvas tool state so switching back does not leave
        // a stale annotation mode active.
        annotationToolbar->setAnnotationMode(false);
    } else if (PDFViewer* viewer = viewWidget->getCurrentViewer()) {
        viewer->setAnnotationTool(annotationToolbar->getCurrentTool());
        viewer->setAnnotationColor(annotationToolbar->getCurrentColor());
    }
    m_annotationToolbarEnabled = enabled;
}

void MainWindow::openFileDialog() {
    QStringList filePaths = QFileDialog::getOpenFileNames(
        this, tr("Open PDF Files"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("PDF Files (*.pdf)"));
    if (!filePaths.isEmpty()) {
        m_documentOrchestrator->openDocuments(filePaths);
    }
}

void MainWindow::openFolderDialog() {
    QString folderPath = QFileDialog::getExistingDirectory(
        this, tr("Open Folder"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!folderPath.isEmpty()) {
        QStringList pdfFiles = FileUtils::scanFolderForPDFs(folderPath);
        if (!pdfFiles.isEmpty()) {
            m_documentOrchestrator->openDocuments(pdfFiles);
        }
    }
}

void MainWindow::saveDocumentAs() {
    if (!documentModel || documentModel->isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先打开一个PDF文档"));
        return;
    }

    QString currentFilePath = documentModel->getCurrentFilePath();
    QString currentFileName = documentModel->getCurrentFileName();
    QString suggestedName = currentFileName.isEmpty()
                                ? "document_copy.pdf"
                                : currentFileName + "_copy.pdf";

    QString destPath = QFileDialog::getSaveFileName(
        this, tr("另存副本"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
            "/" + suggestedName,
        tr("PDF Files (*.pdf)"));

    if (destPath.isEmpty())
        return;

    if (!destPath.toLower().endsWith(".pdf"))
        destPath += ".pdf";

    // Check overwrite
    if (QFile::exists(destPath)) {
        int ret = QMessageBox::question(
            this, tr("文件已存在"),
            tr("目标文件已存在：\n%1\n\n是否覆盖？").arg(destPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }

    DocumentCopy::Result result =
        DocumentCopy::copyFile(currentFilePath, destPath);

    if (result.success) {
        QMessageBox::information(
            this, tr("保存成功"),
            tr("文档副本已成功保存到：\n%1").arg(destPath));
    } else {
        QMessageBox::critical(this, tr("保存失败"), result.errorMessage);
    }
}

void MainWindow::showDocumentMetadata() {
    if (!documentModel || documentModel->isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先打开一个PDF文档"));
        return;
    }

    QString currentFilePath = documentModel->getCurrentFilePath();
    auto currentDoc = documentModel->getCurrentDocument();

    auto* dialog = new DocumentMetadataDialog(this);
    dialog->setDocument(currentDoc, currentFilePath);
    dialog->exec();
    dialog->deleteLater();
}
