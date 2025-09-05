#include "ThumbnailDelegate.h"
#include "ThumbnailModel.h"
#include "../../managers/StyleManager.h"
#include <QApplication>
#include <QPainterPath>
#include <QFontMetrics>
#include <QDebug>
#include <QEvent>
#include <QMouseEvent>
#include <QAbstractItemView>
#include <QEasingCurve>
#include <cmath>

// Chrome风格颜色常量
const QColor ThumbnailDelegate::GOOGLE_BLUE = QColor(66, 133, 244);
const QColor ThumbnailDelegate::GOOGLE_BLUE_DARK = QColor(26, 115, 232);
const QColor ThumbnailDelegate::GOOGLE_RED = QColor(234, 67, 53);
const QColor ThumbnailDelegate::LIGHT_BACKGROUND = QColor(255, 255, 255);
const QColor ThumbnailDelegate::LIGHT_BORDER = QColor(200, 200, 200);
const QColor ThumbnailDelegate::LIGHT_TEXT = QColor(60, 60, 60);
const QColor ThumbnailDelegate::DARK_BACKGROUND = QColor(45, 45, 45);
const QColor ThumbnailDelegate::DARK_BORDER = QColor(100, 100, 100);
const QColor ThumbnailDelegate::DARK_TEXT = QColor(220, 220, 220);

ThumbnailDelegate::ThumbnailDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
    , m_thumbnailSize(DEFAULT_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_HEIGHT)
    , m_margin(DEFAULT_MARGIN)
    , m_borderRadius(DEFAULT_BORDER_RADIUS)
    , m_pageNumberHeight(DEFAULT_PAGE_NUMBER_HEIGHT)
    , m_shadowEnabled(true)
    , m_animationEnabled(true)
    , m_shadowBlurRadius(DEFAULT_SHADOW_BLUR_RADIUS)
    , m_shadowOffset(DEFAULT_SHADOW_OFFSET)
    , m_borderWidth(DEFAULT_BORDER_WIDTH)
{
    // 根据当前StyleManager主题设置颜色方案
    if (STYLE.currentTheme() == Theme::Dark) {
        setDarkTheme();
    } else {
        setLightTheme();
    }

    // 连接主题变更信号
    connect(&STYLE, &StyleManager::themeChanged, this, [this](Theme theme) {
        if (theme == Theme::Dark) {
            setDarkTheme();
        } else {
            setLightTheme();
        }
        // 通知视图更新
        if (this->parent()) {
            if (auto* view = qobject_cast<QAbstractItemView*>(this->parent())) {
                view->update();
            }
        }
    });

    // 设置字体
    m_pageNumberFont = QApplication::font();
    m_pageNumberFont.setPixelSize(11);
    m_pageNumberFont.setBold(true);

    m_errorFont = QApplication::font();
    m_errorFont.setPixelSize(10);

    // 创建加载动画定时器
    m_loadingTimer = new QTimer(this);
    m_loadingTimer->setInterval(LOADING_ANIMATION_INTERVAL);
    connect(m_loadingTimer, &QTimer::timeout, this, &ThumbnailDelegate::onLoadingAnimationTimer);
    m_loadingTimer->start();
}

ThumbnailDelegate::~ThumbnailDelegate()
{
    cleanupAnimations();
}

void ThumbnailDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, 
                             const QModelIndex& index) const
{
    if (!index.isValid()) {
        return;
    }
    
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    
    // 获取数据
    QPixmap pixmap = index.data(ThumbnailModel::PixmapRole).value<QPixmap>();
    bool isLoading = index.data(ThumbnailModel::LoadingRole).toBool();
    bool hasError = index.data(ThumbnailModel::ErrorRole).toBool();
    QString errorMessage = index.data(ThumbnailModel::ErrorMessageRole).toString();
    int pageNumber = index.data(ThumbnailModel::PageNumberRole).toInt();
    
    // 计算绘制区域
    QRect thumbnailRect = getThumbnailRect(option.rect);
    QRect pageNumberRect = getPageNumberRect(thumbnailRect);
    
    // 绘制阴影
    if (m_shadowEnabled) {
        paintShadow(painter, thumbnailRect, option);
    }
    
    // 绘制背景
    paintBackground(painter, thumbnailRect, option);
    
    // 绘制缩略图内容
    if (hasError) {
        paintErrorIndicator(painter, thumbnailRect, errorMessage, option);
    } else if (isLoading) {
        paintLoadingIndicator(painter, thumbnailRect, option);
    } else if (!pixmap.isNull()) {
        paintThumbnail(painter, thumbnailRect, pixmap, option);
    } else {
        // 绘制占位符
        painter->fillRect(thumbnailRect, m_placeholderColor);
        painter->setPen(m_borderColorNormal);
        QFont font = painter->font();
        font.setPixelSize(24);
        painter->setFont(font);
        painter->drawText(thumbnailRect, Qt::AlignCenter, "📄");
    }
    
    // 绘制边框
    paintBorder(painter, thumbnailRect, option);
    
    // 绘制页码
    paintPageNumber(painter, pageNumberRect, pageNumber, option);
    
    painter->restore();
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, 
                                 const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    
    return QSize(m_thumbnailSize.width() + 2 * m_margin,
                 m_thumbnailSize.height() + m_pageNumberHeight + 2 * m_margin);
}

