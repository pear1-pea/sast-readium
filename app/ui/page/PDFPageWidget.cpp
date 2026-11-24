#include "PDFPageWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QFutureWatcher>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGlobal>
#include <chrono>
#include <cmath>
#include <limits>
#include "managers/StyleManager.h"
#include "utils/LoggingMacros.h"

namespace {

constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 5.0;
constexpr int kZoomQuantize = 10000;
constexpr int kDprQuantize = 100;
constexpr double kMaxRenderDpi = 300.0;

double canonicalZoom(double zoom) {
    const double clamped = qBound(kMinZoom, zoom, kMaxZoom);
    return qRound(clamped * kZoomQuantize) / double(kZoomQuantize);
}

int normalizedRotation(int rotation) {
    const int normalized = ((rotation % 360) + 360) % 360;
    return (normalized % 90 == 0) ? normalized : 0;
}

QPointF rotatePagePoint(const QPointF& point, const QSizeF& pageSize,
                        int rotation) {
    switch (rotation) {
        case 90:
            return QPointF(pageSize.height() - point.y(), point.x());
        case 180:
            return QPointF(pageSize.width() - point.x(),
                           pageSize.height() - point.y());
        case 270:
            return QPointF(point.y(), pageSize.width() - point.x());
        default:
            return point;
    }
}

QPointF unrotatePagePoint(const QPointF& point, const QSizeF& pageSize,
                          int rotation) {
    switch (rotation) {
        case 90:
            return QPointF(point.y(), pageSize.height() - point.x());
        case 180:
            return QPointF(pageSize.width() - point.x(),
                           pageSize.height() - point.y());
        case 270:
            return QPointF(pageSize.width() - point.y(), point.x());
        default:
            return point;
    }
}

void applyRenderHints(Poppler::Document* document) {
    if (!document) {
        return;
    }
    document->setRenderHint(Poppler::Document::Antialiasing, true);
    document->setRenderHint(Poppler::Document::TextAntialiasing, true);
    document->setRenderHint(Poppler::Document::TextHinting, true);
}

QSizeF rotatedSize(const QSizeF& pageSize, int rotation) {
    return (rotation == 90 || rotation == 270)
               ? QSizeF(pageSize.height(), pageSize.width())
               : pageSize;
}

QImage renderWithPage(Poppler::Page* page, double zoom, int rotation,
                      double devicePixelRatio) {
    if (!page) {
        return QImage();
    }
    const double dpi = qMin(72.0 * zoom * devicePixelRatio, kMaxRenderDpi);
    if (!std::isfinite(dpi) || dpi <= 0.0) {
        return QImage();
    }
    return page->renderToImage(
        dpi, dpi, -1, -1, -1, -1,
        static_cast<Poppler::Page::Rotation>(rotation / 90));
}

// Renders with a document owned by the calling worker thread, so the GUI-owned
// Poppler document is never touched off the GUI thread.
QImage renderFromSource(const QString& path, quint64 documentToken,
                        int pageIndex, double zoom, int rotation,
                        double devicePixelRatio) {
    struct ThreadDocument {
        QString path;
        quint64 token = 0;
        std::unique_ptr<Poppler::Document> document;
    };
    thread_local ThreadDocument context;

    if (path.isEmpty()) {
        return QImage();
    }
    if (context.path != path || context.token != documentToken ||
        !context.document) {
        context.document.reset();
        context.path = path;
        context.token = documentToken;
        context.document = Poppler::Document::load(path);
        applyRenderHints(context.document.get());
    }
    if (!context.document || pageIndex < 0 ||
        pageIndex >= context.document->numPages()) {
        return QImage();
    }
    std::unique_ptr<Poppler::Page> page(context.document->page(pageIndex));
    return renderWithPage(page.get(), zoom, rotation, devicePixelRatio);
}

}  // namespace

PDFPageWidget::PDFPageWidget(QWidget* parent)
    : QLabel(parent),
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

PDFPageWidget::~PDFPageWidget() = default;

void PDFPageWidget::setRenderCache(PDFRenderCache* cache) {
    m_renderCache = cache;
}

