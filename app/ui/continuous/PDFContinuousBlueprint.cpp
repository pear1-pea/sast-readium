#include "PDFContinuousBlueprint.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

void PDFContinuousBlueprint::clear() {
    m_pages.clear();
    m_documentSize = QSizeF();
    ++m_revision;
}

bool PDFContinuousBlueprint::isEmpty() const { return m_pages.isEmpty(); }

int PDFContinuousBlueprint::pageCount() const { return m_pages.size(); }

std::uint64_t PDFContinuousBlueprint::revision() const { return m_revision; }

void PDFContinuousBlueprint::rebuild(
    const QVector<QSizeF>& originalPageSizes,
    const PDFContinuousLayoutOptions& options) {
    m_options = options;
    m_options.rotation = normalizeRotation(options.rotation);
    m_pages.clear();

    qreal maxPageWidth = 0.0;
    QVector<QSizeF> scaledSizes;
    scaledSizes.reserve(originalPageSizes.size());
    for (const QSizeF& originalSize : originalPageSizes) {
        const QSizeF rotated = rotatedSize(originalSize, m_options.rotation);
        const QSizeF scaled(rotated.width() * m_options.zoom,
                            rotated.height() * m_options.zoom);
        scaledSizes.push_back(scaled);
        maxPageWidth = std::max(maxPageWidth, scaled.width());
    }

    const qreal availableWidth = std::max(
        qreal(0.0), m_options.viewportWidth - m_options.documentMargins.left() -
                        m_options.documentMargins.right());
    const qreal layoutWidth = std::max(maxPageWidth, availableWidth);
    const qreal documentWidth = m_options.documentMargins.left() + layoutWidth +
                                m_options.documentMargins.right();

    qreal y = m_options.documentMargins.top();
    m_pages.reserve(originalPageSizes.size());
    for (int i = 0; i < originalPageSizes.size(); ++i) {
        const QSizeF scaled = scaledSizes.at(i);
        qreal x = m_options.documentMargins.left();
        if (m_options.horizontalAlignment.testFlag(Qt::AlignHCenter)) {
            x += (layoutWidth - scaled.width()) / 2.0;
        } else if (m_options.horizontalAlignment.testFlag(Qt::AlignRight)) {
            x += layoutWidth - scaled.width();
        }

        PDFPageGeometry geometry;
        geometry.pageIndex = i;
        geometry.originalSize = originalPageSizes.at(i);
        geometry.pageRect = QRectF(QPointF(x, y), scaled);
        geometry.contentRect = geometry.pageRect;
        m_pages.push_back(geometry);

        y += scaled.height();
        if (i != originalPageSizes.size() - 1) {
            y += m_options.pageSpacing;
        }
    }

    y += m_options.documentMargins.bottom();
    m_documentSize = QSizeF(documentWidth, y);
    ++m_revision;
}

const PDFContinuousLayoutOptions& PDFContinuousBlueprint::layoutOptions()
    const {
    return m_options;
}

QSizeF PDFContinuousBlueprint::documentSize() const { return m_documentSize; }

qreal PDFContinuousBlueprint::documentHeight() const {
    return m_documentSize.height();
}

const PDFPageGeometry* PDFContinuousBlueprint::pageGeometry(
    int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= m_pages.size()) {
        return nullptr;
    }
    return &m_pages[pageIndex];
}

QRectF PDFContinuousBlueprint::pageDocumentRect(int pageIndex) const {
    const PDFPageGeometry* geometry = pageGeometry(pageIndex);
    return geometry ? geometry->pageRect : QRectF();
}

QRectF PDFContinuousBlueprint::pageContentRect(int pageIndex) const {
    const PDFPageGeometry* geometry = pageGeometry(pageIndex);
    return geometry ? geometry->contentRect : QRectF();
}

int PDFContinuousBlueprint::pageAt(const QPointF& documentPoint) const {
    const auto it =
        std::upper_bound(m_pages.begin(), m_pages.end(), documentPoint.y(),
                         [](qreal y, const PDFPageGeometry& geometry) {
                             return y < geometry.contentRect.top();
                         });

    if (it == m_pages.begin()) {
        return -1;
    }

    const PDFPageGeometry& candidate = *(it - 1);
    return candidate.contentRect.contains(documentPoint) ? candidate.pageIndex
                                                         : -1;
}

std::optional<PDFPageHit> PDFContinuousBlueprint::hitTest(
    const QPointF& documentPoint) const {
    const int pageIndex = pageAt(documentPoint);
    if (pageIndex < 0) {
        return std::nullopt;
    }

    PDFPageHit hit;
    hit.pageIndex = pageIndex;
    hit.insidePage = true;
    hit.documentPoint = documentPoint;
    hit.pagePoint = documentToPage(pageIndex, documentPoint);
    return hit;
}

QPointF PDFContinuousBlueprint::viewportToDocument(const QPointF& viewportPoint,
                                                   qreal contentOffsetY) const {
    return QPointF(viewportPoint.x(), viewportPoint.y() + contentOffsetY);
}