void ThumbnailDelegate::setThumbnailSize(const QSize& size)
{
    if (m_thumbnailSize != size) {
        m_thumbnailSize = size;
        // 通知视图更新
        if (parent()) {
            if (auto* view = qobject_cast<QAbstractItemView*>(parent())) {
                // scheduleDelayedItemsLayout是受保护的，使用update替代
                view->update();
            }
        }
    }
}

void ThumbnailDelegate::setMargins(int margin)
{
    if (m_margin != margin) {
        m_margin = qMax(0, margin);
        if (parent()) {
            if (auto* view = qobject_cast<QAbstractItemView*>(parent())) {
                // scheduleDelayedItemsLayout是受保护的，使用update替代
                view->update();
            }
        }
    }
}

void ThumbnailDelegate::setBorderRadius(int radius)
{
    m_borderRadius = qMax(0, radius);
}

void ThumbnailDelegate::setShadowEnabled(bool enabled)
{
    m_shadowEnabled = enabled;
}

void ThumbnailDelegate::setAnimationEnabled(bool enabled)
{
    m_animationEnabled = enabled;
    if (!enabled) {
        cleanupAnimations();
    }
}

void ThumbnailDelegate::setLightTheme()
{
    m_backgroundColor = LIGHT_BACKGROUND;
    m_borderColorNormal = LIGHT_BORDER;
    m_borderColorHovered = GOOGLE_BLUE;
    m_borderColorSelected = GOOGLE_BLUE_DARK;
    m_shadowColor = QColor(0, 0, 0, 40);
    m_pageNumberBgColor = QColor(0, 0, 0, 180);
    m_pageNumberTextColor = QColor(255, 255, 255);
    m_loadingColor = GOOGLE_BLUE;
    m_errorColor = GOOGLE_RED;
    m_placeholderColor = QColor(245, 245, 245);
}

void ThumbnailDelegate::setDarkTheme()
{
    m_backgroundColor = DARK_BACKGROUND;
    m_borderColorNormal = DARK_BORDER;
    m_borderColorHovered = GOOGLE_BLUE;
    m_borderColorSelected = GOOGLE_BLUE_DARK;
    m_shadowColor = QColor(0, 0, 0, 80);
    m_pageNumberBgColor = QColor(0, 0, 0, 200);
    m_pageNumberTextColor = QColor(255, 255, 255);
    m_loadingColor = GOOGLE_BLUE;
    m_errorColor = GOOGLE_RED;
    m_placeholderColor = QColor(60, 60, 60);
}

void ThumbnailDelegate::setCustomColors(const QColor& background, const QColor& border, 
                                       const QColor& text, const QColor& accent)
{
    m_backgroundColor = background;
    m_borderColorNormal = border;
    m_borderColorHovered = accent;
    m_borderColorSelected = accent.darker(120);
    m_pageNumberTextColor = text;
    m_loadingColor = accent;
}

QRect ThumbnailDelegate::getThumbnailRect(const QRect& itemRect) const
{
    return QRect(itemRect.x() + m_margin, 
                 itemRect.y() + m_margin,
                 m_thumbnailSize.width(), 
                 m_thumbnailSize.height());
}

QRect ThumbnailDelegate::getPageNumberRect(const QRect& thumbnailRect) const
{
    return QRect(thumbnailRect.left(), 
                 thumbnailRect.bottom() + 4,
                 thumbnailRect.width(), 
                 m_pageNumberHeight - 4);
}

void ThumbnailDelegate::paintBackground(QPainter* painter, const QRect& rect, 
                                       const QStyleOptionViewItem& option) const
{
    Q_UNUSED(option)
    
    QPainterPath path;
    path.addRoundedRect(rect, m_borderRadius, m_borderRadius);
    painter->fillPath(path, m_backgroundColor);
}