void PDFPageWidget::setDocumentSource(const QString& documentPath,
                                      quint64 documentToken) {
    if (documentPath == m_documentPath && documentToken == m_documentToken) {
        return;
    }
    m_documentPath = documentPath;
    m_documentToken = documentToken;
    // Invalidate in-flight results from the previous document.
    ++m_requestGeneration;
    m_renderTimer->stop();
    if (m_documentPath.isEmpty()) {
        resetFrame(QStringLiteral("No PDF loaded"));
    }
}

void PDFPageWidget::setPage(std::unique_ptr<Poppler::Page> page,
                            double scaleFactor, int rotation) {
    const bool samePage = m_page && page && m_page->index() == page->index();
    m_page = std::move(page);
    currentScaleFactor = canonicalZoom(scaleFactor);
    currentRotation = normalizedRotation(rotation);
    clearSelection();

    if (!m_page) {
        m_renderTimer->stop();
        ++m_requestGeneration;
        resetFrame(QStringLiteral("No page to render"));
        setFixedSize(minimumSize());
        return;
    }

    if (!samePage) {
        // A different page must not be previewed with the previous page image.
        resetFrame(QStringLiteral("Rendering…"));
    }
    setFixedSize(roundedLogicalSize());
    scheduleRender();
}

void PDFPageWidget::setScaleFactor(double factor) {
    if (!std::isfinite(factor)) {
        return;
    }
    const double zoom = canonicalZoom(factor);
    if (qAbs(zoom - currentScaleFactor) < 1e-9) {
        return;
    }
    currentScaleFactor = zoom;
    if (m_page) {
        setFixedSize(roundedLogicalSize());
    }
    scheduleRender();
    updateSearchResultCoordinates();
    emit scaleChanged(currentScaleFactor);
}

void PDFPageWidget::setRotation(int degrees) {
    const int rotation = normalizedRotation(degrees);
    if (degrees % 90 != 0 || rotation == currentRotation) {
        return;
    }
    currentRotation = rotation;
    clearSelection();
    if (m_page) {
        setFixedSize(roundedLogicalSize());
    }
    scheduleRender();
    updateSearchResultCoordinates();
}

int PDFPageWidget::pageIndex() const { return m_page ? m_page->index() : -1; }

bool PDFPageWidget::isFrameReady() const {
    return m_hasCommittedFrame && m_page && committedMatchesTarget();
}

void PDFPageWidget::renderPage() { scheduleRender(); }

PDFPageWidget::FrameKey PDFPageWidget::targetFrameKey() const {
    FrameKey key;
    key.documentToken = m_documentToken;
    key.pageIndex = pageIndex();
    key.zoomKey = qRound(currentScaleFactor * kZoomQuantize);
    key.rotation = currentRotation;
    key.dprKey = qMax(1, qRound(devicePixelRatioF() * kDprQuantize));
    return key;
}

bool PDFPageWidget::FrameKey::operator==(const FrameKey& other) const {
    return documentToken == other.documentToken &&
           pageIndex == other.pageIndex && zoomKey == other.zoomKey &&
           rotation == other.rotation && dprKey == other.dprKey;
}

bool PDFPageWidget::committedMatchesTarget() const {
    return m_hasCommittedFrame && m_committedKey == targetFrameKey();
}

QSizeF PDFPageWidget::rotatedPageSize() const {
    if (!m_page) {
        return {};
    }
    return rotatedSize(m_page->pageSizeF(), currentRotation);
}

QSizeF PDFPageWidget::logicalPageSize() const {
    return rotatedPageSize() * currentScaleFactor;
}

QSize PDFPageWidget::roundedLogicalSize() const {
    return roundedLogicalSizeFor(qRound(currentScaleFactor * kZoomQuantize));
}

QSize PDFPageWidget::roundedLogicalSizeFor(int zoomKey) const {
    if (!m_page) {
        return {};
    }
    const QSizeF pageSize = rotatedSize(m_page->pageSizeF(), currentRotation);
    return QSize(qMax(1, qRound(pageSize.width() * zoomKey / kZoomQuantize)),
                 qMax(1, qRound(pageSize.height() * zoomKey / kZoomQuantize)));
}

