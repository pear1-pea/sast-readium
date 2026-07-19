#include "PDFViewer.h"
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QPropertyAnimation>
#include <QRect>
#include <QScrollBar>
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
#include <chrono>
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
#include "model/AnnotationModel.h"
#include "ui/continuous/PDFContinuousCanvas.h"
#include "ui/continuous/PDFContinuousImageCache.h"
#include "ui/continuous/PDFContinuousRenderScheduler.h"
#include "utils/LoggingMacros.h"

struct PDFViewer::Private {
    // UI组件
    QVBoxLayout* mainLayout = nullptr;
    QStackedWidget* viewStack = nullptr;

    // 单页视图组件
    QScrollArea* singlePageScrollArea = nullptr;
    PDFPageWidget* singlePageWidget = nullptr;

    // 连续滚动视图组件
    QAbstractScrollArea* continuousScrollArea = nullptr;
    PDFContinuousCanvas* continuousCanvas = nullptr;
    bool isWidgetReady = false;

    // Profiling state
    bool continuousProfilingActive = false;
    std::chrono::steady_clock::time_point continuousSwitchStart;
    int profiledTargetPage = -1;
    int renderSamplesRemaining = 0;

    // 搜索控件
    SearchWidget* searchWidget = nullptr;

    // 搜索控制器
    SearchController* searchController = nullptr;
    AnnotationModel* annotationModel = nullptr;

    // 文档数据
    std::shared_ptr<Poppler::Document> document;
    int currentPageNumber = 0;
    PDFViewMode currentViewMode = PDFViewMode::SinglePage;
    int currentRotation = 0;

    // 缩放控制
    ZoomController* zoomController = nullptr;

    // 测试支持
    bool m_enableStyling = true;

    // 虚拟化渲染
    int visiblePageStart = -1;
    int visiblePageEnd = -1;
    int renderBuffer = 2;
    QTimer* scrollTimer = nullptr;

    // 动画效果
    QPropertyAnimation* fadeAnimation = nullptr;
    QGraphicsOpacityEffect* opacityEffect = nullptr;

    // 渲染缓存
    PDFRenderCache m_renderCache{100};
    PDFContinuousImageCache continuousImageCache;
    PDFContinuousRenderScheduler* continuousRenderScheduler = nullptr;

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

    d->continuousRenderScheduler = new PDFContinuousRenderScheduler(this);

    // 启用拖放功能
    setAcceptDrops(true);

    // 缩放控制器
    d->zoomController = new ZoomController(this);

    // 搜索控制器
    d->searchController = new SearchController(this);
    d->annotationModel = new AnnotationModel(this);

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
    d->continuousScrollArea = new QAbstractScrollArea(this);
    d->continuousScrollArea->setObjectName("continuousScrollArea");
    d->continuousCanvas =
        new PDFContinuousCanvas(d->continuousScrollArea->viewport());
    d->continuousCanvas->setImageCache(&d->continuousImageCache);
    d->continuousCanvas->setRenderScheduler(d->continuousRenderScheduler);
    d->continuousCanvas->setGeometry(
        d->continuousScrollArea->viewport()->rect());
    d->continuousCanvas->show();

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
    d->continuousScrollArea->viewport()->installEventFilter(this);

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

    connect(d->continuousScrollArea->verticalScrollBar(),
            &QScrollBar::valueChanged, this, [this](int value) {
                if (d->continuousCanvas) {
                    d->continuousCanvas->setContentOffsetY(value);
                }
                d->scrollTimer->start();
            });

    connect(d->continuousCanvas,
            &PDFContinuousCanvas::currentPageCandidateChanged, this,
            [this](int pageIndex) {
                if (pageIndex >= 0 && pageIndex != d->currentPageNumber) {
                    d->currentPageNumber = pageIndex;
                    emit pageChanged(pageIndex);
                }
            });
    connect(d->continuousCanvas, &PDFContinuousCanvas::gotoLinkClicked, this,
            [this](int pageIndex) { goToPage(pageIndex); });
    connect(d->continuousCanvas, &PDFContinuousCanvas::browseLinkClicked, this,
            [](const QString& url) { QDesktopServices::openUrl(QUrl(url)); });

