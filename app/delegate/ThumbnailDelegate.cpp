#include "ThumbnailDelegate.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QDebug>
#include <QPainter>
#include "managers/StyleManager.h"
#include "model/ThumbnailModel.h"
#include "ui/thumbnail/ThumbnailListView.h"

// Chrome-style color constants
const QColor ThumbnailDelegate::GOOGLE_BLUE = QColor(66, 133, 244);
const QColor ThumbnailDelegate::GOOGLE_RED = QColor(234, 67, 53);

ThumbnailDelegate::ThumbnailDelegate(QObject* parent)
    : QStyledItemDelegate(parent),
      m_thumbnailSize(DEFAULT_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_HEIGHT),
      m_margin(DEFAULT_MARGIN),
      m_borderRadius(0),
      m_pageNumberHeight(DEFAULT_PAGE_NUMBER_HEIGHT),
      m_shadowEnabled(false),
      m_animationEnabled(true),
      m_shadowOffset(DEFAULT_SHADOW_OFFSET),
      m_borderWidth(DEFAULT_BORDER_WIDTH) {
    setLightTheme();
    m_pageNumberFont = QFont("Arial", 9);
    m_errorFont = QFont("Arial", 8);
}

ThumbnailDelegate::~ThumbnailDelegate() = default;

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(m_thumbnailSize.width() + 2 * m_margin,
                 m_thumbnailSize.height() + m_pageNumberHeight + 2 * m_margin);
}

void ThumbnailDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    if (!index.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    int pageNumber = index.data(ThumbnailModel::PageNumberRole).toInt();
    QPixmap thumbnail = index.data(ThumbnailModel::PixmapRole).value<QPixmap>();
    bool isLoading = index.data(ThumbnailModel::LoadingRole).toBool();
    bool hasError = index.data(ThumbnailModel::ErrorRole).toBool();
    QString errorMessage =
        index.data(ThumbnailModel::ErrorMessageRole).toString();

    QRect thumbnailRect = getThumbnailRect(option.rect);
    QRect pageNumberRect = getPageNumberRect(thumbnailRect);
    QRect pageRect = (!thumbnail.isNull() && !isLoading && !hasError)
                         ? getThumbnailPixmapRect(thumbnailRect, thumbnail)
                         : thumbnailRect;

    paintBackground(painter, option.rect, option);

    if (m_shadowEnabled)
        paintShadow(painter, pageRect, option);

    paintBorder(painter, pageRect, pageNumber, option);

    if (hasError) {
        paintErrorIndicator(painter, thumbnailRect, errorMessage, option);
    } else if (isLoading) {
        paintLoadingIndicator(painter, thumbnailRect, option, pageNumber);
    } else if (!thumbnail.isNull()) {
        paintThumbnail(painter, thumbnailRect, thumbnail, option);
    }

    paintPageNumber(painter, pageNumberRect, pageNumber, option);

    painter->restore();
}

void ThumbnailDelegate::setThumbnailSize(const QSize& size) {
    if (m_thumbnailSize != size) {
        m_thumbnailSize = size;
        regenerateShadowPixmap();
        emit sizeHintChanged(QModelIndex());
    }
}

void ThumbnailDelegate::setMargins(int margin) {
    if (m_margin != margin) {
        m_margin = margin;
        emit sizeHintChanged(QModelIndex());
    }
}

void ThumbnailDelegate::setBorderRadius(int radius) { m_borderRadius = radius; }

void ThumbnailDelegate::setShadowEnabled(bool enabled) {
    m_shadowEnabled = enabled;
}

void ThumbnailDelegate::setAnimationEnabled(bool enabled) {
    m_animationEnabled = enabled;
}

void ThumbnailDelegate::setTheme(Theme theme) {
    if (theme == Theme::Dark)
        setDarkTheme();
    else
        setLightTheme();
}

void ThumbnailDelegate::setLightTheme() {
    m_backgroundColor = STYLE.surfaceColor();
    m_borderColorNormal = STYLE.borderColor();
    m_borderColorHovered = GOOGLE_BLUE.lighter(150);
    m_borderColorSelected = STYLE.accentColor();
    m_pageNumberBgColor = QColor(0, 0, 0, 0);
    m_pageNumberTextColor = STYLE.textSecondaryColor();
    m_loadingColor = STYLE.accentColor();
    m_errorColor = GOOGLE_RED;
    m_overlayColor = QColor(255, 255, 255, 200);
    regenerateShadowPixmap();
}

