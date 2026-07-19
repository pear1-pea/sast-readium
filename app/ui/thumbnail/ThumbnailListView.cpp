#include "ThumbnailListView.h"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDebug>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include "delegate/ThumbnailDelegate.h"
#include "managers/StyleManager.h"
#include "model/ThumbnailModel.h"
#include "ui/thumbnail/ProgressiveThumbnailLoader.h"
#include "ui/thumbnail/ThumbnailGenerator.h"
#include "utils/LoggingMacros.h"

namespace {
struct PreloadSpan {
    int before = 0;
    int after = 0;
};

PreloadSpan weightedPreloadSpan(int baseCount, double velocity,
                                double slowThreshold, double mediumThreshold,
                                int biasDirection) {
    const int total = baseCount * 2;
    if (total <= 0) {
        return {};
    }

    double beforeRatio = 0.5;
    if (biasDirection > 0) {
        beforeRatio = velocity < slowThreshold     ? 0.4
                      : velocity < mediumThreshold ? 0.25
                                                   : 0.1;
    } else if (biasDirection < 0) {
        beforeRatio = velocity < slowThreshold     ? 0.6
                      : velocity < mediumThreshold ? 0.65
                                                   : 0.75;
    }

    const int before = qBound(0, qRound(total * beforeRatio), total);
    return {before, total - before};
}
}  // namespace

ThumbnailListView::ThumbnailListView(QWidget* parent)
    : QListView(parent),
      m_thumbnailModel(nullptr),
      m_thumbnailDelegate(nullptr),
      m_thumbnailSize(DEFAULT_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_HEIGHT),
      m_thumbnailSpacing(DEFAULT_SPACING),
      m_animationEnabled(true),
      m_smoothScrolling(true),
      m_scrollAnimation(nullptr),
      m_targetScrollPosition(0),
      m_isScrollAnimating(false),
      m_preloadMargin(DEFAULT_PRELOAD_MARGIN),
      m_autoPreload(true),
      m_preloadTimer(nullptr),
      m_idleStartTimer(nullptr),
      m_idlePreloadTimer(nullptr),
      m_contextMenuEnabled(true),
      m_contextMenu(nullptr),
      m_contextMenuPage(-1),
      m_currentPage(-1),
      m_progressiveLoader(nullptr),
      m_delegateAnimationTimer(nullptr),
      m_viewportUpdatePending(false),
      m_lastVisibleStart(-1),
      m_lastVisibleEnd(-1),
      m_lastPreloadStart(-1),
      m_lastPreloadEnd(-1),
      m_scrollVelocity(0.0),
      m_lastScrollTime(0),
      m_lastScrollPosition(0),
      m_scrollDirection(0),
      m_candidatePreloadDirection(0),
      m_candidatePreloadDirectionSince(0),
      m_preloadBiasDirection(0),
      m_idlePreloadDirection(0) {
    setupUI();
    setupScrollBars();
    setupAnimations();
    setupContextMenu();
    connectSignals();
    m_animationClock.start();
}

ThumbnailListView::~ThumbnailListView() {
    if (m_scrollAnimation) {
        m_scrollAnimation->stop();
    }

    if (m_preloadTimer) {
        m_preloadTimer->stop();
    }

    if (m_idleStartTimer) {
        m_idleStartTimer->stop();
    }

    if (m_idlePreloadTimer) {
        m_idlePreloadTimer->stop();
    }

    if (m_delegateAnimationTimer) {
        m_delegateAnimationTimer->stop();
    }
}

void ThumbnailListView::setupUI() {
    // 设置基本属性
    setViewMode(QListView::IconMode);
    setFlow(QListView::TopToBottom);
    setWrapping(false);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectItems);

    // 设置滚动属性
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 设置间距
    setSpacing(m_thumbnailSpacing);

    // 设置边距
    setContentsMargins(8, 8, 8, 8);

    // 启用鼠标跟踪
    setMouseTracking(true);

    // 设置焦点策略
    setFocusPolicy(Qt::StrongFocus);

    // 设置拖拽（暂时禁用）
    setDragDropMode(QAbstractItemView::NoDragDrop);
}

void ThumbnailListView::setupScrollBars() {
    // 自定义滚动条样式将在CSS中定义
    QScrollBar* vScrollBar = verticalScrollBar();
    if (vScrollBar) {
        vScrollBar->setObjectName("ThumbnailScrollBar");
    }

    updateScrollBarStyle();
}

