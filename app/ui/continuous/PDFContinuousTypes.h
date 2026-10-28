#pragma once

#include <QMarginsF>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <Qt>

#include <cstdint>

struct PDFPageGeometry {
    int pageIndex = -1;
    QSizeF originalSize;
    QRectF pageRect;
    QRectF contentRect;
};

enum class PDFLayoutType {
    Single,  // Single column (default continuous)
    Spread,  // Two-page spreads, first page optionally solo
    Grid,    // Multi-column grid
};

enum class PDFVerticalAlignment {
    AlignTop,
    AlignCenter,
    AlignBottom,
};

struct PDFContinuousLayoutOptions {
    double zoom = 1.0;
    int rotation = 0;
    qreal pageSpacing = 16.0;
    QMarginsF documentMargins = QMarginsF(12.0, 12.0, 12.0, 12.0);
    qreal viewportWidth = 0.0;
    Qt::Alignment horizontalAlignment = Qt::AlignHCenter;
    PDFLayoutType layoutType = PDFLayoutType::Single;
    bool firstPageAlone = false;  // Spread: first page spans full row
    PDFVerticalAlignment verticalAlignment = PDFVerticalAlignment::AlignTop;
    int gridColumns = 2;         // Grid: column count
    qreal columnSpacing = 16.0;  // Grid/Spread: inter-column gap
};

enum class PageScrollAlignment {
    Top,
    Center,
    KeepVisible,
};

struct PDFViewportAnchor {
    int pageIndex = -1;
    QPointF pagePoint;
    QPointF viewportPoint;
};

struct PDFPageHit {
    int pageIndex = -1;
    bool insidePage = false;
    QPointF documentPoint;
    QPointF pagePoint;
};

struct PDFContinuousRevision {
    std::uint64_t value = 0;
};
