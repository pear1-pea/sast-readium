#pragma once

#include <QPointF>
#include <QRectF>

#include <functional>
#include <memory>
#include <optional>

#include "PDFContinuousBlueprint.h"

class PDFContinuousCoordinateMapper {
public:
    void setBlueprint(std::shared_ptr<const PDFContinuousBlueprint> blueprint);
    void setContentOffset(const QPointF& offset);

    QPointF viewportToDocument(const QPointF& point) const;
    QPointF documentToViewport(const QPointF& point) const;
    std::optional<PDFPageHit> hitTestViewport(const QPointF& point) const;

    QPointF pageToOriginalTopLeft(int pageIndex, const QPointF& point) const;
    QPointF originalTopLeftToPage(int pageIndex, const QPointF& point) const;
    QPointF pageToPdf(int pageIndex, const QPointF& point) const;
    QPointF pdfToPage(int pageIndex, const QPointF& point) const;
    QRectF pageRectToPdf(int pageIndex, const QRectF& rect) const;
    QRectF pdfRectToNormalizedOriginal(int pageIndex, const QRectF& rect) const;
    QRectF pageRectToOriginalTopLeft(int pageIndex, const QRectF& rect) const;
    QRectF originalTopLeftRectToNormalized(int pageIndex,
                                           const QRectF& rect) const;
    QRectF originalTopLeftRectToPage(int pageIndex, const QRectF& rect) const;
    QRectF pageRectToViewport(int pageIndex, const QRectF& rect) const;
    QRectF pdfRectToViewport(int pageIndex, const QRectF& rect) const;
    QRectF normalizedOriginalRectToPage(int pageIndex,
                                        const QRectF& rect) const;
    QRectF pageRectToNormalizedOriginal(int pageIndex,
                                        const QRectF& rect) const;

private:
    const PDFPageGeometry* geometryForPage(int pageIndex) const;
    static int normalizeRotation(int rotation);
    static QPointF rotateToOriginal(const QPointF& point, const QSizeF& size,
                                    int rotation);
    static QPointF rotateFromOriginal(const QPointF& point, const QSizeF& size,
                                      int rotation);
    static QRectF mapRect(const QRectF& rect,
                          const std::function<QPointF(const QPointF&)>& map);

    std::shared_ptr<const PDFContinuousBlueprint> m_blueprint;
    QPointF m_contentOffset;
};