void ThumbnailListView::setupAnimations() {
    // 滚动动画
    m_scrollAnimation =
        new QPropertyAnimation(verticalScrollBar(), "value", this);
    m_scrollAnimation->setDuration(SCROLL_ANIMATION_DURATION);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_scrollAnimation, &QPropertyAnimation::finished, this,
            &ThumbnailListView::onScrollAnimationFinished);

    // 预加载定时器
    m_preloadTimer = new QTimer(this);
    m_preloadTimer->setInterval(PRELOAD_TIMER_INTERVAL);
    m_preloadTimer->setSingleShot(true);
    connect(m_preloadTimer, &QTimer::timeout, this,
            &ThumbnailListView::onPreloadTimer);

    m_idleStartTimer = new QTimer(this);
    m_idleStartTimer->setSingleShot(true);
    connect(m_idleStartTimer, &QTimer::timeout, this,
            &ThumbnailListView::onIdleStartTimer);

    m_idlePreloadTimer = new QTimer(this);
    m_idlePreloadTimer->setSingleShot(true);
    connect(m_idlePreloadTimer, &QTimer::timeout, this,
            &ThumbnailListView::onIdlePreloadTimer);

    // 视口更新定时器 - 性能优化
    m_viewportUpdateTimer = new QTimer(this);
    m_viewportUpdateTimer->setSingleShot(true);
    m_viewportUpdateTimer->setInterval(50);  // 50ms延迟
    connect(m_viewportUpdateTimer, &QTimer::timeout, this,
            &ThumbnailListView::optimizedUpdateVisibleRange);

    // Delegate animation driver (~30fps) — advances lerp/states and triggers
    // repaint. Started on demand by restartAnimationTimer().
    m_delegateAnimationTimer = new QTimer(this);
    m_delegateAnimationTimer->setInterval(DELEGATE_ANIMATION_INTERVAL);
    connect(m_delegateAnimationTimer, &QTimer::timeout, this,
            &ThumbnailListView::onDelegateAnimationTick);
    // NOT started here — starts on first hover/loading
}

void ThumbnailListView::setupContextMenu() {
    m_contextMenu = new QMenu(this);
    m_contextMenu->setObjectName("ThumbnailContextMenu");

    // 添加默认动作
    QAction* copyAction = new QAction("复制页面", this);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, [this]() {
        if (m_contextMenuPage >= 0) {
            copyPageToClipboard(m_contextMenuPage);
        }
    });

    QAction* exportAction = new QAction("导出页面", this);
    connect(exportAction, &QAction::triggered, [this]() {
        if (m_contextMenuPage >= 0) {
            exportPageToFile(m_contextMenuPage);
        }
    });

    m_contextMenu->addAction(copyAction);
    m_contextMenu->addAction(exportAction);
    m_contextMenuActions << copyAction << exportAction;
}

void ThumbnailListView::connectSignals() {
    // 滚动条信号
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &ThumbnailListView::onScrollBarValueChanged);
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
            &ThumbnailListView::onScrollBarRangeChanged);
}

void ThumbnailListView::setThumbnailModel(ThumbnailModel* model) {
    if (m_thumbnailModel == model) {
        return;
    }

    // 断开旧模型的信号
    if (m_thumbnailModel) {
        disconnect(m_thumbnailModel, nullptr, this, nullptr);
    }

    m_thumbnailModel = model;
    setModel(model);

    // 连接新模型的信号
    if (m_thumbnailModel) {
        connect(m_thumbnailModel, &QAbstractItemModel::dataChanged, this,
                &ThumbnailListView::onModelDataChanged);
        connect(m_thumbnailModel, &QAbstractItemModel::rowsInserted, this,
                &ThumbnailListView::onModelRowsInserted);
        connect(m_thumbnailModel, &QAbstractItemModel::rowsRemoved, this,
                &ThumbnailListView::onModelRowsRemoved);
    }

    // Recreate progressive loader tied to the new model's generator
    delete m_progressiveLoader;
    m_progressiveLoader = nullptr;
    ThumbnailGenerator* gen =
        m_thumbnailModel ? m_thumbnailModel->findChild<ThumbnailGenerator*>()
                         : nullptr;
    if (gen) {
        m_progressiveLoader = new ProgressiveThumbnailLoader(gen, this);
        connect(m_progressiveLoader,
                &ProgressiveThumbnailLoader::requestHighRes, m_thumbnailModel,
                &ThumbnailModel::requestThumbnail);
    }

    updateItemSizes();
    scheduleViewportUpdate();
}

ThumbnailModel* ThumbnailListView::thumbnailModel() const {
    return m_thumbnailModel;
}

void ThumbnailListView::setThumbnailDelegate(ThumbnailDelegate* delegate) {
    if (m_thumbnailDelegate == delegate) {
        return;
    }

    m_thumbnailDelegate = delegate;
    setItemDelegate(delegate);

    if (delegate) {
        delegate->setParent(this);
    }

    // Enable hover detection via WA_Hover so the delegate's paint() reads
    // State_MouseOver; the entered signal ensures repaint on item change.
    viewport()->setAttribute(Qt::WA_Hover);
    connect(this, &QAbstractItemView::entered, this,
            [this]() { restartAnimationTimer(); });

    updateItemSizes();
}

ThumbnailDelegate* ThumbnailListView::thumbnailDelegate() const {
    return m_thumbnailDelegate;
}

void ThumbnailListView::setThumbnailSize(const QSize& size) {
    if (m_thumbnailSize != size && size.isValid()) {
        m_thumbnailSize = size;

        if (m_thumbnailDelegate) {
            m_thumbnailDelegate->setThumbnailSize(size);
        }

        if (m_thumbnailModel) {
            m_thumbnailModel->setThumbnailSize(size);
        }

        updateItemSizes();
        scheduleDelayedItemsLayout();
    }
}

