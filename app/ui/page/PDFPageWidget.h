#pragma once

#include <poppler/qt6/poppler-qt6.h>
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <QVector>
#include <cstdint>
#include <memory>
#include "../viewer/PDFRenderCache.h"
#include "model/SearchModel.h"
#include "rag/domain/EvidenceHighlightRequest.h"

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QWheelEvent;

/**
 * Single-page display widget.
 *
 * The widget owns the Poppler page it displays. Rendering runs in a worker
 * thread with an independent document instance; results are committed only
 * when their page, zoom, rotation and document token still match the target.
 * Text selection is stored in unrotated page coordinates so it survives zoom
 * changes and can be transformed back to widget space for painting.
 */
class PDFPageWidget : public QLabel {
    Q_OBJECT

public:
    explicit PDFPageWidget(QWidget* parent = nullptr);
    ~PDFPageWidget() override;

    // Document source used by the background renderer. An empty path disables
    // asynchronous rendering and falls back to the owned page on the GUI
    // thread.
    void setDocumentSource(const QString& documentPath, quint64 documentToken);

    void setPage(std::unique_ptr<Poppler::Page> page, double scaleFactor = 1.0,
                 int rotation = 0);
    void setScaleFactor(double factor);
    void setRotation(int degrees);
    double getScaleFactor() const { return currentScaleFactor; }
    int getRotation() const { return currentRotation; }
    int pageIndex() const;
    void renderPage();

    // True when the visible frame belongs to the current page, zoom, rotation
    // and document. Selection and copy are only available in this state.
    bool isFrameReady() const;

    // Page coordinates use points, top-left origin, before rotation.
    QPointF pagePointAtWidgetPoint(const QPointF& widgetPoint) const;
    QPointF widgetPointForPagePoint(const QPointF& pagePoint) const;

    // Cache management
    void setRenderCache(PDFRenderCache* cache);

    // Search highlight management
    void setSearchResults(const QList<SearchResult>& results);
    void clearSearchHighlights();
    void setCurrentSearchResult(int index);
    void updateHighlightColors(const QColor& normalColor,
                               const QColor& currentColor);
    bool hasSearchResults() const { return !m_searchResults.isEmpty(); }
    void setEvidenceHighlights(
        const QVector<Rag::EvidenceHighlightRequest>& highlights);
    void clearEvidenceHighlights();
    bool copySelectedText();
    void clearSelection();

signals:
    void scaleChanged(double scale);
    void zoomAnchorRequested(const QPointF& pagePoint,
                             const QPoint& viewportPosition);
    void frameCommitted(int pageIndex);
    void renderFailed(int pageIndex, const QString& error);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onRenderTimeout();

private:
    struct FrameKey {
        quint64 documentToken = 0;
        int pageIndex = -1;
        int zoomKey = 0;
        int rotation = 0;
        int dprKey = 0;

        bool operator==(const FrameKey& other) const;
        bool isValid() const { return pageIndex >= 0 && zoomKey > 0; }
    };

    FrameKey targetFrameKey() const;
    bool committedMatchesTarget() const;
    QSizeF rotatedPageSize() const;
    QSizeF logicalPageSize() const;
    QSize roundedLogicalSize() const;
    QSize roundedLogicalSizeFor(int zoomKey) const;
    QPointF pageToWidgetPoint(const QPointF& pagePoint) const;
    QPointF widgetToPagePoint(const QPointF& widgetPoint) const;
    QRectF pageRectToWidget(const QRectF& pageRect) const;
    QRectF widgetRectToPage(const QRectF& widgetRect) const;
    QRectF selectionPageRect() const;

    void scheduleRender();
    void startRender();
    void applyFrame(const QPixmap& pixmap, const FrameKey& key);
    void applyPreview();
    void resetFrame(const QString& message);

    // Helper methods for highlighting
    void drawSearchHighlights(QPainter& painter);
    void drawEvidenceHighlights(QPainter& painter);
    void updateSearchResultCoordinates();
    QString selectedText() const;

    std::unique_ptr<Poppler::Page> m_page;
    QString m_documentPath;
    quint64 m_documentToken = 0;
    double currentScaleFactor = 1.0;
    int currentRotation = 0;

    bool m_hasCommittedFrame = false;
    FrameKey m_committedKey;
    QPixmap m_committedPixmap;
    QPixmap m_displayPixmap;

    quint64 m_requestGeneration = 0;

    // Search highlighting members
    QList<SearchResult> m_searchResults;
    int m_currentSearchResultIndex;
    QColor m_normalHighlightColor;
    QColor m_currentHighlightColor;
    QVector<Rag::EvidenceHighlightRequest> m_evidenceHighlights;
    bool m_isSelectingText = false;
    QPointF m_selectionStartPage;
    QPointF m_selectionEndPage;

    QTimer* m_renderTimer;
    PDFRenderCache* m_renderCache;

    static constexpr int RENDER_DELAY_MS = 100;
};
