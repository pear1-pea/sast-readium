#include "PDFPageWidget.h"

#include <QPainter>
#include <QPen>
#include <QWheelEvent>
#include <QtGlobal>
#include <chrono>
#include <stdexcept>
#include "managers/StyleManager.h"
#include "utils/LoggingMacros.h"

PDFPageWidget::PDFPageWidget(QWidget* parent)
    : QLabel(parent),
      currentPage(nullptr),
      currentScaleFactor(1.0),
      currentRotation(0),
      m_currentSearchResultIndex(-1),
      m_normalHighlightColor(QColor(255, 255, 0, 100)),
      m_currentHighlightColor(QColor(255, 165, 0, 150)),
      m_renderTimer(nullptr),
      m_renderCache(nullptr) {
    static int s_widgetConstructionCount = 0;
    const int widgetIndex = ++s_widgetConstructionCount;
    const bool shouldProfileWidget =
        widgetIndex <= 5 || (widgetIndex % 50) == 0;
    auto ctorStart = std::chrono::steady_clock::now();
    auto checkpoint = ctorStart;

    setAlignment(Qt::AlignCenter);
    setMinimumSize(200, 200);
    setObjectName("pdfPage");
    if (shouldProfileWidget) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - checkpoint)
                           .count();
        LOG_INFO(
            "[continuous-prof] widget ctor phase=index={} phase=base_setup "
            "elapsed_ms={}",
            widgetIndex, elapsed);
        checkpoint = std::chrono::steady_clock::now();
    }

    // Apply modern page style (non-test environments only)
    try {
        setStyleSheet(QString(R"(
            QLabel#pdfPage {
                background-color: white;
                border: 1px solid %1;
                border-radius: 8px;
                margin: 12px;
                padding: 8px;
            }
        )")
                          .arg(STYLE.borderColor().name()));
    } catch (...) {
        // Ignore style errors in test environments
        setStyleSheet(
            "QLabel#pdfPage { background-color: white; border: 1px solid gray; "
            "}");
    }
    if (shouldProfileWidget) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - checkpoint)
                           .count();
        LOG_INFO(
            "[continuous-prof] widget ctor phase=index={} phase=stylesheet "
            "elapsed_ms={}",
            widgetIndex, elapsed);
        checkpoint = std::chrono::steady_clock::now();
    }

    setText("No PDF loaded");

    // Add shadow effect
    QGraphicsDropShadowEffect* shadowEffect =
        new QGraphicsDropShadowEffect(this);
    shadowEffect->setBlurRadius(15);
    shadowEffect->setColor(QColor(0, 0, 0, 50));
    shadowEffect->setOffset(0, 4);
    setGraphicsEffect(shadowEffect);
    if (shouldProfileWidget) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - checkpoint)
                           .count();
        LOG_INFO(
            "[continuous-prof] widget ctor phase=index={} phase=shadow "
            "elapsed_ms={}",
            widgetIndex, elapsed);
        checkpoint = std::chrono::steady_clock::now();
    }

    // Debounced render timer
    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(RENDER_DELAY_MS);
    connect(m_renderTimer, &QTimer::timeout, this,
            &PDFPageWidget::onRenderTimeout);

    if (shouldProfileWidget) {
        auto phaseElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - checkpoint)
                .count();
        auto totalElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - ctorStart)
                .count();
        LOG_INFO(
            "[continuous-prof] widget ctor phase=index={} phase=timer "
            "elapsed_ms={}",
            widgetIndex, phaseElapsed);
        LOG_INFO("[continuous-prof] widget ctor total index={} elapsed_ms={}",
                 widgetIndex, totalElapsed);
    }
}

void PDFPageWidget::setRenderCache(PDFRenderCache* cache) {
    m_renderCache = cache;
}

void PDFPageWidget::setPage(Poppler::Page* page, double scaleFactor,
                            int rotation) {
    currentPage = page;
    currentScaleFactor = scaleFactor;
    currentRotation = rotation;
    renderPage();
}

void PDFPageWidget::setScaleFactor(double factor) {
    if (factor != currentScaleFactor) {
        currentScaleFactor = factor;
        renderPage();
        emit scaleChanged(factor);
    }
}

void PDFPageWidget::quickScale(double factor) {
    if (originalPixmap.isNull()) {
        return;
    }

    currentScaleFactor = factor;

    double scaleRatio = factor / originalScaleFactor;

    QSize originalSize = originalPixmap.size();
    QSize newSize = QSize(static_cast<int>(originalSize.width() * scaleRatio),
                          static_cast<int>(originalSize.height() * scaleRatio));

    QPixmap scaledPixmap = originalPixmap.scaled(newSize, Qt::KeepAspectRatio,
                                                 Qt::FastTransformation);

    setPixmap(scaledPixmap);
    setFixedSize(scaledPixmap.size() / scaledPixmap.devicePixelRatio());
}

void PDFPageWidget::setRotation(int degrees) {
    degrees = ((degrees % 360) + 360) % 360;
    if (degrees != currentRotation) {
        currentRotation = degrees;
        renderPage();
    }
}

void PDFPageWidget::renderPage() {
    if (!currentPage) {
        setText("No page to render");
        return;
    }

    // Check cache
    if (m_renderCache) {
        PDFRenderCache::CacheKey key{currentPage->index(), currentScaleFactor,
                                     currentRotation};

        if (m_renderCache->contains(key)) {
            QPixmap cached = m_renderCache->get(key);
            if (!cached.isNull()) {
                renderedPixmap = cached;
                originalPixmap = cached;
                originalScaleFactor = currentScaleFactor;
                setPixmap(cached);
                setFixedSize(cached.size() / cached.devicePixelRatio());
                return;
            }
        }
    }

    // Cache miss, start debounce timer
    m_renderTimer->start();
}

