#include "SearchModel.h"
#include <QDebug>
// #include <QtConcurrent> // Not available in this setup
#include <QApplication>
#include <QPointF>
#include <QRectF>
#include <QRegularExpression>
#include <QSize>
#include <QSizeF>
#include <QTransform>
#include <QtGlobal>
#include <cmath>

namespace {

Poppler::Page::SearchFlags searchFlagsFor(const SearchOptions& options) {
    Poppler::Page::SearchFlags flags = Poppler::Page::NoSearchFlags;
    if (!options.caseSensitive) {
        flags |= Poppler::Page::IgnoreCase;
    }
    if (options.wholeWords) {
        flags |= Poppler::Page::WholeWords;
    }
    return flags;
}

QRectF popplerRectToPdfCoordinates(const QRectF& rect, const QSizeF& pageSize) {
    if (rect.isEmpty() || pageSize.isEmpty()) {
        return QRectF();
    }
    return QRectF(rect.left(), pageSize.height() - rect.bottom(), rect.width(),
                  rect.height());
}

QList<QRectF> popplerRectsToPdfCoordinates(const QList<QRectF>& rects,
                                           const QSizeF& pageSize) {
    QList<QRectF> pdfRects;
    pdfRects.reserve(rects.size());
    for (const QRectF& rect : rects) {
        pdfRects.append(popplerRectToPdfCoordinates(rect, pageSize));
    }
    return pdfRects;
}

}  // namespace

SearchModel::SearchModel(QObject* parent)
    : QAbstractListModel(parent),
      m_currentResultIndex(-1),
      m_isSearching(false),
      m_document(nullptr),
      m_searchWatcher(new QFutureWatcher<QList<SearchResult>>(this)),
      m_realTimeSearchTimer(new QTimer(this)),
      m_isRealTimeSearchEnabled(true),
      m_realTimeSearchDelay(300) {
    connect(m_searchWatcher, &QFutureWatcher<QList<SearchResult>>::finished,
            this, &SearchModel::onSearchFinished);

    // Setup real-time search timer
    m_realTimeSearchTimer->setSingleShot(true);
    connect(m_realTimeSearchTimer, &QTimer::timeout, this,
            &SearchModel::performRealTimeSearch);
}

int SearchModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    return m_results.size();
}

QVariant SearchModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_results.size()) {
        return QVariant();
    }

    const SearchResult& result = m_results.at(index.row());

    switch (role) {
        case Qt::DisplayRole:
            return QString("Page %1: %2")
                .arg(result.pageNumber + 1)
                .arg(result.context);
        case PageNumberRole:
            return result.pageNumber;
        case TextRole:
            return result.text;
        case ContextRole:
            return result.context;
        case BoundingRectRole:
            return result.boundingRect;
        case StartIndexRole:
            return result.startIndex;
        case LengthRole:
            return result.length;
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> SearchModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    roles[PageNumberRole] = "pageNumber";
    roles[TextRole] = "text";
    roles[ContextRole] = "context";
    roles[BoundingRectRole] = "boundingRect";
    roles[StartIndexRole] = "startIndex";
    roles[LengthRole] = "length";
    return roles;
}

void SearchModel::startSearch(std::shared_ptr<Poppler::Document> document,
                              const QString& query,
                              const SearchOptions& options) {
    if (m_isSearching) {
        cancelSearch();
    }

    if (!document || query.isEmpty()) {
        emit searchError("Invalid document or empty query");
        return;
    }

    m_document = document;
    m_currentQuery = query;
    m_currentOptions = options;
    m_isSearching = true;
    m_currentResultIndex = -1;

    clearResults();
    emit searchStarted();

    // Start search (synchronous for now)
    performSearch();
    m_isSearching = false;
    emit searchFinished(m_searchResults.size());
}

void SearchModel::startRealTimeSearch(
    std::shared_ptr<Poppler::Document> document, const QString& query,
    const SearchOptions& options) {
    if (!m_isRealTimeSearchEnabled || query.isEmpty()) {
        return;
    }

    // Cancel any pending real-time search
    m_realTimeSearchTimer->stop();

    // Store search parameters
    m_document = document;
    m_currentQuery = query;
    m_currentOptions = options;

    // Start debounced search
    m_realTimeSearchTimer->start(m_realTimeSearchDelay);
}

void SearchModel::clearResults() {
    beginResetModel();
    m_results.clear();
    m_currentResultIndex = -1;
    endResetModel();
    emit resultsCleared();
}

void SearchModel::cancelSearch() {
    if (m_isSearching && !m_searchFuture.isFinished()) {
        m_searchFuture.cancel();
        m_isSearching = false;
        emit searchCancelled();
    }
}