QSize ThumbnailListView::thumbnailSize() const { return m_thumbnailSize; }

void ThumbnailListView::setThumbnailSpacing(int spacing) {
    if (m_thumbnailSpacing != spacing && spacing >= 0) {
        m_thumbnailSpacing = spacing;
        setSpacing(spacing);

        if (m_thumbnailDelegate) {
            m_thumbnailDelegate->setMargins(spacing / 2);
        }

        scheduleDelayedItemsLayout();
    }
}

int ThumbnailListView::thumbnailSpacing() const { return m_thumbnailSpacing; }

void ThumbnailListView::scrollToPage(int pageNumber, bool animated) {
    if (!m_thumbnailModel || pageNumber < 0 ||
        pageNumber >= m_thumbnailModel->rowCount()) {
        return;
    }

    QModelIndex index = m_thumbnailModel->index(pageNumber);
    if (!index.isValid()) {
        return;
    }

    if (animated && m_animationEnabled) {
        // 首先获取当前滚动条值
        int currentValue = verticalScrollBar()->value();

        // 临时禁用动画，获取目标页面居中的准确位置
        scrollTo(index, QAbstractItemView::PositionAtCenter);
        int targetValue = verticalScrollBar()->value();

        // 恢复原始位置
        verticalScrollBar()->setValue(currentValue);

        // 现在可以安全地进行动画滚动
        if (targetValue != currentValue) {
            animateScrollTo(targetValue);
        }
    } else {
        scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
}

void ThumbnailListView::scrollToTop(bool animated) {
    if (animated && m_animationEnabled) {
        animateScrollTo(verticalScrollBar()->minimum());
    } else {
        verticalScrollBar()->setValue(verticalScrollBar()->minimum());
    }
}

void ThumbnailListView::scrollToBottom(bool animated) {
    if (animated && m_animationEnabled) {
        animateScrollTo(verticalScrollBar()->maximum());
    } else {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
}

int ThumbnailListView::currentPage() const { return m_currentPage; }

void ThumbnailListView::setCurrentPage(int pageNumber, bool animated) {
    if (m_currentPage == pageNumber) {
        return;
    }

    LOG_INFO("ThumbnailListView: Current page changed from {} to {}",
             m_currentPage + 1, pageNumber + 1);
    m_currentPage = pageNumber;

    if (pageNumber >= 0) {
        QModelIndex index = indexAtPage(pageNumber);
        if (index.isValid()) {
            setCurrentIndex(index);
            scrollToPage(pageNumber, animated);
        }
    }

    emit currentPageChanged(pageNumber);
}

void ThumbnailListView::selectPage(int pageNumber) {
    QModelIndex index = indexAtPage(pageNumber);
    if (index.isValid()) {
        selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
        setCurrentIndex(index);
    }
}

void ThumbnailListView::selectPages(const QList<int>& pageNumbers) {
    if (selectionMode() == QAbstractItemView::SingleSelection &&
        pageNumbers.size() > 1) {
        return;  // 单选模式下不能选择多个
    }

    selectionModel()->clearSelection();

    for (int pageNumber : pageNumbers) {
        QModelIndex index = indexAtPage(pageNumber);
        if (index.isValid()) {
            selectionModel()->select(index, QItemSelectionModel::Select);
        }
    }

    if (!pageNumbers.isEmpty()) {
        setCurrentIndex(indexAtPage(pageNumbers.first()));
    }
}

void ThumbnailListView::clearSelection() {
    selectionModel()->clearSelection();
    m_selectedPages.clear();
    emit pageSelectionChanged(m_selectedPages);
}

QList<int> ThumbnailListView::selectedPages() const {
    QList<int> pages;
    QModelIndexList selectedIndexes = selectionModel()->selectedIndexes();

    for (const QModelIndex& index : selectedIndexes) {
        int page = pageAtIndex(index);
        if (page >= 0) {
            pages.append(page);
        }
    }

    std::sort(pages.begin(), pages.end());
    return pages;
}

void ThumbnailListView::setAnimationEnabled(bool enabled) {
    m_animationEnabled = enabled;
    if (!enabled) {
        m_animationStates.clear();
        m_delegateAnimationTimer->stop();
    }
}

void ThumbnailListView::setSmoothScrolling(bool enabled) {
    m_smoothScrolling = enabled;
}

void ThumbnailListView::setPreloadMargin(int margin) {
    m_preloadMargin = qMax(0, margin);
    updatePreloadRange();
}

void ThumbnailListView::setAutoPreload(bool enabled) {
    m_autoPreload = enabled;
    if (enabled) {
        updatePreloadRange();
    }
}

void ThumbnailListView::setContextMenuEnabled(bool enabled) {
    m_contextMenuEnabled = enabled;
}

void ThumbnailListView::addContextMenuAction(QAction* action) {
    if (action && !m_contextMenuActions.contains(action)) {
        m_contextMenu->addAction(action);
        m_contextMenuActions.append(action);
    }
}

void ThumbnailListView::removeContextMenuAction(QAction* action) {
    if (action && m_contextMenuActions.contains(action)) {
        m_contextMenu->removeAction(action);
        m_contextMenuActions.removeOne(action);
    }
}

void ThumbnailListView::clearContextMenuActions() {
    m_contextMenu->clear();
    m_contextMenuActions.clear();
}

void ThumbnailListView::wheelEvent(QWheelEvent* event) {
    if (m_smoothScrolling && m_animationEnabled) {
        // 平滑滚动
        int delta = event->angleDelta().y();
        int steps = delta / 120;  // 标准滚轮步数
        int scrollAmount = steps * SMOOTH_SCROLL_STEP;

        int currentValue = verticalScrollBar()->value();
        int targetValue =
            qBound(verticalScrollBar()->minimum(), currentValue - scrollAmount,
                   verticalScrollBar()->maximum());

        if (targetValue != currentValue) {
            animateScrollTo(targetValue);
        }

        event->accept();
    } else {
        QListView::wheelEvent(event);
    }

    // 触发预加载
    if (m_autoPreload) {
        m_preloadTimer->start();
    }
}

void ThumbnailListView::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Home:
            scrollToTop(m_animationEnabled);
            event->accept();
            break;

        case Qt::Key_End:
            scrollToBottom(m_animationEnabled);
            event->accept();
            break;

        case Qt::Key_PageUp:
            verticalScrollBar()->triggerAction(
                QAbstractSlider::SliderPageStepSub);
            event->accept();
            break;

        case Qt::Key_PageDown:
            verticalScrollBar()->triggerAction(
                QAbstractSlider::SliderPageStepAdd);
            event->accept();
            break;

        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            QListView::keyPressEvent(event);
            if (m_autoPreload) {
                m_preloadTimer->start();
            }
            break;

        default:
            QListView::keyPressEvent(event);
            break;
    }
}