    connect(
        d->annotationModel, &AnnotationModel::annotationsLoaded, this,
        [this](int) {
            if (d->continuousCanvas) {
                QHash<int, QList<PDFAnnotation>> annotationsByPage;
                for (const PDFAnnotation& annotation :
                     d->annotationModel->getAllAnnotations()) {
                    annotationsByPage[annotation.pageNumber].append(annotation);
                }
                d->continuousCanvas->setAnnotations(annotationsByPage);
            }
        });
    connect(
        d->annotationModel, &AnnotationModel::annotationAdded, this,
        [this](const PDFAnnotation&) {
            if (d->continuousCanvas) {
                QHash<int, QList<PDFAnnotation>> annotationsByPage;
                for (const PDFAnnotation& annotation :
                     d->annotationModel->getAllAnnotations()) {
                    annotationsByPage[annotation.pageNumber].append(annotation);
                }
                d->continuousCanvas->setAnnotations(annotationsByPage);
            }
        });
    connect(
        d->annotationModel, &AnnotationModel::annotationUpdated, this,
        [this](const PDFAnnotation&) {
            if (d->continuousCanvas) {
                QHash<int, QList<PDFAnnotation>> annotationsByPage;
                for (const PDFAnnotation& annotation :
                     d->annotationModel->getAllAnnotations()) {
                    annotationsByPage[annotation.pageNumber].append(annotation);
                }
                d->continuousCanvas->setAnnotations(annotationsByPage);
            }
        });
    connect(
        d->annotationModel, &AnnotationModel::annotationRemoved, this,
        [this](const QString&) {
            if (d->continuousCanvas) {
                QHash<int, QList<PDFAnnotation>> annotationsByPage;
                for (const PDFAnnotation& annotation :
                     d->annotationModel->getAllAnnotations()) {
                    annotationsByPage[annotation.pageNumber].append(annotation);
                }
                d->continuousCanvas->setAnnotations(annotationsByPage);
            }
        });
    connect(d->annotationModel, &AnnotationModel::annotationsCleared, this,
            [this]() {
                if (d->continuousCanvas) {
                    d->continuousCanvas->clearAnnotationOverlays();
                }
            });

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
    QShortcut* zoomActualSize = new QShortcut(QKeySequence("Ctrl+Alt+0"), this);
    QShortcut* zoom25 = new QShortcut(QKeySequence("Ctrl+Alt+1"), this);
    QShortcut* zoom50 = new QShortcut(QKeySequence("Ctrl+Alt+2"), this);
    QShortcut* zoom75 = new QShortcut(QKeySequence("Ctrl+Alt+3"), this);
    QShortcut* zoom100 = new QShortcut(QKeySequence("Ctrl+Alt+4"), this);
    QShortcut* zoom150 = new QShortcut(QKeySequence("Ctrl+Alt+5"), this);
    QShortcut* zoom200 = new QShortcut(QKeySequence("Ctrl+Alt+6"), this);

    QShortcut* rotate180 = new QShortcut(QKeySequence("Ctrl+Shift+R"), this);

    // 导航快捷键 - 高级
    QShortcut* nextPage2 = new QShortcut(QKeySequence("Space"), this);
    QShortcut* prevPage2 = new QShortcut(QKeySequence("Shift+Space"), this);
    QShortcut* nextPage3 = new QShortcut(QKeySequence("Right"), this);
    QShortcut* prevPage3 = new QShortcut(QKeySequence("Left"), this);
    QShortcut* nextPage4 = new QShortcut(QKeySequence("Down"), this);
    QShortcut* prevPage4 = new QShortcut(QKeySequence("Up"), this);
    QShortcut* jump10Forward = new QShortcut(QKeySequence("Alt+Right"), this);
    QShortcut* jump10Backward = new QShortcut(QKeySequence("Alt+Left"), this);

    // 书签快捷键
    QShortcut* addBookmark = new QShortcut(QKeySequence("Ctrl+D"), this);

