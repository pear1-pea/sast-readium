#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QSlider>
#include <QProgressBar>
#include <QListWidget>
#include <QTreeWidget>
#include <QSplitter>
#include <QTimer>
#include <QCompleter>
#include <QStringListModel>
#include <QRegularExpression>
#include <QSettings>
#include <QMutex>

/**
 * Search types supported
 */
enum class SearchType {
    PlainText,          // Simple text search
    RegularExpression,  // Regex search
    Wildcard,          // Wildcard patterns
    Fuzzy,             // Fuzzy matching
    Phonetic,          // Phonetic matching
    Semantic           // Semantic search
};

/**
 * Search scope options
 */
enum class SearchScope {
    CurrentDocument,    // Search in current document only
    AllOpenDocuments,   // Search in all open documents
    DocumentCollection, // Search in document collection
    Annotations,        // Search in annotations only
    Bookmarks,         // Search in bookmarks only
    Metadata           // Search in document metadata
};

/**
 * Search result item
 */
struct AdvancedSearchResult {
    QString documentPath;
    QString documentTitle;
    int pageNumber;
    QString context;
    QString matchedText;
    QRectF boundingRect;
    double relevanceScore;
    qint64 timestamp;
    SearchType searchType;
    
    AdvancedSearchResult()
        : pageNumber(-1), relevanceScore(0.0), timestamp(0)
        , searchType(SearchType::PlainText) {}
};

/**
 * Search query with advanced options
 */
struct SearchQuery {
    QString text;
    SearchType type;
    SearchScope scope;
    bool caseSensitive;
    bool wholeWords;
    bool useRegex;
    bool searchBackwards;
    int maxResults;
    QStringList includePages;
    QStringList excludePages;
    QString dateRange;
    double fuzzyThreshold;
    
    SearchQuery()
        : type(SearchType::PlainText)
        , scope(SearchScope::CurrentDocument)
        , caseSensitive(false)
        , wholeWords(false)
        , useRegex(false)
        , searchBackwards(false)
        , maxResults(100)
        , fuzzyThreshold(0.8) {}
    
    bool isValid() const {
        return !text.isEmpty();
    }
    
    QString toDisplayString() const {
        return QString("%1 (%2)").arg(text).arg(static_cast<int>(type));
    }
};

/**
 * Search history manager
 */
class SearchHistoryManager : public QObject
{
    Q_OBJECT

public:
    explicit SearchHistoryManager(QObject* parent = nullptr);
    
    void addQuery(const SearchQuery& query);
    void removeQuery(const QString& queryText);
    void clearHistory();
    
    QList<SearchQuery> getHistory() const;
    QStringList getQueryTexts() const;
    SearchQuery getLastQuery() const;
    
    void setMaxHistorySize(int size);
    int getMaxHistorySize() const { return m_maxSize; }
    
    void loadHistory();
    void saveHistory();

signals:
    void historyChanged();

private:
    QList<SearchQuery> m_history;
    int m_maxSize;
    mutable QMutex m_mutex;
    QSettings* m_settings;
    
    void enforceMaxSize();
    QVariantMap queryToVariant(const SearchQuery& query) const;
    SearchQuery variantToQuery(const QVariantMap& data) const;
};

/**
 * Advanced search widget with comprehensive search capabilities
 */
class AdvancedSearchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AdvancedSearchWidget(QWidget* parent = nullptr);
    ~AdvancedSearchWidget();

    // Search operations
    void startSearch(const SearchQuery& query);
    void stopSearch();
    void clearResults();
    
    // Query management
    void setQuery(const SearchQuery& query);
    SearchQuery getCurrentQuery() const;
    void resetQuery();
    
    // Results
    QList<AdvancedSearchResult> getResults() const;
    AdvancedSearchResult getSelectedResult() const;
    int getResultCount() const;
    
    // Configuration
    void setSearchScope(SearchScope scope);
    void setMaxResults(int maxResults);
    void enableRealTimeSearch(bool enable);
    void setSearchDelay(int milliseconds);
    
    // UI state
    void showAdvancedOptions(bool show);
    void setCompactMode(bool compact);
    
    // Settings
    void loadSettings();
    void saveSettings();

public slots:
    void focusSearchField();
    void selectNextResult();
    void selectPreviousResult();
    void goToSelectedResult();

signals:
    void searchStarted(const SearchQuery& query);
    void searchFinished(int resultCount);
    void searchProgress(int current, int total);
    void resultSelected(const AdvancedSearchResult& result);
    void queryChanged(const SearchQuery& query);

private slots:
    void onSearchTextChanged();
    void onSearchButtonClicked();
    void onStopButtonClicked();
    void onClearButtonClicked();
    void onAdvancedToggled(bool show);
    void onSearchTypeChanged();
    void onScopeChanged();
    void onOptionsChanged();
    void onResultItemClicked();
    void onResultItemDoubleClicked();
    void onSearchTimer();
    void onHistoryItemSelected();

private:
    // UI Components
    QVBoxLayout* m_mainLayout;
    QHBoxLayout* m_searchLayout;
    QGridLayout* m_advancedLayout;
    
    // Search input
    QLineEdit* m_searchEdit;
    QComboBox* m_searchTypeCombo;
    QPushButton* m_searchButton;
    QPushButton* m_stopButton;
    QPushButton* m_clearButton;
    QPushButton* m_advancedButton;
    
    // Advanced options
    QGroupBox* m_advancedGroup;
    QComboBox* m_scopeCombo;
    QCheckBox* m_caseSensitiveCheck;
    QCheckBox* m_wholeWordsCheck;
    QCheckBox* m_regexCheck;
    QCheckBox* m_backwardsCheck;
    QSpinBox* m_maxResultsSpin;
    QSlider* m_fuzzySlider;
    QLabel* m_fuzzyLabel;
    
    // Results display
    QSplitter* m_resultsSplitter;
    QTreeWidget* m_resultsTree;
    QLabel* m_resultsLabel;
    QProgressBar* m_progressBar;
    
    // History
    QComboBox* m_historyCombo;
    SearchHistoryManager* m_historyManager;
    
    // Search management
    QTimer* m_searchTimer;
    QTimer* m_realTimeTimer;
    bool m_isSearching;
    bool m_realTimeSearchEnabled;
    int m_searchDelay;
    
    // Results
    QList<AdvancedSearchResult> m_results;
    mutable QMutex m_resultsMutex;
    
    // Settings
    QSettings* m_settings;
    bool m_compactMode;
    bool m_advancedVisible;
    
    // Helper methods
    void setupUI();
    void setupConnections();
    void setupCompleter();
    void updateUI();
    void updateResultsDisplay();
    void updateProgressDisplay();
    
    // Search execution
    void executeSearch();
    QList<AdvancedSearchResult> performTextSearch(const SearchQuery& query);
    QList<AdvancedSearchResult> performRegexSearch(const SearchQuery& query);
    QList<AdvancedSearchResult> performFuzzySearch(const SearchQuery& query);
    
    // Result processing
    void addResult(const AdvancedSearchResult& result);
    void sortResults();
    double calculateRelevanceScore(const AdvancedSearchResult& result, const SearchQuery& query);
    
    // UI helpers
    void populateSearchTypes();
    void populateSearchScopes();
    void updateAdvancedOptionsVisibility();
    void updateButtonStates();
    QString formatResultText(const AdvancedSearchResult& result) const;
    QIcon getIconForSearchType(SearchType type) const;
    
    // Validation
    bool validateQuery(const SearchQuery& query) const;
    QString getQueryValidationError(const SearchQuery& query) const;
};
