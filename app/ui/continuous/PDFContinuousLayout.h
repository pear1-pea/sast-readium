#pragma once

#include <QVector>

#include <optional>

#include "PDFContinuousTypes.h"

class PDFContinuousLayout {
public:
    void clear();
    bool isEmpty() const;
    int pageCount() const;

    void rebuild(const QVector<QSizeF>& originalPageSizes,
                 const PDFContinuousLayoutOptions& options);

    const PDFContinuousLayoutOptions& layoutOptions() const;
    QSizeF documentSize() const;
    qreal documentHeight() const;

    const PDFPageGeometry* pageGeometry(int pageIndex) const;
    QRectF pageDocumentRect(int pageIndex) const;
    QRectF pageContentRect(int pageIndex) const;

    int pageAt(const QPointF& documentPoint) const;
    std::optional<PDFPageHit> hitTest(const QPointF& documentPoint) const;

    QPointF viewportToDocument(const QPointF& viewportPoint,
                               qreal contentOffsetY) const;
    QPointF documentToViewport(const QPointF& documentPoint,
                               qreal contentOffsetY) const;

    QPointF documentToPage(int pageIndex, const QPointF& documentPoint) const;
    QPointF pageToDocument(int pageIndex, const QPointF& pagePoint) const;

    int currentPageForViewport(const QRectF& viewportRect,
                               int previousPage = -1,
                               qreal hysteresisPx = 24.0) const;

    qreal scrollOffsetForPage(int pageIndex, qreal viewportHeight,
                              PageScrollAlignment alignment) const;

    PDFViewportAnchor captureAnchor(const QRectF& documentViewportRect,
                                    int preferredPage = -1) const;
    qreal restoreAnchorOffset(const PDFViewportAnchor& anchor,
                              qreal viewportHeight) const;

private:
    static int normalizeRotation(int rotation);
    static QSizeF rotatedSize(const QSizeF& size, int rotation);
    int nearestPageByY(qreal documentY) const;

    QVector<PDFPageGeometry> m_pages;
    PDFContinuousLayoutOptions m_options;
    QSizeF m_documentSize;
};
