#include "PDFContinuousCoordinateMapper.h"

#include <algorithm>

void PDFContinuousCoordinateMapper::setBlueprint(
    std::shared_ptr<const PDFContinuousBlueprint> blueprint) {
    m_blueprint = std::move(blueprint);
}

void PDFContinuousCoordinateMapper::setContentOffset(const QPointF& offset) {
    m_contentOffset = offset;
}

QPointF PDFContinuousCoordinateMapper::viewportToDocument(
    const QPointF& point) const {
    return m_blueprint ? m_blueprint->viewportToDocument(point, m_contentOffset)
                       : QPointF();
}

QPointF PDFContinuousCoordinateMapper::documentToViewport(
    const QPointF& point) const {
    return m_blueprint ? m_blueprint->documentToViewport(point, m_contentOffset)
                       : QPointF();
}

std::optional<PDFPageHit> PDFContinuousCoordinateMapper::hitTestViewport(
    const QPointF& point) const {
    return m_blueprint ? m_blueprint->hitTest(viewportToDocument(point))
                       : std::nullopt;
}

const PDFPageGeometry* PDFContinuousCoordinateMapper::geometryForPage(
    int pageIndex) const {
    return m_blueprint ? m_blueprint->pageGeometry(pageIndex) : nullptr;
}

QPointF PDFContinuousCoordinateMapper::pageToOriginalTopLeft(
    int pageIndex, const QPointF& point) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry || geometry->originalSize.isEmpty() || !m_blueprint) {
        return QPointF();
    }
    const auto options = m_blueprint->layoutOptions();
    const QPointF unscaled = point / options.zoom;
    return rotateToOriginal(unscaled, geometry->originalSize,
                            normalizeRotation(options.rotation));
}

QPointF PDFContinuousCoordinateMapper::originalTopLeftToPage(
    int pageIndex, const QPointF& point) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry || geometry->originalSize.isEmpty() || !m_blueprint) {
        return QPointF();
    }
    const auto options = m_blueprint->layoutOptions();
    return rotateFromOriginal(point, geometry->originalSize,
                              normalizeRotation(options.rotation)) *
           options.zoom;
}

QPointF PDFContinuousCoordinateMapper::pageToPdf(int pageIndex,
                                                 const QPointF& point) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry) {
        return QPointF();
    }
    const QPointF original = pageToOriginalTopLeft(pageIndex, point);
    return {original.x(), geometry->originalSize.height() - original.y()};
}

QPointF PDFContinuousCoordinateMapper::pdfToPage(int pageIndex,
                                                 const QPointF& point) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry) {
        return QPointF();
    }
    return originalTopLeftToPage(
        pageIndex,
        QPointF(point.x(), geometry->originalSize.height() - point.y()));
}

QRectF PDFContinuousCoordinateMapper::pageRectToPdf(int pageIndex,
                                                    const QRectF& rect) const {
    return mapRect(rect, [this, pageIndex](const QPointF& point) {
        return pageToPdf(pageIndex, point);
    });
}

QRectF PDFContinuousCoordinateMapper::pdfRectToNormalizedOriginal(
    int pageIndex, const QRectF& rect) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry || geometry->originalSize.isEmpty()) {
        return QRectF();
    }
    const QRectF pageRect =
        mapRect(rect, [this, pageIndex](const QPointF& point) {
            return pdfToPage(pageIndex, point);
        });
    return pageRectToNormalizedOriginal(pageIndex, pageRect);
}

QRectF PDFContinuousCoordinateMapper::pageRectToOriginalTopLeft(
    int pageIndex, const QRectF& rect) const {
    return mapRect(rect, [this, pageIndex](const QPointF& point) {
        return pageToOriginalTopLeft(pageIndex, point);
    });
}

QRectF PDFContinuousCoordinateMapper::originalTopLeftRectToPage(
    int pageIndex, const QRectF& rect) const {
    return mapRect(rect, [this, pageIndex](const QPointF& point) {
        return originalTopLeftToPage(pageIndex, point);
    });
}