QPointF PDFPageWidget::pageToWidgetPoint(const QPointF& pagePoint) const {
    const QSizeF pageSize = m_page ? m_page->pageSizeF() : QSizeF();
    const QSizeF rotated = rotatedSize(pageSize, currentRotation);
    if (rotated.isEmpty() || width() <= 0 || height() <= 0) {
        return {};
    }
    const QPointF rotatedPoint =
        rotatePagePoint(pagePoint, pageSize, currentRotation);
    return QPointF(rotatedPoint.x() / rotated.width() * width(),
                   rotatedPoint.y() / rotated.height() * height());
}

QPointF PDFPageWidget::widgetToPagePoint(const QPointF& widgetPoint) const {
    const QSizeF pageSize = m_page ? m_page->pageSizeF() : QSizeF();
    const QSizeF rotated = rotatedSize(pageSize, currentRotation);
    if (rotated.isEmpty() || width() <= 0 || height() <= 0) {
        return {};
    }
    const QPointF rotatedPoint(widgetPoint.x() / width() * rotated.width(),
                               widgetPoint.y() / height() * rotated.height());
    return unrotatePagePoint(rotatedPoint, pageSize, currentRotation);
}

QPointF PDFPageWidget::pagePointAtWidgetPoint(
    const QPointF& widgetPoint) const {
    return widgetToPagePoint(widgetPoint);
}

QPointF PDFPageWidget::widgetPointForPagePoint(const QPointF& pagePoint) const {
    return pageToWidgetPoint(pagePoint);
}

QRectF PDFPageWidget::pageRectToWidget(const QRectF& pageRect) const {
    if (pageRect.isEmpty()) {
        return {};
    }
    const QPointF topLeft = pageToWidgetPoint(pageRect.topLeft());
    const QPointF bottomRight = pageToWidgetPoint(pageRect.bottomRight());
    return QRectF(topLeft, bottomRight).normalized();
}

QRectF PDFPageWidget::widgetRectToPage(const QRectF& widgetRect) const {
    if (widgetRect.isEmpty()) {
        return {};
    }
    return QRectF(widgetToPagePoint(widgetRect.topLeft()),
                  widgetToPagePoint(widgetRect.bottomRight()))
        .normalized();
}

void PDFPageWidget::scheduleRender() {
    if (!m_page) {
        return;
    }
    ++m_requestGeneration;
    if (m_hasCommittedFrame) {
        applyPreview();
    } else {
        setText(QStringLiteral("Rendering…"));
    }
    m_renderTimer->start();
}

void PDFPageWidget::onRenderTimeout() { startRender(); }

void PDFPageWidget::startRender() {
    if (!m_page) {
        return;
    }
    const FrameKey key = targetFrameKey();
    if (!key.isValid()) {
        return;
    }

    if (m_renderCache) {
        const PDFRenderCache::CacheKey cacheKey =
            PDFRenderCache::CacheKey::make(key.documentToken, key.pageIndex,
                                           key.zoomKey, key.rotation,
                                           key.dprKey);
        const QPixmap cached = m_renderCache->get(cacheKey);
        if (!cached.isNull()) {
            applyFrame(cached, key);
            return;
        }
    }

    const double zoom = key.zoomKey / double(kZoomQuantize);
    const double dpr = key.dprKey / double(kDprQuantize);
    const quint64 generation = m_requestGeneration;
    const int pageIndex = key.pageIndex;

    if (m_documentPath.isEmpty()) {
        const QImage image =
            renderWithPage(m_page.get(), zoom, key.rotation, dpr);
        if (generation != m_requestGeneration || !(key == targetFrameKey())) {
            return;
        }
        if (image.isNull()) {
            emit renderFailed(pageIndex, QStringLiteral("Page render failed"));
            return;
        }
        applyFrame(QPixmap::fromImage(image), key);
        return;
    }

    const QString path = m_documentPath;
    const quint64 token = key.documentToken;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, key, generation, pageIndex]() {
                const QImage image = watcher->result();
                watcher->deleteLater();
                if (generation != m_requestGeneration ||
                    !(key == targetFrameKey())) {
                    return;
                }
                if (image.isNull()) {
                    emit renderFailed(pageIndex,
                                      QStringLiteral("Page render failed"));
                    return;
                }
                applyFrame(QPixmap::fromImage(image), key);
            });
    watcher->setFuture(QtConcurrent::run(&renderFromSource, path, token,
                                         pageIndex, zoom, key.rotation, dpr));
}

