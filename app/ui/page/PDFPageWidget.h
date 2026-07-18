#pragma once

#include <poppler/qt6/poppler-qt6.h>
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QTimer>
#include "../viewer/PDFRenderCache.h"
#include "model/SearchModel.h"

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QWheelEvent;

class PDFPageWidget : public QLabel {
    Q_OBJECT

public:
    explicit PDFPageWidget(QWidget* parent = nullptr);

    void setPage(Poppler::Page* page, double scaleFactor = 1.0,
                 int rotation = 0);
    void setScaleFactor(double factor);
    void setRotation(int degrees);
    double getScaleFactor() const { return currentScaleFactor; }
    int getRotation() const { return currentRotation; }
    void renderPage();

    // Fast scaling: scale existing pixmap without re-rendering PDF
    void quickScale(double factor);

    // Cache management
    void setRenderCache(PDFRenderCache* cache);

    // Search highlight management
    void setSearchResults(const QList<SearchResult>& results);
    void clearSearchHighlights();
    void setCurrentSearchResult(int index);
    void updateHighlightColors(const QColor& normalColor,
                               const QColor& currentColor);
    bool hasSearchResults() const { return !m_searchResults.isEmpty(); }

signals:
    void scaleChanged(double scale);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onRenderTimeout();

private:
    Poppler::Page* currentPage;
    double currentScaleFactor;
    int currentRotation;
    QPixmap renderedPixmap;
    QPixmap originalPixmap;
    double originalScaleFactor;

    // Search highlighting members
    QList<SearchResult> m_searchResults;
    int m_currentSearchResultIndex;
    QColor m_normalHighlightColor;
    QColor m_currentHighlightColor;

    // Rendering optimization
    QTimer* m_renderTimer;
    PDFRenderCache* m_renderCache;

    static constexpr int RENDER_DELAY_MS = 100;

    // Helper methods for highlighting
    void drawSearchHighlights(QPainter& painter);
    void updateSearchResultCoordinates();
};