void ThumbnailDelegate::setDarkTheme() {
    m_backgroundColor = STYLE.surfaceColor();
    m_borderColorNormal = STYLE.borderColor();
    m_borderColorHovered = GOOGLE_BLUE.lighter(150);
    m_borderColorSelected = STYLE.accentColor();
    m_pageNumberBgColor = QColor(0, 0, 0, 0);
    m_pageNumberTextColor = STYLE.textSecondaryColor();
    m_loadingColor = STYLE.accentColor();
    m_errorColor = GOOGLE_RED;
    m_overlayColor = QColor(0, 0, 0, 200);
    regenerateShadowPixmap();
}

void ThumbnailDelegate::setCustomColors(const QColor& background,
                                        const QColor& border,
                                        const QColor& text,
                                        const QColor& accent) {
    m_backgroundColor = background;
    m_borderColorNormal = border;
    m_borderColorHovered = accent.lighter(150);
    m_borderColorSelected = accent;
    m_pageNumberTextColor = text;
    m_loadingColor = accent;
}

QRect ThumbnailDelegate::getThumbnailRect(const QRect& itemRect) const {
    int x = itemRect.x() + m_margin;
    int y = itemRect.y() + m_margin;
    return QRect(x, y, m_thumbnailSize.width(), m_thumbnailSize.height());
}

QRect ThumbnailDelegate::getThumbnailPixmapRect(const QRect& thumbnailRect,
                                                const QPixmap& pixmap) const {
    if (pixmap.isNull()) {
        return thumbnailRect;
    }

    int x = thumbnailRect.x() + (thumbnailRect.width() - pixmap.width()) / 2;
    int y = thumbnailRect.y() + (thumbnailRect.height() - pixmap.height()) / 2;
    return QRect(x, y, pixmap.width(), pixmap.height());
}

QRect ThumbnailDelegate::getPageNumberRect(const QRect& thumbnailRect) const {
    int x = thumbnailRect.x();
    int y = thumbnailRect.bottom() + 2;
    int width = thumbnailRect.width();
    return QRect(x, y, width, m_pageNumberHeight);
}

void ThumbnailDelegate::paintThumbnail(
    QPainter* painter, const QRect& rect, const QPixmap& pixmap,
    const QStyleOptionViewItem& option) const {
    Q_UNUSED(option)

    if (pixmap.isNull()) {
        painter->setPen(QColor(120, 120, 120));
        QFont font = painter->font();
        font.setPixelSize(24);
        painter->setFont(font);
        painter->drawText(rect, Qt::AlignCenter,
                          QStringLiteral("\xF0\x9F\x93\x84"));
        return;
    }

    // Pixmap is already pre-scaled by ThumbnailModel — center and draw
    int x = rect.x() + (rect.width() - pixmap.width()) / 2;
    int y = rect.y() + (rect.height() - pixmap.height()) / 2;
    painter->drawPixmap(x, y, pixmap);
}

void ThumbnailDelegate::paintBackground(
    QPainter* painter, const QRect& rect,
    const QStyleOptionViewItem& option) const {
    Q_UNUSED(option)
    painter->fillRect(rect, m_backgroundColor);
}

void ThumbnailDelegate::paintBorder(QPainter* painter, const QRect& rect,
                                    int pageNumber,
                                    const QStyleOptionViewItem& option) const {
    QColor borderColor = m_borderColorNormal;

    if (option.state & QStyle::State_Selected) {
        borderColor = m_borderColorSelected;
    } else if (option.state & QStyle::State_MouseOver) {
        borderColor = m_borderColorHovered;
    }

    // Apply hover opacity lerp from view's pre-computed state
    if (m_animationEnabled) {
        if (auto* view = qobject_cast<const ThumbnailListView*>(parent())) {
            if (const auto* state = view->animationState(pageNumber)) {
                qreal hoverBlend = state->hoverOpacity;
                borderColor = QColor::fromRgbF(
                    m_borderColorNormal.redF() * (1.0 - hoverBlend) +
                        m_borderColorHovered.redF() * hoverBlend,
                    m_borderColorNormal.greenF() * (1.0 - hoverBlend) +
                        m_borderColorHovered.greenF() * hoverBlend,
                    m_borderColorNormal.blueF() * (1.0 - hoverBlend) +
                        m_borderColorHovered.blueF() * hoverBlend);
            }
        }
    }

    QPen borderPen(borderColor, m_borderWidth);
    painter->setPen(borderPen);
    painter->setBrush(Qt::NoBrush);

    if (m_borderRadius > 0)
        painter->drawRoundedRect(rect, m_borderRadius, m_borderRadius);
    else
        painter->drawRect(rect);
}

QPixmap ThumbnailDelegate::cachedShadowPixmap() const { return m_cachedShadow; }

