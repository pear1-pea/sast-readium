#include "PDFViewer.h"
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QGesture>
#include <QGestureEvent>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPanGesture>
#include <QPen>
#include <QPinchGesture>
#include <QPropertyAnimation>
#include <QRect>
#include <QScrollBar>
#include <QSettings>
#include <QSize>
#include <QSizeF>
#include <QSplitter>
#include <QStackedWidget>
#include <QSwipeGesture>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QtCore>
#include <QtGlobal>
#include <QtWidgets>
#include <memory>
#include <stdexcept>
#include "managers/StyleManager.h"

// PDFPageWidget Implementation
PDFPageWidget::PDFPageWidget(QWidget* parent)
    : QLabel(parent),
      currentPage(nullptr),
      currentScaleFactor(1.0),
      currentRotation(0),
      isDragging(false),
      m_currentSearchResultIndex(-1),
      m_normalHighlightColor(QColor(255, 255, 0, 100)),
      m_currentHighlightColor(QColor(255, 165, 0, 150)) {
    setAlignment(Qt::AlignCenter);
    setMinimumSize(200, 200);
    setObjectName("pdfPage");

    // Enable gesture support
    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::SwipeGesture);
    grabGesture(Qt::PanGesture);

    // Enable touch events
    setAttribute(Qt::WA_AcceptTouchEvents, true);

    // 设置现代化的页面样式 (仅在非测试环境中)
    try {
        setStyleSheet(QString(R"(
            QLabel#pdfPage {
                background-color: white;
                border: 1px solid %1;
                border-radius: 8px;
                margin: 12px;
                padding: 8px;
            }
        )")
                          .arg(STYLE.borderColor().name()));
    } catch (...) {
        // 在测试环境中忽略样式错误
        setStyleSheet(
            "QLabel#pdfPage { background-color: white; border: 1px solid gray; "
            "}");
    }

    setText("No PDF loaded");

    // 添加阴影效果
    QGraphicsDropShadowEffect* shadowEffect =
        new QGraphicsDropShadowEffect(this);
    shadowEffect->setBlurRadius(15);
    shadowEffect->setColor(QColor(0, 0, 0, 50));
    shadowEffect->setOffset(0, 4);
    setGraphicsEffect(shadowEffect);
}

void PDFPageWidget::setPage(Poppler::Page* page, double scaleFactor,
                            int rotation) {
    currentPage = page;
    currentScaleFactor = scaleFactor;
    currentRotation = rotation;
    renderPage();
}

void PDFPageWidget::setScaleFactor(double factor) {
    if (factor != currentScaleFactor) {
        currentScaleFactor = factor;
        renderPage();
        emit scaleChanged(factor);
    }
}

void PDFPageWidget::quickScale(double factor) {
    if (originalPixmap.isNull()) {
        return;  // 没有原始渲染的内容，无法快速缩放
    }

    // 更新当前缩放因子
    currentScaleFactor = factor;

    // 始终从原始pixmap进行缩放，避免累积误差
    double scaleRatio = factor / originalScaleFactor;

    QSize originalSize = originalPixmap.size();
    QSize newSize = QSize(static_cast<int>(originalSize.width() * scaleRatio),
                          static_cast<int>(originalSize.height() * scaleRatio));

    // 使用快速变换
    QPixmap scaledPixmap = originalPixmap.scaled(newSize, Qt::KeepAspectRatio,
                                                 Qt::FastTransformation);

    setPixmap(scaledPixmap);
    setFixedSize(scaledPixmap.size() / scaledPixmap.devicePixelRatio());
}

void PDFPageWidget::setRotation(int degrees) {
    // 确保旋转角度是90度的倍数
    degrees = ((degrees % 360) + 360) % 360;
    if (degrees != currentRotation) {
        currentRotation = degrees;
        renderPage();
    }
}

void PDFPageWidget::renderPage() {
    if (!currentPage) {
        setText("No page to render");
        return;
    }

    try {
        // Enhanced rendering with optimized DPI calculation
        double devicePixelRatio = devicePixelRatioF();
        double baseDpi = 72.0 * currentScaleFactor;
        double optimizedDpi = baseDpi * devicePixelRatio;

        // Limit DPI to prevent excessive memory usage
        optimizedDpi = qMin(optimizedDpi, 300.0);

        QSizeF pageSize = currentPage->pageSizeF();

        // Note: Document configuration would be done at document level
        // High-quality rendering is achieved through optimized DPI and render
        // hints

        // 渲染页面为图像，包含旋转和优化设置
        QImage image = currentPage->renderToImage(
            optimizedDpi, optimizedDpi, -1, -1, -1, -1,
            static_cast<Poppler::Page::Rotation>(currentRotation / 90));
        if (image.isNull()) {
            setText("Failed to render page");
            return;
        }

        // Apply device pixel ratio for high-DPI displays
        renderedPixmap = QPixmap::fromImage(image);
        renderedPixmap.setDevicePixelRatio(devicePixelRatio);

        // 保存原始渲染的pixmap用于快速缩放
        originalPixmap = renderedPixmap;
        originalScaleFactor = currentScaleFactor;

        setPixmap(renderedPixmap);
        setFixedSize(renderedPixmap.size() / renderedPixmap.devicePixelRatio());

    } catch (const std::exception& e) {
        setText(QString("渲染错误: %1").arg(e.what()));
        qDebug() << "Page render error:" << e.what();
    } catch (...) {
        setText("未知渲染错误");
        qDebug() << "Unknown page render error";
    }
}

void PDFPageWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);

    // Enable high-quality rendering hints
    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);

    // 先绘制边框
    if (!renderedPixmap.isNull()) {
        QRect pixmapRect = rect();
        painter.setPen(QPen(QColor(0, 0, 0, 30), 1));
        painter.drawRect(pixmapRect.adjusted(0, 0, -1, -1));
    }

    // Draw search highlights
    if (!m_searchResults.isEmpty()) {
        drawSearchHighlights(painter);
    }

    // 然后调用父类实现
    QLabel::paintEvent(event);
}

bool PDFPageWidget::event(QEvent* event) {
    if (event->type() == QEvent::Gesture) {
        return gestureEvent(static_cast<QGestureEvent*>(event));
    } else if (event->type() == QEvent::TouchBegin ||
               event->type() == QEvent::TouchUpdate ||
               event->type() == QEvent::TouchEnd) {
        touchEvent(static_cast<QTouchEvent*>(event));
        return true;
    }
    return QLabel::event(event);
}

bool PDFPageWidget::gestureEvent(QGestureEvent* event) {
    if (QGesture* swipe = event->gesture(Qt::SwipeGesture)) {
        swipeTriggered(static_cast<QSwipeGesture*>(swipe));
    }
    if (QGesture* pan = event->gesture(Qt::PanGesture)) {
        panTriggered(static_cast<QPanGesture*>(pan));
    }
    if (QGesture* pinch = event->gesture(Qt::PinchGesture)) {
        pinchTriggered(static_cast<QPinchGesture*>(pinch));
    }
    return true;
}

void PDFPageWidget::pinchTriggered(QPinchGesture* gesture) {
    QPinchGesture::ChangeFlags changeFlags = gesture->changeFlags();

    if (changeFlags & QPinchGesture::ScaleFactorChanged) {
        qreal scaleFactor = gesture->totalScaleFactor();

        // Apply pinch zoom
        double newScale = currentScaleFactor * scaleFactor;
        newScale = qBound(0.1, newScale, 5.0);  // Limit zoom range

        if (qAbs(newScale - currentScaleFactor) > 0.01) {
            setScaleFactor(newScale);
            emit scaleChanged(newScale);
        }
    }

    if (gesture->state() == Qt::GestureFinished) {
        // Gesture completed
        update();
    }
}

void PDFPageWidget::swipeTriggered(QSwipeGesture* gesture) {
    if (gesture->state() == Qt::GestureFinished) {
        if (gesture->horizontalDirection() == QSwipeGesture::Left) {
            // Swipe left - next page
            emit pageClicked(
                QPoint(-1, 0));  // Use special coordinates to indicate swipe
        } else if (gesture->horizontalDirection() == QSwipeGesture::Right) {
            // Swipe right - previous page
            emit pageClicked(
                QPoint(-2, 0));  // Use special coordinates to indicate swipe
        }
    }
}

void PDFPageWidget::panTriggered(QPanGesture* gesture) {
    QPointF delta = gesture->delta();

    if (gesture->state() == Qt::GestureStarted) {
        setCursor(Qt::ClosedHandCursor);
    } else if (gesture->state() == Qt::GestureUpdated) {
        // Handle panning - this would typically scroll the parent scroll area
        // For now, we'll emit a signal that the parent can handle
        emit pageClicked(
            QPoint(static_cast<int>(delta.x()), static_cast<int>(delta.y())));
    } else if (gesture->state() == Qt::GestureFinished ||
               gesture->state() == Qt::GestureCanceled) {
        setCursor(Qt::ArrowCursor);
    }
}

void PDFPageWidget::touchEvent(QTouchEvent* event) {
    // Handle multi-touch events
    const QList<QEventPoint>& touchPoints = event->points();

    switch (event->type()) {
        case QEvent::TouchBegin:
            if (touchPoints.count() == 1) {
                // Single touch - potential tap
                lastPanPoint = touchPoints.first().position().toPoint();
            }
            break;

        case QEvent::TouchUpdate:
            if (touchPoints.count() == 1) {
                // Single touch drag
                QPoint currentPoint = touchPoints.first().position().toPoint();
                QPoint delta = currentPoint - lastPanPoint;
                lastPanPoint = currentPoint;

                // Emit pan signal
                emit pageClicked(delta);
            }
            break;

        case QEvent::TouchEnd:
            if (touchPoints.count() == 1) {
                // Single touch end - potential tap
                QPoint tapPoint = touchPoints.first().position().toPoint();
                emit pageClicked(tapPoint);
            }
            break;

        default:
            break;
    }

    event->accept();
}

