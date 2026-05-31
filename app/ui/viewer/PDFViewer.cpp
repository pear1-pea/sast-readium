#include "PDFViewer.h"
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QLayoutItem>
#include <QPropertyAnimation>
#include <QRect>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSize>
#include <QSizeF>
#include <QSplitter>
#include <QStackedWidget>
#include <QWheelEvent>
#include <QtCore>
#include <QtGlobal>
#include <QtWidgets>
#include <memory>
#include <stdexcept>
#include "../page/PDFPageWidget.h"
#include "../widgets/SearchWidget.h"
#include "PDFAnimations.h"
#include "PDFPrerenderer.h"
#include "PDFRenderCache.h"
#include "controller/SearchController.h"
#include "controller/ZoomController.h"
#include "managers/StyleManager.h"

struct PDFViewer::Private {
    // UI组件
    QVBoxLayout* mainLayout = nullptr;
    QStackedWidget* viewStack = nullptr;

    // 单页视图组件
    QScrollArea* singlePageScrollArea = nullptr;
    PDFPageWidget* singlePageWidget = nullptr;

    // 连续滚动视图组件
    QScrollArea* continuousScrollArea = nullptr;
    QWidget* continuousWidget = nullptr;
    QVBoxLayout* continuousLayout = nullptr;
    bool isWidgetReady = false;

    // 搜索控件
    SearchWidget* searchWidget = nullptr;

    // 搜索控制器
    SearchController* searchController = nullptr;

    // 文档数据
    std::shared_ptr<Poppler::Document> document;
    int currentPageNumber = 0;
    PDFViewMode currentViewMode = PDFViewMode::SinglePage;
    int currentRotation = 0;

    // 缩放控制
    ZoomController* zoomController = nullptr;
    double oldZoomFactor = 1.0;

    // 测试支持
    bool m_enableStyling = true;

    // 虚拟化渲染
    int visiblePageStart = -1;
    int visiblePageEnd = -1;
    int renderBuffer = 2;
    QTimer* scrollTimer = nullptr;
    QSet<QPair<int, double>> renderedPages;

    // 动画效果
    QPropertyAnimation* fadeAnimation = nullptr;
    QGraphicsOpacityEffect* opacityEffect = nullptr;

    // 键盘快捷键
    QShortcut* zoomInShortcut = nullptr;
    QShortcut* zoomOutShortcut = nullptr;
    QShortcut* fitPageShortcut = nullptr;
    QShortcut* fitWidthShortcut = nullptr;
    QShortcut* fitHeightShortcut = nullptr;
    QShortcut* rotateLeftShortcut = nullptr;
    QShortcut* rotateRightShortcut = nullptr;
    QShortcut* firstPageShortcut = nullptr;
    QShortcut* lastPageShortcut = nullptr;
    QShortcut* nextPageShortcut = nullptr;
    QShortcut* prevPageShortcut = nullptr;

    // 渲染缓存
    PDFRenderCache m_renderCache{100};

    // 动画管理器
    PDFAnimationManager* animationManager = nullptr;

    // 预渲染器
    PDFPrerenderer* prerenderer = nullptr;
};

// PDFViewer Implementation
PDFViewer::PDFViewer(QWidget* parent, bool enableStyling)
    : QWidget(parent), d(std::make_unique<Private>()) {
    d->m_enableStyling = enableStyling;

    // 初始化动画管理器
    d->animationManager = new PDFAnimationManager(this);

    // 初始化预渲染器
    d->prerenderer = new PDFPrerenderer(this);
    d->prerenderer->setStrategy(PDFPrerenderer::PrerenderStrategy::Balanced);
    d->prerenderer->setMaxWorkerThreads(2);
    d->prerenderer->setRenderCache(&d->m_renderCache);

    // 启用拖放功能
    setAcceptDrops(true);

    // 缩放控制器
    d->zoomController = new ZoomController(this);

    // 搜索控制器
    d->searchController = new SearchController(this);

    // 初始化虚拟化渲染
    d->visiblePageStart = -1;
    d->visiblePageEnd = -1;
    d->renderBuffer = 2;

    d->scrollTimer = new QTimer(this);
    d->scrollTimer->setSingleShot(true);
    d->scrollTimer->setInterval(100);

    // 初始化动画效果
    d->opacityEffect = new QGraphicsOpacityEffect(this);
    d->fadeAnimation =
        new QPropertyAnimation(d->opacityEffect, "opacity", this);
    d->fadeAnimation->setDuration(300);

    setupUI();
    setupConnections();
    setupShortcuts();
    loadZoomSettings();
}

PDFViewer::~PDFViewer() = default;

void PDFViewer::setupUI() {
    d->mainLayout = new QVBoxLayout(this);
    d->mainLayout->setContentsMargins(0, 0, 0, 0);

    // 应用样式 (仅在非测试环境中)
    if (d->m_enableStyling) {
        setStyleSheet(STYLE.getApplicationStyleSheet());
    }

    // 创建视图堆叠组件
    d->viewStack = new QStackedWidget(this);

    setupViewModes();

    // 创建搜索组件
    d->searchWidget = new SearchWidget(this);
    d->searchWidget->setVisible(false);  // 默认隐藏

    d->mainLayout->addWidget(d->searchWidget);
    d->mainLayout->addWidget(d->viewStack, 1);
}

