#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include "../model/SearchModel.h"

class SearchController : public QObject {
    Q_OBJECT

public:
    explicit SearchController(QObject* parent = nullptr);

    // Search state management
    void setResults(const QList<SearchResult>& results);
    void clear();
    void highlightCurrent(const SearchResult& result);

    // Query
    QList<SearchResult> resultsForPage(int pageNumber) const;
    QHash<int, QList<SearchResult>> resultsByPage() const;
    bool isEmpty() const;
    int currentResultIndex() const { return m_currentIndex; }

signals:
    void resultsChanged();

private:
    int findResultIndex(const SearchResult& target) const;

    QList<SearchResult> m_results;
    int m_currentIndex = -1;
};
