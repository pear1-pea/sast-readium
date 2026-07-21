#include "PDFContinuousBlueprint.h"

void PDFContinuousBlueprint::clear() {
    m_layout.clear();
    ++m_revision;
}

bool PDFContinuousBlueprint::isEmpty() const { return m_layout.isEmpty(); }

int PDFContinuousBlueprint::pageCount() const { return m_layout.pageCount(); }

std::uint64_t PDFContinuousBlueprint::revision() const { return m_revision; }

void PDFContinuousBlueprint::rebuild(
    const QVector<QSizeF>& originalPageSizes,
    const PDFContinuousLayoutOptions& options) {
    m_layout.rebuild(originalPageSizes, options);
    ++m_revision;
}

const PDFContinuousLayoutOptions& PDFContinuousBlueprint::layoutOptions()
    const {
    return m_layout.layoutOptions();
}

QSizeF PDFContinuousBlueprint::documentSize() const {
    return m_layout.documentSize();
}

qreal PDFContinuousBlueprint::documentHeight() const {
    return m_layout.documentHeight();
}

const PDFPageGeometry* PDFContinuousBlueprint::pageGeometry(
    int pageIndex) const {
    return m_layout.pageGeometry(pageIndex);
}

QRectF PDFContinuousBlueprint::pageDocumentRect(int pageIndex) const {
    return m_layout.pageDocumentRect(pageIndex);
}

QRectF PDFContinuousBlueprint::pageContentRect(int pageIndex) const {
    return m_layout.pageContentRect(pageIndex);
}

int PDFContinuousBlueprint::pageAt(const QPointF& documentPoint) const {
    return m_layout.pageAt(documentPoint);
}

std::optional<PDFPageHit> PDFContinuousBlueprint::hitTest(
    const QPointF& documentPoint) const {
    return m_layout.hitTest(documentPoint);
}

QPointF PDFContinuousBlueprint::viewportToDocument(const QPointF& viewportPoint,
                                                   qreal contentOffsetY) const {
    return m_layout.viewportToDocument(viewportPoint, contentOffsetY);
}

QPointF PDFContinuousBlueprint::documentToViewport(const QPointF& documentPoint,
                                                   qreal contentOffsetY) const {
    return m_layout.documentToViewport(documentPoint, contentOffsetY);
}

QPointF PDFContinuousBlueprint::documentToPage(
    int pageIndex, const QPointF& documentPoint) const {
    return m_layout.documentToPage(pageIndex, documentPoint);
}

QPointF PDFContinuousBlueprint::pageToDocument(int pageIndex,
                                               const QPointF& pagePoint) const {
    return m_layout.pageToDocument(pageIndex, pagePoint);
}

int PDFContinuousBlueprint::currentPageForViewport(const QRectF& viewportRect,
                                                   int previousPage,
                                                   qreal hysteresisPx) const {
    return m_layout.currentPageForViewport(viewportRect, previousPage,
                                           hysteresisPx);
}

qreal PDFContinuousBlueprint::scrollOffsetForPage(
    int pageIndex, qreal viewportHeight, PageScrollAlignment alignment) const {
    return m_layout.scrollOffsetForPage(pageIndex, viewportHeight, alignment);
}

PDFViewportAnchor PDFContinuousBlueprint::captureAnchor(
    const QRectF& documentViewportRect, int preferredPage) const {
    return m_layout.captureAnchor(documentViewportRect, preferredPage);
}

qreal PDFContinuousBlueprint::restoreAnchorOffset(
    const PDFViewportAnchor& anchor, qreal viewportHeight) const {
    return m_layout.restoreAnchorOffset(anchor, viewportHeight);
}