void PDFViewer::setupViewModes() {
    // 创建单页视图
    d->singlePageScrollArea = new QScrollArea(this);
    // 便于调试
    d->singlePageScrollArea->setObjectName("singlePageScrollArea");

    d->singlePageWidget = new PDFPageWidget(d->singlePageScrollArea);
    d->singlePageWidget->setRenderCache(&d->m_renderCache);  // 设置缓存
    d->singlePageScrollArea->setWidget(d->singlePageWidget);
    d->singlePageScrollArea->setWidgetResizable(true);
    d->singlePageScrollArea->setAlignment(Qt::AlignCenter);

    // 应用样式
    if (d->m_enableStyling) {
        d->singlePageScrollArea->setStyleSheet(STYLE.getPDFViewerStyleSheet() +
                                               STYLE.getScrollBarStyleSheet());
    }

    // 创建连续滚动视图
    d->continuousScrollArea = new QScrollArea(this);
    // 便于调试
    d->continuousScrollArea->setObjectName("continuousScrollArea");

    d->continuousWidget = new QWidget(d->continuousScrollArea);
    d->continuousLayout = new QVBoxLayout(d->continuousWidget);

    d->continuousLayout->setAlignment(Qt::AlignCenter);  // 居中对齐

    if (d->m_enableStyling) {
        d->continuousLayout->setContentsMargins(STYLE.margin(), STYLE.margin(),
                                                STYLE.margin(), STYLE.margin());
        d->continuousLayout->setSpacing(STYLE.spacing() * 2);
    } else {
        d->continuousLayout->setContentsMargins(12, 12, 12, 12);
        d->continuousLayout->setSpacing(16);
    }
    d->continuousScrollArea->setWidget(d->continuousWidget);
    d->continuousScrollArea->setWidgetResizable(true);

    // 应用样式
    if (d->m_enableStyling) {
        d->continuousScrollArea->setStyleSheet(STYLE.getPDFViewerStyleSheet() +
                                               STYLE.getScrollBarStyleSheet());
    }

    // 添加到堆叠组件
    d->viewStack->addWidget(d->singlePageScrollArea);  // index 0
    d->viewStack->addWidget(d->continuousScrollArea);  // index 1

    // 为连续滚动区域安装事件过滤器以处理Ctrl+滚轮缩放
    d->continuousScrollArea->installEventFilter(this);

    // 默认显示单页视图
    d->viewStack->setCurrentIndex(0);
}

void PDFViewer::setupConnections() {
    // 搜索组件连接
    if (d->searchWidget) {
        connect(d->searchWidget, &SearchWidget::searchRequested, this,
                &PDFViewer::onSearchRequested);
        connect(d->searchWidget, &SearchWidget::resultSelected, this,
                &PDFViewer::onSearchResultSelected);
        connect(d->searchWidget, &SearchWidget::navigateToResult, this,
                &PDFViewer::onNavigateToSearchResult);
        connect(d->searchWidget, &SearchWidget::searchClosed, this,
                &PDFViewer::hideSearch);
        connect(d->searchWidget, &SearchWidget::searchCleared, this,
                &PDFViewer::clearSearchHighlights);

        // Connect search model signals for real-time highlighting
        if (d->searchWidget->getSearchModel()) {
            connect(d->searchWidget->getSearchModel(),
                    &SearchModel::realTimeResultsUpdated, this,
                    &PDFViewer::setSearchResults);
        }
    }

    // 缩放信号 — ZoomController 触发后应用到控件
    connect(d->zoomController, &ZoomController::zoomChanged, this,
            [this](double factor) {
                if (d->currentViewMode == PDFViewMode::SinglePage) {
                    d->singlePageWidget->blockSignals(true);
                    d->singlePageWidget->setScaleFactor(factor);
                    d->singlePageWidget->blockSignals(false);
                } else {
                    updateContinuousView();
                }
                d->zoomController->saveSettings();
                emit zoomChanged(factor);
            });
    connect(d->scrollTimer, &QTimer::timeout, this,
            &PDFViewer::onScrollChanged);

    // 搜索结果更新 → 重新应用高亮
    connect(d->searchController, &SearchController::resultsChanged, this,
            [this]() { updateSearchHighlightsForCurrentPage(); });

    // 页面组件信号
    connect(d->singlePageWidget, &PDFPageWidget::scaleChanged, this,
            &PDFViewer::onScaleChanged);

    // 监听 StyleManager 的样式表应用信号，确保在主题样式表应用后再更新组件
    connect(&StyleManager::instance(), &StyleManager::styleSheetApplied, this,
            [this]() { updateThemeUI(); });
}