QPointF PDFContinuousBlueprint::documentToViewport(const QPointF& documentPoint,
                                                   qreal contentOffsetY) const {
    return QPointF(documentPoint.x(), documentPoint.y() - contentOffsetY);
}

QPointF PDFContinuousBlueprint::documentToPage(
    int pageIndex, const QPointF& documentPoint) const {
    const PDFPageGeometry* geometry = pageGeometry(pageIndex);
    if (!geometry) {
        return QPointF();
    }
    return documentPoint - geometry->contentRect.topLeft();
}

QPointF PDFContinuousBlueprint::pageToDocument(int pageIndex,
                                               const QPointF& pagePoint) const {
    const PDFPageGeometry* geometry = pageGeometry(pageIndex);
    if (!geometry) {
        return QPointF();
    }
    return geometry->contentRect.topLeft() + pagePoint;
}

int PDFContinuousBlueprint::currentPageForViewport(const QRectF& viewportRect,
                                                   int previousPage,
                                                   qreal hysteresisPx) const {
    if (m_pages.isEmpty()) {
        return -1;
    }

    const QPointF center = viewportRect.center();
    const int strictPage = pageAt(center);
    if (strictPage >= 0) {
        return strictPage;
    }

    if (previousPage >= 0 && previousPage < m_pages.size()) {
        const QRectF previousRect = m_pages[previousPage].contentRect.adjusted(
            0.0, -hysteresisPx, 0.0, hysteresisPx);
        if (previousRect.contains(center)) {
            return previousPage;
        }
    }

    return nearestPageByY(center.y());
}

qreal PDFContinuousBlueprint::scrollOffsetForPage(
    int pageIndex, qreal viewportHeight, PageScrollAlignment alignment) const {
    const PDFPageGeometry* geometry = pageGeometry(pageIndex);
    if (!geometry) {
        return 0.0;
    }

    qreal offset = geometry->contentRect.top();
    if (alignment == PageScrollAlignment::Center) {
        offset = geometry->contentRect.center().y() - viewportHeight / 2.0;
    } else if (alignment == PageScrollAlignment::KeepVisible) {
        offset = geometry->contentRect.top();
    }

    const qreal maxOffset =
        std::max(qreal(0.0), documentHeight() - viewportHeight);
    return qBound(qreal(0.0), offset, maxOffset);
}

PDFViewportAnchor PDFContinuousBlueprint::captureAnchor(
    const QRectF& documentViewportRect, int preferredPage) const {
    PDFViewportAnchor anchor;
    anchor.viewportPoint = QPointF(documentViewportRect.width() / 2.0,
                                   documentViewportRect.height() / 2.0);

    const QPointF documentPoint = documentViewportRect.center();
    int pageIndex = preferredPage;
    if (pageIndex < 0 || pageIndex >= m_pages.size() ||
        !m_pages[pageIndex].contentRect.contains(documentPoint)) {
        pageIndex = pageAt(documentPoint);
    }
    if (pageIndex < 0) {
        pageIndex = nearestPageByY(documentPoint.y());
    }

    anchor.pageIndex = pageIndex;
    anchor.pagePoint =
        documentToPage(pageIndex, documentPoint) / m_options.zoom;
    return anchor;
}

qreal PDFContinuousBlueprint::restoreAnchorOffset(
    const PDFViewportAnchor& anchor, qreal viewportHeight) const {
    const PDFPageGeometry* geometry = pageGeometry(anchor.pageIndex);
    if (!geometry) {
        return 0.0;
    }

    const QPointF scaledPagePoint = anchor.pagePoint * m_options.zoom;
    const QPointF documentPoint =
        pageToDocument(anchor.pageIndex, scaledPagePoint);
    const qreal offset = documentPoint.y() - anchor.viewportPoint.y();
    const qreal maxOffset =
        std::max(qreal(0.0), documentHeight() - viewportHeight);
    return qBound(qreal(0.0), offset, maxOffset);
}

int PDFContinuousBlueprint::normalizeRotation(int rotation) {
    int normalized = rotation % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return normalized;
}

QSizeF PDFContinuousBlueprint::rotatedSize(const QSizeF& size, int rotation) {
    const int normalized = normalizeRotation(rotation);
    if (normalized == 90 || normalized == 270) {
        return QSizeF(size.height(), size.width());
    }
    return size;
}

int PDFContinuousBlueprint::nearestPageByY(qreal documentY) const {
    if (m_pages.isEmpty()) {
        return -1;
    }

    int bestPage = 0;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (const PDFPageGeometry& geometry : m_pages) {
        qreal distance = 0.0;
        if (documentY < geometry.contentRect.top()) {
            distance = geometry.contentRect.top() - documentY;
        } else if (documentY > geometry.contentRect.bottom()) {
            distance = documentY - geometry.contentRect.bottom();
        }

        if (distance < bestDistance) {
            bestDistance = distance;
            bestPage = geometry.pageIndex;
        }
    }
    return bestPage;
}