SearchResult SearchModel::getResult(int index) const {
    if (index >= 0 && index < m_results.size()) {
        return m_results.at(index);
    }
    return SearchResult();
}

void SearchModel::setCurrentResultIndex(int index) {
    if (index >= -1 && index < m_results.size() &&
        index != m_currentResultIndex) {
        m_currentResultIndex = index;
        emit currentResultChanged(index);
    }
}

bool SearchModel::hasNext() const {
    return m_currentResultIndex < m_results.size() - 1;
}

bool SearchModel::hasPrevious() const { return m_currentResultIndex > 0; }

SearchResult SearchModel::nextResult() {
    if (hasNext()) {
        setCurrentResultIndex(m_currentResultIndex + 1);
        return m_results.at(m_currentResultIndex);
    }
    return SearchResult();
}

SearchResult SearchModel::previousResult() {
    if (hasPrevious()) {
        setCurrentResultIndex(m_currentResultIndex - 1);
        return m_results.at(m_currentResultIndex);
    }
    return SearchResult();
}

void SearchModel::onSearchFinished() {
    if (m_searchFuture.isCanceled()) {
        m_isSearching = false;
        emit searchCancelled();
        return;
    }

    try {
        QList<SearchResult> results = m_searchFuture.result();

        beginResetModel();
        m_results = results;
        endResetModel();

        if (!m_results.isEmpty()) {
            setCurrentResultIndex(0);
        }

        m_isSearching = false;
        emit searchFinished(m_results.size());

    } catch (const std::exception& e) {
        m_isSearching = false;
        emit searchError(QString("Search failed: %1").arg(e.what()));
    }
}

void SearchModel::performSearch() {
    QList<SearchResult> allResults;

    if (!m_document) {
        return;
    }

    const int pageCount = m_document->numPages();

    for (int i = 0; i < pageCount && !m_searchFuture.isCanceled(); ++i) {
        std::unique_ptr<Poppler::Page> page(m_document->page(i));
        if (page) {
            QList<SearchResult> pageResults =
                searchInPage(page.get(), i, m_currentQuery, m_currentOptions);
            allResults.append(pageResults);

            if (allResults.size() >= m_currentOptions.maxResults) {
                break;
            }
        }
    }

    // Update the model with results
    beginResetModel();
    m_searchResults = allResults;
    m_results = allResults;
    m_currentResultIndex = m_results.isEmpty() ? -1 : 0;
    endResetModel();
}

QList<SearchResult> SearchModel::searchInPage(Poppler::Page* page,
                                              int pageNumber,
                                              const QString& query,
                                              const SearchOptions& options) {
    QList<SearchResult> results;

    if (!page) {
        return results;
    }

    QString pageText = page->text(QRectF());
    if (pageText.isEmpty()) {
        return results;
    }

    if (!options.useRegex) {
        return searchPlainTextInPage(page, pageNumber, query, pageText,
                                     options);
    }

    return searchRegexInPage(page, pageNumber, pageText, options);
}

QList<SearchResult> SearchModel::searchPlainTextInPage(
    Poppler::Page* page, int pageNumber, const QString& query,
    const QString& pageText, const SearchOptions& options) {
    QList<SearchResult> results;

    const QList<QRectF> rects = popplerRectsToPdfCoordinates(
        page->search(query, searchFlagsFor(options)), page->pageSizeF());
    if (rects.isEmpty()) {
        return results;
    }

    // Use page text only for the result list metadata. Poppler's rectangles
    // are the source of truth for the visual match positions.
    const QRegularExpression regex = createSearchRegex(query, options);
    QRegularExpressionMatchIterator iterator = regex.globalMatch(pageText);
    QList<QRegularExpressionMatch> matches;
    while (iterator.hasNext()) {
        matches.append(iterator.next());
    }

    const bool metadataMatchesGeometry = matches.size() == rects.size();
    const int resultCount = qMin(options.maxResults, rects.size());
    for (int i = 0; i < resultCount; ++i) {
        // Do not attach a guessed text offset when Poppler and Qt disagree on
        // occurrence count; keep a unique fallback identity for navigation.
        int startPos = -1 - i;
        int length = query.size();
        QString matchedText = query;
        QString context = query;
        if (metadataMatchesGeometry) {
            const QRegularExpressionMatch& match = matches.at(i);
            startPos = match.capturedStart();
            length = match.capturedLength();
            matchedText = match.captured();
            context = extractContext(pageText, startPos, length);
        }

        SearchResult result(pageNumber, matchedText, context, rects.at(i),
                            startPos, length);
        results.append(result);
    }

    return results;
}

