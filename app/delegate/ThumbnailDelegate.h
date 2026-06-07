#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

#include "common/Theme.h"

class ThumbnailDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ThumbnailDelegate(QObject* parent = nullptr);
    ~ThumbnailDelegate() override;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    void setThumbnailSize(const QSize& size);
    QSize thumbnailSize() const { return m_thumbnailSize; }

    void setMargins(int margin);
    int margins() const { return m_margin; }

    void setBorderRadius(int radius);
    int borderRadius() const { return m_borderRadius; }

    void setShadowEnabled(bool enabled);
    bool shadowEnabled() const { return m_shadowEnabled; }

    void setAnimationEnabled(bool enabled);
    bool animationEnabled() const { return m_animationEnabled; }

    // Theme
    void setTheme(Theme theme);
    void setCustomColors(const QColor& background, const QColor& border,
                         const QColor& text, const QColor& accent);

private:
    void paintThumbnail(QPainter* painter, const QRect& rect,
                        const QPixmap& pixmap,
                        const QStyleOptionViewItem& option) const;
    void paintBackground(QPainter* painter, const QRect& rect,
                         const QStyleOptionViewItem& option) const;
    void paintBorder(QPainter* painter, const QRect& rect, int pageNumber,
                     const QStyleOptionViewItem& option) const;
    void paintShadow(QPainter* painter, const QRect& rect,
                     const QStyleOptionViewItem& option) const;
    void paintPageNumber(QPainter* painter, const QRect& rect, int pageNumber,
                         const QStyleOptionViewItem& option) const;
    void paintLoadingIndicator(QPainter* painter, const QRect& rect,
                               const QStyleOptionViewItem& option,
                               int pageNumber) const;
    void paintErrorIndicator(QPainter* painter, const QRect& rect,
                             const QString& errorMessage,
                             const QStyleOptionViewItem& option) const;

    QRect getThumbnailRect(const QRect& itemRect) const;
    QRect getPageNumberRect(const QRect& thumbnailRect) const;

    void setLightTheme();
    void setDarkTheme();

    void regenerateShadowPixmap();
    QPixmap cachedShadowPixmap() const;

private:
    // Sizing
    QSize m_thumbnailSize;
    int m_margin;
    int m_borderRadius;
    int m_pageNumberHeight;

    // Visual flags
    bool m_shadowEnabled;
    bool m_animationEnabled;
    int m_shadowOffset;
    int m_borderWidth;

    // Colors
    QColor m_backgroundColor;
    QColor m_borderColorNormal;
    QColor m_borderColorHovered;
    QColor m_borderColorSelected;
    QColor m_pageNumberBgColor;
    QColor m_pageNumberTextColor;
    QColor m_loadingColor;
    QColor m_errorColor;
    QColor m_overlayColor;

    // Fonts
    QFont m_pageNumberFont;
    QFont m_errorFont;

    // Constants
    static constexpr int DEFAULT_THUMBNAIL_WIDTH = 120;
    static constexpr int DEFAULT_THUMBNAIL_HEIGHT = 160;
    static constexpr int DEFAULT_MARGIN = 8;
    static constexpr int DEFAULT_BORDER_RADIUS = 8;
    static constexpr int DEFAULT_PAGE_NUMBER_HEIGHT = 24;
    static constexpr int DEFAULT_SHADOW_OFFSET = 2;
    static constexpr int DEFAULT_BORDER_WIDTH = 2;
    static constexpr int LOADING_SPINNER_SIZE = 24;

    // Shadow cache — regenerated eagerly when thumbnail size changes
    QPixmap m_cachedShadow;
    static constexpr int SHADOW_BLUR_RADIUS = 8;
    static constexpr qreal SHADOW_OPACITY = 0.35;

    // Chrome-style color constants
    static const QColor GOOGLE_BLUE;
    static const QColor GOOGLE_RED;
    static const QColor LIGHT_BACKGROUND;
    static const QColor LIGHT_BORDER;
    static const QColor LIGHT_TEXT;
    static const QColor DARK_BACKGROUND;
    static const QColor DARK_BORDER;
    static const QColor DARK_TEXT;
};