void ThumbnailListView::mousePressEvent(QMouseEvent* event) {
    QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        int pageNumber = pageAtIndex(index);
        if (pageNumber >= 0) {
            handlePageClick(pageNumber);
        }
    }

    QListView::mousePressEvent(event);
}

void ThumbnailListView::mouseDoubleClickEvent(QMouseEvent* event) {
    QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        int pageNumber = pageAtIndex(index);
        if (pageNumber >= 0) {
            handlePageDoubleClick(pageNumber);
        }
    }

    QListView::mouseDoubleClickEvent(event);
}

void ThumbnailListView::contextMenuEvent(QContextMenuEvent* event) {
    if (!m_contextMenuEnabled) {
        QListView::contextMenuEvent(event);
        return;
    }

    QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        int pageNumber = pageAtIndex(index);
        if (pageNumber >= 0) {
            m_contextMenuPage = pageNumber;
            handlePageRightClick(pageNumber, event->globalPos());
            showContextMenu(event->pos());
        }
    }

    event->accept();
}

// 缺失的方法实现

void ThumbnailListView::onScrollBarValueChanged(int value) {
    Q_UNUSED(value)
    stopIdlePreloadTimers();
    scheduleViewportUpdate();
    if (m_autoPreload) {
        m_preloadTimer->start();
    }
}

void ThumbnailListView::onScrollBarRangeChanged(int min, int max) {
    Q_UNUSED(min);
    Q_UNUSED(max);
    stopIdlePreloadTimers();
    scheduleViewportUpdate();
}

void ThumbnailListView::stopIdlePreloadTimers() {
    if (m_idleStartTimer) {
        m_idleStartTimer->stop();
    }
    if (m_idlePreloadTimer) {
        m_idlePreloadTimer->stop();
    }
    m_idlePreloadDirection = 0;
}

void ThumbnailListView::scheduleIdlePreload() {
    if (!m_autoPreload || !m_idleStartTimer || m_preloadBiasDirection == 0 ||
        m_visibleRange.first < 0) {
        return;
    }

    m_idleStartTimer->start(IDLE_START_DELAY_MS);
}

void ThumbnailListView::onModelDataChanged(const QModelIndex& topLeft,
                                           const QModelIndex& bottomRight) {
    // Repaint only the affected rows instead of the full viewport
    viewport()->update(visualRect(topLeft).united(visualRect(bottomRight)));
}

void ThumbnailListView::onModelRowsInserted(const QModelIndex& parent,
                                            int first, int last) {
    Q_UNUSED(parent);
    Q_UNUSED(first);
    Q_UNUSED(last);
    updateItemSizes();
    scheduleViewportUpdate();
}

void ThumbnailListView::onModelRowsRemoved(const QModelIndex& parent, int first,
                                           int last) {
    Q_UNUSED(parent);
    Q_UNUSED(first);
    Q_UNUSED(last);
    updateItemSizes();
    scheduleViewportUpdate();
}

void ThumbnailListView::onScrollAnimationFinished() {
    m_isScrolling = false;
    m_isScrollAnimating = false;
    scheduleViewportUpdate();
}

void ThumbnailListView::onPreloadTimer() { updatePreloadRange(); }

void ThumbnailListView::onIdleStartTimer() {
    if (!m_autoPreload || m_preloadBiasDirection == 0) {
        return;
    }

    m_idlePreloadDirection = m_preloadBiasDirection;
    const int delay = m_idlePreloadDirection > 0 ? IDLE_PRELOAD_DOWN_DELAY_MS
                                                 : IDLE_PRELOAD_UP_DELAY_MS;
    m_idlePreloadTimer->start(delay);
}