void PDFViewer::setupShortcuts() {
    // 缩放快捷键
    d->zoomInShortcut = new QShortcut(QKeySequence("Ctrl++"), this);
    d->zoomOutShortcut = new QShortcut(QKeySequence("Ctrl+-"), this);
    d->fitPageShortcut = new QShortcut(QKeySequence("Ctrl+0"), this);
    d->fitWidthShortcut = new QShortcut(QKeySequence("Ctrl+1"), this);
    d->fitHeightShortcut = new QShortcut(QKeySequence("Ctrl+2"), this);

    // 额外缩放快捷键
    QShortcut* zoomIn2 = new QShortcut(QKeySequence("Ctrl+="), this);
    QShortcut* zoomActualSize = new QShortcut(QKeySequence("Ctrl+Alt+0"), this);
    QShortcut* zoom25 = new QShortcut(QKeySequence("Ctrl+Alt+1"), this);
    QShortcut* zoom50 = new QShortcut(QKeySequence("Ctrl+Alt+2"), this);
    QShortcut* zoom75 = new QShortcut(QKeySequence("Ctrl+Alt+3"), this);
    QShortcut* zoom100 = new QShortcut(QKeySequence("Ctrl+Alt+4"), this);
    QShortcut* zoom150 = new QShortcut(QKeySequence("Ctrl+Alt+5"), this);
    QShortcut* zoom200 = new QShortcut(QKeySequence("Ctrl+Alt+6"), this);

    // 旋转快捷键
    d->rotateLeftShortcut = new QShortcut(QKeySequence("Ctrl+L"), this);
    d->rotateRightShortcut = new QShortcut(QKeySequence("Ctrl+R"), this);
    QShortcut* rotate180 = new QShortcut(QKeySequence("Ctrl+Shift+R"), this);

    // 主题切换快捷键
    QShortcut* themeToggleShortcut =
        new QShortcut(QKeySequence("Ctrl+Shift+T"), this);

    // 导航快捷键 - 基本
    d->firstPageShortcut = new QShortcut(QKeySequence("Ctrl+Home"), this);
    d->lastPageShortcut = new QShortcut(QKeySequence("Ctrl+End"), this);
    d->nextPageShortcut = new QShortcut(QKeySequence("Page Down"), this);
    d->prevPageShortcut = new QShortcut(QKeySequence("Page Up"), this);

    // 导航快捷键 - 高级
    QShortcut* nextPage2 = new QShortcut(QKeySequence("Space"), this);
    QShortcut* prevPage2 = new QShortcut(QKeySequence("Shift+Space"), this);
    QShortcut* nextPage3 = new QShortcut(QKeySequence("Right"), this);
    QShortcut* prevPage3 = new QShortcut(QKeySequence("Left"), this);
    QShortcut* nextPage4 = new QShortcut(QKeySequence("Down"), this);
    QShortcut* prevPage4 = new QShortcut(QKeySequence("Up"), this);
    QShortcut* jump10Forward = new QShortcut(QKeySequence("Ctrl+Right"), this);
    QShortcut* jump10Backward = new QShortcut(QKeySequence("Ctrl+Left"), this);
    QShortcut* gotoPage = new QShortcut(QKeySequence("Ctrl+G"), this);

    // 视图模式快捷键
    QShortcut* toggleFullscreen = new QShortcut(QKeySequence("F11"), this);
    QShortcut* toggleSidebar = new QShortcut(QKeySequence("F9"), this);
    QShortcut* presentationMode = new QShortcut(QKeySequence("F5"), this);
    QShortcut* readingMode = new QShortcut(QKeySequence("F6"), this);

    // 搜索快捷键
    QShortcut* findShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    QShortcut* findNext = new QShortcut(QKeySequence("F3"), this);
    QShortcut* findPrev = new QShortcut(QKeySequence("Shift+F3"), this);

    // 书签快捷键
    QShortcut* addBookmark = new QShortcut(QKeySequence("Ctrl+D"), this);
    QShortcut* showBookmarks = new QShortcut(QKeySequence("Ctrl+B"), this);

    // 文档操作快捷键
    QShortcut* refresh = new QShortcut(QKeySequence("F5"), this);
    QShortcut* properties = new QShortcut(QKeySequence("Alt+Enter"), this);
    QShortcut* selectAll = new QShortcut(QKeySequence("Ctrl+A"), this);
    QShortcut* copyText = new QShortcut(QKeySequence("Ctrl+C"), this);

    // 连接快捷键信号 - 基本缩放
    connect(d->zoomInShortcut, &QShortcut::activated, this, &PDFViewer::zoomIn);
    connect(d->zoomOutShortcut, &QShortcut::activated, this,
            &PDFViewer::zoomOut);
    connect(zoomIn2, &QShortcut::activated, this, &PDFViewer::zoomIn);
    connect(d->fitPageShortcut, &QShortcut::activated, this,
            &PDFViewer::zoomToFit);
    connect(d->fitWidthShortcut, &QShortcut::activated, this,
            &PDFViewer::zoomToWidth);
    connect(d->fitHeightShortcut, &QShortcut::activated, this,
            &PDFViewer::zoomToHeight);

    // 连接预设缩放级别
    connect(zoomActualSize, &QShortcut::activated, this,
            [this]() { setZoom(1.0); });
    connect(zoom25, &QShortcut::activated, this, [this]() { setZoom(0.25); });
    connect(zoom50, &QShortcut::activated, this, [this]() { setZoom(0.5); });
    connect(zoom75, &QShortcut::activated, this, [this]() { setZoom(0.75); });
    connect(zoom100, &QShortcut::activated, this, [this]() { setZoom(1.0); });
    connect(zoom150, &QShortcut::activated, this, [this]() { setZoom(1.5); });
    connect(zoom200, &QShortcut::activated, this, [this]() { setZoom(2.0); });

    // 连接旋转快捷键
    connect(d->rotateLeftShortcut, &QShortcut::activated, this,
            &PDFViewer::rotateLeft);
    connect(d->rotateRightShortcut, &QShortcut::activated, this,
            &PDFViewer::rotateRight);
    connect(rotate180, &QShortcut::activated, this,
            [this]() { setRotation(d->currentRotation + 180); });

    // 连接主题快捷键
    connect(themeToggleShortcut, &QShortcut::activated, this,
            &PDFViewer::toggleTheme);

    // 连接基本导航快捷键
    connect(d->firstPageShortcut, &QShortcut::activated, this,
            &PDFViewer::firstPage);
    connect(d->lastPageShortcut, &QShortcut::activated, this,
            &PDFViewer::lastPage);
    connect(d->nextPageShortcut, &QShortcut::activated, this,
            &PDFViewer::nextPage);
    connect(d->prevPageShortcut, &QShortcut::activated, this,
            &PDFViewer::previousPage);

    // 连接高级导航快捷键
    connect(nextPage2, &QShortcut::activated, this, &PDFViewer::nextPage);
    connect(prevPage2, &QShortcut::activated, this, &PDFViewer::previousPage);
    connect(nextPage3, &QShortcut::activated, this, &PDFViewer::nextPage);
    connect(prevPage3, &QShortcut::activated, this, &PDFViewer::previousPage);
    connect(nextPage4, &QShortcut::activated, this, &PDFViewer::nextPage);
    connect(prevPage4, &QShortcut::activated, this, &PDFViewer::previousPage);

    // 连接跳转快捷键
    connect(jump10Forward, &QShortcut::activated, this,
            [this]() { goToPage(d->currentPageNumber + 10); });
    connect(jump10Backward, &QShortcut::activated, this,
            [this]() { goToPage(d->currentPageNumber - 10); });
    connect(gotoPage, &QShortcut::activated, this, [this]() {
        // goto-page shortcut - main toolbar handles this
    });

    // 连接视图模式快捷键
    connect(toggleFullscreen, &QShortcut::activated, this, [this]() {
        // Toggle fullscreen mode
        if (window()->isFullScreen()) {
            window()->showNormal();
        } else {
            window()->showFullScreen();
        }
    });

    connect(toggleSidebar, &QShortcut::activated, this, [this]() {
        // Emit signal to toggle sidebar
        emit sidebarToggleRequested();
    });

    // 连接搜索快捷键
    connect(findShortcut, &QShortcut::activated, this, &PDFViewer::showSearch);

    // 连接书签快捷键
    connect(addBookmark, &QShortcut::activated, this, [this]() {
        if (d->document && d->currentPageNumber >= 0) {
            emit bookmarkRequested(d->currentPageNumber);
        }
    });

    // 连接文档操作快捷键
    connect(refresh, &QShortcut::activated, this, [this]() {
        // Refresh current page
        if (d->singlePageWidget) {
            d->singlePageWidget->renderPage();
        }
    });
}

