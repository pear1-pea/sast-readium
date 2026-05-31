#include "SearchController.h"

SearchController::SearchController(QObject* parent) : QObject(parent) {}

void SearchController::setResults(const QList<SearchResult>& results) {
    m_results = results;
    m_currentIndex = -1;
    emit resultsChanged();
}

void SearchController::clear() {
    m_results.clear();
    m_currentIndex = -1;
    emit resultsChanged();
}

bool SearchController::isEmpty() const { return m_results.isEmpty(); }

void SearchController::highlightCurrent(const SearchResult& result) {
    m_currentIndex = findResultIndex(result);
    emit resultsChanged();
}

QList<SearchResult> SearchController::resultsForPage(int pageNumber) const {
    QList<SearchResult> pageResults;
    for (int i = 0; i < m_results.size(); ++i) {
        SearchResult result = m_results[i];
        if (result.pageNumber == pageNumber) {
            result.isCurrentResult = (i == m_currentIndex);
            pageResults.append(result);
        }
    }
    return pageResults;
}

QHash<int, QList<SearchResult>> SearchController::resultsByPage() const {
    QHash<int, QList<SearchResult>> grouped;
    for (int i = 0; i < m_results.size(); ++i) {
        SearchResult result = m_results[i];
        result.isCurrentResult = (i == m_currentIndex);
        grouped[result.pageNumber].append(result);
    }
    return grouped;
}

int SearchController::findResultIndex(const SearchResult& target) const {
    for (int i = 0; i < m_results.size(); ++i) {
        const SearchResult& r = m_results[i];
        if (r.pageNumber == target.pageNumber &&
            r.startIndex == target.startIndex && r.length == target.length) {
            return i;
        }
    }
    return -1;
}