void ThumbnailListView::onIdlePreloadTimer() {
    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (!thumbnailModel || m_visibleRange.first < 0 ||
        m_idlePreloadDirection == 0) {
        return;
    }

    const int numPages = thumbnailModel->rowCount();
    if (m_lastPreloadStart < 0 || m_lastPreloadEnd < 0) {
        return;
    }

    if (m_idlePreloadDirection > 0) {
        requestThumbnailRange(
            m_lastPreloadEnd + 1,
            qMin(numPages - 1, m_lastPreloadEnd + IDLE_PRELOAD_EXTRA_COUNT));
    } else {
        requestThumbnailRange(
            qMax(0, m_lastPreloadStart - IDLE_PRELOAD_EXTRA_COUNT),
            m_lastPreloadStart - 1);
    }
}

void ThumbnailListView::onDelegateAnimationTick() {
    advanceAnimationStates();
    viewport()->update();
    if (!hasActiveAnimations())
        m_delegateAnimationTimer->stop();
}

void ThumbnailListView::advanceAnimationStates() {
    if (!m_animationEnabled)
        return;

    ThumbnailModel* model = qobject_cast<ThumbnailModel*>(this->model());
    if (!model)
        return;

    QPair<int, int> visible = calculateVisibleRange();
    if (visible.first < 0 || visible.second < 0)
        return;

    // Determine which item (if any) is under the mouse cursor
    QPoint cursorPos = viewport()->mapFromGlobal(QCursor::pos());
    QModelIndex hoveredIndex = indexAt(cursorPos);

    qint64 elapsed = m_animationClock.elapsed();

    for (int page = visible.first; page <= visible.second; ++page) {
        QModelIndex idx = model->index(page, 0);
        if (!idx.isValid())
            continue;

        bool isHovered = (idx == hoveredIndex);
        bool isSelected = selectionModel()->isSelected(idx);
        bool isLoading = idx.data(ThumbnailModel::LoadingRole).toBool();

        AnimationState& state = m_animationStates[page];

        // Exponential interpolation toward target values
        qreal hoverTarget = isHovered ? 1.0 : 0.0;
        qreal selTarget = isSelected ? 1.0 : 0.0;
        state.hoverOpacity +=
            (hoverTarget - state.hoverOpacity) * HOVER_LERP_FACTOR;
        state.selectionOpacity +=
            (selTarget - state.selectionOpacity) * SELECTION_LERP_FACTOR;

        // Compute spinner angle only for loading items
        if (isLoading)
            state.spinnerAngle = elapsed * SPINNER_DEG_PER_MS + page * 20.0;
    }
}

bool ThumbnailListView::hasActiveAnimations() const {
    if (!m_animationEnabled)
        return false;

    // Check if any lerp state is still transitioning
    for (auto it = m_animationStates.constBegin();
         it != m_animationStates.constEnd(); ++it) {
        qreal h = it->hoverOpacity;
        qreal s = it->selectionOpacity;
        if ((h > LERP_EPSILON && h < 1.0 - LERP_EPSILON) ||
            (s > LERP_EPSILON && s < 1.0 - LERP_EPSILON))
            return true;
    }

    // Check if any visible item is still loading
    ThumbnailModel* model = qobject_cast<ThumbnailModel*>(this->model());
    if (model) {
        QPair<int, int> visible = calculateVisibleRange();
        for (int page = visible.first; page <= visible.second; ++page) {
            if (model->isThumbnailLoading(page))
                return true;
        }
    }

    return false;
}

void ThumbnailListView::restartAnimationTimer() {
    if (m_animationEnabled && !m_delegateAnimationTimer->isActive())
        m_delegateAnimationTimer->start();
}

const ThumbnailListView::AnimationState* ThumbnailListView::animationState(
    int pageNumber) const {
    auto it = m_animationStates.find(pageNumber);
    return it != m_animationStates.end() ? &it.value() : nullptr;
}

void ThumbnailListView::updateVisibleRange() {
    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (!thumbnailModel)
        return;

    QPair<int, int> newRange = calculateVisibleRange();
    if (newRange.first < 0 || newRange.second < 0)
        return;

    if (m_visibleRange == newRange)
        return;

    m_visibleRange = newRange;
    emit visibleRangeChanged(newRange.first, newRange.second);

    // Dispatch thumbnail requests for the full target range (visible + preload)
    // in one pass, avoiding duplicate paths between visible and preload logic.
    updatePreloadRange();
}

void ThumbnailListView::paintEvent(QPaintEvent* event) {
    QListView::paintEvent(event);

    // 可以在这里添加额外的绘制逻辑
    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (thumbnailModel && thumbnailModel->rowCount() == 0) {
        QPainter painter(viewport());
        painter.setPen(Qt::gray);
        painter.drawText(viewport()->rect(), Qt::AlignCenter, "没有缩略图");
    }
}

