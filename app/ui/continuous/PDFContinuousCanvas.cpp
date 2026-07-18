#include "PDFContinuousCanvas.h"

#include "PDFContinuousRenderScheduler.h"
#include "managers/StyleManager.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QtGlobal>

#include <algorithm>

#include "utils/LoggingMacros.h"

namespace {
QColor canvasBackgroundColor() { return STYLE.backgroundColor(); }

QColor canvasPageColor() { return STYLE.surfaceColor(); }

QColor canvasShadowColor() {
    return STYLE.currentTheme() == Theme::Light ? QColor(0, 0, 0, 20)
                                                : QColor(0, 0, 0, 110);
}
}  // namespace

PDFContinuousCanvas::PDFContinuousCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAutoFillBackground(false);
    m_lastDevicePixelRatio = devicePixelRatioF();
}

void PDFContinuousCanvas::setBlueprint(
    std::shared_ptr<const PDFContinuousBlueprint> blueprint) {
    m_blueprint = std::move(blueprint);
    m_pageLinks.clear();
    if (!m_blueprint) {
        m_annotationsByPage.clear();
    }
    m_hoveredLink = nullptr;
    m_hoveredLinkPage = -1;
    m_isSelectingText = false;
    m_selectionStart.reset();
    m_selectionEnd.reset();
    unsetCursor();
    m_lastVisibleRange = {-1, -1};
    m_lastCurrentPage = -1;
    updateVisibleRangeSignals();
    requestVisiblePageRenders(devicePixelRatioF());
    update();
}

std::shared_ptr<const PDFContinuousBlueprint> PDFContinuousCanvas::blueprint()
    const {
    return m_blueprint;
}

void PDFContinuousCanvas::setContentOffsetY(qreal offsetY) {
    if (qFuzzyCompare(m_contentOffsetY, offsetY)) {
        return;
    }

    m_contentOffsetY = offsetY;
    updateVisibleRangeSignals();
    requestVisiblePageRenders(devicePixelRatioF());
    update();
}

qreal PDFContinuousCanvas::contentOffsetY() const { return m_contentOffsetY; }

void PDFContinuousCanvas::setDocument(
    std::shared_ptr<Poppler::Document> document) {
    m_document = std::move(document);
    m_pageLinks.clear();
    if (!m_document) {
        m_annotationsByPage.clear();
    }
    m_hoveredLink = nullptr;
    m_hoveredLinkPage = -1;
    m_isSelectingText = false;
    m_selectionStart.reset();
    m_selectionEnd.reset();
    unsetCursor();
}

void PDFContinuousCanvas::setImageCache(PDFContinuousImageCache* imageCache) {
    m_imageCache = imageCache;
    requestVisiblePageRenders(devicePixelRatioF());
    update();
}

void PDFContinuousCanvas::setRenderScheduler(
    PDFContinuousRenderScheduler* renderScheduler) {
    if (m_renderScheduler) {
        disconnect(m_renderScheduler, nullptr, this, nullptr);
    }
    m_renderScheduler = renderScheduler;
    if (!m_renderScheduler) {
        return;
    }

    requestVisiblePageRenders(devicePixelRatioF());
    connect(
        m_renderScheduler, &PDFContinuousRenderScheduler::pageRendered, this,
        [this](const PDFContinuousRenderResult& result) {
            if (!result.success) {
                LOG_WARNING(
                    "[continuous-canvas] drop result reason=render_failed "
                    "page={} generation={} error={}",
                    result.request.key.pageIndex, result.request.generation,
                    result.error.toStdString());
                return;
            }
            if (!m_imageCache) {
                LOG_WARNING(
                    "[continuous-canvas] drop result reason=no_cache page={} "
                    "generation={}",
                    result.request.key.pageIndex, result.request.generation);
                return;
            }
            if (!m_blueprint) {
                LOG_DEBUG(
                    "[continuous-canvas] drop result reason=no_blueprint "
                    "page={} generation={}",
                    result.request.key.pageIndex, result.request.generation);
                return;
            }
            if (!m_renderScheduler ||
                result.request.generation !=
                    m_renderScheduler->currentGeneration()) {
                LOG_DEBUG(
                    "[continuous-canvas] drop result reason=stale_generation "
                    "page={} generation={}",
                    result.request.key.pageIndex, result.request.generation);
                return;
            }
            m_imageCache->insert(result.request.key, result.image);
            LOG_DEBUG(
                "[continuous-canvas] cache inserted page={} generation={} "
                "cache_cost={} bytes",
                result.request.key.pageIndex, result.request.generation,
                m_imageCache->currentCostBytes());
            emit renderApplied(result.request.key.pageIndex,
                               static_cast<quint64>(result.request.generation));
            const PDFPageGeometry* geometry =
                m_blueprint->pageGeometry(result.request.key.pageIndex);
            if (!geometry) {
                LOG_DEBUG(
                    "[continuous-canvas] no geometry for rendered page={} "
                    "generation={}",
                    result.request.key.pageIndex, result.request.generation);
                update();
                return;
            }
            const QRect viewportRect =
                QRectF(documentToViewport(geometry->contentRect.topLeft()),
                       geometry->contentRect.size())
                    .toAlignedRect()
                    .adjusted(-8, -8, 8, 8);
            update(viewportRect);
        });
}