QRectF PDFContinuousCoordinateMapper::originalTopLeftRectToNormalized(
    int pageIndex, const QRectF& rect) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry || geometry->originalSize.isEmpty()) {
        return QRectF();
    }
    const QSizeF size = geometry->originalSize;
    return QRectF(rect.x() / size.width(), rect.y() / size.height(),
                  rect.width() / size.width(), rect.height() / size.height());
}

QRectF PDFContinuousCoordinateMapper::pageRectToViewport(
    int pageIndex, const QRectF& rect) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry) {
        return QRectF();
    }
    return QRectF(
        documentToViewport(geometry->contentRect.topLeft() + rect.topLeft()),
        rect.size());
}

QRectF PDFContinuousCoordinateMapper::pdfRectToViewport(
    int pageIndex, const QRectF& rect) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry) {
        return QRectF();
    }
    return pageRectToViewport(
        pageIndex, mapRect(rect, [this, pageIndex](const QPointF& point) {
            return pdfToPage(pageIndex, point);
        }));
}

QRectF PDFContinuousCoordinateMapper::normalizedOriginalRectToPage(
    int pageIndex, const QRectF& rect) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry || geometry->originalSize.isEmpty()) {
        return QRectF();
    }
    const QSizeF size = geometry->originalSize;
    const QRectF original(rect.x() * size.width(), rect.y() * size.height(),
                          rect.width() * size.width(),
                          rect.height() * size.height());
    return mapRect(original, [this, pageIndex](const QPointF& point) {
        return originalTopLeftToPage(pageIndex, point);
    });
}

QRectF PDFContinuousCoordinateMapper::pageRectToNormalizedOriginal(
    int pageIndex, const QRectF& rect) const {
    const auto* geometry = geometryForPage(pageIndex);
    if (!geometry || geometry->originalSize.isEmpty()) {
        return QRectF();
    }
    const QRectF original = pageRectToOriginalTopLeft(pageIndex, rect);
    const QSizeF size = geometry->originalSize;
    return QRectF(original.x() / size.width(), original.y() / size.height(),
                  original.width() / size.width(),
                  original.height() / size.height());
}

int PDFContinuousCoordinateMapper::normalizeRotation(int rotation) {
    rotation %= 360;
    return rotation < 0 ? rotation + 360 : rotation;
}

QPointF PDFContinuousCoordinateMapper::rotateToOriginal(const QPointF& point,
                                                        const QSizeF& size,
                                                        int rotation) {
    switch (rotation) {
        case 90:
            return {point.y(), size.height() - point.x()};
        case 180:
            return {size.width() - point.x(), size.height() - point.y()};
        case 270:
            return {size.width() - point.y(), point.x()};
        default:
            return point;
    }
}

QPointF PDFContinuousCoordinateMapper::rotateFromOriginal(const QPointF& point,
                                                          const QSizeF& size,
                                                          int rotation) {
    switch (rotation) {
        case 90:
            return {size.height() - point.y(), point.x()};
        case 180:
            return {size.width() - point.x(), size.height() - point.y()};
        case 270:
            return {point.y(), size.width() - point.x()};
        default:
            return point;
    }
}

QRectF PDFContinuousCoordinateMapper::mapRect(
    const QRectF& rect, const std::function<QPointF(const QPointF&)>& map) {
    if (rect.isEmpty()) {
        return QRectF();
    }
    const QVector<QPointF> points{map(rect.topLeft()), map(rect.topRight()),
                                  map(rect.bottomLeft()),
                                  map(rect.bottomRight())};
    qreal left = points.first().x();
    qreal right = left;
    qreal top = points.first().y();
    qreal bottom = top;
    for (const QPointF& point : points) {
        left = std::min(left, point.x());
        right = std::max(right, point.x());
        top = std::min(top, point.y());
        bottom = std::max(bottom, point.y());
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}