QList<SearchResult> SearchModel::searchRegexInPage(
    Poppler::Page* page, int pageNumber, const QString& pageText,
    const SearchOptions& options) {
    QList<SearchResult> results;
    const QRegularExpression regex = createSearchRegex(m_currentQuery, options);
    QRegularExpressionMatchIterator iterator = regex.globalMatch(pageText);

    while (iterator.hasNext() && results.size() < options.maxResults) {
        QRegularExpressionMatch match = iterator.next();
        int startPos = match.capturedStart();
        int length = match.capturedLength();
        QString matchedText = match.captured();

        // Extract context around the match
        QString context = extractContext(pageText, startPos, length);

        // Get bounding rectangle for the matched text
        const QList<QRectF> rects = popplerRectsToPdfCoordinates(
            page->search(matchedText), page->pageSizeF());
        const QRectF boundingRect = rects.isEmpty() ? QRectF() : rects.first();

        SearchResult result(pageNumber, matchedText, context, boundingRect,
                            startPos, length);
        results.append(result);
    }

    return results;
}

QString SearchModel::extractContext(const QString& pageText, int position,
                                    int length, int contextLength) {
    int start = qMax(0, position - contextLength);
    int end = qMin(pageText.length(), position + length + contextLength);

    QString context = pageText.mid(start, end - start);

    // Add ellipsis if we truncated
    if (start > 0) {
        context = "..." + context;
    }
    if (end < pageText.length()) {
        context = context + "...";
    }

    return context.simplified();  // Remove extra whitespace
}

QRegularExpression SearchModel::createSearchRegex(
    const QString& query, const SearchOptions& options) {
    QString pattern = query;

    if (!options.useRegex) {
        pattern = QRegularExpression::escape(pattern);
    }

    if (options.wholeWords) {
        pattern = "\\b" + pattern + "\\b";
    }

    QRegularExpression::PatternOptions regexOptions =
        QRegularExpression::MultilineOption;
    if (!options.caseSensitive) {
        regexOptions |= QRegularExpression::CaseInsensitiveOption;
    }

    return QRegularExpression(pattern, regexOptions);
}

// Real-time search implementation
void SearchModel::performRealTimeSearch() {
    if (!m_document || m_currentQuery.isEmpty()) {
        return;
    }

    emit realTimeSearchStarted();

    QList<SearchResult> allResults;
    const int pageCount = m_document->numPages();

    for (int i = 0; i < pageCount; ++i) {
        std::unique_ptr<Poppler::Page> page(m_document->page(i));
        if (page) {
            QList<SearchResult> pageResults =
                searchInPage(page.get(), i, m_currentQuery, m_currentOptions);
            allResults.append(pageResults);

            // Emit progress and partial results for real-time feedback
            emit realTimeSearchProgress(i + 1, pageCount);
            if (!allResults.isEmpty()) {
                emit realTimeResultsUpdated(allResults);
            }

            // Limit results for performance
            if (allResults.size() >= m_currentOptions.maxResults) {
                break;
            }
        }
    }

    // Update the model with final results
    beginResetModel();
    m_searchResults = allResults;
    m_results = allResults;  // Keep both for compatibility
    endResetModel();

    emit searchFinished(allResults.size());
}

// SearchResult coordinate transformation implementation
void SearchResult::transformToWidgetCoordinates(double scaleFactor,
                                                int rotation,
                                                const QSizeF& pageSize,
                                                const QSize& widgetSize) {
    if (boundingRect.isEmpty()) {
        widgetRect = QRectF();
        return;
    }

    const QSizeF rotatedPageSize =
        (rotation == 90 || rotation == 270)
            ? QSizeF(pageSize.height(), pageSize.width())
            : pageSize;
    const double scaleX = widgetSize.width() / rotatedPageSize.width();
    const double scaleY = widgetSize.height() / rotatedPageSize.height();
    Q_UNUSED(scaleFactor)
    const double uniformScale = qMin(scaleX, scaleY);
    const QPointF offset(
        (widgetSize.width() - rotatedPageSize.width() * uniformScale) / 2.0,
        (widgetSize.height() - rotatedPageSize.height() * uniformScale) / 2.0);

    QTransform transform;
    switch (rotation) {
        case 90:
            transform.translate(pageSize.height(), 0.0);
            transform.rotate(90);
            break;
        case 180:
            transform.translate(pageSize.width(), pageSize.height());
            transform.rotate(180);
            break;
        case 270:
            transform.translate(0.0, pageSize.width());
            transform.rotate(270);
            break;
        default:
            break;
    }

    QRectF qtRect(boundingRect.left(),
                  pageSize.height() - boundingRect.bottom(),
                  boundingRect.width(), boundingRect.height());
    if (rotation != 0) {
        qtRect = transform.mapRect(qtRect);
    }
    widgetRect =
        QRectF(qtRect.left() * uniformScale + offset.x(),
               qtRect.top() * uniformScale + offset.y(),
               qtRect.width() * uniformScale, qtRect.height() * uniformScale);
}