void PDFViewer::setDocument(std::shared_ptr<Poppler::Document> doc) {
    try {
        // 清理旧文档
        if (d->document) {
            d->m_renderCache.clear();  // 清理渲染缓存
        }

        d->document = doc;
        d->currentPageNumber = 0;
        d->currentRotation = 0;  // 重置旋转

        if (d->document) {
            // Configure d->document for high-quality rendering
            d->document->setRenderHint(Poppler::Document::Antialiasing, true);
            d->document->setRenderHint(Poppler::Document::TextAntialiasing,
                                       true);
            d->document->setRenderHint(Poppler::Document::TextHinting, true);
            d->document->setRenderHint(Poppler::Document::TextSlightHinting,
                                       true);
            d->document->setRenderHint(Poppler::Document::ThinLineShape, true);
            d->document->setRenderHint(Poppler::Document::OverprintPreview,
                                       true);

            // Share d->document with d->prerenderer
            if (d->prerenderer) {
                d->prerenderer->setDocument(d->document.get());
            }
            // 验证文档有效性
            int numPages = d->document->numPages();
            if (numPages <= 0) {
                throw std::runtime_error("文档没有有效页面");
            }

            // 测试第一页是否可以访问
            std::unique_ptr<Poppler::Page> testPage(d->document->page(0));
            if (!testPage) {
                throw std::runtime_error("无法访问文档页面");
            }

            updatePageDisplay();

            // 如果是连续模式，创建所有页面
            if (d->currentViewMode == PDFViewMode::ContinuousScroll) {
                createContinuousPages();
            }

            setMessage(QString("文档加载成功，共 %1 页").arg(numPages));

        } else {
            d->singlePageWidget->setPage(nullptr);

            // 清空连续视图
            QLayoutItem* item;
            while ((item = d->continuousLayout->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }

            setMessage("文档已关闭");
        }

        emit documentChanged(d->document != nullptr);

    } catch (const std::exception& e) {
        // 文档加载失败，清理状态
        d->document = nullptr;
        d->singlePageWidget->setPage(nullptr);

        setMessage(QString("文档加载失败: %1").arg(e.what()));
        qDebug() << "Document loading failed:" << e.what();

        emit documentChanged(false);
    }
}

void PDFViewer::clearDocument() { setDocument(nullptr); }

void PDFViewer::goToPage(int pageNumber) {
    goToPageWithValidation(pageNumber, false);
}

bool PDFViewer::goToPageWithValidation(int pageNumber, bool showMessage) {
    if (!d->document) {
        if (showMessage) {
            setMessage("没有打开的文档");
        }
        return false;
    }

    if (pageNumber < 0 || pageNumber >= d->document->numPages()) {
        if (showMessage) {
            setMessage(
                QString("页码超出范围 (1-%1)").arg(d->document->numPages()));
        }
        return false;
    }

    d->currentPageNumber = pageNumber;

    updatePageDisplay();

    // Update search highlights for the new page
    updateSearchHighlightsForCurrentPage();

    emit pageChanged(pageNumber);

    if (showMessage) {
        setMessage(QString("跳转到第 %1 页").arg(pageNumber + 1));
    }

    return true;
}

void PDFViewer::nextPage() {
    if (d->document && d->currentPageNumber < d->document->numPages() - 1) {
        goToPage(d->currentPageNumber + 1);
    }
}

void PDFViewer::previousPage() {
    if (d->document && d->currentPageNumber > 0) {
        goToPage(d->currentPageNumber - 1);
    }
}

void PDFViewer::firstPage() {
    if (d->document) {
        goToPage(0);
    }
}

void PDFViewer::lastPage() {
    if (d->document) {
        goToPage(d->document->numPages() - 1);
    }
}

void PDFViewer::zoomIn() { d->zoomController->zoomIn(); }

void PDFViewer::zoomOut() { d->zoomController->zoomOut(); }

void PDFViewer::zoomToFit() {
    if (!d->document)
        return;

    // 获取当前视图的viewport大小
    QScrollArea* currentScrollArea =
        (d->currentViewMode == PDFViewMode::SinglePage)
            ? d->singlePageScrollArea
            : d->continuousScrollArea;
    QSize viewportSize = currentScrollArea->viewport()->size();

    if (d->document->numPages() > 0) {
        std::unique_ptr<Poppler::Page> page(
            d->document->page(d->currentPageNumber));
        if (page) {
            QSizeF pageSize = page->pageSizeF();
            double scaleX = viewportSize.width() / pageSize.width();
            double scaleY = viewportSize.height() / pageSize.height();
            d->zoomController->setZoom(qMin(scaleX, scaleY) * 0.9,
                                       ZoomType::FitPage);
        }
    }
}

void PDFViewer::zoomToWidth() {
    if (!d->document)
        return;

    QScrollArea* currentScrollArea =
        (d->currentViewMode == PDFViewMode::SinglePage)
            ? d->singlePageScrollArea
            : d->continuousScrollArea;
    QSize viewportSize = currentScrollArea->viewport()->size();

    if (d->document->numPages() > 0) {
        std::unique_ptr<Poppler::Page> page(
            d->document->page(d->currentPageNumber));
        if (page) {
            QSizeF pageSize = page->pageSizeF();
            double scale = viewportSize.width() / pageSize.width();
            d->zoomController->setZoom(scale * 0.95, ZoomType::FitWidth);
        }
    }
}

void PDFViewer::setZoom(double factor) { d->zoomController->setZoom(factor); }

int PDFViewer::getPageCount() const {
    return d->document ? d->document->numPages() : 0;
}

double PDFViewer::getCurrentZoom() const {
    return d->zoomController->currentZoom();
}

PDFViewMode PDFViewer::getViewMode() const { return d->currentViewMode; }

int PDFViewer::getCurrentPage() const { return d->currentPageNumber; }

bool PDFViewer::hasDocument() const { return d->document != nullptr; }

void PDFViewer::updatePageDisplay() {
    if (!d->document || d->currentPageNumber < 0 ||
        d->currentPageNumber >= d->document->numPages()) {
        if (d->currentViewMode == PDFViewMode::SinglePage) {
            d->singlePageWidget->setPage(
                nullptr, d->zoomController->currentZoom(), d->currentRotation);
        }
        return;
    }

    if (d->currentViewMode == PDFViewMode::SinglePage) {
        // 添加淡入淡出动画
        if (d->fadeAnimation->state() != QAbstractAnimation::Running) {
            d->singlePageWidget->setGraphicsEffect(d->opacityEffect);
            d->fadeAnimation->setStartValue(0.3);
            d->fadeAnimation->setEndValue(1.0);
            d->fadeAnimation->start();
        }

        std::unique_ptr<Poppler::Page> page(
            d->document->page(d->currentPageNumber));
        if (page) {
            d->singlePageWidget->setPage(page.release(),
                                         d->zoomController->currentZoom(),
                                         d->currentRotation);
        }
    } else if (d->currentViewMode == PDFViewMode::ContinuousScroll) {
        // 连续滚动模式下需要滚动到对应的页面位置
        scrollToPageInContinuousView(d->currentPageNumber);
    }
}

void PDFViewer::updateContinuousView() {
    if (!d->document || d->currentViewMode != PDFViewMode::ContinuousScroll) {
        return;
    }

    // 缩放或旋转变化时，清空已渲染状态，触发重新渲染
    d->renderedPages.clear();

    // 更新占位符尺寸
    QSizeF placeholderSize(100, 140);  // 默认A4比例
    std::unique_ptr<Poppler::Page> firstPage(d->document->page(0));
    if (firstPage) {
        placeholderSize = firstPage->pageSizeF();
    }

    int placeholderWidth = static_cast<int>(placeholderSize.width() *
                                            d->zoomController->currentZoom());
    int placeholderHeight = static_cast<int>(placeholderSize.height() *
                                             d->zoomController->currentZoom());

    // 更新所有页面的占位符尺寸
    for (int i = 0; i < d->continuousLayout->count() - 1; ++i) {
        QLayoutItem* item = d->continuousLayout->itemAt(i);
        if (item && item->widget()) {
            PDFPageWidget* pageWidget =
                qobject_cast<PDFPageWidget*>(item->widget());
            if (pageWidget) {
                // 只更新占位符尺寸，不立即渲染
                if (!d->renderedPages.contains(
                        qMakePair(i, d->zoomController->currentZoom()))) {
                    pageWidget->setFixedSize(placeholderWidth,
                                             placeholderHeight);
                } else {
                    // 已渲染的页面需要重新渲染
                    pageWidget->blockSignals(true);
                    std::unique_ptr<Poppler::Page> page(d->document->page(i));
                    if (page) {
                        pageWidget->setPage(page.get(),
                                            d->zoomController->currentZoom(),
                                            d->currentRotation);
                        d->renderedPages.insert(
                            qMakePair(i, d->zoomController->currentZoom()));
                    }
                    pageWidget->blockSignals(false);
                }
            }
        }
    }

    // 触发可见页面重新渲染
    QTimer::singleShot(0, this, [this]() { updateVisiblePages(); });
}

void PDFViewer::onScaleChanged(double scale) {
    // 用户交互缩放通过 ZoomController 处理（去重、防抖、信号）
    d->zoomController->setZoom(scale);
}

void PDFViewer::setViewMode(PDFViewMode mode) {
    if (mode == d->currentViewMode) {
        return;
    }

    // 保存当前状态
    int savedPageNumber = d->currentPageNumber;
    int savedRotation = d->currentRotation;

    PDFViewMode oldMode = d->currentViewMode;
    d->currentViewMode = mode;

    try {
        // 更新UI
        // 切换视图
        if (mode == PDFViewMode::SinglePage) {
            switchToSinglePageMode();
        } else {
            switchToContinuousMode();
        }

        // 恢复状态
        d->currentPageNumber = savedPageNumber;
        d->currentRotation = savedRotation;

        // 更新显示
        updatePageDisplay();

        // 如果切换到连续滚动模式，需要设置isWidgetReady并渲染可见页面
        if (mode == PDFViewMode::ContinuousScroll) {
            // 延时0.05秒后设置isWidgetReady为true，确保布局完成
            QTimer::singleShot(50, this, [this]() {
                d->isWidgetReady = true;
                updateVisiblePages();  // 初始渲染可见页面
            });
        }

        emit viewModeChanged(mode);
        setMessage(
            QString("切换到%1模式")
                .arg(mode == PDFViewMode::SinglePage ? "单页" : "连续滚动"));

    } catch (const std::exception& e) {
        // 恢复到原来的模式
        d->currentViewMode = oldMode;

        setMessage(QString("切换视图模式失败: %1").arg(e.what()));
        qDebug() << "View mode switch failed:" << e.what();
    }
}

void PDFViewer::switchToSinglePageMode() {
    d->viewStack->setCurrentIndex(0);
    updatePageDisplay();
}

void PDFViewer::switchToContinuousMode() {
    d->viewStack->setCurrentIndex(1);
    if (d->document) {
        createContinuousPages();
    }
}

void PDFViewer::createContinuousPages() {
    if (!d->document)
        return;

    // 清空现有页面
    QLayoutItem* item;
    while ((item = d->continuousLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    // 清空渲染状态
    d->renderedPages.clear();

    // 获取第一页尺寸用于占位符
    QSizeF placeholderSize(100, 140);  // 默认A4比例
    std::unique_ptr<Poppler::Page> firstPage(d->document->page(0));
    if (firstPage) {
        placeholderSize = firstPage->pageSizeF();
    }

    // 应用缩放后的尺寸
    double scale = d->zoomController->currentZoom();
    int placeholderWidth = static_cast<int>(placeholderSize.width() * scale);
    int placeholderHeight = static_cast<int>(placeholderSize.height() * scale);

    // 创建所有页面占位符（不立即渲染）
    for (int i = 0; i < d->document->numPages(); ++i) {
        PDFPageWidget* pageWidget = new PDFPageWidget(d->continuousWidget);
        pageWidget->setRenderCache(&d->m_renderCache);  // 设置缓存

        // 设置占位符尺寸，但不渲染内容
        pageWidget->setFixedSize(placeholderWidth, placeholderHeight);
        pageWidget->setText(QString("第 %1 页").arg(i + 1));  // 显示占位文本

        d->continuousLayout->addWidget(pageWidget);

        // 连接信号
        connect(pageWidget, &PDFPageWidget::scaleChanged, this,
                &PDFViewer::onScaleChanged);
    }

    d->continuousLayout->addStretch();

    // 连接滚动区域的滚动信号以实现虚拟化渲染
    if (d->continuousScrollArea->verticalScrollBar()) {
        connect(d->continuousScrollArea->verticalScrollBar(),
                &QScrollBar::valueChanged, this, [this]() {
                    d->scrollTimer->start();  // 使用防抖
                });
    }

    // 确保滚动条正确初始化
    d->continuousScrollArea->verticalScrollBar()->setValue(0);

    // 立即渲染初始可见页面
    QTimer::singleShot(0, this, [this]() { updateVisiblePages(); });
}

void PDFViewer::updateVisiblePages() {
    if (!d->document || d->currentViewMode != PDFViewMode::ContinuousScroll ||
        !d->isWidgetReady)
        return;

    QScrollBar* scrollBar = d->continuousScrollArea->verticalScrollBar();
    int viewportTop = scrollBar->value();
    int viewportBottom =
        viewportTop + d->continuousScrollArea->viewport()->height();
    int bufferPx = (viewportBottom - viewportTop);  // 增加缓冲区到一屏高度

    int newVisibleStart = -1;
    int newVisibleEnd = -1;

    int count = d->continuousLayout->count();
    for (int i = 0; i < count; ++i) {
        QWidget* w = d->continuousLayout->itemAt(i)->widget();
        if (!w)
            continue;

        int top = w->y();
        int bottom = top + w->height();

        // 判定是否在视口（含缓冲区）内
        if (bottom >= (viewportTop - bufferPx) &&
            top <= (viewportBottom + bufferPx)) {
            if (newVisibleStart == -1)
                newVisibleStart = i;
            newVisibleEnd = i;
        } else if (newVisibleStart != -1) {
            // 性能优化：既然是垂直排列，一旦离开可见区域就可以停止遍历
            break;
        }
    }

    // 兜底：如果没找到（可能布局还没完成），至少渲染第一页或当前估算的页面
    if (newVisibleStart == -1) {
        newVisibleStart = 0;
        newVisibleEnd = 0;
    }

    if (qAbs(d->oldZoomFactor - d->zoomController->currentZoom()) > 0.001) {
        // 如果缩放变化，强制重新渲染所有可见页面
        // 具体决定渲染什么页面在 renderVisiblePages 里处理
        d->renderedPages.clear();
    } else {
        // 否则，只渲染新增可见的页面
        d->renderedPages.removeIf([this, newVisibleStart,
                                   newVisibleEnd](const auto& key) {
            int pageIndex = key.first;
            double zoom = key.second;
            return (zoom != d->zoomController->currentZoom()) &&  // 缩放不同
                   (pageIndex >= newVisibleStart &&
                    pageIndex <= newVisibleEnd);  // 在可见范围内
        });
    }

    d->oldZoomFactor = d->zoomController->currentZoom();

    d->visiblePageStart = newVisibleStart;
    d->visiblePageEnd = newVisibleEnd;
    renderVisiblePages();
}

void PDFViewer::renderVisiblePages() {
    if (!d->document || !d->isWidgetReady)
        return;

    for (int i = d->visiblePageStart; i <= d->visiblePageEnd; ++i) {
        if (i < 0 || i >= d->continuousLayout->count())
            continue;

        // 如果已经渲染过且缩放没变，则跳过
        if (d->renderedPages.contains(
                qMakePair(i, d->zoomController->currentZoom())))
            continue;

        QLayoutItem* item = d->continuousLayout->itemAt(i);
        PDFPageWidget* pageWidget =
            qobject_cast<PDFPageWidget*>(item ? item->widget() : nullptr);

        if (pageWidget) {
            int pageIndex = i;
            // 使用 Lambda 捕获当前缩放，防止异步执行时缩放已变
            double zoom = d->zoomController->currentZoom();
            int rotation = d->currentRotation;

            // 标记为已提交渲染，防止重复触发 QTimer
            d->renderedPages.insert(qMakePair(pageIndex, zoom));

            QTimer::singleShot(
                0, this, [this, pageWidget, pageIndex, zoom, rotation]() {
                    if (!d->document)
                        return;

                    // 再次检查，防止重复渲染
                    if (qAbs(zoom - d->zoomController->currentZoom()) > 0.001)
                        return;

                    std::unique_ptr<Poppler::Page> page(
                        d->document->page(pageIndex));
                    if (page) {
                        // 内部应处理：如果请求的 zoom
                        // 与当前已显示的相同，则不重绘
                        pageWidget->setPage(page.release(), zoom, rotation);
                    }
                });
        }
    }
}

void PDFViewer::onScrollChanged() {
    if (d->currentViewMode == PDFViewMode::ContinuousScroll) {
        updateVisiblePages();
    }
}

void PDFViewer::scrollToPageInContinuousView(int pageNumber) {
    if (!d->document || d->currentViewMode != PDFViewMode::ContinuousScroll ||
        pageNumber < 0 || pageNumber >= d->document->numPages()) {
        return;
    }

    // 确保连续视图布局已经创建
    if (d->continuousLayout->count() <= pageNumber) {
        return;
    }

    // 获取目标页面的widget
    QLayoutItem* item = d->continuousLayout->itemAt(pageNumber);
    if (!item || !item->widget()) {
        return;
    }

    QWidget* pageWidget = item->widget();

    // 计算滚动位置，将页面滚动到视口中央
    int targetY =
        pageWidget->y() -
        (d->continuousScrollArea->viewport()->height() - pageWidget->height()) /
            2;

    // 限制在有效范围内
    QScrollBar* scrollBar = d->continuousScrollArea->verticalScrollBar();
    targetY = qBound(scrollBar->minimum(), targetY, scrollBar->maximum());

    // 平滑滚动到目标位置
    scrollBar->setValue(targetY);

    // 确保目标页面被渲染
    updateVisiblePages();
}

void PDFViewer::toggleTheme() {
    STYLE.toggleTheme();

    setMessage(QString("已切换到%1主题")
                   .arg(STYLE.currentTheme() == Theme::Dark ? "暗色" : "亮色"));
}

void PDFViewer::updateThemeUI() {
    // 重新应用主界面样式
    setStyleSheet(STYLE.getApplicationStyleSheet());

    // 更新滚动区域样式
    QString scrollStyle =
        STYLE.getPDFViewerStyleSheet() + STYLE.getScrollBarStyleSheet();
    if (d->singlePageScrollArea)
        d->singlePageScrollArea->setStyleSheet(scrollStyle);
    if (d->continuousScrollArea)
        d->continuousScrollArea->setStyleSheet(scrollStyle);

    // 递归更新所有子控件的样式，强制重新绘制
    QList<QWidget*> allWidgets = findChildren<QWidget*>();
    for (QWidget* widget : allWidgets) {
        // 取消样式缓存并重新应用
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }

    // 强制整体重绘
    update();
    repaint();
}

void PDFViewer::setZoomFromPercentage(int percentage) {
    d->zoomController->setFromPercentage(percentage);
}

void PDFViewer::setZoomWithType(double factor, ZoomType type) {
    if (!d->document || d->document->numPages() == 0) {
        return;
    }
    d->zoomController->setZoom(factor, type);
}

void PDFViewer::zoomToHeight() {
    if (!d->document)
        return;

    QScrollArea* currentScrollArea =
        (d->currentViewMode == PDFViewMode::SinglePage)
            ? d->singlePageScrollArea
            : d->continuousScrollArea;
    QSize viewportSize = currentScrollArea->viewport()->size();

    if (d->document->numPages() > 0) {
        std::unique_ptr<Poppler::Page> page(
            d->document->page(d->currentPageNumber));
        if (page) {
            QSizeF pageSize = page->pageSizeF();
            double scale = viewportSize.height() / pageSize.height();
            d->zoomController->setZoom(scale * 0.95, ZoomType::FitHeight);
        }
    }
}

void PDFViewer::saveZoomSettings() { d->zoomController->saveSettings(); }

void PDFViewer::loadZoomSettings() { d->zoomController->loadSettings(); }

bool PDFViewer::eventFilter(QObject* object, QEvent* event) {
    // 处理连续滚动区域的Ctrl+滚轮缩放
    if (object == d->continuousScrollArea && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier) {
            int delta = wheelEvent->angleDelta().y();
            if (delta != 0) {
                double scaleDelta = delta > 0 ? 1.15 : (1.0 / 1.15);
                double newZoom = d->zoomController->currentZoom() * scaleDelta;
                d->zoomController->setZoom(newZoom);
            }
            return true;
        }
    }

    return QWidget::eventFilter(object, event);
}

void PDFViewer::keyPressEvent(QKeyEvent* event) {
    QWidget::keyPressEvent(event);
}

void PDFViewer::setMessage(const QString& message) {
    // 发出信号让主窗口显示消息
    // 这里可以通过信号传递给StatusBar或者其他消息显示组件
    qDebug() << "PDFViewer Message:" << message;
}

void PDFViewer::rotateLeft() {
    if (!d->document || d->document->numPages() == 0) {
        setMessage("没有可旋转的文档");
        return;
    }
    setRotation(d->currentRotation - 90);
}

void PDFViewer::rotateRight() {
    if (!d->document || d->document->numPages() == 0) {
        setMessage("没有可旋转的文档");
        return;
    }
    setRotation(d->currentRotation + 90);
}

void PDFViewer::resetRotation() {
    if (!d->document || d->document->numPages() == 0) {
        setMessage("没有可重置的文档");
        return;
    }
    setRotation(0);
}

void PDFViewer::setRotation(int degrees) {
    // 检查文档有效性
    if (!d->document || d->document->numPages() == 0) {
        qDebug() << "Cannot rotate: no valid d->document";
        return;
    }

    // 确保旋转角度是90度的倍数
    degrees = ((degrees % 360) + 360) % 360;

    if (degrees != d->currentRotation) {
        int oldRotation = d->currentRotation;
        d->currentRotation = degrees;

        try {
            // 更新当前视图
            if (d->currentViewMode == PDFViewMode::SinglePage) {
                if (d->currentPageNumber >= 0 &&
                    d->currentPageNumber < d->document->numPages()) {
                    std::unique_ptr<Poppler::Page> page(
                        d->document->page(d->currentPageNumber));
                    if (page) {
                        d->singlePageWidget->setPage(
                            page.release(), d->zoomController->currentZoom(),
                            d->currentRotation);
                    } else {
                        throw std::runtime_error(
                            "Failed to get page for rotation");
                    }
                }
            } else {
                // 更新连续视图中的所有页面
                updateContinuousViewRotation();
            }

            emit rotationChanged(d->currentRotation);
            setMessage(QString("页面已旋转到 %1 度").arg(d->currentRotation));
        } catch (const std::exception& e) {
            // 恢复旧的旋转状态
            d->currentRotation = oldRotation;
            setMessage(QString("旋转失败: %1").arg(e.what()));
            qDebug() << "Rotation failed:" << e.what();
        }
    }
}

void PDFViewer::updateContinuousViewRotation() {
    if (!d->document || d->currentViewMode != PDFViewMode::ContinuousScroll) {
        return;
    }

    // 旋转变化时，清空已渲染状态，触发重新渲染
    d->renderedPages.clear();

    int totalPages =
        d->continuousLayout->count() - 1;  // -1 因为最后一个是stretch

    // 更新连续视图中所有页面的旋转
    // 使用延迟渲染，避免卡顿
    for (int i = 0; i < totalPages; ++i) {
        QLayoutItem* item = d->continuousLayout->itemAt(i);
        if (item && item->widget()) {
            PDFPageWidget* pageWidget =
                qobject_cast<PDFPageWidget*>(item->widget());
            if (pageWidget && i < d->document->numPages()) {
                // 只更新占位符文本，实际渲染由 renderVisiblePages 处理
                pageWidget->setText(QString("第 %1 页").arg(i + 1));
            }
        }
    }

    // 触发可见页面重新渲染
    QTimer::singleShot(0, this, [this]() { updateVisiblePages(); });
}

// 搜索功能实现
void PDFViewer::showSearch() {
    if (d->searchWidget) {
        d->searchWidget->setVisible(true);
        d->searchWidget->focusSearchInput();
        d->searchWidget->setDocument(d->document);
    }
}

void PDFViewer::hideSearch() {
    if (d->searchWidget) {
        d->searchWidget->setVisible(false);
        d->searchWidget->clearSearch();
    }
}

void PDFViewer::toggleSearch() {
    if (d->searchWidget) {
        if (d->searchWidget->isVisible()) {
            hideSearch();
        } else {
            showSearch();
        }
    }
}

void PDFViewer::findNext() {
    if (d->searchWidget && d->searchWidget->isVisible()) {
        d->searchWidget->nextResult();
    }
}

void PDFViewer::findPrevious() {
    if (d->searchWidget && d->searchWidget->isVisible()) {
        d->searchWidget->previousResult();
    }
}

void PDFViewer::clearSearch() {
    if (d->searchWidget) {
        d->searchWidget->clearSearch();
    }
}

// 搜索相关槽函数
void PDFViewer::onSearchRequested(const QString& query,
                                  const SearchOptions& options) {
    clearSearchHighlights();

    if (!query.isEmpty() && d->document) {
        setMessage(QString("搜索: %1").arg(query));
    }
}

void PDFViewer::onSearchResultSelected(const SearchResult& result) {
    if (result.pageNumber >= 0) {
        goToPage(result.pageNumber);
        d->searchController->highlightCurrent(result);
    }
}

void PDFViewer::onNavigateToSearchResult(int pageNumber, const QRectF& rect) {
    if (pageNumber >= 0 &&
        pageNumber < (d->document ? d->document->numPages() : 0)) {
        goToPage(pageNumber);
        updateSearchHighlightsForCurrentPage();
        setMessage(QString("已导航到第 %1 页的搜索结果").arg(pageNumber + 1));
    }
}

// Search highlighting implementation
void PDFViewer::setSearchResults(const QList<SearchResult>& results) {
    d->searchController->setResults(results);
}

void PDFViewer::clearSearchHighlights() {
    d->searchController->clear();

    // Clear highlights from current page widget
    if (d->currentViewMode == PDFViewMode::SinglePage && d->singlePageWidget) {
        d->singlePageWidget->clearSearchHighlights();
    } else if (d->currentViewMode == PDFViewMode::ContinuousScroll) {
        for (int i = 0; i < d->continuousLayout->count() - 1; ++i) {
            QLayoutItem* item = d->continuousLayout->itemAt(i);
            if (item && item->widget()) {
                PDFPageWidget* pageWidget =
                    qobject_cast<PDFPageWidget*>(item->widget());
                if (pageWidget) {
                    pageWidget->clearSearchHighlights();
                }
            }
        }
    }
}

void PDFViewer::highlightCurrentSearchResult(const SearchResult& result) {
    d->searchController->highlightCurrent(result);
}

void PDFViewer::updateSearchHighlightsForCurrentPage() {
    if (d->searchController->isEmpty()) {
        return;
    }

    if (d->currentViewMode == PDFViewMode::SinglePage && d->singlePageWidget) {
        QList<SearchResult> pageResults =
            d->searchController->resultsForPage(d->currentPageNumber);
        d->singlePageWidget->setSearchResults(pageResults);

    } else if (d->currentViewMode == PDFViewMode::ContinuousScroll) {
        updateAllPagesSearchHighlights();
    }
}

void PDFViewer::updateAllPagesSearchHighlights() {
    if (d->searchController->isEmpty() ||
        d->currentViewMode != PDFViewMode::ContinuousScroll) {
        return;
    }

    QHash<int, QList<SearchResult>> byPage =
        d->searchController->resultsByPage();

    for (int pageNum = 0; pageNum < d->continuousLayout->count() - 1;
         ++pageNum) {
        QLayoutItem* item = d->continuousLayout->itemAt(pageNum);
        if (item && item->widget()) {
            PDFPageWidget* pageWidget =
                qobject_cast<PDFPageWidget*>(item->widget());
            if (pageWidget) {
                if (byPage.contains(pageNum)) {
                    pageWidget->setSearchResults(byPage[pageNum]);
                } else {
                    pageWidget->clearSearchHighlights();
                }
            }
        }
    }
}

// 书签功能实现
void PDFViewer::addBookmark() {
    if (d->document && d->currentPageNumber >= 0) {
        addBookmarkForPage(d->currentPageNumber);
    }
}

void PDFViewer::addBookmarkForPage(int pageNumber) {
    if (!d->document || pageNumber < 0 ||
        pageNumber >= d->document->numPages()) {
        setMessage("无法添加书签：页面无效");
        return;
    }

    // 发出书签请求信号，让上层组件处理
    emit bookmarkRequested(pageNumber);
    setMessage(QString("已为第 %1 页添加书签").arg(pageNumber + 1));
}

void PDFViewer::removeBookmark() {
    if (d->document && d->currentPageNumber >= 0) {
        // 这里需要与BookmarkWidget集成来实际删除书签
        // 目前只是发出信号
        setMessage(
            QString("已移除第 %1 页的书签").arg(d->currentPageNumber + 1));
    }
}

void PDFViewer::toggleBookmark() {
    if (hasBookmarkForCurrentPage()) {
        removeBookmark();
    } else {
        addBookmark();
    }
}

bool PDFViewer::hasBookmarkForCurrentPage() const {
    // 这里需要与BookmarkWidget集成来检查书签状态
    // 目前返回false作为占位符
    return false;
}

void PDFViewer::setCacheSize(int maxCostMB) {
    d->m_renderCache.setMaxCost(maxCostMB);
}

int PDFViewer::getCacheSize() const { return d->m_renderCache.maxCost(); }

void PDFViewer::clearCache() { d->m_renderCache.clear(); }