// Drag and Drop Implementation
void PDFPageWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            QString fileName = urls.first().toLocalFile();
            if (fileName.toLower().endsWith(".pdf")) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void PDFPageWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void PDFPageWidget::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            QString fileName = urls.first().toLocalFile();
            if (fileName.toLower().endsWith(".pdf")) {
                // Emit signal to parent to handle file opening
                emit pageClicked(
                    QPoint(-100, -100));  // Special coordinates for file drop
                // Store the file path for retrieval
                setProperty("droppedFile", fileName);
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void PDFPageWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Ctrl + 滚轮进行缩放
        int delta = event->angleDelta().y();
        if (delta != 0) {
            // 使用更平滑的缩放步长
            double scaleDelta = delta > 0 ? 1.15 : (1.0 / 1.15);
            double newScale = currentScaleFactor * scaleDelta;

            // 限制缩放范围
            newScale = qBound(0.1, newScale, 5.0);

            // 应用缩放
            setScaleFactor(newScale);
        }
        event->accept();
    } else {
        QLabel::wheelEvent(event);
    }
}

void PDFPageWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        isDragging = true;
        lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QLabel::mousePressEvent(event);
}

void PDFPageWidget::mouseMoveEvent(QMouseEvent* event) {
    if (isDragging && (event->buttons() & Qt::LeftButton)) {
        // 实现拖拽平移功能
        QPoint delta = event->pos() - lastPanPoint;
        lastPanPoint = event->pos();

        // 这里可以实现滚动区域的平移
        // 由于我们在ScrollArea中，这个功能由ScrollArea处理
    }
    QLabel::mouseMoveEvent(event);
}

void PDFPageWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        isDragging = false;
        setCursor(Qt::ArrowCursor);
    }
    QLabel::mouseReleaseEvent(event);
}

// Search highlighting implementation
void PDFPageWidget::setSearchResults(const QList<SearchResult>& results) {
    m_searchResults = results;
    m_currentSearchResultIndex = -1;

    // Transform coordinates for all results
    updateSearchResultCoordinates();

    // Trigger repaint to show highlights
    update();
}

void PDFPageWidget::clearSearchHighlights() {
    m_searchResults.clear();
    m_currentSearchResultIndex = -1;
    update();
}

void PDFPageWidget::setCurrentSearchResult(int index) {
    if (index >= 0 && index < m_searchResults.size()) {
        // Clear previous current result
        if (m_currentSearchResultIndex >= 0 &&
            m_currentSearchResultIndex < m_searchResults.size()) {
            m_searchResults[m_currentSearchResultIndex].isCurrentResult = false;
        }

        // Set new current result
        m_currentSearchResultIndex = index;
        m_searchResults[index].isCurrentResult = true;

        update();
    }
}

void PDFPageWidget::updateHighlightColors(const QColor& normalColor,
                                          const QColor& currentColor) {
    m_normalHighlightColor = normalColor;
    m_currentHighlightColor = currentColor;
    update();
}

void PDFPageWidget::updateSearchResultCoordinates() {
    if (!currentPage || m_searchResults.isEmpty()) {
        return;
    }

    QSizeF pageSize = currentPage->pageSizeF();
    QSize widgetSize = size();

    for (SearchResult& result : m_searchResults) {
        result.transformToWidgetCoordinates(currentScaleFactor, currentRotation,
                                            pageSize, widgetSize);
    }
}

void PDFPageWidget::drawSearchHighlights(QPainter& painter) {
    if (m_searchResults.isEmpty()) {
        return;
    }

    painter.save();

    for (const SearchResult& result : m_searchResults) {
        if (!result.isValidForHighlight() || result.widgetRect.isEmpty()) {
            continue;
        }

        // Choose color based on whether this is the current result
        QColor highlightColor = result.isCurrentResult ? m_currentHighlightColor
                                                       : m_normalHighlightColor;

        // Draw highlight rectangle
        painter.fillRect(result.widgetRect, highlightColor);

        // Draw border for current result
        if (result.isCurrentResult) {
            painter.setPen(QPen(highlightColor.darker(150), 2));
            painter.drawRect(result.widgetRect);
        }
    }

    painter.restore();
}

// PDFViewer Implementation
PDFViewer::PDFViewer(QWidget* parent, bool enableStyling)
    : QWidget(parent),
      document(nullptr),
      currentPageNumber(0),
      currentZoomFactor(DEFAULT_ZOOM),
      currentViewMode(PDFViewMode::SinglePage),
      currentZoomType(ZoomType::FixedValue),
      currentRotation(0),
      pendingZoomFactor(DEFAULT_ZOOM),
      isZoomPending(false),
      isSliderDragging(false),
      m_currentSearchResultIndex(-1),
      m_enableStyling(enableStyling) {
    // 初始化动画管理器
    animationManager = new PDFAnimationManager(this);

    // 初始化预渲染器
    prerenderer = new PDFPrerenderer(this);
    prerenderer->setStrategy(PDFPrerenderer::PrerenderStrategy::Balanced);
    prerenderer->setMaxWorkerThreads(2);  // Use 2 background threads

#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
    // 初始化QGraphics PDF查看器
    qgraphicsViewer = nullptr;
    useQGraphicsViewer = false;  // 默认使用传统渲染
#endif

    // 启用拖放功能
    setAcceptDrops(true);

    // 初始化防抖定时器
    zoomTimer = new QTimer(this);
    zoomTimer->setSingleShot(true);
    zoomTimer->setInterval(150);  // 150ms防抖延迟

    // 初始化虚拟化渲染
    visiblePageStart = -1;
    visiblePageEnd = -1;
    renderBuffer = 2;  // 预渲染前后2页

    scrollTimer = new QTimer(this);
    scrollTimer->setSingleShot(true);
    scrollTimer->setInterval(100);  // 100ms滚动防抖

    // 初始化页面缓存
    maxCacheSize = 20;  // 最多缓存20页

    // 初始化动画效果
    opacityEffect = new QGraphicsOpacityEffect(this);
    fadeAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
    fadeAnimation->setDuration(300);  // 300ms动画时间

    setupUI();
    setupConnections();
    setupShortcuts();
    loadZoomSettings();
    updateNavigationButtons();
    updateZoomControls();
}

void PDFViewer::setupUI() {
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // 应用样式 (仅在非测试环境中)
    if (m_enableStyling) {
        setStyleSheet(STYLE.getApplicationStyleSheet());
    }

    // 创建工具栏
    toolbar = new QWidget(this);
    toolbar->setObjectName("toolbar");
    if (m_enableStyling) {
        toolbar->setStyleSheet(STYLE.getToolbarStyleSheet());
        toolbarLayout = new QHBoxLayout(toolbar);
        toolbarLayout->setContentsMargins(STYLE.margin(), STYLE.spacing(),
                                          STYLE.margin(), STYLE.spacing());
        toolbarLayout->setSpacing(STYLE.spacing());
    } else {
        toolbarLayout = new QHBoxLayout(toolbar);
        toolbarLayout->setContentsMargins(8, 8, 8, 8);
        toolbarLayout->setSpacing(8);
    }

    // 页面导航控件
    navGroup = new QGroupBox("页面导航", toolbar);
    QHBoxLayout* navLayout = new QHBoxLayout(navGroup);

    // 使用现代化图标
    firstPageBtn = new QPushButton("⏮", navGroup);
    prevPageBtn = new QPushButton("◀", navGroup);
    pageNumberSpinBox = new QSpinBox(navGroup);
    pageCountLabel = new QLabel("/ 0", navGroup);
    nextPageBtn = new QPushButton("▶", navGroup);
    lastPageBtn = new QPushButton("⏭", navGroup);

    // 设置按钮样式和尺寸
    QString buttonStyle = STYLE.getButtonStyleSheet();
    firstPageBtn->setStyleSheet(buttonStyle);
    prevPageBtn->setStyleSheet(buttonStyle);
    nextPageBtn->setStyleSheet(buttonStyle);
    lastPageBtn->setStyleSheet(buttonStyle);

    firstPageBtn->setFixedSize(STYLE.buttonHeight(), STYLE.buttonHeight());
    prevPageBtn->setFixedSize(STYLE.buttonHeight(), STYLE.buttonHeight());
    nextPageBtn->setFixedSize(STYLE.buttonHeight(), STYLE.buttonHeight());
    lastPageBtn->setFixedSize(STYLE.buttonHeight(), STYLE.buttonHeight());
    pageNumberSpinBox->setMaximumWidth(60);

    // 设置工具提示
    firstPageBtn->setToolTip("第一页 (Ctrl+Home)");
    prevPageBtn->setToolTip("上一页 (Page Up)");
    nextPageBtn->setToolTip("下一页 (Page Down)");
    lastPageBtn->setToolTip("最后一页 (Ctrl+End)");

    navLayout->addWidget(firstPageBtn);
    navLayout->addWidget(prevPageBtn);
    navLayout->addWidget(pageNumberSpinBox);
    navLayout->addWidget(pageCountLabel);
    navLayout->addWidget(nextPageBtn);
    navLayout->addWidget(lastPageBtn);

    // 缩放控件
    zoomGroup = new QGroupBox("缩放", toolbar);
    QHBoxLayout* zoomLayout = new QHBoxLayout(zoomGroup);

    // 使用现代化图标
    zoomOutBtn = new QPushButton("🔍-", zoomGroup);
    zoomInBtn = new QPushButton("🔍+", zoomGroup);
    zoomSlider = new QSlider(Qt::Horizontal, zoomGroup);
    zoomPercentageSpinBox = new QSpinBox(zoomGroup);
    fitWidthBtn = new QPushButton("📏", zoomGroup);
    fitHeightBtn = new QPushButton("📐", zoomGroup);
    fitPageBtn = new QPushButton("🗎", zoomGroup);

    // 设置按钮样式
    zoomOutBtn->setStyleSheet(buttonStyle);
    zoomInBtn->setStyleSheet(buttonStyle);
    fitWidthBtn->setStyleSheet(buttonStyle);
    fitHeightBtn->setStyleSheet(buttonStyle);
    fitPageBtn->setStyleSheet(buttonStyle);

    zoomSlider->setRange(10, 500);  // 10% to 500%
    zoomSlider->setValue(100);
    zoomSlider->setMinimumWidth(120);

    // 配置百分比输入框
    zoomPercentageSpinBox->setRange(10, 500);
    zoomPercentageSpinBox->setValue(100);
    zoomPercentageSpinBox->setSuffix("%");
    zoomPercentageSpinBox->setMinimumWidth(80);
    zoomPercentageSpinBox->setMaximumWidth(80);

    // 设置工具提示
    zoomOutBtn->setToolTip("缩小 (Ctrl+-)");
    zoomInBtn->setToolTip("放大 (Ctrl++)");
    fitWidthBtn->setToolTip("适合宽度 (Ctrl+1)");
    fitHeightBtn->setToolTip("适合高度 (Ctrl+2)");
    fitPageBtn->setToolTip("适合页面 (Ctrl+0)");

    zoomLayout->addWidget(zoomOutBtn);
    zoomLayout->addWidget(zoomInBtn);
    zoomLayout->addWidget(zoomSlider);
    zoomLayout->addWidget(zoomPercentageSpinBox);
    zoomLayout->addWidget(fitWidthBtn);
    zoomLayout->addWidget(fitHeightBtn);
    zoomLayout->addWidget(fitPageBtn);

    // 旋转控件
    rotateGroup = new QGroupBox("旋转", toolbar);
    QHBoxLayout* rotateLayout = new QHBoxLayout(rotateGroup);

    rotateLeftBtn = new QPushButton("↺", rotateGroup);
    rotateRightBtn = new QPushButton("↻", rotateGroup);

    // 设置旋转按钮样式
    rotateLeftBtn->setStyleSheet(buttonStyle);
    rotateRightBtn->setStyleSheet(buttonStyle);

    rotateLeftBtn->setFixedSize(STYLE.buttonHeight(), STYLE.buttonHeight());
    rotateRightBtn->setFixedSize(STYLE.buttonHeight(), STYLE.buttonHeight());
    rotateLeftBtn->setToolTip("向左旋转90度 (Ctrl+L)");
    rotateRightBtn->setToolTip("向右旋转90度 (Ctrl+R)");

    rotateLayout->addWidget(rotateLeftBtn);
    rotateLayout->addWidget(rotateRightBtn);

    // 主题切换控件
    themeGroup = new QGroupBox("主题", toolbar);
    QHBoxLayout* themeLayout = new QHBoxLayout(themeGroup);

    themeToggleBtn = new QPushButton("🌙", themeGroup);
    themeToggleBtn->setStyleSheet(buttonStyle);
    themeToggleBtn->setFixedSize(STYLE.buttonHeight(), STYLE.buttonHeight());
    themeToggleBtn->setToolTip("切换主题 (Ctrl+Shift+T)");

    themeLayout->addWidget(themeToggleBtn);

    // 查看模式控件
    viewGroup = new QGroupBox("查看模式", toolbar);
    QHBoxLayout* viewLayout = new QHBoxLayout(viewGroup);

    viewModeComboBox = new QComboBox(viewGroup);
    viewModeComboBox->addItem("单页视图",
                              static_cast<int>(PDFViewMode::SinglePage));
    viewModeComboBox->addItem("连续滚动",
                              static_cast<int>(PDFViewMode::ContinuousScroll));
    viewModeComboBox->setCurrentIndex(0);  // 默认单页视图

    viewLayout->addWidget(viewModeComboBox);

    toolbarLayout->addWidget(navGroup);
    toolbarLayout->addWidget(zoomGroup);
    toolbarLayout->addWidget(rotateGroup);
    toolbarLayout->addWidget(themeGroup);
    toolbarLayout->addWidget(viewGroup);
    toolbarLayout->addStretch();

    // 创建视图堆叠组件
    viewStack = new QStackedWidget(this);

    setupViewModes();

    // 创建搜索组件
    searchWidget = new SearchWidget(this);
    searchWidget->setVisible(false);  // 默认隐藏

    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(searchWidget);
    mainLayout->addWidget(viewStack, 1);
}

