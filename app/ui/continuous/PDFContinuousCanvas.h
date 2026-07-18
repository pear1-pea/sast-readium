#pragma once

#include <poppler/qt6/poppler-link.h>
#include <poppler/qt6/poppler-qt6.h>
#include <QPair>
#include <QWidget>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "PDFContinuousBlueprint.h"
#include "PDFContinuousImageCache.h"
#include "model/AnnotationModel.h"
#include "model/SearchModel.h"

class PDFContinuousRenderScheduler;
class QPainter;

class PDFContinuousCanvas : public QWidget {
    Q_OBJECT

public:
    explicit PDFContinuousCanvas(QWidget* parent = nullptr);

    void setBlueprint(std::shared_ptr<const PDFContinuousBlueprint> blueprint);
    std::shared_ptr<const PDFContinuousBlueprint> blueprint() const;

    void setContentOffsetY(qreal offsetY);
    qreal contentOffsetY() const;

    void setDocument(std::shared_ptr<Poppler::Document> document);
    void setImageCache(PDFContinuousImageCache* imageCache);
    void setRenderScheduler(PDFContinuousRenderScheduler* renderScheduler);
    void requestVisiblePageRenders(qreal devicePixelRatio);
    void setSearchResults(const QHash<int, QList<SearchResult>>& resultsByPage);
    void clearSearchHighlights();
    void setAnnotations(
        const QHash<int, QList<PDFAnnotation>>& annotationsByPage);
    void clearAnnotationOverlays();

    QRectF viewportRectInDocument() const;
    QPair<int, int> visiblePageRange(qreal preloadMargin = 0.0) const;

    int currentPageCandidate(int previousPage = -1) const;

    QPointF viewportToDocument(const QPointF& viewportPoint) const;
    QPointF documentToViewport(const QPointF& documentPoint) const;

    std::optional<PDFPageHit> hitTestViewportPoint(
        const QPointF& viewportPoint) const;
    const Poppler::Link* linkAtViewportPoint(
        const QPointF& viewportPoint) const;

signals:
    void visiblePageRangeChanged(int firstPage, int lastPage);
    void currentPageCandidateChanged(int pageIndex);
    void pageClicked(int pageIndex, QPointF pagePoint);
    void linkHovered(int pageIndex, const Poppler::Link* link);
    void linkClicked(int pageIndex, const Poppler::Link* link);
    void browseLinkClicked(const QString& url);
    void gotoLinkClicked(int pageIndex);
    void renderApplied(int pageIndex, quint64 generation);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateVisibleRangeSignals();
    PDFContinuousRenderKey renderKeyForPage(int pageIndex,
                                            qreal devicePixelRatio) const;
    void loadPageLinks(int pageIndex) const;
    QPointF linkPointForHit(const PDFPageHit& hit) const;
    void drawSearchHighlights(QPainter& painter, int pageIndex,
                              const PDFPageGeometry& geometry);
    void drawAnnotationOverlays(QPainter& painter, int pageIndex,
                                const PDFPageGeometry& geometry);
    void drawTextSelection(QPainter& painter, int pageIndex,
                           const PDFPageGeometry& geometry);
    QRectF pageRectToViewportRect(const QRectF& pageRect,
                                  const PDFPageGeometry& geometry) const;
    QRectF annotationRectToViewportRect(const QRectF& annotationRect,
                                        const PDFPageGeometry& geometry) const;
    void emitTypedLinkClicked(const Poppler::Link* link);

    std::shared_ptr<const PDFContinuousBlueprint> m_blueprint;
    std::shared_ptr<Poppler::Document> m_document;
    qreal m_contentOffsetY = 0.0;
    PDFContinuousImageCache* m_imageCache = nullptr;
    PDFContinuousRenderScheduler* m_renderScheduler = nullptr;
    mutable std::unordered_map<int, std::vector<std::unique_ptr<Poppler::Link>>>
        m_pageLinks;
    QHash<int, QList<SearchResult>> m_searchResultsByPage;
    QHash<int, QList<PDFAnnotation>> m_annotationsByPage;
    qreal m_lastDevicePixelRatio = 1.0;
    QPair<int, int> m_lastVisibleRange{-1, -1};
    int m_lastCurrentPage = -1;
    const Poppler::Link* m_hoveredLink = nullptr;
    int m_hoveredLinkPage = -1;
    bool m_isSelectingText = false;
    std::optional<PDFPageHit> m_selectionStart;
    std::optional<PDFPageHit> m_selectionEnd;
};