void PDFPageWidget::applyFrame(const QPixmap& pixmap, const FrameKey& key) {
    if (pixmap.isNull()) {
        emit renderFailed(key.pageIndex, QStringLiteral("Empty page image"));
        return;
    }

    m_committedPixmap = pixmap;
    m_committedKey = key;
    m_hasCommittedFrame = true;
    m_displayPixmap = pixmap;
    setText(QString());
    setFixedSize(roundedLogicalSizeFor(key.zoomKey));

    if (m_renderCache) {
        m_renderCache->insert(PDFRenderCache::CacheKey::make(
                                  key.documentToken, key.pageIndex, key.zoomKey,
                                  key.rotation, key.dprKey),
                              pixmap);
    }

    updateSearchResultCoordinates();
    update();
    emit frameCommitted(key.pageIndex);
}

void PDFPageWidget::applyPreview() {
    if (!m_hasCommittedFrame || m_committedPixmap.isNull() || !m_page) {
        return;
    }
    const QSize target = roundedLogicalSize();
    setFixedSize(target);
    m_displayPixmap = m_committedPixmap.scaled(target, Qt::IgnoreAspectRatio,
                                               Qt::FastTransformation);
    update();
}

void PDFPageWidget::resetFrame(const QString& message) {
    m_hasCommittedFrame = false;
    m_committedKey = FrameKey();
    m_committedPixmap = QPixmap();
    m_displayPixmap = QPixmap();
    clearSelection();
    setText(message);
    update();
}

void PDFPageWidget::paintEvent(QPaintEvent* event) {
    // QLabel paints the background and any placeholder text first.
    QLabel::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);

    if (!m_displayPixmap.isNull()) {
        painter.drawPixmap(rect(), m_displayPixmap);
        painter.setPen(QPen(QColor(0, 0, 0, 30), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

    // Overlays describe the committed frame; do not paint them against a
    // preview of a different page, zoom or rotation.
    if (!m_hasCommittedFrame || !committedMatchesTarget()) {
        return;
    }

    if (!m_searchResults.isEmpty()) {
        drawSearchHighlights(painter);
    }
    if (!m_evidenceHighlights.isEmpty()) {
        drawEvidenceHighlights(painter);
    }

    const QRectF selection = selectionPageRect();
    if (m_isSelectingText || !selection.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(64, 128, 255, 90));
        painter.drawRect(pageRectToWidget(selection));
    }
}

void PDFPageWidget::wheelEvent(QWheelEvent* event) {
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QLabel::wheelEvent(event);
        return;
    }

    // Track continuous input instead of treating every non-zero delta as one
    // fixed 15% step.
    double steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps) && !event->pixelDelta().isNull()) {
        steps = event->pixelDelta().y() / 15.0;
    }
    if (qFuzzyIsNull(steps)) {
        event->accept();
        return;
    }

    const QPointF pagePoint = widgetToPagePoint(event->position());
    QPoint viewportPosition = event->position().toPoint();
    // setWidget() reparents the page to the scroll area's viewport, so the
    // scroll area is the grandparent unless this widget is used standalone.
    QWidget* parent = parentWidget();
    QScrollArea* area = qobject_cast<QScrollArea*>(parent);
    if (!area && parent) {
        area = qobject_cast<QScrollArea*>(parent->parentWidget());
    }
    if (area && area->viewport()) {
        viewportPosition =
            area->viewport()->mapFromGlobal(event->globalPosition().toPoint());
    }
    emit zoomAnchorRequested(pagePoint, viewportPosition);

    setScaleFactor(currentScaleFactor * std::pow(1.15, steps));
    event->accept();
}

void PDFPageWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isFrameReady()) {
        setFocus(Qt::MouseFocusReason);
        m_isSelectingText = true;
        m_selectionStartPage = widgetToPagePoint(event->position());
        m_selectionEndPage = m_selectionStartPage;
        update();
        event->accept();
        return;
    }
    QLabel::mousePressEvent(event);
}

void PDFPageWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_isSelectingText) {
        m_selectionEndPage = widgetToPagePoint(event->position());
        update();
        event->accept();
        return;
    }
    QLabel::mouseMoveEvent(event);
}

void PDFPageWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isSelectingText) {
        m_selectionEndPage = widgetToPagePoint(event->position());
        m_isSelectingText = false;
        update();
        event->accept();
        return;
    }
    QLabel::mouseReleaseEvent(event);
}

void PDFPageWidget::changeEvent(QEvent* event) {
    QLabel::changeEvent(event);
    if (event->type() == QEvent::DevicePixelRatioChange && m_page) {
        scheduleRender();
    }
}

void PDFPageWidget::clearSelection() {
    m_isSelectingText = false;
    m_selectionStartPage = QPointF();
    m_selectionEndPage = QPointF();
    update();
}

QRectF PDFPageWidget::selectionPageRect() const {
    if (!m_page) {
        return {};
    }
    if (m_selectionStartPage == m_selectionEndPage && !m_isSelectingText) {
        return {};
    }
    QRectF rect(m_selectionStartPage, m_selectionEndPage);
    rect = rect.normalized();
    return rect.intersected(QRectF(QPointF(0, 0), m_page->pageSizeF()));
}

bool PDFPageWidget::copySelectedText() {
    if (!isFrameReady()) {
        return false;
    }
    const QString text = selectedText();
    if (text.isEmpty()) {
        return false;
    }
    if (QClipboard* clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
        return true;
    }
    return false;
}

QString PDFPageWidget::selectedText() const {
    const QRectF selection = selectionPageRect();
    if (selection.isEmpty() || !m_page) {
        return QString();
    }

    // The selection is stored in unrotated page coordinates, so text boxes
    // must be requested without page rotation.
    const auto textBoxes = m_page->textList(Poppler::Page::Rotate0);
    QString result;
    qreal previousTop = 0.0;
    qreal previousHeight = 0.0;
    bool hasPrevious = false;
    for (const std::unique_ptr<Poppler::TextBox>& textBox : textBoxes) {
        if (!textBox || !textBox->boundingBox().intersects(selection)) {
            continue;
        }

        const QRectF box = textBox->boundingBox();
        if (hasPrevious) {
            const qreal lineThreshold = qMax(previousHeight, box.height());
            if (qAbs(box.top() - previousTop) > lineThreshold * 0.8) {
                result += QChar('\n');
            } else if (!result.endsWith(QChar('\n')) &&
                       !result.endsWith(QChar(' '))) {
                result += QChar(' ');
            }
        }
        result += textBox->text();
        if (textBox->hasSpaceAfter()) {
            result += QChar(' ');
        }
        previousTop = box.top();
        previousHeight = box.height();
        hasPrevious = true;
    }
    return result.trimmed();
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

void PDFPageWidget::setEvidenceHighlights(
    const QVector<Rag::EvidenceHighlightRequest>& highlights) {
    m_evidenceHighlights = highlights;
    update();
}

void PDFPageWidget::clearEvidenceHighlights() {
    m_evidenceHighlights.clear();
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
    if (!m_page || m_searchResults.isEmpty()) {
        return;
    }

    const QSizeF pageSize = m_page->pageSizeF();
    const QSize widgetSize = size();

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

void PDFPageWidget::drawEvidenceHighlights(QPainter& painter) {
    if (!m_page) {
        return;
    }

    painter.save();
    for (const Rag::EvidenceHighlightRequest& request : m_evidenceHighlights) {
        if (request.pageIndex != m_page->index()) {
            continue;
        }
        for (const QRectF& rect : request.originalTopLeftRects) {
            const QRectF widgetRect = pageRectToWidget(rect);
            if (widgetRect.isEmpty()) {
                continue;
            }
            painter.fillRect(widgetRect, request.color);
            if (request.focus) {
                painter.setPen(QPen(request.color.darker(160), 2));
                painter.drawRect(widgetRect);
            }
        }
    }
    painter.restore();
}