void PDFViewer::setupViewModes() {
    // 创建单页视图
    singlePageScrollArea = new QScrollArea(this);
    // 便于调试
    singlePageScrollArea->setObjectName("singlePageScrollArea");

    singlePageWidget = new PDFPageWidget(singlePageScrollArea);
    singlePageScrollArea->setWidget(singlePageWidget);
    singlePageScrollArea->setWidgetResizable(true);
    singlePageScrollArea->setAlignment(Qt::AlignCenter);

    // 应用样式
    if (m_enableStyling) {
        singlePageScrollArea->setStyleSheet(STYLE.getPDFViewerStyleSheet() +
                                            STYLE.getScrollBarStyleSheet());
    }

    // 创建连续滚动视图
    continuousScrollArea = new QScrollArea(this);
    // 便于调试
    continuousScrollArea->setObjectName("continuousScrollArea");

    continuousWidget = new QWidget(continuousScrollArea);
    continuousLayout = new QVBoxLayout(continuousWidget);

    continuousLayout->setAlignment(Qt::AlignCenter);  // 居中对齐

    if (m_enableStyling) {
        continuousLayout->setContentsMargins(STYLE.margin(), STYLE.margin(),
                                             STYLE.margin(), STYLE.margin());
        continuousLayout->setSpacing(STYLE.spacing() * 2);
    } else {
        continuousLayout->setContentsMargins(12, 12, 12, 12);
        continuousLayout->setSpacing(16);
    }
    continuousScrollArea->setWidget(continuousWidget);
    continuousScrollArea->setWidgetResizable(true);

    // 应用样式
    if (m_enableStyling) {
        continuousScrollArea->setStyleSheet(STYLE.getPDFViewerStyleSheet() +
                                            STYLE.getScrollBarStyleSheet());
    }

    // 添加到堆叠组件
    viewStack->addWidget(singlePageScrollArea);  // index 0
    viewStack->addWidget(continuousScrollArea);  // index 1

    // 为连续滚动区域安装事件过滤器以处理Ctrl+滚轮缩放
    continuousScrollArea->installEventFilter(this);

    // 默认显示单页视图
    viewStack->setCurrentIndex(0);
}

void PDFViewer::setupConnections() {
    // 页面导航
    connect(firstPageBtn, &QPushButton::clicked, this, &PDFViewer::firstPage);
    connect(prevPageBtn, &QPushButton::clicked, this, &PDFViewer::previousPage);
    connect(nextPageBtn, &QPushButton::clicked, this, &PDFViewer::nextPage);
    connect(lastPageBtn, &QPushButton::clicked, this, &PDFViewer::lastPage);
    connect(pageNumberSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PDFViewer::onPageNumberChanged);

    // 缩放控制
    connect(zoomInBtn, &QPushButton::clicked, this, &PDFViewer::zoomIn);
    connect(zoomOutBtn, &QPushButton::clicked, this, &PDFViewer::zoomOut);
    connect(zoomSlider, &QSlider::valueChanged, this,
            &PDFViewer::onZoomSliderChanged);
    connect(zoomSlider, &QSlider::sliderPressed, this,
            &PDFViewer::onZoomSliderPressed);
    connect(zoomSlider, &QSlider::sliderReleased, this,
            &PDFViewer::onZoomSliderReleased);
    connect(zoomPercentageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PDFViewer::onZoomPercentageChanged);
    connect(fitWidthBtn, &QPushButton::clicked, this, &PDFViewer::zoomToWidth);
    connect(fitHeightBtn, &QPushButton::clicked, this,
            &PDFViewer::zoomToHeight);
    connect(fitPageBtn, &QPushButton::clicked, this, &PDFViewer::zoomToFit);

    // 旋转控制
    connect(rotateLeftBtn, &QPushButton::clicked, this, &PDFViewer::rotateLeft);
    connect(rotateRightBtn, &QPushButton::clicked, this,
            &PDFViewer::rotateRight);

    // 主题切换
    connect(themeToggleBtn, &QPushButton::clicked, this,
            &PDFViewer::toggleTheme);

    // 搜索组件连接
    if (searchWidget) {
        connect(searchWidget, &SearchWidget::searchRequested, this,
                &PDFViewer::onSearchRequested);
        connect(searchWidget, &SearchWidget::resultSelected, this,
                &PDFViewer::onSearchResultSelected);
        connect(searchWidget, &SearchWidget::navigateToResult, this,
                &PDFViewer::onNavigateToSearchResult);
        connect(searchWidget, &SearchWidget::searchClosed, this,
                &PDFViewer::hideSearch);
        connect(searchWidget, &SearchWidget::searchCleared, this,
                &PDFViewer::clearSearchHighlights);

        // Connect search model signals for real-time highlighting
        if (searchWidget->getSearchModel()) {
            connect(searchWidget->getSearchModel(),
                    &SearchModel::realTimeResultsUpdated, this,
                    &PDFViewer::setSearchResults);
        }
    }

    // 防抖定时器
    connect(zoomTimer, &QTimer::timeout, this, &PDFViewer::onZoomTimerTimeout);
    connect(scrollTimer, &QTimer::timeout, this, &PDFViewer::onScrollChanged);

    // 查看模式控制
    connect(viewModeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PDFViewer::onViewModeChanged);

    // 页面组件信号
    connect(singlePageWidget, &PDFPageWidget::scaleChanged, this,
            &PDFViewer::onScaleChanged);

    // 监听 StyleManager 的样式表应用信号，确保在主题样式表应用后再更新组件
    connect(&StyleManager::instance(), &StyleManager::styleSheetApplied, this,
            [this]() { updateThemeUI(); });
}