void ThumbnailDelegate::paintThumbnail(QPainter* painter, const QRect& rect, 
                                      const QPixmap& pixmap, const QStyleOptionViewItem& option) const
{
    Q_UNUSED(option)
    
    // 创建圆角剪切路径
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, m_borderRadius, m_borderRadius);
    painter->setClipPath(clipPath);
    
    // 缩放并居中绘制缩略图
    QPixmap scaledPixmap = pixmap.scaled(rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    QRect targetRect = rect;
    if (scaledPixmap.size() != rect.size()) {
        int x = rect.x() + (rect.width() - scaledPixmap.width()) / 2;
        int y = rect.y() + (rect.height() - scaledPixmap.height()) / 2;
        targetRect = QRect(x, y, scaledPixmap.width(), scaledPixmap.height());
    }
    
    painter->drawPixmap(targetRect, scaledPixmap);
    painter->setClipping(false);
}

void ThumbnailDelegate::paintBorder(QPainter* painter, const QRect& rect,
                                   const QStyleOptionViewItem& option) const
{
    AnimationState* state = getAnimationState(option.index);

    QColor borderColor = m_borderColorNormal;
    qreal opacity = 0.0;

    if (option.state & QStyle::State_Selected) {
        borderColor = m_borderColorSelected;
        opacity = state ? state->selectionOpacity : 1.0;
    } else if (option.state & QStyle::State_MouseOver) {
        borderColor = m_borderColorHovered;
        opacity = state ? state->hoverOpacity : 1.0;
    }

    if (opacity > 0.001) {
        borderColor.setAlphaF(opacity);
        painter->setPen(QPen(borderColor, m_borderWidth));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(rect.adjusted(m_borderWidth/2, m_borderWidth/2,
                                              -m_borderWidth/2, -m_borderWidth/2),
                                m_borderRadius, m_borderRadius);
    }
}

void ThumbnailDelegate::paintShadow(QPainter* painter, const QRect& rect,
                                   const QStyleOptionViewItem& option) const
{
    Q_UNUSED(option)

    // 简化的阴影绘制
    QRect shadowRect = rect.adjusted(-m_shadowOffset, -m_shadowOffset,
                                    m_shadowOffset, m_shadowOffset);

    QPainterPath shadowPath;
    shadowPath.addRoundedRect(shadowRect, m_borderRadius + 2, m_borderRadius + 2);

    painter->fillPath(shadowPath, m_shadowColor);
}

void ThumbnailDelegate::paintPageNumber(QPainter* painter, const QRect& rect,
                                       int pageNumber, const QStyleOptionViewItem& option) const
{
    Q_UNUSED(option)

    if (rect.height() <= 0) return;

    // 绘制页码背景
    QPainterPath bgPath;
    bgPath.addRoundedRect(rect, 4, 4);
    painter->fillPath(bgPath, m_pageNumberBgColor);

    // 绘制页码文字
    painter->setPen(m_pageNumberTextColor);
    painter->setFont(m_pageNumberFont);

    QString pageText = QString::number(pageNumber + 1); // 页码从1开始显示
    painter->drawText(rect, Qt::AlignCenter, pageText);
}

void ThumbnailDelegate::paintLoadingIndicator(QPainter* painter, const QRect& rect,
                                             const QStyleOptionViewItem& option) const
{
    Q_UNUSED(option)

    // 绘制半透明遮罩
    painter->fillRect(rect, QColor(255, 255, 255, 200));

    // 获取动画状态
    AnimationState* state = getAnimationState(option.index);
    int angle = state ? state->loadingAngle : 0;

    // 绘制旋转的加载指示器
    QRect spinnerRect(rect.center().x() - LOADING_SPINNER_SIZE/2,
                      rect.center().y() - LOADING_SPINNER_SIZE/2,
                      LOADING_SPINNER_SIZE, LOADING_SPINNER_SIZE);

    painter->save();
    painter->translate(spinnerRect.center());
    painter->rotate(angle);

    painter->setPen(QPen(m_loadingColor, 3, Qt::SolidLine, Qt::RoundCap));
    painter->drawArc(-LOADING_SPINNER_SIZE/2, -LOADING_SPINNER_SIZE/2,
                    LOADING_SPINNER_SIZE, LOADING_SPINNER_SIZE,
                    0, 270 * 16); // 3/4 圆弧

    painter->restore();
}