void ThumbnailListView::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    stopIdlePreloadTimers();
    updateItemSizes();
    scheduleViewportUpdate();
}

void ThumbnailListView::showEvent(QShowEvent* event) {
    QListView::showEvent(event);
    optimizedUpdateVisibleRange();
    scheduleIdlePreload();
}

void ThumbnailListView::scrollContentsBy(int dx, int dy) {
    Q_UNUSED(dx)
    QListView::scrollContentsBy(0, dy);

    m_isScrolling = true;
    stopIdlePreloadTimers();

    // Update scroll velocity tracking
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    updateScrollVelocity(dy, now);

    // Notify progressive loader of scroll state
    if (m_progressiveLoader) {
        m_progressiveLoader->onScrollStateChanged(m_scrollVelocity,
                                                  m_scrollDirection);
    }

    // Use dynamic debounce based on velocity
    scheduleViewportUpdate();
}

void ThumbnailListView::updateScrollBarStyle() {
    // 使用和PDF显示界面相同的动态滚动条样式
    auto& styleManager = StyleManager::instance();
    QString scrollBarStyle = styleManager.getScrollBarStyleSheet();

    if (verticalScrollBar()) {
        verticalScrollBar()->setStyleSheet(scrollBarStyle);
    }
    if (horizontalScrollBar()) {
        horizontalScrollBar()->setStyleSheet(scrollBarStyle);
    }
}

void ThumbnailListView::updateItemSizes() {
    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (!thumbnailModel)
        return;

    // 根据当前缩略图大小更新项目大小
    QSize itemSize = m_thumbnailSize + QSize(20, 40);  // 添加边距
    setGridSize(itemSize);

    // 只触发布局更新，避免强制重绘所有项目
    scheduleDelayedItemsLayout();
}

QPair<int, int> ThumbnailListView::calculateVisibleRange() const {
    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (!thumbnailModel) {
        return qMakePair(-1, -1);
    }

    QRect viewportRect = viewport()->rect();
    int firstVisible = indexAt(viewportRect.topLeft()).row();
    int lastVisible = indexAt(viewportRect.bottomRight()).row();

    // Layout not ready yet — caller handles (-1, -1) with early return
    if (firstVisible < 0 || lastVisible < 0)
        return qMakePair(-1, -1);

    return qMakePair(firstVisible, lastVisible);
}

void ThumbnailListView::updateScrollVelocity(int delta, qint64 timestamp) {
    constexpr qint64 VELOCITY_WINDOW_MS = 100;
    qint64 elapsed = timestamp - m_lastScrollTime;

    if (elapsed > 0 && elapsed < VELOCITY_WINDOW_MS) {
        // Exponential moving average for smooth velocity
        double instantVelocity = qAbs(static_cast<double>(delta)) / elapsed;
        if (m_scrollVelocity == 0.0) {
            m_scrollVelocity = instantVelocity;
        } else {
            m_scrollVelocity = 0.3 * instantVelocity + 0.7 * m_scrollVelocity;
        }
    } else if (elapsed >= VELOCITY_WINDOW_MS) {
        // Reset if too much time passed (free-fall detection)
        m_scrollVelocity =
            qAbs(static_cast<double>(delta)) / qMax(elapsed, 1LL);
    }

    m_scrollDirection = (delta > 0) ? 1 : (delta < 0) ? -1 : 0;
    updatePreloadBiasDirection(m_scrollDirection, timestamp);
    m_lastScrollTime = timestamp;
    m_lastScrollPosition = verticalScrollBar()->value();
}

void ThumbnailListView::updatePreloadBiasDirection(int direction,
                                                   qint64 timestamp) {
    if (direction == 0) {
        m_candidatePreloadDirection = 0;
        m_candidatePreloadDirectionSince = 0;
        m_preloadBiasDirection = 0;
        return;
    }

    if (direction != m_candidatePreloadDirection) {
        m_candidatePreloadDirection = direction;
        m_candidatePreloadDirectionSince = timestamp;
        m_preloadBiasDirection = 0;
        return;
    }

    const qint64 lockDelay =
        direction > 0 ? PRELOAD_BIAS_DOWN_LOCK_MS : PRELOAD_BIAS_UP_LOCK_MS;
    if (timestamp - m_candidatePreloadDirectionSince >= lockDelay) {
        m_preloadBiasDirection = direction;
    }
}

int ThumbnailListView::predictLandingPage() const {
    if (m_scrollVelocity < VELOCITY_SLOW_THRESHOLD || m_scrollDirection == 0) {
        return -1;
    }

    // Predict landing position: current position + velocity * decay factor
    // Decay simulates friction - fast scrolls travel further proportionally
    double decayFactor =
        (m_scrollVelocity > VELOCITY_MEDIUM_THRESHOLD) ? 1.5 : 0.8;
    int predictedDelta = static_cast<int>(m_scrollVelocity * 300 * decayFactor);
    int predictedPosition =
        m_lastScrollPosition + m_scrollDirection * predictedDelta;

    // Clamp and convert to page number
    predictedPosition =
        qBound(verticalScrollBar()->minimum(), predictedPosition,
               verticalScrollBar()->maximum());

    QModelIndex predictedIndex = indexAt(
        viewport()->rect().adjusted(0, 0, 0, predictedPosition).bottomRight());
    return predictedIndex.row();
}