void PDFViewer::setupShortcuts() {
    // 缩放快捷键
    zoomInShortcut = new QShortcut(QKeySequence("Ctrl++"), this);
    zoomOutShortcut = new QShortcut(QKeySequence("Ctrl+-"), this);
    fitPageShortcut = new QShortcut(QKeySequence("Ctrl+0"), this);
    fitWidthShortcut = new QShortcut(QKeySequence("Ctrl+1"), this);
    fitHeightShortcut = new QShortcut(QKeySequence("Ctrl+2"), this);

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
    rotateLeftShortcut = new QShortcut(QKeySequence("Ctrl+L"), this);
    rotateRightShortcut = new QShortcut(QKeySequence("Ctrl+R"), this);
    QShortcut* rotate180 = new QShortcut(QKeySequence("Ctrl+Shift+R"), this);

    // 主题切换快捷键
    QShortcut* themeToggleShortcut =
        new QShortcut(QKeySequence("Ctrl+Shift+T"), this);

    // 导航快捷键 - 基本
    firstPageShortcut = new QShortcut(QKeySequence("Ctrl+Home"), this);
    lastPageShortcut = new QShortcut(QKeySequence("Ctrl+End"), this);
    nextPageShortcut = new QShortcut(QKeySequence("Page Down"), this);
    prevPageShortcut = new QShortcut(QKeySequence("Page Up"), this);

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
    connect(zoomInShortcut, &QShortcut::activated, this, &PDFViewer::zoomIn);
    connect(zoomOutShortcut, &QShortcut::activated, this, &PDFViewer::zoomOut);
    connect(zoomIn2, &QShortcut::activated, this, &PDFViewer::zoomIn);
    connect(fitPageShortcut, &QShortcut::activated, this,
            &PDFViewer::zoomToFit);
    connect(fitWidthShortcut, &QShortcut::activated, this,
            &PDFViewer::zoomToWidth);
    connect(fitHeightShortcut, &QShortcut::activated, this,
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
    connect(rotateLeftShortcut, &QShortcut::activated, this,
            &PDFViewer::rotateLeft);
    connect(rotateRightShortcut, &QShortcut::activated, this,
            &PDFViewer::rotateRight);
    connect(rotate180, &QShortcut::activated, this,
            [this]() { setRotation(currentRotation + 180); });

    // 连接主题快捷键
    connect(themeToggleShortcut, &QShortcut::activated, this,
            &PDFViewer::toggleTheme);

    // 连接基本导航快捷键
    connect(firstPageShortcut, &QShortcut::activated, this,
            &PDFViewer::firstPage);
    connect(lastPageShortcut, &QShortcut::activated, this,
            &PDFViewer::lastPage);
    connect(nextPageShortcut, &QShortcut::activated, this,
            &PDFViewer::nextPage);
    connect(prevPageShortcut, &QShortcut::activated, this,
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
            [this]() { goToPage(currentPageNumber + 10); });
    connect(jump10Backward, &QShortcut::activated, this,
            [this]() { goToPage(currentPageNumber - 10); });
    connect(gotoPage, &QShortcut::activated, this, [this]() {
        // Focus on page number input
        if (pageNumberSpinBox) {
            pageNumberSpinBox->setFocus();
            pageNumberSpinBox->selectAll();
        }
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
        if (document && currentPageNumber >= 0) {
            emit bookmarkRequested(currentPageNumber);
        }
    });

    // 连接文档操作快捷键
    connect(refresh, &QShortcut::activated, this, [this]() {
        // Refresh current page
        if (singlePageWidget) {
            singlePageWidget->renderPage();
        }
    });
}

void PDFViewer::setDocument(std::shared_ptr<Poppler::Document> doc) {
    try {
        // 清理旧文档
        if (document) {
            clearPageCache();  // 清理缓存
        }

        document = doc;
        currentPageNumber = 0;
        currentRotation = 0;  // 重置旋转

        if (document) {
            // Configure document for high-quality rendering
            document->setRenderHint(Poppler::Document::Antialiasing, true);
            document->setRenderHint(Poppler::Document::TextAntialiasing, true);
            document->setRenderHint(Poppler::Document::TextHinting, true);
            document->setRenderHint(Poppler::Document::TextSlightHinting, true);
            document->setRenderHint(Poppler::Document::ThinLineShape, true);
            document->setRenderHint(Poppler::Document::OverprintPreview, true);
            // 验证文档有效性
            int numPages = document->numPages();
            if (numPages <= 0) {
                throw std::runtime_error("文档没有有效页面");
            }

            // 测试第一页是否可以访问
            std::unique_ptr<Poppler::Page> testPage(document->page(0));
            if (!testPage) {
                throw std::runtime_error("无法访问文档页面");
            }

            pageNumberSpinBox->setRange(1, numPages);
            pageNumberSpinBox->setValue(1);
            pageCountLabel->setText(QString("/ %1").arg(numPages));
            updatePageDisplay();

            // 如果是连续模式，创建所有页面
            if (currentViewMode == PDFViewMode::ContinuousScroll) {
                createContinuousPages();
            }

            setMessage(QString("文档加载成功，共 %1 页").arg(numPages));

#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
            // 如果启用了QGraphics渲染，也为其设置文档
            if (useQGraphicsViewer && qgraphicsViewer) {
                qgraphicsViewer->setDocument(document);
                qgraphicsViewer->goToPage(currentPageNumber);
            }
#endif

        } else {
            pageNumberSpinBox->setRange(0, 0);
            pageCountLabel->setText("/ 0");
            singlePageWidget->setPage(nullptr);

            // 清空连续视图
            QLayoutItem* item;
            while ((item = continuousLayout->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }

            setMessage("文档已关闭");

#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
            // 清理QGraphics查看器
            if (qgraphicsViewer) {
                qgraphicsViewer->clearDocument();
            }
#endif
        }

        updateNavigationButtons();
        emit documentChanged(document != nullptr);

    } catch (const std::exception& e) {
        // 文档加载失败，清理状态
        document = nullptr;
        pageNumberSpinBox->setRange(0, 0);
        pageCountLabel->setText("/ 0");
        singlePageWidget->setPage(nullptr);

        setMessage(QString("文档加载失败: %1").arg(e.what()));
        qDebug() << "Document loading failed:" << e.what();

        updateNavigationButtons();
        emit documentChanged(false);
    }
}

void PDFViewer::clearDocument() { setDocument(nullptr); }

void PDFViewer::goToPage(int pageNumber) {
    goToPageWithValidation(pageNumber, false);
}

bool PDFViewer::goToPageWithValidation(int pageNumber, bool showMessage) {
    if (!document) {
        if (showMessage) {
            setMessage("没有打开的文档");
        }
        return false;
    }

    if (pageNumber < 0 || pageNumber >= document->numPages()) {
        if (showMessage) {
            setMessage(
                QString("页码超出范围 (1-%1)").arg(document->numPages()));
        }
        return false;
    }

    currentPageNumber = pageNumber;
    pageNumberSpinBox->setValue(pageNumber + 1);

#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
    // 如果使用QGraphics渲染，也更新QGraphics查看器
    if (useQGraphicsViewer && qgraphicsViewer) {
        qgraphicsViewer->goToPage(pageNumber);
    } else {
        updatePageDisplay();
    }
#else
    updatePageDisplay();
#endif

    updateNavigationButtons();

    // Update search highlights for the new page
    updateSearchHighlightsForCurrentPage();

    emit pageChanged(pageNumber);

    if (showMessage) {
        setMessage(QString("跳转到第 %1 页").arg(pageNumber + 1));
    }

    return true;
}

void PDFViewer::nextPage() {
    if (document && currentPageNumber < document->numPages() - 1) {
        goToPage(currentPageNumber + 1);
    }
}

void PDFViewer::previousPage() {
    if (document && currentPageNumber > 0) {
        goToPage(currentPageNumber - 1);
    }
}

void PDFViewer::firstPage() {
    if (document) {
        goToPage(0);
    }
}

void PDFViewer::lastPage() {
    if (document) {
        goToPage(document->numPages() - 1);
    }
}

void PDFViewer::zoomIn() {
    double newZoom = currentZoomFactor + ZOOM_STEP;
    setZoomWithType(newZoom, ZoomType::FixedValue);
}

void PDFViewer::zoomOut() {
    double newZoom = currentZoomFactor - ZOOM_STEP;
    setZoomWithType(newZoom, ZoomType::FixedValue);
}

void PDFViewer::zoomToFit() {
    if (!document)
        return;

    // 获取当前视图的viewport大小
    QScrollArea* currentScrollArea =
        (currentViewMode == PDFViewMode::SinglePage) ? singlePageScrollArea
                                                     : continuousScrollArea;
    QSize viewportSize = currentScrollArea->viewport()->size();

    if (document->numPages() > 0) {
        std::unique_ptr<Poppler::Page> page(document->page(currentPageNumber));
        if (page) {
            QSizeF pageSize = page->pageSizeF();
            double scaleX = viewportSize.width() / pageSize.width();
            double scaleY = viewportSize.height() / pageSize.height();
            setZoomWithType(qMin(scaleX, scaleY) * 0.9,
                            ZoomType::FitPage);  // 留一些边距
        }
    }
}

void PDFViewer::zoomToWidth() {
    if (!document)
        return;

    // 获取当前视图的viewport大小
    QScrollArea* currentScrollArea =
        (currentViewMode == PDFViewMode::SinglePage) ? singlePageScrollArea
                                                     : continuousScrollArea;
    QSize viewportSize = currentScrollArea->viewport()->size();

    if (document->numPages() > 0) {
        std::unique_ptr<Poppler::Page> page(document->page(currentPageNumber));
        if (page) {
            QSizeF pageSize = page->pageSizeF();
            double scale = viewportSize.width() / pageSize.width();
            setZoomWithType(scale * 0.95, ZoomType::FitWidth);  // 留一些边距
        }
    }
}

void PDFViewer::setZoom(double factor) {
    setZoomWithType(factor, ZoomType::FixedValue);
}

int PDFViewer::getPageCount() const {
    return document ? document->numPages() : 0;
}

double PDFViewer::getCurrentZoom() const { return currentZoomFactor; }

void PDFViewer::updatePageDisplay() {
    if (!document || currentPageNumber < 0 ||
        currentPageNumber >= document->numPages()) {
        if (currentViewMode == PDFViewMode::SinglePage) {
            singlePageWidget->setPage(nullptr, currentZoomFactor,
                                      currentRotation);
        }
        return;
    }

    if (currentViewMode == PDFViewMode::SinglePage) {
        // 添加淡入淡出动画
        if (fadeAnimation->state() != QAbstractAnimation::Running) {
            singlePageWidget->setGraphicsEffect(opacityEffect);
            fadeAnimation->setStartValue(0.3);
            fadeAnimation->setEndValue(1.0);
            fadeAnimation->start();
        }

        std::unique_ptr<Poppler::Page> page(document->page(currentPageNumber));
        if (page) {
            singlePageWidget->setPage(page.release(), currentZoomFactor,
                                      currentRotation);
        }
    } else if (currentViewMode == PDFViewMode::ContinuousScroll) {
        // 连续滚动模式下需要滚动到对应的页面位置
        scrollToPageInContinuousView(currentPageNumber);
    }
}

void PDFViewer::updateContinuousView() {
    if (!document || currentViewMode != PDFViewMode::ContinuousScroll) {
        return;
    }

    // 缩放或旋转变化时，清空已渲染状态，触发重新渲染
    renderedPages.clear();

    // 更新占位符尺寸
    QSizeF placeholderSize(100, 140);  // 默认A4比例
    std::unique_ptr<Poppler::Page> firstPage(document->page(0));
    if (firstPage) {
        placeholderSize = firstPage->pageSizeF();
    }

    int placeholderWidth =
        static_cast<int>(placeholderSize.width() * currentZoomFactor);
    int placeholderHeight =
        static_cast<int>(placeholderSize.height() * currentZoomFactor);

    // 更新所有页面的占位符尺寸
    for (int i = 0; i < continuousLayout->count() - 1; ++i) {
        QLayoutItem* item = continuousLayout->itemAt(i);
        if (item && item->widget()) {
            PDFPageWidget* pageWidget =
                qobject_cast<PDFPageWidget*>(item->widget());
            if (pageWidget) {
                // 只更新占位符尺寸，不立即渲染
                if (!renderedPages.contains(qMakePair(i, currentZoomFactor))) {
                    pageWidget->setFixedSize(placeholderWidth,
                                             placeholderHeight);
                } else {
                    // 已渲染的页面需要重新渲染
                    pageWidget->blockSignals(true);
                    std::unique_ptr<Poppler::Page> page(document->page(i));
                    if (page) {
                        pageWidget->setPage(page.get(), currentZoomFactor,
                                            currentRotation);
                        renderedPages.insert(qMakePair(i, currentZoomFactor));
                    }
                    pageWidget->blockSignals(false);
                }
            }
        }
    }

    // 触发可见页面重新渲染
    QTimer::singleShot(0, this, [this]() { updateVisiblePages(); });
}

void PDFViewer::updateNavigationButtons() {
    bool hasDoc = (document != nullptr);
    bool hasPages = hasDoc && document->numPages() > 0;
    bool notFirst = hasPages && currentPageNumber > 0;
    bool notLast = hasPages && currentPageNumber < document->numPages() - 1;

    // 导航按钮状态
    firstPageBtn->setEnabled(notFirst);
    prevPageBtn->setEnabled(notFirst);
    nextPageBtn->setEnabled(notLast);
    lastPageBtn->setEnabled(notLast);
    pageNumberSpinBox->setEnabled(hasPages);

    // 缩放按钮状态
    zoomInBtn->setEnabled(hasPages && currentZoomFactor < MAX_ZOOM);
    zoomOutBtn->setEnabled(hasPages && currentZoomFactor > MIN_ZOOM);
    zoomSlider->setEnabled(hasPages);
    zoomPercentageSpinBox->setEnabled(hasPages);
    fitWidthBtn->setEnabled(hasPages);
    fitHeightBtn->setEnabled(hasPages);
    fitPageBtn->setEnabled(hasPages);

    // 旋转按钮状态
    rotateLeftBtn->setEnabled(hasPages);
    rotateRightBtn->setEnabled(hasPages);

    // 查看模式选择状态
    viewModeComboBox->setEnabled(hasPages);

    // 更新按钮工具提示
    if (!hasPages) {
        firstPageBtn->setToolTip("需要先打开文档");
        prevPageBtn->setToolTip("需要先打开文档");
        nextPageBtn->setToolTip("需要先打开文档");
        lastPageBtn->setToolTip("需要先打开文档");
        rotateLeftBtn->setToolTip("需要先打开文档");
        rotateRightBtn->setToolTip("需要先打开文档");
    } else {
        firstPageBtn->setToolTip("第一页");
        prevPageBtn->setToolTip("上一页");
        nextPageBtn->setToolTip("下一页");
        lastPageBtn->setToolTip("最后一页");
        rotateLeftBtn->setToolTip("向左旋转90度");
        rotateRightBtn->setToolTip("向右旋转90度");
    }
}

void PDFViewer::updateZoomControls() {
    int percentageValue = static_cast<int>(currentZoomFactor * 100);

    // 更新滑块和百分比输入框（阻止信号避免循环）
    zoomSlider->blockSignals(true);
    zoomPercentageSpinBox->blockSignals(true);

    zoomSlider->setValue(percentageValue);
    zoomPercentageSpinBox->setValue(percentageValue);

    zoomSlider->blockSignals(false);
    zoomPercentageSpinBox->blockSignals(false);

    // 更新按钮状态
    zoomInBtn->setEnabled(currentZoomFactor < MAX_ZOOM);
    zoomOutBtn->setEnabled(currentZoomFactor > MIN_ZOOM);
}

void PDFViewer::onPageNumberChanged(int pageNumber) {
    goToPage(pageNumber - 1);  // SpinBox is 1-based, internal is 0-based
}

void PDFViewer::onZoomSliderChanged(int value) {
    double factor = value / 100.0;
    factor = qBound(MIN_ZOOM, factor, MAX_ZOOM);

    if (isSliderDragging) {
        // 拖动过程中：使用快速缩放（只对已渲染的页面进行图像缩放）
        pendingZoomFactor = factor;
        quickApplyZoom(factor);
    } else {
        // 非拖动状态（如点击滑块、键盘操作等）：直接应用完整缩放
        setZoom(factor);
    }
}

void PDFViewer::onZoomSliderPressed() {
    isSliderDragging = true;
    oldZoomFactor = currentZoomFactor;
}

void PDFViewer::onZoomSliderReleased() {
    if (isSliderDragging) {
        isSliderDragging = false;

        // 应用待处理的缩放值并强制重新渲染
        if (qAbs(pendingZoomFactor - currentZoomFactor) > 0.001) {
            // 强制重新渲染，即使值相同也要确保质量
            setZoomWithType(pendingZoomFactor, ZoomType::FixedValue);
        } else {
            // 如果缩放值没有变化，仍然需要确保页面正确渲染
            if (currentViewMode == PDFViewMode::SinglePage &&
                singlePageWidget) {
                singlePageWidget->renderPage();
            } else if (currentViewMode == PDFViewMode::ContinuousScroll) {
                // 重新渲染当前可见页面
                updateVisiblePages();
            }
        }
    }
}

void PDFViewer::onScaleChanged(double scale) {
    // 防止信号循环：只有当缩放来自用户交互（如Ctrl+滚轮）时才处理
    if (scale != currentZoomFactor && !isZoomPending) {
        currentZoomFactor = scale;
        updateZoomControls();
        saveZoomSettings();
        emit zoomChanged(scale);
    }
}

void PDFViewer::setViewMode(PDFViewMode mode) {
    if (mode == currentViewMode) {
        return;
    }

    // 保存当前状态
    int savedPageNumber = currentPageNumber;
    double savedZoomFactor = currentZoomFactor;
    int savedRotation = currentRotation;

    PDFViewMode oldMode = currentViewMode;
    currentViewMode = mode;

    try {
        // 更新UI
        viewModeComboBox->blockSignals(true);
        viewModeComboBox->setCurrentIndex(static_cast<int>(mode));
        viewModeComboBox->blockSignals(false);

        // 切换视图
        if (mode == PDFViewMode::SinglePage) {
            switchToSinglePageMode();
        } else {
            switchToContinuousMode();
        }

        // 恢复状态
        currentPageNumber = savedPageNumber;
        currentZoomFactor = savedZoomFactor;
        currentRotation = savedRotation;

        // 更新显示
        updatePageDisplay();
        updateNavigationButtons();
        updateZoomControls();

        // 如果切换到连续滚动模式，需要设置isWidgetReady并渲染可见页面
        if (mode == PDFViewMode::ContinuousScroll) {
            // 延时0.05秒后设置isWidgetReady为true，确保布局完成
            QTimer::singleShot(50, this, [this]() {
                isWidgetReady = true;
                updateVisiblePages();  // 初始渲染可见页面
            });
        }

        emit viewModeChanged(mode);
        setMessage(
            QString("切换到%1模式")
                .arg(mode == PDFViewMode::SinglePage ? "单页" : "连续滚动"));

    } catch (const std::exception& e) {
        // 恢复到原来的模式
        currentViewMode = oldMode;
        viewModeComboBox->blockSignals(true);
        viewModeComboBox->setCurrentIndex(static_cast<int>(oldMode));
        viewModeComboBox->blockSignals(false);

        setMessage(QString("切换视图模式失败: %1").arg(e.what()));
        qDebug() << "View mode switch failed:" << e.what();
    }
}

void PDFViewer::switchToSinglePageMode() {
    viewStack->setCurrentIndex(0);
    updatePageDisplay();
}

void PDFViewer::switchToContinuousMode() {
    viewStack->setCurrentIndex(1);
    if (document) {
        createContinuousPages();
    }
}

void PDFViewer::createContinuousPages() {
    if (!document)
        return;

    // 清空现有页面
    QLayoutItem* item;
    while ((item = continuousLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    // 清空渲染状态
    renderedPages.clear();

    // 获取第一页尺寸用于占位符
    QSizeF placeholderSize(100, 140);  // 默认A4比例
    std::unique_ptr<Poppler::Page> firstPage(document->page(0));
    if (firstPage) {
        placeholderSize = firstPage->pageSizeF();
    }

    // 应用缩放后的尺寸
    double scale = currentZoomFactor;
    int placeholderWidth = static_cast<int>(placeholderSize.width() * scale);
    int placeholderHeight = static_cast<int>(placeholderSize.height() * scale);

    // 创建所有页面占位符（不立即渲染）
    for (int i = 0; i < document->numPages(); ++i) {
        PDFPageWidget* pageWidget = new PDFPageWidget(continuousWidget);

        // 设置占位符尺寸，但不渲染内容
        pageWidget->setFixedSize(placeholderWidth, placeholderHeight);
        pageWidget->setText(QString("第 %1 页").arg(i + 1));  // 显示占位文本

        continuousLayout->addWidget(pageWidget);

        // 连接信号
        connect(pageWidget, &PDFPageWidget::scaleChanged, this,
                &PDFViewer::onScaleChanged);
    }

    continuousLayout->addStretch();

    // 连接滚动区域的滚动信号以实现虚拟化渲染
    if (continuousScrollArea->verticalScrollBar()) {
        connect(continuousScrollArea->verticalScrollBar(),
                &QScrollBar::valueChanged, this, [this]() {
                    scrollTimer->start();  // 使用防抖
                });
    }

    // 确保滚动条正确初始化
    continuousScrollArea->verticalScrollBar()->setValue(0);

    // 立即渲染初始可见页面
    QTimer::singleShot(0, this, [this]() { updateVisiblePages(); });
}

void PDFViewer::updateVisiblePages() {
    if (!document || currentViewMode != PDFViewMode::ContinuousScroll ||
        !isWidgetReady)
        return;

    QScrollBar* scrollBar = continuousScrollArea->verticalScrollBar();
    int viewportTop = scrollBar->value();
    int viewportBottom =
        viewportTop + continuousScrollArea->viewport()->height();
    int bufferPx = (viewportBottom - viewportTop);  // 增加缓冲区到一屏高度

    int newVisibleStart = -1;
    int newVisibleEnd = -1;

    int count = continuousLayout->count();
    for (int i = 0; i < count; ++i) {
        QWidget* w = continuousLayout->itemAt(i)->widget();
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

    if (qAbs(oldZoomFactor - currentZoomFactor) > 0.001) {
        // 如果缩放变化，强制重新渲染所有可见页面
        // 具体决定渲染什么页面在 renderVisiblePages 里处理
        renderedPages.clear();
    } else {
        // 否则，只渲染新增可见的页面
        renderedPages.removeIf(
            [this, newVisibleStart, newVisibleEnd](const auto& key) {
                int pageIndex = key.first;
                double zoom = key.second;
                return (zoom != currentZoomFactor) &&  // 缩放不同
                       (pageIndex >= newVisibleStart &&
                        pageIndex <= newVisibleEnd);  // 在可见范围内
            });
    }

    oldZoomFactor = currentZoomFactor;

    visiblePageStart = newVisibleStart;
    visiblePageEnd = newVisibleEnd;
    renderVisiblePages();
}

void PDFViewer::renderVisiblePages() {
    if (!document || !isWidgetReady)
        return;

    for (int i = visiblePageStart; i <= visiblePageEnd; ++i) {
        if (i < 0 || i >= continuousLayout->count())
            continue;

        // 如果已经渲染过且缩放没变，则跳过
        if (renderedPages.contains(qMakePair(i, currentZoomFactor)))
            continue;

        QLayoutItem* item = continuousLayout->itemAt(i);
        PDFPageWidget* pageWidget =
            qobject_cast<PDFPageWidget*>(item ? item->widget() : nullptr);

        if (pageWidget) {
            int pageIndex = i;
            // 使用 Lambda 捕获当前缩放，防止异步执行时缩放已变
            double zoom = currentZoomFactor;
            int rotation = currentRotation;

            // 标记为已提交渲染，防止重复触发 QTimer
            renderedPages.insert(qMakePair(pageIndex, zoom));

            QTimer::singleShot(
                0, this, [this, pageWidget, pageIndex, zoom, rotation]() {
                    if (!document)
                        return;

                    // 再次检查，防止重复渲染
                    if (qAbs(zoom - currentZoomFactor) > 0.001)
                        return;

                    std::unique_ptr<Poppler::Page> page(
                        document->page(pageIndex));
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
    if (currentViewMode == PDFViewMode::ContinuousScroll) {
        updateVisiblePages();
    }
}

void PDFViewer::scrollToPageInContinuousView(int pageNumber) {
    if (!document || currentViewMode != PDFViewMode::ContinuousScroll ||
        pageNumber < 0 || pageNumber >= document->numPages()) {
        return;
    }

    // 确保连续视图布局已经创建
    if (continuousLayout->count() <= pageNumber) {
        return;
    }

    // 获取目标页面的widget
    QLayoutItem* item = continuousLayout->itemAt(pageNumber);
    if (!item || !item->widget()) {
        return;
    }

    QWidget* pageWidget = item->widget();

    // 计算滚动位置，将页面滚动到视口中央
    int targetY =
        pageWidget->y() -
        (continuousScrollArea->viewport()->height() - pageWidget->height()) / 2;

    // 限制在有效范围内
    QScrollBar* scrollBar = continuousScrollArea->verticalScrollBar();
    targetY = qBound(scrollBar->minimum(), targetY, scrollBar->maximum());

    // 平滑滚动到目标位置
    scrollBar->setValue(targetY);

    // 确保目标页面被渲染
    updateVisiblePages();
}

QPixmap PDFViewer::getCachedPage(int pageNumber, double zoomFactor,
                                 int rotation) {
    auto it = pageCache.find(pageNumber);
    if (it != pageCache.end()) {
        const PageCacheItem& item = it.value();
        // 检查缓存是否匹配当前参数
        if (qAbs(item.zoomFactor - zoomFactor) < 0.001 &&
            item.rotation == rotation) {
            // 更新访问时间
            const_cast<PageCacheItem&>(item).lastAccessed =
                QDateTime::currentMSecsSinceEpoch();
            return item.pixmap;
        }
    }
    return QPixmap();  // 返回空的QPixmap表示缓存未命中
}

void PDFViewer::setCachedPage(int pageNumber, const QPixmap& pixmap,
                              double zoomFactor, int rotation) {
    // 如果缓存已满，清理最旧的条目
    if (pageCache.size() >= maxCacheSize) {
        cleanupCache();
    }

    PageCacheItem item;
    item.pixmap = pixmap;
    item.zoomFactor = zoomFactor;
    item.rotation = rotation;
    item.lastAccessed = QDateTime::currentMSecsSinceEpoch();

    pageCache[pageNumber] = item;
}

void PDFViewer::clearPageCache() { pageCache.clear(); }

void PDFViewer::cleanupCache() {
    if (pageCache.size() <= maxCacheSize / 2) {
        return;  // 不需要清理
    }

    // 找到最旧的条目并删除
    QList<int> keysToRemove;
    qint64 oldestTime = QDateTime::currentMSecsSinceEpoch();

    for (auto it = pageCache.begin(); it != pageCache.end(); ++it) {
        if (it.value().lastAccessed < oldestTime) {
            oldestTime = it.value().lastAccessed;
        }
    }

    // 删除超过一半的最旧条目
    int removeCount = pageCache.size() - maxCacheSize / 2;
    for (auto it = pageCache.begin(); it != pageCache.end() && removeCount > 0;
         ++it) {
        if (it.value().lastAccessed <= oldestTime + 1000) {  // 1秒容差
            keysToRemove.append(it.key());
            removeCount--;
        }
    }

    for (int key : keysToRemove) {
        pageCache.remove(key);
    }
}

void PDFViewer::toggleTheme() {
    STYLE.toggleTheme();

    setMessage(QString("已切换到%1主题")
                   .arg(STYLE.currentTheme() == Theme::Dark ? "暗色" : "亮色"));
}

void PDFViewer::updateThemeUI() {
    Theme currentTheme = STYLE.currentTheme();

    // 更新主题按钮图标
    if (currentTheme == Theme::Dark) {
        themeToggleBtn->setText("☀");
        themeToggleBtn->setToolTip("切换到亮色主题 (Ctrl+Shift+T)");
    } else {
        themeToggleBtn->setText("🌙");
        themeToggleBtn->setToolTip("切换到暗色主题 (Ctrl+Shift+T)");
    }

    // 重新应用主界面样式
    setStyleSheet(STYLE.getApplicationStyleSheet());

    // 更新工具栏样式
    if (toolbar) {
        toolbar->setStyleSheet(STYLE.getToolbarStyleSheet());
    }

    // 更新所有按钮的样式
    QString buttonStyle = STYLE.getButtonStyleSheet();
    if (firstPageBtn)
        firstPageBtn->setStyleSheet(buttonStyle);
    if (prevPageBtn)
        prevPageBtn->setStyleSheet(buttonStyle);
    if (nextPageBtn)
        nextPageBtn->setStyleSheet(buttonStyle);
    if (lastPageBtn)
        lastPageBtn->setStyleSheet(buttonStyle);
    if (zoomOutBtn)
        zoomOutBtn->setStyleSheet(buttonStyle);
    if (zoomInBtn)
        zoomInBtn->setStyleSheet(buttonStyle);
    if (fitWidthBtn)
        fitWidthBtn->setStyleSheet(buttonStyle);
    if (fitHeightBtn)
        fitHeightBtn->setStyleSheet(buttonStyle);
    if (fitPageBtn)
        fitPageBtn->setStyleSheet(buttonStyle);
    if (rotateLeftBtn)
        rotateLeftBtn->setStyleSheet(buttonStyle);
    if (rotateRightBtn)
        rotateRightBtn->setStyleSheet(buttonStyle);
    if (themeToggleBtn)
        themeToggleBtn->setStyleSheet(buttonStyle);

    // 更新滚动区域样式
    QString scrollStyle =
        STYLE.getPDFViewerStyleSheet() + STYLE.getScrollBarStyleSheet();
    if (singlePageScrollArea)
        singlePageScrollArea->setStyleSheet(scrollStyle);
    if (continuousScrollArea)
        continuousScrollArea->setStyleSheet(scrollStyle);

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

void PDFViewer::onViewModeChanged(int index) {
    PDFViewMode mode = static_cast<PDFViewMode>(index);
    setViewMode(mode);
    if (mode == PDFViewMode::ContinuousScroll) {
        // 延时0.05秒后设置isWidgetReady为true，确保布局完成
        QTimer::singleShot(50, this, [this]() {
            isWidgetReady = true;
            updateVisiblePages();  // 初始渲染可见页面
        });
    }
}

void PDFViewer::onZoomPercentageChanged() {
    int percentage = zoomPercentageSpinBox->value();
    setZoomFromPercentage(percentage);
}

void PDFViewer::onZoomTimerTimeout() {
    if (isZoomPending) {
        double factor = pendingZoomFactor;
        isZoomPending = false;
        applyZoom(factor);
    }
}

void PDFViewer::setZoomFromPercentage(int percentage) {
    double factor = percentage / 100.0;
    setZoomWithType(factor, ZoomType::FixedValue);
}

void PDFViewer::setZoomWithType(double factor, ZoomType type) {
    // 检查文档有效性
    if (!document || document->numPages() == 0) {
        qDebug() << "Cannot zoom: no valid document";
        return;
    }

    currentZoomType = type;

    // 限制缩放范围
    factor = qBound(MIN_ZOOM, factor, MAX_ZOOM);

    // 如果值没有变化，直接返回
    if (qAbs(factor - currentZoomFactor) < 0.001) {
        return;
    }

    // 改进的防抖机制
    bool shouldUseDebounce = false;

    // 根据缩放类型和变化幅度决定是否使用防抖
    if (type == ZoomType::FixedValue) {
        double changeMagnitude = qAbs(factor - currentZoomFactor);
        // 小幅度变化且不是第一次缩放时使用防抖
        shouldUseDebounce = (changeMagnitude < 0.15 && zoomTimer->isActive());
    }

    if (shouldUseDebounce) {
        // 使用防抖机制
        pendingZoomFactor = factor;
        isZoomPending = true;
        zoomTimer->start();  // 重新启动定时器
    } else {
        // 立即应用缩放
        if (zoomTimer->isActive()) {
            zoomTimer->stop();
        }

        // 如果有待处理的缩放，先清除
        if (isZoomPending) {
            isZoomPending = false;
        }

        applyZoom(factor);
    }
}

void PDFViewer::zoomToHeight() {
    if (!document)
        return;

    // 获取当前视图的viewport大小
    QScrollArea* currentScrollArea =
        (currentViewMode == PDFViewMode::SinglePage) ? singlePageScrollArea
                                                     : continuousScrollArea;
    QSize viewportSize = currentScrollArea->viewport()->size();

    if (document->numPages() > 0) {
        std::unique_ptr<Poppler::Page> page(document->page(currentPageNumber));
        if (page) {
            QSizeF pageSize = page->pageSizeF();
            double scale = viewportSize.height() / pageSize.height();
            setZoomWithType(scale * 0.95, ZoomType::FitHeight);  // 留一些边距
        }
    }
}

void PDFViewer::applyZoom(double factor) {
    factor = qBound(MIN_ZOOM, factor, MAX_ZOOM);
    if (factor != currentZoomFactor) {
        // 设置标志，防止信号循环
        bool wasZoomPending = isZoomPending;
        isZoomPending = true;

        currentZoomFactor = factor;

#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
        // 如果使用QGraphics渲染，更新QGraphics查看器
        if (useQGraphicsViewer && qgraphicsViewer) {
            qgraphicsViewer->setZoom(factor);
        } else {
#endif
            if (currentViewMode == PDFViewMode::SinglePage) {
                // 阻止信号发出，避免循环
                singlePageWidget->blockSignals(true);
                singlePageWidget->setScaleFactor(factor);
                singlePageWidget->blockSignals(false);
            } else {
                updateContinuousView();
            }
#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
        }
#endif

        updateZoomControls();
        saveZoomSettings();  // 保存缩放设置
        emit zoomChanged(factor);

        // 恢复标志状态
        isZoomPending = wasZoomPending;
    }
}

void PDFViewer::quickApplyZoom(double factor) {
    if (qAbs(factor - currentZoomFactor) < 0.001) {
        return;  // 缩放值没有变化
    }

    currentZoomFactor = factor;

    // 更新UI控件
    zoomPercentageSpinBox->blockSignals(true);
    zoomPercentageSpinBox->setValue(static_cast<int>(factor * 100));
    zoomPercentageSpinBox->blockSignals(false);

    zoomSlider->blockSignals(true);
    zoomSlider->setValue(static_cast<int>(factor * 100));
    zoomSlider->blockSignals(false);

    // 对已渲染的页面进行快速图像缩放
#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
    if (useQGraphicsViewer && qgraphicsViewer) {
        qgraphicsViewer->setZoom(factor);
    } else {
#endif
        if (currentViewMode == PDFViewMode::SinglePage) {
            // 单页模式：快速缩放当前页面
            singlePageWidget->blockSignals(true);
            singlePageWidget->quickScale(factor);
            singlePageWidget->blockSignals(false);
        } else if (currentViewMode == PDFViewMode::ContinuousScroll) {
            // 连续滚动模式：快速缩放已渲染的页面
            for (int i = 0; i < continuousLayout->count() - 1; ++i) {
                if (renderedPages.contains(qMakePair(i, oldZoomFactor))) {
                    QLayoutItem* item = continuousLayout->itemAt(i);
                    if (item && item->widget()) {
                        PDFPageWidget* pageWidget =
                            qobject_cast<PDFPageWidget*>(item->widget());
                        if (pageWidget) {
                            pageWidget->blockSignals(true);
                            pageWidget->quickScale(factor);
                            pageWidget->blockSignals(false);
                        }
                    }
                }
            }
        }
#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
    }
#endif

    // 更新按钮状态
    zoomInBtn->setEnabled(currentZoomFactor < MAX_ZOOM);
    zoomOutBtn->setEnabled(currentZoomFactor > MIN_ZOOM);

    emit zoomChanged(factor);
}

bool PDFViewer::eventFilter(QObject* object, QEvent* event) {
    // 处理连续滚动区域的Ctrl+滚轮缩放
    if (object == continuousScrollArea && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier) {
            int delta = wheelEvent->angleDelta().y();
            if (delta != 0) {
                // 使用与PDFPageWidget相同的缩放逻辑
                double scaleDelta = delta > 0 ? 1.15 : (1.0 / 1.15);
                double newZoom = currentZoomFactor * scaleDelta;
                setZoomWithType(newZoom, ZoomType::FixedValue);
            }
            return true;  // 事件已处理
        }
    }

    return QWidget::eventFilter(object, event);
}

void PDFViewer::saveZoomSettings() {
    QSettings settings;
    settings.beginGroup("PDFViewer");
    settings.setValue("defaultZoom", currentZoomFactor);
    settings.setValue("zoomType", static_cast<int>(currentZoomType));
    settings.endGroup();
}

void PDFViewer::loadZoomSettings() {
    QSettings settings;
    settings.beginGroup("PDFViewer");

    // 加载默认缩放值
    double savedZoom = settings.value("defaultZoom", DEFAULT_ZOOM).toDouble();
    int savedZoomType =
        settings.value("zoomType", static_cast<int>(ZoomType::FixedValue))
            .toInt();

    settings.endGroup();

    // 应用保存的设置
    currentZoomFactor = qBound(MIN_ZOOM, savedZoom, MAX_ZOOM);
    currentZoomType = static_cast<ZoomType>(savedZoomType);
}

void PDFViewer::keyPressEvent(QKeyEvent* event) {
    // 处理页码输入框的回车键
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (pageNumberSpinBox->hasFocus()) {
            // 如果页码输入框有焦点，应用当前值并跳转
            int pageNumber = pageNumberSpinBox->value();
            if (goToPageWithValidation(
                    pageNumber - 1,
                    true)) {  // SpinBox is 1-based, internal is 0-based
                pageNumberSpinBox->clearFocus();  // 清除焦点
            }
            event->accept();
            return;
        }
    }

    QWidget::keyPressEvent(event);
}

void PDFViewer::setMessage(const QString& message) {
    // 发出信号让主窗口显示消息
    // 这里可以通过信号传递给StatusBar或者其他消息显示组件
    qDebug() << "PDFViewer Message:" << message;
}

void PDFViewer::rotateLeft() {
    if (!document || document->numPages() == 0) {
        setMessage("没有可旋转的文档");
        return;
    }
    setRotation(currentRotation - 90);
}

void PDFViewer::rotateRight() {
    if (!document || document->numPages() == 0) {
        setMessage("没有可旋转的文档");
        return;
    }
    setRotation(currentRotation + 90);
}

void PDFViewer::resetRotation() {
    if (!document || document->numPages() == 0) {
        setMessage("没有可重置的文档");
        return;
    }
    setRotation(0);
}

void PDFViewer::setRotation(int degrees) {
    // 检查文档有效性
    if (!document || document->numPages() == 0) {
        qDebug() << "Cannot rotate: no valid document";
        return;
    }

    // 确保旋转角度是90度的倍数
    degrees = ((degrees % 360) + 360) % 360;

    if (degrees != currentRotation) {
        int oldRotation = currentRotation;
        currentRotation = degrees;

        try {
#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
            // 如果使用QGraphics渲染，更新QGraphics查看器
            if (useQGraphicsViewer && qgraphicsViewer) {
                qgraphicsViewer->setRotation(currentRotation);
            } else {
#endif
                // 更新当前视图
                if (currentViewMode == PDFViewMode::SinglePage) {
                    if (currentPageNumber >= 0 &&
                        currentPageNumber < document->numPages()) {
                        std::unique_ptr<Poppler::Page> page(
                            document->page(currentPageNumber));
                        if (page) {
                            singlePageWidget->setPage(page.release(),
                                                      currentZoomFactor,
                                                      currentRotation);
                        } else {
                            throw std::runtime_error(
                                "Failed to get page for rotation");
                        }
                    }
                } else {
                    // 更新连续视图中的所有页面
                    updateContinuousViewRotation();
                }
#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
            }
#endif

            emit rotationChanged(currentRotation);
            setMessage(QString("页面已旋转到 %1 度").arg(currentRotation));
        } catch (const std::exception& e) {
            // 恢复旧的旋转状态
            currentRotation = oldRotation;
            setMessage(QString("旋转失败: %1").arg(e.what()));
            qDebug() << "Rotation failed:" << e.what();
        }
    }
}

void PDFViewer::updateContinuousViewRotation() {
    if (!document || currentViewMode != PDFViewMode::ContinuousScroll) {
        return;
    }

    // 旋转变化时，清空已渲染状态，触发重新渲染
    renderedPages.clear();

    int totalPages = continuousLayout->count() - 1;  // -1 因为最后一个是stretch

    // 更新连续视图中所有页面的旋转
    // 使用延迟渲染，避免卡顿
    for (int i = 0; i < totalPages; ++i) {
        QLayoutItem* item = continuousLayout->itemAt(i);
        if (item && item->widget()) {
            PDFPageWidget* pageWidget =
                qobject_cast<PDFPageWidget*>(item->widget());
            if (pageWidget && i < document->numPages()) {
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
    if (searchWidget) {
        searchWidget->setVisible(true);
        searchWidget->focusSearchInput();
        searchWidget->setDocument(document);
    }
}

void PDFViewer::hideSearch() {
    if (searchWidget) {
        searchWidget->setVisible(false);
        searchWidget->clearSearch();
    }
}

void PDFViewer::toggleSearch() {
    if (searchWidget) {
        if (searchWidget->isVisible()) {
            hideSearch();
        } else {
            showSearch();
        }
    }
}

void PDFViewer::findNext() {
    if (searchWidget && searchWidget->isVisible()) {
        searchWidget->nextResult();
    }
}

void PDFViewer::findPrevious() {
    if (searchWidget && searchWidget->isVisible()) {
        searchWidget->previousResult();
    }
}

void PDFViewer::clearSearch() {
    if (searchWidget) {
        searchWidget->clearSearch();
    }
}

// 搜索相关槽函数
void PDFViewer::onSearchRequested(const QString& query,
                                  const SearchOptions& options) {
    // Clear previous search highlights
    clearSearchHighlights();

    if (!query.isEmpty() && document) {
        // The search is handled by SearchWidget, but we prepare for
        // highlighting
        setMessage(QString("搜索: %1").arg(query));
    }
}

void PDFViewer::onSearchResultSelected(const SearchResult& result) {
    // 当搜索结果被选中时，导航到对应页面并高亮
    if (result.pageNumber >= 0) {
        goToPage(result.pageNumber);
        highlightCurrentSearchResult(result);
    }
}

void PDFViewer::onNavigateToSearchResult(int pageNumber, const QRectF& rect) {
    // 导航到搜索结果位置并应用高亮
    if (pageNumber >= 0 && pageNumber < (document ? document->numPages() : 0)) {
        goToPage(pageNumber);

        // Apply highlighting to the current page
        updateSearchHighlightsForCurrentPage();

        setMessage(QString("已导航到第 %1 页的搜索结果").arg(pageNumber + 1));
    }
}

// Search highlighting implementation
void PDFViewer::setSearchResults(const QList<SearchResult>& results) {
    m_allSearchResults = results;
    updateSearchHighlightsForCurrentPage();
}

void PDFViewer::clearSearchHighlights() {
    m_allSearchResults.clear();
    m_currentSearchResultIndex = -1;

    // Clear highlights from current page widget
    if (currentViewMode == PDFViewMode::SinglePage && singlePageWidget) {
        singlePageWidget->clearSearchHighlights();
    } else if (currentViewMode == PDFViewMode::ContinuousScroll) {
        // Clear highlights from all visible page widgets in continuous mode
        for (int i = 0; i < continuousLayout->count() - 1; ++i) {
            QLayoutItem* item = continuousLayout->itemAt(i);
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
    m_currentSearchResultIndex = findSearchResultIndex(result);
    updateSearchHighlightsForCurrentPage();
}

void PDFViewer::updateSearchHighlightsForCurrentPage() {
    if (m_allSearchResults.isEmpty()) {
        return;
    }

    if (currentViewMode == PDFViewMode::SinglePage && singlePageWidget) {
        // Filter results for current page in single-page mode
        QList<SearchResult> currentPageResults;
        for (int i = 0; i < m_allSearchResults.size(); ++i) {
            SearchResult result = m_allSearchResults[i];
            if (result.pageNumber == currentPageNumber) {
                // Mark current result if this is the selected one
                result.isCurrentResult = (i == m_currentSearchResultIndex);
                currentPageResults.append(result);
            }
        }
        singlePageWidget->setSearchResults(currentPageResults);

    } else if (currentViewMode == PDFViewMode::ContinuousScroll) {
        // Apply highlights to all visible pages in continuous mode
        updateAllPagesSearchHighlights();
    }
}

void PDFViewer::updateAllPagesSearchHighlights() {
    if (m_allSearchResults.isEmpty() ||
        currentViewMode != PDFViewMode::ContinuousScroll) {
        return;
    }

    // Group results by page number
    QHash<int, QList<SearchResult>> resultsByPage;
    for (int i = 0; i < m_allSearchResults.size(); ++i) {
        SearchResult result = m_allSearchResults[i];
        result.isCurrentResult = (i == m_currentSearchResultIndex);
        resultsByPage[result.pageNumber].append(result);
    }

    // Apply highlights to each page widget
    for (int pageNum = 0; pageNum < continuousLayout->count() - 1; ++pageNum) {
        QLayoutItem* item = continuousLayout->itemAt(pageNum);
        if (item && item->widget()) {
            PDFPageWidget* pageWidget =
                qobject_cast<PDFPageWidget*>(item->widget());
            if (pageWidget) {
                if (resultsByPage.contains(pageNum)) {
                    pageWidget->setSearchResults(resultsByPage[pageNum]);
                } else {
                    pageWidget->clearSearchHighlights();
                }
            }
        }
    }
}

int PDFViewer::findSearchResultIndex(const SearchResult& target) {
    for (int i = 0; i < m_allSearchResults.size(); ++i) {
        const SearchResult& result = m_allSearchResults[i];
        if (result.pageNumber == target.pageNumber &&
            result.startIndex == target.startIndex &&
            result.length == target.length) {
            return i;
        }
    }
    return -1;
}

// 书签功能实现
void PDFViewer::addBookmark() {
    if (document && currentPageNumber >= 0) {
        addBookmarkForPage(currentPageNumber);
    }
}

void PDFViewer::addBookmarkForPage(int pageNumber) {
    if (!document || pageNumber < 0 || pageNumber >= document->numPages()) {
        setMessage("无法添加书签：页面无效");
        return;
    }

    // 发出书签请求信号，让上层组件处理
    emit bookmarkRequested(pageNumber);
    setMessage(QString("已为第 %1 页添加书签").arg(pageNumber + 1));
}

void PDFViewer::removeBookmark() {
    if (document && currentPageNumber >= 0) {
        // 这里需要与BookmarkWidget集成来实际删除书签
        // 目前只是发出信号
        setMessage(QString("已移除第 %1 页的书签").arg(currentPageNumber + 1));
    }
}

void PDFViewer::toggleBookmark() {
    if (hasBookmarkForCurrentPage()) {
        removeBookmark();
    } else {
        addBookmark();
    }
}

#ifdef ENABLE_QGRAPHICS_PDF_SUPPORT
void PDFViewer::setQGraphicsRenderingEnabled(bool enabled) {
    if (useQGraphicsViewer == enabled) {
        return;  // No change needed
    }

    useQGraphicsViewer = enabled;

    if (enabled) {
        // Create QGraphics viewer if not exists
        if (!qgraphicsViewer) {
            try {
                qgraphicsViewer = new QGraphicsPDFViewer(this);
            } catch (...) {
                // If QGraphics viewer creation fails, fall back to traditional
                // mode
                useQGraphicsViewer = false;
                return;
            }

            // Connect signals
            connect(qgraphicsViewer, &QGraphicsPDFViewer::currentPageChanged,
                    this, [this](int page) {
                        currentPageNumber = page;
                        emit pageChanged(page);
                    });

            connect(qgraphicsViewer, &QGraphicsPDFViewer::zoomChanged, this,
                    [this](double zoom) {
                        currentZoomFactor = zoom;
                        emit zoomChanged(zoom);
                    });

            connect(qgraphicsViewer, &QGraphicsPDFViewer::rotationChanged, this,
                    [this](int rotation) {
                        currentRotation = rotation;
                        emit rotationChanged(rotation);
                    });

            connect(qgraphicsViewer, &QGraphicsPDFViewer::documentChanged, this,
                    &PDFViewer::documentChanged);
        }

        // Hide traditional viewer components and show QGraphics viewer
        if (singlePageWidget)
            singlePageWidget->hide();
        if (continuousScrollArea)
            continuousScrollArea->hide();

        if (qgraphicsViewer) {
            qgraphicsViewer->show();
            if (document) {
                qgraphicsViewer->setDocument(document);
                qgraphicsViewer->goToPage(currentPageNumber);
                qgraphicsViewer->setZoom(currentZoomFactor);
                qgraphicsViewer->setRotation(currentRotation);
            }
        }

        // Update layout to include QGraphics viewer
        if (layout() && qgraphicsViewer->parent() != this) {
            // Only add to layout if not already added
            QVBoxLayout* vLayout = qobject_cast<QVBoxLayout*>(layout());
            if (vLayout) {
                vLayout->addWidget(qgraphicsViewer,
                                   1);  // Give it stretch factor
            }
        }

    } else {
        // Hide QGraphics viewer and show traditional components
        if (qgraphicsViewer) {
            qgraphicsViewer->hide();
        }

        if (singlePageWidget)
            singlePageWidget->show();
        if (continuousScrollArea &&
            currentViewMode == PDFViewMode::ContinuousScroll) {
            continuousScrollArea->show();
        }

        // Update traditional viewer with current state
        updatePageDisplay();
    }
}

bool PDFViewer::isQGraphicsRenderingEnabled() const {
    return useQGraphicsViewer;
}

void PDFViewer::setQGraphicsHighQualityRendering(bool enabled) {
    if (qgraphicsViewer) {
        qgraphicsViewer->setHighQualityRendering(enabled);
    }
}

void PDFViewer::setQGraphicsViewMode(int mode) {
    if (qgraphicsViewer) {
        QGraphicsPDFViewer::ViewMode viewMode =
            static_cast<QGraphicsPDFViewer::ViewMode>(mode);
        qgraphicsViewer->setViewMode(viewMode);
    }
}
#endif

bool PDFViewer::hasBookmarkForCurrentPage() const {
    // 这里需要与BookmarkWidget集成来检查书签状态
    // 目前返回false作为占位符
    return false;
}