    // 连接预设缩放级别
    connect(zoomActualSize, &QShortcut::activated, this,
            [this]() { setZoom(1.0); });
    connect(zoom25, &QShortcut::activated, this, [this]() { setZoom(0.25); });
    connect(zoom50, &QShortcut::activated, this, [this]() { setZoom(0.5); });
    connect(zoom75, &QShortcut::activated, this, [this]() { setZoom(0.75); });
    connect(zoom100, &QShortcut::activated, this, [this]() { setZoom(1.0); });
    connect(zoom150, &QShortcut::activated, this, [this]() { setZoom(1.5); });
    connect(zoom200, &QShortcut::activated, this, [this]() { setZoom(2.0); });

    connect(rotate180, &QShortcut::activated, this,
            [this]() { setRotation(d->currentRotation + 180); });

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

    // 连接书签快捷键
    connect(addBookmark, &QShortcut::activated, this, [this]() {
        if (d->document && d->currentPageNumber >= 0) {
            emit bookmarkRequested(d->currentPageNumber);
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
            if (d->continuousRenderScheduler) {
                d->continuousRenderScheduler->setDocument(d->document);
            }
            if (d->continuousCanvas) {
                d->continuousCanvas->setDocument(d->document);
            }
            if (d->annotationModel) {
                d->annotationModel->setDocument(d->document);
            }
            d->continuousImageCache.clear();
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
            d->continuousImageCache.clear();
            if (d->continuousRenderScheduler) {
                d->continuousRenderScheduler->setDocument(nullptr);
            }
            if (d->continuousCanvas) {
                d->continuousCanvas->setDocument(nullptr);
                d->continuousCanvas->setBlueprint(nullptr);
            }
            if (d->annotationModel) {
                d->annotationModel->setDocument(nullptr);
            }
            d->continuousScrollArea->verticalScrollBar()->setRange(0, 0);

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
    QAbstractScrollArea* currentScrollArea =
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

    QAbstractScrollArea* currentScrollArea =
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
        if (!d->isWidgetReady) {
            return;
        }

        // 连续滚动模式下需要滚动到对应的页面位置
        scrollToPageInContinuousView(d->currentPageNumber);
    }
}

void PDFViewer::updateContinuousView() {
    if (!d->document || d->currentViewMode != PDFViewMode::ContinuousScroll) {
        return;
    }

    const auto oldBlueprint = d->continuousCanvas->blueprint();
    const PDFViewportAnchor anchor =
        oldBlueprint ? oldBlueprint->captureAnchor(
                           d->continuousCanvas->viewportRectInDocument(),
                           d->currentPageNumber)
                     : PDFViewportAnchor{};
    rebuildContinuousCanvasBlueprint();
    const auto newBlueprint = d->continuousCanvas->blueprint();
    if (newBlueprint && anchor.pageIndex >= 0) {
        d->continuousScrollArea->verticalScrollBar()->setValue(
            qRound(newBlueprint->restoreAnchorOffset(
                anchor, d->continuousScrollArea->viewport()->height())));
    }
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
        if (mode == PDFViewMode::ContinuousScroll) {
            d->continuousProfilingActive = true;
            d->continuousSwitchStart = std::chrono::steady_clock::now();
            d->profiledTargetPage = savedPageNumber;
            d->renderSamplesRemaining = 3;
            LOG_INFO(
                "[continuous-prof] switch start mode=continuous page={} "
                "pages={} zoom={:.3f} rotation={}",
                savedPageNumber, d->document ? d->document->numPages() : 0,
                d->zoomController->currentZoom(), savedRotation);
        } else {
            d->continuousProfilingActive = false;
            d->profiledTargetPage = -1;
            d->renderSamplesRemaining = 0;
        }

        // 切换视图
        if (mode == PDFViewMode::SinglePage) {
            d->currentPageNumber = savedPageNumber;
            d->currentRotation = savedRotation;
            switchToSinglePageMode();
            updatePageDisplay();
        } else {
            d->currentPageNumber = savedPageNumber;
            d->currentRotation = savedRotation;
            switchToContinuousMode();

            QTimer::singleShot(0, this, [this, savedPageNumber]() {
                if (d->currentViewMode != PDFViewMode::ContinuousScroll ||
                    !d->document) {
                    return;
                }

                auto readyMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() -
                        d->continuousSwitchStart)
                        .count();
                LOG_INFO(
                    "[continuous-prof] ready callback elapsed_ms={} "
                    "scroll_max={}",
                    readyMs,
                    d->continuousScrollArea->verticalScrollBar()->maximum());

                d->isWidgetReady = true;
                d->currentPageNumber = savedPageNumber;

                d->visiblePageStart =
                    qMax(0, savedPageNumber - d->renderBuffer);
                d->visiblePageEnd = qMin(d->document->numPages() - 1,
                                         savedPageNumber + d->renderBuffer);
                LOG_INFO(
                    "[continuous-prof] initial render window start={} end={} "
                    "target_page={}",
                    d->visiblePageStart, d->visiblePageEnd, savedPageNumber);
                renderVisiblePages();

                finalizeContinuousModeInitialization(savedPageNumber);

                auto totalMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() -
                        d->continuousSwitchStart)
                        .count();
                LOG_INFO(
                    "[continuous-prof] switch scheduled target page={} "
                    "elapsed_ms={}",
                    savedPageNumber, totalMs);
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
    d->isWidgetReady = false;
    d->scrollTimer->stop();
    d->visiblePageStart = -1;
    d->visiblePageEnd = -1;
    d->viewStack->setCurrentIndex(0);
}

void PDFViewer::switchToContinuousMode() {
    d->isWidgetReady = false;
    d->scrollTimer->stop();
    d->visiblePageStart = -1;
    d->visiblePageEnd = -1;
    d->viewStack->setCurrentIndex(1);
    if (d->document) {
        createContinuousPages();
    }
}

void PDFViewer::createContinuousPages() { rebuildContinuousCanvasBlueprint(); }

void PDFViewer::rebuildContinuousCanvasBlueprint() {
    if (!d->document || !d->continuousCanvas) {
        return;
    }

    auto rebuildStart = std::chrono::steady_clock::now();
    QVector<QSizeF> pageSizes;
    pageSizes.reserve(d->document->numPages());
    for (int i = 0; i < d->document->numPages(); ++i) {
        std::unique_ptr<Poppler::Page> page(d->document->page(i));
        pageSizes.push_back(page ? page->pageSizeF() : QSizeF(100, 140));
    }

    PDFContinuousLayoutOptions options;
    options.zoom = d->zoomController->currentZoom();
    options.rotation = d->currentRotation;
    options.viewportWidth = d->continuousScrollArea->viewport()->width();
    if (d->m_enableStyling) {
        options.documentMargins = QMarginsF(STYLE.margin(), STYLE.margin(),
                                            STYLE.margin(), STYLE.margin());
        options.pageSpacing = STYLE.spacing() * 2;
    }

    auto blueprint = std::make_shared<PDFContinuousBlueprint>();
    blueprint->rebuild(pageSizes, options);
    d->continuousCanvas->setBlueprint(blueprint);
    updateContinuousCanvasGeometry();
    updateAllPagesSearchHighlights();
    if (d->annotationModel) {
        QHash<int, QList<PDFAnnotation>> annotationsByPage;
        for (const PDFAnnotation& annotation :
             d->annotationModel->getAllAnnotations()) {
            annotationsByPage[annotation.pageNumber].append(annotation);
        }
        d->continuousCanvas->setAnnotations(annotationsByPage);
    }

    auto rebuildMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - rebuildStart)
                         .count();
    LOG_INFO(
        "[continuous-prof] rebuild canvas blueprint pages={} document_h={:.1f} "
        "elapsed_ms={}",
        d->document->numPages(), blueprint->documentHeight(), rebuildMs);
}

void PDFViewer::updateContinuousCanvasGeometry() {
    if (!d->continuousCanvas) {
        return;
    }

    const QRect viewportRect = d->continuousScrollArea->viewport()->rect();
    if (d->continuousCanvas->geometry() != viewportRect) {
        d->continuousCanvas->setGeometry(viewportRect);
    }

    const auto blueprint = d->continuousCanvas->blueprint();
    const qreal documentHeight = blueprint ? blueprint->documentHeight() : 0.0;
    const int viewportHeight = d->continuousScrollArea->viewport()->height();
    const int maxOffset = qMax(0, qCeil(documentHeight) - viewportHeight);
    QScrollBar* verticalBar = d->continuousScrollArea->verticalScrollBar();
    verticalBar->setRange(0, maxOffset);
    verticalBar->setPageStep(viewportHeight);
    verticalBar->setSingleStep(qMax(16, viewportHeight / 12));
    d->continuousScrollArea->horizontalScrollBar()->setRange(0, 0);
    d->continuousCanvas->setContentOffsetY(verticalBar->value());
    d->continuousCanvas->update();
}

void PDFViewer::updateVisiblePages() {
    if (!d->document || d->currentViewMode != PDFViewMode::ContinuousScroll ||
        !d->isWidgetReady || !d->continuousCanvas) {
        return;
    }

    const auto range = d->continuousCanvas->visiblePageRange(
        d->continuousScrollArea->viewport()->height());
    d->visiblePageStart = range.first;
    d->visiblePageEnd = range.second;
    d->continuousCanvas->requestVisiblePageRenders(
        d->continuousCanvas->devicePixelRatioF());
}

void PDFViewer::renderVisiblePages() {
    if (!d->document || !d->isWidgetReady || !d->continuousCanvas) {
        return;
    }
    d->continuousCanvas->requestVisiblePageRenders(
        d->continuousCanvas->devicePixelRatioF());
}

void PDFViewer::onScrollChanged() {
    if (d->currentViewMode == PDFViewMode::ContinuousScroll) {
        updateVisiblePages();
    }
}

void PDFViewer::finalizeContinuousModeInitialization(int pageNumber,
                                                     int attemptsRemaining) {
    Q_UNUSED(attemptsRemaining)
    if (d->currentViewMode != PDFViewMode::ContinuousScroll || !d->document) {
        return;
    }

    scrollToPageInContinuousView(pageNumber);
    updateVisiblePages();
}

void PDFViewer::scrollToPageInContinuousView(int pageNumber) {
    if (!d->document || d->currentViewMode != PDFViewMode::ContinuousScroll ||
        !d->continuousCanvas || pageNumber < 0 ||
        pageNumber >= d->document->numPages()) {
        return;
    }

    const auto blueprint = d->continuousCanvas->blueprint();
    if (!blueprint) {
        return;
    }

    const qreal targetOffset = blueprint->scrollOffsetForPage(
        pageNumber, d->continuousScrollArea->viewport()->height(),
        PageScrollAlignment::Center);
    d->continuousScrollArea->verticalScrollBar()->setValue(
        qRound(targetOffset));
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

    QAbstractScrollArea* currentScrollArea =
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
    if ((object == d->continuousScrollArea ||
         object == d->continuousScrollArea->viewport()) &&
        event->type() == QEvent::Wheel) {
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

    if ((object == d->continuousScrollArea ||
         object == d->continuousScrollArea->viewport()) &&
        event->type() == QEvent::Resize &&
        d->currentViewMode == PDFViewMode::ContinuousScroll && d->document) {
        rebuildContinuousCanvasBlueprint();
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
    updateContinuousView();
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

    if (d->currentViewMode == PDFViewMode::SinglePage && d->singlePageWidget) {
        d->singlePageWidget->clearSearchHighlights();
    } else if (d->currentViewMode == PDFViewMode::ContinuousScroll &&
               d->continuousCanvas) {
        d->continuousCanvas->clearSearchHighlights();
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
        d->currentViewMode != PDFViewMode::ContinuousScroll ||
        !d->continuousCanvas) {
        return;
    }

    d->continuousCanvas->setSearchResults(d->searchController->resultsByPage());
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