void ThumbnailListView::animateScrollTo(int position) {
    // 停止当前动画
    if (m_scrollAnimation->state() == QPropertyAnimation::Running) {
        m_scrollAnimation->stop();
    }

    // 设置目标位置并启动动画
    m_targetScrollPosition = qBound(verticalScrollBar()->minimum(), position,
                                    verticalScrollBar()->maximum());

    m_scrollAnimation->setStartValue(verticalScrollBar()->value());
    m_scrollAnimation->setEndValue(m_targetScrollPosition);
    m_scrollAnimation->start();
    m_isScrollAnimating = true;
}

void ThumbnailListView::animateScrollToPage(int pageNumber) {
    QModelIndex index = indexAtPage(pageNumber);
    if (index.isValid()) {
        scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
}

void ThumbnailListView::stopScrollAnimation() {
    if (m_scrollAnimation &&
        m_scrollAnimation->state() == QPropertyAnimation::Running) {
        m_scrollAnimation->stop();
        m_isScrollAnimating = false;
    }
}

QModelIndex ThumbnailListView::indexAtPage(int pageNumber) const {
    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (!thumbnailModel)
        return QModelIndex();
    return thumbnailModel->index(pageNumber, 0);
}

int ThumbnailListView::pageAtIndex(const QModelIndex& index) const {
    if (!index.isValid())
        return -1;
    return index.row();
}

void ThumbnailListView::updatePreloadRange() {
    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (!thumbnailModel || m_visibleRange.first < 0)
        return;

    int numPages = thumbnailModel->rowCount();
    int startPage = 0, endPage = 0;
    int preloadCount = PRELOAD_COUNT_SLOW;

    if (m_scrollVelocity < VELOCITY_SLOW_THRESHOLD) {
        preloadCount = PRELOAD_COUNT_SLOW;
    } else if (m_scrollVelocity < VELOCITY_MEDIUM_THRESHOLD) {
        preloadCount = PRELOAD_COUNT_MEDIUM;
    } else {
        // Fast scroll: cancel pending non-visible requests, jump to predicted
        // target
        thumbnailModel->cancelOutOfRangeRequests(m_visibleRange.first,
                                                 m_visibleRange.second);
        int predictedPage = predictLandingPage();
        if (predictedPage >= 0) {
            const PreloadSpan span = weightedPreloadSpan(
                PRELOAD_COUNT_FAST, m_scrollVelocity, VELOCITY_SLOW_THRESHOLD,
                VELOCITY_MEDIUM_THRESHOLD, m_preloadBiasDirection);
            startPage = qMax(0, predictedPage - span.before);
            endPage = qMin(numPages - 1, predictedPage + span.after);
            m_lastPreloadStart = startPage;
            m_lastPreloadEnd = endPage;
            requestThumbnailRange(startPage, endPage);
            scheduleIdlePreload();
            return;
        } else {
            preloadCount = PRELOAD_COUNT_FAST;
        }
    }

    const PreloadSpan span = weightedPreloadSpan(
        preloadCount, m_scrollVelocity, VELOCITY_SLOW_THRESHOLD,
        VELOCITY_MEDIUM_THRESHOLD, m_preloadBiasDirection);
    startPage = qMax(0, m_visibleRange.first - span.before);
    endPage = qMin(numPages - 1, m_visibleRange.second + span.after);
    m_lastPreloadStart = startPage;
    m_lastPreloadEnd = endPage;
    requestThumbnailRange(startPage, endPage);
    scheduleIdlePreload();
}

void ThumbnailListView::requestThumbnailRange(int startPage, int endPage) {
    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (!thumbnailModel || startPage > endPage)
        return;

    // Request pages not already cached or loading.
    // Visible range is included so updateVisibleRange does not need its own
    // request loop — all thumbnail dispatch converges here.
    for (int i = startPage; i <= endPage; ++i) {
        if (!thumbnailModel->hasCachedThumbnail(i) &&
            !thumbnailModel->isThumbnailLoading(i)) {
            thumbnailModel->requestThumbnail(i);
        }
    }
}

void ThumbnailListView::handlePageClick(int pageNumber) {
    emit pageClicked(pageNumber);
}

void ThumbnailListView::handlePageDoubleClick(int pageNumber) {
    emit pageDoubleClicked(pageNumber);
}

void ThumbnailListView::handlePageRightClick(int pageNumber,
                                             const QPoint& position) {
    emit pageRightClicked(pageNumber, position);
}

void ThumbnailListView::showContextMenu(const QPoint& position) {
    QModelIndex index = indexAt(position);
    if (index.isValid()) {
        int pageNumber = pageAtIndex(index);
        if (pageNumber >= 0) {
            handlePageRightClick(pageNumber, mapToGlobal(position));
        }
    }
}

void ThumbnailListView::copyPageToClipboard(int pageNumber) {
    if (!m_thumbnailModel || pageNumber < 0) {
        return;
    }

    // Get the thumbnail pixmap from the model
    QModelIndex index = m_thumbnailModel->index(pageNumber, 0);
    if (!index.isValid()) {
        return;
    }

    QPixmap pixmap = index.data(ThumbnailModel::PixmapRole).value<QPixmap>();
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "错误", "无法获取页面图像");
        return;
    }

    // Copy to clipboard
    QClipboard* clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setPixmap(pixmap);
        QMessageBox::information(
            this, "复制成功",
            QString("第 %1 页图像已复制到剪贴板").arg(pageNumber + 1));
    }
}