void PDFContinuousCanvas::requestVisiblePageRenders(qreal devicePixelRatio) {
    if (!m_blueprint || !m_renderScheduler || !m_imageCache) {
        LOG_DEBUG(
            "[continuous-canvas] skip render request missing blueprint={} "
            "scheduler={} cache={}",
            m_blueprint != nullptr, m_renderScheduler != nullptr,
            m_imageCache != nullptr);
        return;
    }

    const auto range = visiblePageRange(height());
    if (range.first < 0 || range.second < 0) {
        LOG_DEBUG(
            "[continuous-canvas] skip render request no visible pages "
            "height={} offset={:.1f}",
            height(), m_contentOffsetY);
        return;
    }

    QVector<PDFContinuousRenderRequest> requests;
    const std::uint64_t generation = m_renderScheduler->currentGeneration();
    for (int i = range.first; i <= range.second; ++i) {
        const PDFPageGeometry* geometry = m_blueprint->pageGeometry(i);
        if (!geometry) {
            continue;
        }

        PDFContinuousRenderRequest request;
        request.key = renderKeyForPage(i, devicePixelRatio);
        const QSizeF targetSize = geometry->contentRect.size();
        request.targetPixelSize =
            QSize(qCeil(targetSize.width() * devicePixelRatio),
                  qCeil(targetSize.height() * devicePixelRatio));
        request.generation = generation;
        if (!m_imageCache->contains(request.key)) {
            requests.push_back(request);
        }
    }
    LOG_DEBUG(
        "[continuous-canvas] request visible renders first={} last={} "
        "submitted={} generation={}",
        range.first, range.second, requests.size(), generation);
    m_renderScheduler->requestPages(requests);
}

void PDFContinuousCanvas::setSearchResults(
    const QHash<int, QList<SearchResult>>& resultsByPage) {
    m_searchResultsByPage = resultsByPage;
    update();
}

void PDFContinuousCanvas::clearSearchHighlights() {
    m_searchResultsByPage.clear();
    update();
}

void PDFContinuousCanvas::setAnnotations(
    const QHash<int, QList<PDFAnnotation>>& annotationsByPage) {
    m_annotationsByPage = annotationsByPage;
    update();
}

void PDFContinuousCanvas::clearAnnotationOverlays() {
    m_annotationsByPage.clear();
    update();
}

QRectF PDFContinuousCanvas::viewportRectInDocument() const {
    if (!m_blueprint) {
        return QRectF();
    }

    return QRectF(QPointF(0.0, m_contentOffsetY), QSizeF(width(), height()));
}

QPair<int, int> PDFContinuousCanvas::visiblePageRange(
    qreal preloadMargin) const {
    if (!m_blueprint || m_blueprint->isEmpty()) {
        return {-1, -1};
    }

    const QRectF visibleRect = viewportRectInDocument().adjusted(
        0.0, -preloadMargin, 0.0, preloadMargin);
    int first = -1;
    int last = -1;

    for (int i = 0; i < m_blueprint->pageCount(); ++i) {
        const PDFPageGeometry* geometry = m_blueprint->pageGeometry(i);
        if (!geometry) {
            continue;
        }
        if (geometry->contentRect.intersects(visibleRect)) {
            if (first < 0) {
                first = i;
            }
            last = i;
        } else if (first >= 0) {
            break;
        }
    }

    return {first, last};
}