void PDFPageWidget::onRenderTimeout() {
    if (!currentPage) {
        return;
    }

    const int pageIndex = currentPage->index();
    const bool shouldProfileRender = pageIndex < 3;
    auto totalStart = std::chrono::steady_clock::now();

    try {
        double devicePixelRatio = devicePixelRatioF();
        double baseDpi = 72.0 * currentScaleFactor;
        double optimizedDpi = baseDpi * devicePixelRatio;

        optimizedDpi = qMin(optimizedDpi, 300.0);

        QSizeF pageSize = currentPage->pageSizeF();
        Q_UNUSED(pageSize)

        auto renderStart = std::chrono::steady_clock::now();
        QImage image = currentPage->renderToImage(
            optimizedDpi, optimizedDpi, -1, -1, -1, -1,
            static_cast<Poppler::Page::Rotation>(currentRotation / 90));
        auto renderMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - renderStart)
                            .count();
        if (image.isNull()) {
            setText("Failed to render page");
            return;
        }

        auto applyStart = std::chrono::steady_clock::now();
        renderedPixmap = QPixmap::fromImage(image);
        renderedPixmap.setDevicePixelRatio(devicePixelRatio);

        originalPixmap = renderedPixmap;
        originalScaleFactor = currentScaleFactor;

        setPixmap(renderedPixmap);
        setFixedSize(renderedPixmap.size() / renderedPixmap.devicePixelRatio());

        if (m_renderCache) {
            PDFRenderCache::CacheKey key{currentPage->index(),
                                         currentScaleFactor, currentRotation};
            m_renderCache->insert(key, renderedPixmap);
        }
        auto applyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - applyStart)
                           .count();

        if (shouldProfileRender) {
            auto totalMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - totalStart)
                    .count();
            LOG_INFO(
                "[continuous-prof] render page={} zoom={:.3f} rotation={} "
                "render_ms={} apply_ms={} total_ms={}",
                pageIndex, currentScaleFactor, currentRotation, renderMs,
                applyMs, totalMs);
        }

    } catch (const std::exception& e) {
        setText(QString("Render error: %1").arg(e.what()));
        qDebug() << "Page render error:" << e.what();
    } catch (...) {
        setText("Unknown render error");
        qDebug() << "Unknown page render error";
    }
}

void PDFPageWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);

    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);

    if (!renderedPixmap.isNull()) {
        QRect pixmapRect = rect();
        painter.setPen(QPen(QColor(0, 0, 0, 30), 1));
        painter.drawRect(pixmapRect.adjusted(0, 0, -1, -1));
    }

    if (!m_searchResults.isEmpty()) {
        drawSearchHighlights(painter);
    }

    QLabel::paintEvent(event);
}

void PDFPageWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        if (delta != 0) {
            double scaleDelta = delta > 0 ? 1.15 : (1.0 / 1.15);
            double newScale = currentScaleFactor * scaleDelta;

            newScale = qBound(0.1, newScale, 5.0);

            setScaleFactor(newScale);
        }
        event->accept();
    } else {
        QLabel::wheelEvent(event);
    }
}

void PDFPageWidget::mousePressEvent(QMouseEvent* event) {
    QLabel::mousePressEvent(event);
}

void PDFPageWidget::mouseMoveEvent(QMouseEvent* event) {
    QLabel::mouseMoveEvent(event);
}

void PDFPageWidget::mouseReleaseEvent(QMouseEvent* event) {
    QLabel::mouseReleaseEvent(event);
}

// Search highlighting implementation
void PDFPageWidget::setSearchResults(const QList<SearchResult>& results) {
    m_searchResults = results;
    m_currentSearchResultIndex = -1;
    updateSearchResultCoordinates();
    update();
}

void PDFPageWidget::clearSearchHighlights() {
    m_searchResults.clear();
    m_currentSearchResultIndex = -1;
    update();
}

void PDFPageWidget::setCurrentSearchResult(int index) {
    if (index >= 0 && index < m_searchResults.size()) {
        if (m_currentSearchResultIndex >= 0 &&
            m_currentSearchResultIndex < m_searchResults.size()) {
            m_searchResults[m_currentSearchResultIndex].isCurrentResult = false;
        }

        m_currentSearchResultIndex = index;
        m_searchResults[index].isCurrentResult = true;

        update();
    }
}

void PDFPageWidget::updateHighlightColors(const QColor& normalColor,
                                          const QColor& currentColor) {
    m_normalHighlightColor = normalColor;
    m_currentHighlightColor = currentColor;
    update();
}

void PDFPageWidget::updateSearchResultCoordinates() {
    if (!currentPage || m_searchResults.isEmpty()) {
        return;
    }

    QSizeF pageSize = currentPage->pageSizeF();
    QSize widgetSize = size();

    for (SearchResult& result : m_searchResults) {
        result.transformToWidgetCoordinates(currentScaleFactor, currentRotation,
                                            pageSize, widgetSize);
    }
}

void PDFPageWidget::drawSearchHighlights(QPainter& painter) {
    if (m_searchResults.isEmpty()) {
        return;
    }

    painter.save();

    for (const SearchResult& result : m_searchResults) {
        if (!result.isValidForHighlight() || result.widgetRect.isEmpty()) {
            continue;
        }

        QColor highlightColor = result.isCurrentResult ? m_currentHighlightColor
                                                       : m_normalHighlightColor;

        painter.fillRect(result.widgetRect, highlightColor);

        if (result.isCurrentResult) {
            painter.setPen(QPen(highlightColor.darker(150), 2));
            painter.drawRect(result.widgetRect);
        }
    }

    painter.restore();
}