void ThumbnailListView::exportPageToFile(int pageNumber) {
    if (!m_thumbnailModel || pageNumber < 0) {
        return;
    }

    // Get default export path
    QString documentsPath =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString defaultFileName =
        QString("page_%1.png").arg(pageNumber + 1, 3, 10, QChar('0'));
    QString defaultPath = QDir(documentsPath).filePath(defaultFileName);

    QString filePath = QFileDialog::getSaveFileName(
        this, QString("导出第 %1 页").arg(pageNumber + 1), defaultPath,
        "PNG图像 (*.png);;JPEG图像 (*.jpg);;所有文件 (*.*)");

    if (filePath.isEmpty()) {
        return;
    }

    // Get the thumbnail pixmap from the model
    QModelIndex index = m_thumbnailModel->index(pageNumber, 0);
    if (!index.isValid()) {
        QMessageBox::warning(this, "错误", "无法获取页面数据");
        return;
    }

    QPixmap pixmap = index.data(ThumbnailModel::PixmapRole).value<QPixmap>();
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "错误", "无法获取页面图像");
        return;
    }

    // Determine file format
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    QString format =
        (extension == "jpg" || extension == "jpeg") ? "JPEG" : "PNG";

    // Save the image
    if (pixmap.save(filePath, format.toUtf8().constData())) {
        QMessageBox::information(this, "导出成功",
                                 QString("第 %1 页已成功导出到:\n%2")
                                     .arg(pageNumber + 1)
                                     .arg(filePath));
    } else {
        QMessageBox::critical(this, "错误", "保存文件失败");
    }
}

void ThumbnailListView::scheduleViewportUpdate() {
    if (!m_viewportUpdateTimer)
        return;

    // Dynamic debounce: fast scroll = longer delay to skip intermediate frames
    int delay;
    if (m_scrollVelocity < VELOCITY_SLOW_THRESHOLD) {
        delay = VIEWPORT_DEBOUNCE_SLOW;
    } else if (m_scrollVelocity < VELOCITY_MEDIUM_THRESHOLD) {
        delay = VIEWPORT_DEBOUNCE_MEDIUM;
    } else {
        delay = VIEWPORT_DEBOUNCE_FAST;
    }

    // Update interval on each call in case velocity changed
    m_viewportUpdateTimer->setInterval(delay);

    if (!m_viewportUpdatePending) {
        m_viewportUpdatePending = true;
        m_viewportUpdateTimer->start();
    } else {
        // Restart to re-arm the timer with the new delay
        m_viewportUpdateTimer->start();
    }
}

void ThumbnailListView::optimizedUpdateVisibleRange() {
    m_viewportUpdatePending = false;

    ThumbnailModel* thumbnailModel = qobject_cast<ThumbnailModel*>(model());
    if (!thumbnailModel)
        return;

    QPair<int, int> newRange = calculateVisibleRange();
    if (newRange.first < 0 || newRange.second < 0)
        return;

    // Only update if visible range changed significantly
    if (qAbs(newRange.first - m_lastVisibleStart) > 0 ||
        qAbs(newRange.second - m_lastVisibleEnd) > 0) {
        QPair<int, int> oldRange = m_visibleRange;
        m_visibleRange = newRange;
        m_lastVisibleStart = newRange.first;
        m_lastVisibleEnd = newRange.second;

        // Restore viewport range in model so lazy loading and priority
        // scheduling work correctly.
        thumbnailModel->setViewportRange(newRange.first, newRange.second,
                                         m_preloadMargin);

        // Notify progressive loader for two-stage rendering.
        // Loader handles low-res instantly via synchronous call, then
        // dwell timer triggers high-res via model->requestThumbnail.
        if (m_progressiveLoader) {
            m_progressiveLoader->onVisibleRangeChanged(newRange.first,
                                                       newRange.second);
        } else {
            // Fallback: direct thumbnail request (no progressive loader)
            for (int i = newRange.first; i <= newRange.second; ++i) {
                QModelIndex index = thumbnailModel->index(i, 0);
                if (index.isValid()) {
                    if (!thumbnailModel->hasCachedThumbnail(i) &&
                        !thumbnailModel->isThumbnailLoading(i)) {
                        thumbnailModel->requestThumbnail(i);
                    }
                }
            }
        }

        // Emit signal if range actually changed
        if (oldRange != m_visibleRange) {
            emit visibleRangeChanged(m_visibleRange.first,
                                     m_visibleRange.second);
        }

        // Trigger preload after visible range update
        if (m_autoPreload) {
            updatePreloadRange();
        }
    } else if (!m_isScrolling) {
        scheduleIdlePreload();
    }

    m_isScrolling = false;
}