int PDFContinuousCanvas::currentPageCandidate(int previousPage) const {
    if (!m_blueprint) {
        return -1;
    }
    return m_blueprint->currentPageForViewport(viewportRectInDocument(),
                                               previousPage);
}

QPointF PDFContinuousCanvas::viewportToDocument(
    const QPointF& viewportPoint) const {
    if (!m_blueprint) {
        return QPointF();
    }
    return m_blueprint->viewportToDocument(viewportPoint, m_contentOffsetY);
}

QPointF PDFContinuousCanvas::documentToViewport(
    const QPointF& documentPoint) const {
    if (!m_blueprint) {
        return QPointF();
    }
    return m_blueprint->documentToViewport(documentPoint, m_contentOffsetY);
}

std::optional<PDFPageHit> PDFContinuousCanvas::hitTestViewportPoint(
    const QPointF& viewportPoint) const {
    if (!m_blueprint) {
        return std::nullopt;
    }
    return m_blueprint->hitTest(viewportToDocument(viewportPoint));
}

const Poppler::Link* PDFContinuousCanvas::linkAtViewportPoint(
    const QPointF& viewportPoint) const {
    const auto hit = hitTestViewportPoint(viewportPoint);
    if (!hit || !m_document) {
        return nullptr;
    }

    loadPageLinks(hit->pageIndex);
    const auto it = m_pageLinks.find(hit->pageIndex);
    if (it == m_pageLinks.end()) {
        return nullptr;
    }

    const QPointF linkPoint = linkPointForHit(*hit);
    for (const std::unique_ptr<Poppler::Link>& link : it->second) {
        if (link && link->linkArea().contains(linkPoint)) {
            return link.get();
        }
    }
    return nullptr;
}

void PDFContinuousCanvas::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(event->rect(), canvasBackgroundColor());
    static int s_paintEventLogBudget = 12;
    if (s_paintEventLogBudget > 0) {
        LOG_DEBUG(
            "[continuous-canvas] paint event rect=({}, {}, {}, {}) "
            "widget={}x{} "
            "offset={:.1f} has_blueprint={}",
            event->rect().x(), event->rect().y(), event->rect().width(),
            event->rect().height(), width(), height(), m_contentOffsetY,
            m_blueprint != nullptr);
        --s_paintEventLogBudget;
    }

    if (!m_blueprint || m_blueprint->isEmpty()) {
        return;
    }

    const auto range = visiblePageRange();
    static int s_paintRangeLogBudget = 12;
    if (s_paintRangeLogBudget > 0) {
        LOG_DEBUG("[continuous-canvas] paint range first={} last={}",
                  range.first, range.second);
        --s_paintRangeLogBudget;
    }
    if (range.first < 0 || range.second < 0) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    static int s_paintLogBudget = 8;
    for (int i = range.first; i <= range.second; ++i) {
        const PDFPageGeometry* geometry = m_blueprint->pageGeometry(i);
        if (!geometry) {
            continue;
        }

        const QRectF viewportRect =
            QRectF(documentToViewport(geometry->contentRect.topLeft()),
                   geometry->contentRect.size());
        painter.fillRect(viewportRect.adjusted(4, 4, 4, 4),
                         canvasShadowColor());
        painter.fillRect(viewportRect, canvasPageColor());
        if (m_imageCache) {
            const PDFContinuousRenderKey key =
                renderKeyForPage(i, devicePixelRatioF());
            const QImage image = m_imageCache->get(key);
            if (!image.isNull()) {
                if (s_paintLogBudget > 0) {
                    const QColor sample = image.pixelColor(
                        qBound(0, image.width() / 2, image.width() - 1),
                        qBound(0, image.height() / 2, image.height() - 1));
                    LOG_DEBUG(
                        "[continuous-canvas] paint page={} rect=({}, {}, {}, "
                        "{}) "
                        "image={}x{} dpr={:.2f} sample=({}, {}, {}, {})",
                        i, viewportRect.x(), viewportRect.y(),
                        viewportRect.width(), viewportRect.height(),
                        image.width(), image.height(), image.devicePixelRatio(),
                        sample.red(), sample.green(), sample.blue(),
                        sample.alpha());
                    --s_paintLogBudget;
                }
                painter.drawImage(viewportRect, image);
            } else {
                if (s_paintLogBudget > 0) {
                    LOG_DEBUG(
                        "[continuous-canvas] paint cache miss page={} "
                        "rect=({}, {}, {}, {}) "
                        "dpr={:.2f}",
                        i, viewportRect.x(), viewportRect.y(),
                        viewportRect.width(), viewportRect.height(),
                        key.devicePixelRatio);
                    --s_paintLogBudget;
                }
                painter.setPen(STYLE.textSecondaryColor());
                painter.drawText(viewportRect, Qt::AlignCenter,
                                 QStringLiteral("Loading page %1").arg(i + 1));
            }
        } else {
            painter.setPen(STYLE.textSecondaryColor());
            painter.drawText(viewportRect, Qt::AlignCenter,
                             QStringLiteral("Page %1").arg(i + 1));
        }
        drawSearchHighlights(painter, i, *geometry);
        drawAnnotationOverlays(painter, i, *geometry);
        drawTextSelection(painter, i, *geometry);
        painter.setPen(QPen(STYLE.borderColor(), 1.0));
        painter.drawRect(viewportRect);
    }
}

void PDFContinuousCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateVisibleRangeSignals();
    requestVisiblePageRenders(devicePixelRatioF());
}

bool PDFContinuousCanvas::event(QEvent* event) {
    if (event->type() == QEvent::DevicePixelRatioChange) {
        const qreal currentDevicePixelRatio = devicePixelRatioF();
        if (!qFuzzyCompare(m_lastDevicePixelRatio, currentDevicePixelRatio)) {
            m_lastDevicePixelRatio = currentDevicePixelRatio;
            requestVisiblePageRenders(currentDevicePixelRatio);
            update();
        }
    }
    return QWidget::event(event);
}

void PDFContinuousCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_isSelectingText) {
        const auto hit = hitTestViewportPoint(event->position());
        if (hit && hit->insidePage && m_selectionStart &&
            hit->pageIndex == m_selectionStart->pageIndex) {
            m_selectionEnd = hit;
            update();
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    const auto hit = hitTestViewportPoint(event->position());
    const Poppler::Link* link = linkAtViewportPoint(event->position());
    const int linkPage = hit ? hit->pageIndex : -1;
    setCursor(link ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (link != m_hoveredLink || linkPage != m_hoveredLinkPage) {
        m_hoveredLink = link;
        m_hoveredLinkPage = link ? linkPage : -1;
        emit linkHovered(m_hoveredLinkPage, m_hoveredLink);
    }
    QWidget::mouseMoveEvent(event);
}

void PDFContinuousCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const Poppler::Link* link = linkAtViewportPoint(event->position());
        const auto hit = hitTestViewportPoint(event->position());
        if (link && hit) {
            emit linkClicked(hit->pageIndex, link);
            emitTypedLinkClicked(link);
        } else if (hit) {
            if (hit->insidePage) {
                m_isSelectingText = true;
                m_selectionStart = hit;
                m_selectionEnd = hit;
                update();
            }
            emit pageClicked(hit->pageIndex, hit->pagePoint);
        }
    }
    QWidget::mousePressEvent(event);
}

void PDFContinuousCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isSelectingText) {
        const auto hit = hitTestViewportPoint(event->position());
        if (hit && hit->insidePage && m_selectionStart &&
            hit->pageIndex == m_selectionStart->pageIndex) {
            m_selectionEnd = hit;
        }
        m_isSelectingText = false;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void PDFContinuousCanvas::loadPageLinks(int pageIndex) const {
    if (m_pageLinks.contains(pageIndex) || !m_document || pageIndex < 0 ||
        pageIndex >= m_document->numPages()) {
        return;
    }

    std::unique_ptr<Poppler::Page> page(m_document->page(pageIndex));
    if (!page) {
        m_pageLinks.emplace(pageIndex,
                            std::vector<std::unique_ptr<Poppler::Link>>{});
        return;
    }
    m_pageLinks.emplace(pageIndex, page->links());
}

QPointF PDFContinuousCanvas::linkPointForHit(const PDFPageHit& hit) const {
    if (!m_blueprint) {
        return QPointF();
    }
    const PDFPageGeometry* geometry = m_blueprint->pageGeometry(hit.pageIndex);
    if (!geometry || geometry->contentRect.isEmpty()) {
        return QPointF();
    }

    const qreal x = hit.pagePoint.x() / geometry->contentRect.width();
    const qreal y = hit.pagePoint.y() / geometry->contentRect.height();
    return QPointF(std::clamp(x, qreal(0.0), qreal(1.0)),
                   std::clamp(y, qreal(0.0), qreal(1.0)));
}

void PDFContinuousCanvas::drawSearchHighlights(
    QPainter& painter, int pageIndex, const PDFPageGeometry& geometry) {
    const auto it = m_searchResultsByPage.constFind(pageIndex);
    if (it == m_searchResultsByPage.constEnd() || it.value().isEmpty()) {
        return;
    }

    painter.save();
    const QPointF pageViewportTopLeft =
        documentToViewport(geometry.contentRect.topLeft());
    const QSize widgetSize(qRound(geometry.contentRect.width()),
                           qRound(geometry.contentRect.height()));

    for (SearchResult result : it.value()) {
        if (!result.isValidForHighlight()) {
            continue;
        }

        result.transformToWidgetCoordinates(
            1.0, m_blueprint->layoutOptions().rotation, geometry.originalSize,
            widgetSize);
        if (result.widgetRect.isEmpty()) {
            continue;
        }

        QRectF highlightRect =
            result.widgetRect.translated(pageViewportTopLeft);
        const QColor color = result.isCurrentResult ? QColor(255, 165, 0, 150)
                                                    : QColor(255, 255, 0, 100);
        painter.fillRect(highlightRect, color);
        if (result.isCurrentResult) {
            painter.setPen(QPen(color.darker(150), 2));
            painter.drawRect(highlightRect);
        }
    }

    painter.restore();
}

QRectF PDFContinuousCanvas::pageRectToViewportRect(
    const QRectF& pageRect, const PDFPageGeometry& geometry) const {
    return QRectF(
        documentToViewport(geometry.contentRect.topLeft() + pageRect.topLeft()),
        pageRect.size());
}

QRectF PDFContinuousCanvas::annotationRectToViewportRect(
    const QRectF& annotationRect, const PDFPageGeometry& geometry) const {
    return pageRectToViewportRect(
        QRectF(annotationRect.x() * geometry.contentRect.width(),
               annotationRect.y() * geometry.contentRect.height(),
               annotationRect.width() * geometry.contentRect.width(),
               annotationRect.height() * geometry.contentRect.height()),
        geometry);
}

void PDFContinuousCanvas::drawAnnotationOverlays(
    QPainter& painter, int pageIndex, const PDFPageGeometry& geometry) {
    const auto it = m_annotationsByPage.constFind(pageIndex);
    if (it == m_annotationsByPage.constEnd() || it.value().isEmpty()) {
        return;
    }

    painter.save();
    for (const PDFAnnotation& annotation : it.value()) {
        if (!annotation.isVisible || annotation.boundingRect.isEmpty()) {
            continue;
        }

        QColor color =
            annotation.color.isValid() ? annotation.color : Qt::yellow;
        color.setAlphaF(std::clamp(annotation.opacity, 0.0, 1.0));
        const QRectF rect =
            annotationRectToViewportRect(annotation.boundingRect, geometry);
        const qreal lineWidth = qMax(annotation.lineWidth, 1.0);
        painter.setPen(QPen(color, lineWidth));

        switch (annotation.type) {
            case AnnotationType::Highlight:
                color.setAlphaF(
                    std::clamp(annotation.opacity * 0.35, 0.0, 1.0));
                painter.fillRect(rect, color);
                break;
            case AnnotationType::Underline:
                painter.drawLine(rect.bottomLeft(), rect.bottomRight());
                break;
            case AnnotationType::StrikeOut:
                painter.drawLine(rect.left(), rect.center().y(), rect.right(),
                                 rect.center().y());
                break;
            case AnnotationType::Rectangle:
                painter.drawRect(rect);
                break;
            case AnnotationType::Circle:
                painter.drawEllipse(rect);
                break;
            case AnnotationType::Line:
            case AnnotationType::Arrow:
                painter.drawLine(
                    annotationRectToViewportRect(
                        QRectF(annotation.startPoint, QSizeF(0.0, 0.0)),
                        geometry)
                        .center(),
                    annotationRectToViewportRect(
                        QRectF(annotation.endPoint, QSizeF(0.0, 0.0)), geometry)
                        .center());
                break;
            case AnnotationType::Ink: {
                QPainterPath path;
                for (int i = 0; i < annotation.inkPath.size(); ++i) {
                    const QPointF point =
                        annotationRectToViewportRect(
                            QRectF(annotation.inkPath.at(i), QSizeF(0.0, 0.0)),
                            geometry)
                            .center();
                    if (i == 0) {
                        path.moveTo(point);
                    } else {
                        path.lineTo(point);
                    }
                }
                painter.drawPath(path);
                break;
            }
            case AnnotationType::Note:
            case AnnotationType::FreeText:
            case AnnotationType::Squiggly:
                painter.drawRect(rect);
                if (!annotation.content.isEmpty()) {
                    painter.drawText(
                        rect.adjusted(4, 4, -4, -4),
                        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                        annotation.content);
                }
                break;
        }
    }
    painter.restore();
}

void PDFContinuousCanvas::drawTextSelection(QPainter& painter, int pageIndex,
                                            const PDFPageGeometry& geometry) {
    if (!m_selectionStart || !m_selectionEnd ||
        m_selectionStart->pageIndex != pageIndex ||
        m_selectionEnd->pageIndex != pageIndex) {
        return;
    }

    const QRectF selectionRect =
        QRectF(m_selectionStart->pagePoint, m_selectionEnd->pagePoint)
            .normalized();
    if (selectionRect.width() < 1.0 && selectionRect.height() < 1.0) {
        return;
    }

    painter.save();
    painter.fillRect(pageRectToViewportRect(selectionRect, geometry),
                     QColor(64, 128, 255, 90));
    painter.restore();
}

void PDFContinuousCanvas::emitTypedLinkClicked(const Poppler::Link* link) {
    if (!link) {
        return;
    }

    if (link->linkType() == Poppler::Link::Browse) {
        const auto* browseLink = static_cast<const Poppler::LinkBrowse*>(link);
        emit browseLinkClicked(browseLink->url());
    } else if (link->linkType() == Poppler::Link::Goto) {
        const auto* gotoLink = static_cast<const Poppler::LinkGoto*>(link);
        if (!gotoLink->isExternal()) {
            const int pageIndex = gotoLink->destination().pageNumber() - 1;
            emit gotoLinkClicked(pageIndex);
        }
    }
}

void PDFContinuousCanvas::updateVisibleRangeSignals() {
    const auto range = visiblePageRange();
    if (range != m_lastVisibleRange) {
        m_lastVisibleRange = range;
        emit visiblePageRangeChanged(range.first, range.second);
    }

    const int page = currentPageCandidate(m_lastCurrentPage);
    if (page != m_lastCurrentPage) {
        m_lastCurrentPage = page;
        emit currentPageCandidateChanged(page);
    }
}

PDFContinuousRenderKey PDFContinuousCanvas::renderKeyForPage(
    int pageIndex, qreal devicePixelRatio) const {
    PDFContinuousRenderKey key;
    key.pageIndex = pageIndex;
    if (m_blueprint) {
        const PDFContinuousLayoutOptions& options =
            m_blueprint->layoutOptions();
        key.zoom = options.zoom;
        key.rotation = options.rotation;
    }
    key.devicePixelRatio = devicePixelRatio;
    return key;
}