void ThumbnailDelegate::paintErrorIndicator(QPainter* painter, const QRect& rect,
                                           const QString& errorMessage, const QStyleOptionViewItem& option) const
{
    Q_UNUSED(option)

    // 绘制半透明遮罩
    painter->fillRect(rect, QColor(255, 255, 255, 200));

    // 绘制错误图标
    painter->setPen(QPen(m_errorColor, 2));
    painter->setBrush(Qt::NoBrush);

    QRect iconRect(rect.center().x() - 12, rect.center().y() - 12, 24, 24);
    painter->drawEllipse(iconRect);

    // 绘制感叹号
    painter->setPen(QPen(m_errorColor, 3, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(iconRect.center().x(), iconRect.top() + 6,
                     iconRect.center().x(), iconRect.center().y() + 2);
    painter->drawPoint(iconRect.center().x(), iconRect.bottom() - 4);

    // 绘制错误消息（如果有空间）
    if (!errorMessage.isEmpty() && rect.height() > 60) {
        painter->setPen(m_errorColor);
        painter->setFont(m_errorFont);
        QRect textRect = rect.adjusted(4, iconRect.bottom() + 4, -4, -4);
        painter->drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, errorMessage);
    }
}

ThumbnailDelegate::AnimationState* ThumbnailDelegate::getAnimationState(const QModelIndex& index) const
{
    if (!m_animationEnabled || !index.isValid()) {
        return nullptr;
    }

    QPersistentModelIndex persistentIndex(index);
    auto it = m_animationStates.find(persistentIndex);

    if (it == m_animationStates.end()) {
        AnimationState* state = new AnimationState();
        setupAnimations(state, index);
        m_animationStates[persistentIndex] = state;
        return state;
    }

    return it.value();
}

void ThumbnailDelegate::setupAnimations(AnimationState* state, const QModelIndex& index) const
{
    Q_UNUSED(index)

    // 悬停动画
    state->hoverAnimation = new QPropertyAnimation();
    state->hoverAnimation->setTargetObject(const_cast<ThumbnailDelegate*>(this));
    state->hoverAnimation->setPropertyName("animationValue");
    state->hoverAnimation->setDuration(HOVER_ANIMATION_DURATION);
    state->hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(state->hoverAnimation, &QPropertyAnimation::valueChanged,
            this, &ThumbnailDelegate::onAnimationValueChanged);

    // 选中动画
    state->selectionAnimation = new QPropertyAnimation();
    state->selectionAnimation->setTargetObject(const_cast<ThumbnailDelegate*>(this));
    state->selectionAnimation->setPropertyName("animationValue");
    state->selectionAnimation->setDuration(SELECTION_ANIMATION_DURATION);
    state->selectionAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(state->selectionAnimation, &QPropertyAnimation::valueChanged,
            this, &ThumbnailDelegate::onAnimationValueChanged);
}

void ThumbnailDelegate::cleanupAnimations()
{
    for (auto it = m_animationStates.begin(); it != m_animationStates.end(); ++it) {
        delete it.value();
    }
    m_animationStates.clear();
}

void ThumbnailDelegate::onAnimationValueChanged()
{
    // 触发重绘
    if (parent()) {
        if (auto* view = qobject_cast<QAbstractItemView*>(parent())) {
            view->viewport()->update();
        }
    }
}

void ThumbnailDelegate::onLoadingAnimationTimer()
{
    // 更新所有加载动画状态
    for (auto it = m_animationStates.begin(); it != m_animationStates.end(); ++it) {
        AnimationState* state = it.value();
        state->loadingAngle = (state->loadingAngle + 15) % 360;
    }

    // 触发重绘
    if (parent()) {
        if (auto* view = qobject_cast<QAbstractItemView*>(parent())) {
            view->viewport()->update();
        }
    }
}

bool ThumbnailDelegate::eventFilter(QObject* object, QEvent* event)
{
    // 处理鼠标事件以触发动画
    if (auto* view = qobject_cast<QAbstractItemView*>(object)) {
        if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QModelIndex index = view->indexAt(mouseEvent->pos());

            if (index.isValid()) {
                updateHoverState(index, true);
            }
        }
    }

    return QStyledItemDelegate::eventFilter(object, event);
}

void ThumbnailDelegate::updateHoverState(const QModelIndex& index, bool hovered)
{
    if (!m_animationEnabled) return;

    AnimationState* state = getAnimationState(index);
    if (!state || !state->hoverAnimation) return;

    qreal targetOpacity = hovered ? 1.0 : 0.0;

    if (qAbs(state->hoverOpacity - targetOpacity) > 0.001) {
        state->hoverAnimation->setStartValue(state->hoverOpacity);
        state->hoverAnimation->setEndValue(targetOpacity);
        state->hoverAnimation->start();
    }
}

void ThumbnailDelegate::updateSelectionState(const QModelIndex& index, bool selected)
{
    if (!m_animationEnabled) return;

    AnimationState* state = getAnimationState(index);
    if (!state || !state->selectionAnimation) return;

    qreal targetOpacity = selected ? 1.0 : 0.0;

    if (qAbs(state->selectionOpacity - targetOpacity) > 0.001) {
        state->selectionAnimation->setStartValue(state->selectionOpacity);
        state->selectionAnimation->setEndValue(targetOpacity);
        state->selectionAnimation->start();
    }
}