void ThumbnailDelegate::regenerateShadowPixmap() {
    QSize shadowSize =
        m_thumbnailSize + QSize(2 * m_shadowOffset, 2 * m_shadowOffset);

    int r = SHADOW_BLUR_RADIUS;
    int w = shadowSize.width();
    int h = shadowSize.height();

    QImage img(w + 2 * r, h + 2 * r, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    {
        QPainter p(&img);
        QRect inner(r, r, w, h);
        p.setRenderHint(QPainter::Antialiasing);

        // Multi-pass alpha stack to approximate a soft shadow
        for (int i = r; i > 0; --i) {
            qreal alpha = SHADOW_OPACITY * (1.0 - qreal(i) / (r + 1));
            QColor c = STYLE.borderColor();
            c.setAlpha(static_cast<int>(alpha * 255));
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            QRect layer = inner.adjusted(-i, -i, i, i);
            p.drawRoundedRect(layer, 4, 4);
        }
    }

    m_cachedShadow = QPixmap::fromImage(img.copy(r, r, w, h));
}

void ThumbnailDelegate::paintShadow(QPainter* painter, const QRect& rect,
                                    const QStyleOptionViewItem& option) const {
    Q_UNUSED(option)

    QPixmap shadow = cachedShadowPixmap();
    if (shadow.isNull())
        return;

    painter->drawPixmap(rect.adjusted(-m_shadowOffset, -m_shadowOffset,
                                      m_shadowOffset, m_shadowOffset),
                        shadow);
}

void ThumbnailDelegate::paintPageNumber(
    QPainter* painter, const QRect& rect, int pageNumber,
    const QStyleOptionViewItem& option) const {
    Q_UNUSED(option)

    if (rect.height() <= 0)
        return;

    painter->fillRect(rect, m_pageNumberBgColor);
    painter->setPen(m_pageNumberTextColor);
    painter->setFont(m_pageNumberFont);
    QString pageText = QString::number(pageNumber + 1);
    painter->drawText(rect, Qt::AlignCenter, pageText);
}

void ThumbnailDelegate::paintLoadingIndicator(
    QPainter* painter, const QRect& rect, const QStyleOptionViewItem& option,
    int pageNumber) const {
    Q_UNUSED(option)

    painter->fillRect(rect, m_overlayColor);

    // Read pre-computed spinner angle from view's animation state
    qreal angle = 0.0;
    if (auto* view = qobject_cast<const ThumbnailListView*>(parent())) {
        if (const auto* state = view->animationState(pageNumber))
            angle = state->spinnerAngle;
    }

    QRect spinnerRect(rect.center().x() - LOADING_SPINNER_SIZE / 2,
                      rect.center().y() - LOADING_SPINNER_SIZE / 2,
                      LOADING_SPINNER_SIZE, LOADING_SPINNER_SIZE);

    painter->save();
    painter->translate(spinnerRect.center());
    painter->rotate(static_cast<qreal>(angle));
    painter->setPen(QPen(m_loadingColor, 3, Qt::SolidLine, Qt::RoundCap));
    painter->drawArc(-LOADING_SPINNER_SIZE / 2, -LOADING_SPINNER_SIZE / 2,
                     LOADING_SPINNER_SIZE, LOADING_SPINNER_SIZE, 0, 270 * 16);
    painter->restore();
}

void ThumbnailDelegate::paintErrorIndicator(
    QPainter* painter, const QRect& rect, const QString& errorMessage,
    const QStyleOptionViewItem& option) const {
    Q_UNUSED(option)

    painter->fillRect(rect, m_overlayColor);
    painter->setPen(QPen(m_errorColor, 2));
    painter->setBrush(Qt::NoBrush);

    // Icon size proportional to rect, capped at 24px minimum
    int iconSize = qMax(24, qMin(rect.width(), rect.height()) / 3);
    int half = iconSize / 2;
    int cx = rect.center().x();
    int cy = rect.center().y();
    QRect iconRect(cx - half, cy - half, iconSize, iconSize);
    painter->drawEllipse(iconRect);

    // Exclamation mark — relative to iconSize
    painter->setPen(QPen(m_errorColor, 3, Qt::SolidLine, Qt::RoundCap));
    int lineTop = iconRect.top() + iconSize / 4;
    int lineMid = iconRect.center().y() + iconSize / 10;
    painter->drawLine(cx, lineTop, cx, lineMid);
    painter->drawPoint(cx, iconRect.bottom() - iconSize / 7);

    if (!errorMessage.isEmpty() && rect.height() > 60) {
        painter->setPen(m_errorColor);
        painter->setFont(m_errorFont);
        int margin = qMax(4, rect.width() / 20);
        QRect textRect =
            rect.adjusted(margin, iconRect.bottom() + margin, -margin, -margin);
        painter->drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap,
                          errorMessage);
    }
}
