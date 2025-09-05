#include "AdvancedSearchWidget.h"
#include <QApplication>
#include <QDebug>
#include <QMutexLocker>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextDocument>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

// SearchHistoryManager Implementation
SearchHistoryManager::SearchHistoryManager(QObject* parent)
    : QObject(parent)
    , m_maxSize(50)
    , m_settings(nullptr)
{
    m_settings = new QSettings("SAST", "Readium-SearchHistory", this);
    loadHistory();
}

void SearchHistoryManager::addQuery(const SearchQuery& query)
{
    if (!query.isValid()) return;
    
    QMutexLocker locker(&m_mutex);
    
    // Remove existing identical query
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&query](const SearchQuery& q) { return q.text == query.text; });
    if (it != m_history.end()) {
        m_history.erase(it);
    }
    
    // Add to front
    m_history.prepend(query);
    
    // Enforce size limit
    enforceMaxSize();
    
    emit historyChanged();
}

QList<SearchQuery> SearchHistoryManager::getHistory() const
{
    QMutexLocker locker(&m_mutex);
    return m_history;
}

QStringList SearchHistoryManager::getQueryTexts() const
{
    QMutexLocker locker(&m_mutex);
    QStringList texts;
    for (const SearchQuery& query : m_history) {
        texts.append(query.text);
    }
    return texts;
}

void SearchHistoryManager::loadHistory()
{
    if (!m_settings) return;
    
    QMutexLocker locker(&m_mutex);
    m_history.clear();
    
    int size = m_settings->beginReadArray("searchHistory");
    for (int i = 0; i < size; ++i) {
        m_settings->setArrayIndex(i);
        QVariantMap data = m_settings->value("query").toMap();
        if (!data.isEmpty()) {
            SearchQuery query = variantToQuery(data);
            if (query.isValid()) {
                m_history.append(query);
            }
        }
    }
    m_settings->endArray();
}

void SearchHistoryManager::saveHistory()
{
    if (!m_settings) return;
    
    QMutexLocker locker(&m_mutex);
    
    m_settings->beginWriteArray("searchHistory");
    for (int i = 0; i < m_history.size(); ++i) {
        m_settings->setArrayIndex(i);
        QVariantMap data = queryToVariant(m_history[i]);
        m_settings->setValue("query", data);
    }
    m_settings->endArray();
    m_settings->sync();
}

QVariantMap SearchHistoryManager::queryToVariant(const SearchQuery& query) const
{
    QVariantMap data;
    data["text"] = query.text;
    data["type"] = static_cast<int>(query.type);
    data["scope"] = static_cast<int>(query.scope);
    data["caseSensitive"] = query.caseSensitive;
    data["wholeWords"] = query.wholeWords;
    data["useRegex"] = query.useRegex;
    data["maxResults"] = query.maxResults;
    data["fuzzyThreshold"] = query.fuzzyThreshold;
    return data;
}

SearchQuery SearchHistoryManager::variantToQuery(const QVariantMap& data) const
{
    SearchQuery query;
    query.text = data["text"].toString();
    query.type = static_cast<SearchType>(data["type"].toInt());
    query.scope = static_cast<SearchScope>(data["scope"].toInt());
    query.caseSensitive = data["caseSensitive"].toBool();
    query.wholeWords = data["wholeWords"].toBool();
    query.useRegex = data["useRegex"].toBool();
    query.maxResults = data["maxResults"].toInt();
    query.fuzzyThreshold = data["fuzzyThreshold"].toDouble();
    return query;
}

// AdvancedSearchWidget Implementation
AdvancedSearchWidget::AdvancedSearchWidget(QWidget* parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_searchLayout(nullptr)
    , m_advancedLayout(nullptr)
    , m_searchEdit(nullptr)
    , m_searchTypeCombo(nullptr)
    , m_searchButton(nullptr)
    , m_stopButton(nullptr)
    , m_clearButton(nullptr)
    , m_advancedButton(nullptr)
    , m_advancedGroup(nullptr)
    , m_scopeCombo(nullptr)
    , m_caseSensitiveCheck(nullptr)
    , m_wholeWordsCheck(nullptr)
    , m_regexCheck(nullptr)
    , m_backwardsCheck(nullptr)
    , m_maxResultsSpin(nullptr)
    , m_fuzzySlider(nullptr)
    , m_fuzzyLabel(nullptr)
    , m_resultsSplitter(nullptr)
    , m_resultsTree(nullptr)
    , m_resultsLabel(nullptr)
    , m_progressBar(nullptr)
    , m_historyCombo(nullptr)
    , m_historyManager(nullptr)
    , m_searchTimer(nullptr)
    , m_realTimeTimer(nullptr)
    , m_isSearching(false)
    , m_realTimeSearchEnabled(true)
    , m_searchDelay(500)
    , m_settings(nullptr)
    , m_compactMode(false)
    , m_advancedVisible(false)
{
    // Initialize components
    m_historyManager = new SearchHistoryManager(this);
    m_settings = new QSettings("SAST", "Readium-AdvancedSearch", this);
    
    // Setup UI
    setupUI();
    setupConnections();
    setupCompleter();
    
    // Load settings
    loadSettings();
    
    // Initialize timers
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &AdvancedSearchWidget::onSearchTimer);
    
    m_realTimeTimer = new QTimer(this);
    m_realTimeTimer->setSingleShot(true);
    m_realTimeTimer->setInterval(m_searchDelay);
    connect(m_realTimeTimer, &QTimer::timeout, this, &AdvancedSearchWidget::executeSearch);
    
    updateUI();
    
    qDebug() << "AdvancedSearchWidget: Initialized";
}

AdvancedSearchWidget::~AdvancedSearchWidget()
{
    saveSettings();
    m_historyManager->saveHistory();
}

void AdvancedSearchWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(6, 6, 6, 6);
    m_mainLayout->setSpacing(6);
    
    // Search input layout
    m_searchLayout = new QHBoxLayout();
    
    // Search field with history
    m_historyCombo = new QComboBox();
    m_historyCombo->setEditable(true);
    m_historyCombo->setInsertPolicy(QComboBox::NoInsert);
    m_historyCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_searchEdit = m_historyCombo->lineEdit();
    m_searchEdit->setPlaceholderText("Enter search text...");
    m_searchLayout->addWidget(m_historyCombo);
    
    // Search type combo
    m_searchTypeCombo = new QComboBox();
    m_searchTypeCombo->setToolTip("Search Type");
    populateSearchTypes();
    m_searchLayout->addWidget(m_searchTypeCombo);
    
    // Search buttons
    m_searchButton = new QPushButton("Search");
    m_searchButton->setDefault(true);
    m_searchButton->setToolTip("Start Search (Ctrl+Enter)");
    m_searchLayout->addWidget(m_searchButton);
    
    m_stopButton = new QPushButton("Stop");
    m_stopButton->setEnabled(false);
    m_stopButton->setToolTip("Stop Current Search");
    m_searchLayout->addWidget(m_stopButton);
    
    m_clearButton = new QPushButton("Clear");
    m_clearButton->setToolTip("Clear Search Results");
    m_searchLayout->addWidget(m_clearButton);
    
    m_advancedButton = new QPushButton("Advanced");
    m_advancedButton->setCheckable(true);
    m_advancedButton->setToolTip("Show/Hide Advanced Options");
    m_searchLayout->addWidget(m_advancedButton);
    
    m_mainLayout->addLayout(m_searchLayout);
    
    // Advanced options group
    m_advancedGroup = new QGroupBox("Advanced Search Options");
    m_advancedGroup->setVisible(false);
    
    m_advancedLayout = new QGridLayout(m_advancedGroup);
    
    // Search scope
    m_advancedLayout->addWidget(new QLabel("Scope:"), 0, 0);
    m_scopeCombo = new QComboBox();
    populateSearchScopes();
    m_advancedLayout->addWidget(m_scopeCombo, 0, 1);
    
    // Search options
    m_caseSensitiveCheck = new QCheckBox("Case sensitive");
    m_advancedLayout->addWidget(m_caseSensitiveCheck, 1, 0);
    
    m_wholeWordsCheck = new QCheckBox("Whole words only");
    m_advancedLayout->addWidget(m_wholeWordsCheck, 1, 1);
    
    m_regexCheck = new QCheckBox("Regular expressions");
    m_advancedLayout->addWidget(m_regexCheck, 2, 0);
    
    m_backwardsCheck = new QCheckBox("Search backwards");
    m_advancedLayout->addWidget(m_backwardsCheck, 2, 1);
    
    // Max results
    m_advancedLayout->addWidget(new QLabel("Max results:"), 3, 0);
    m_maxResultsSpin = new QSpinBox();
    m_maxResultsSpin->setRange(1, 10000);
    m_maxResultsSpin->setValue(100);
    m_advancedLayout->addWidget(m_maxResultsSpin, 3, 1);
    
    // Fuzzy threshold
    m_fuzzyLabel = new QLabel("Fuzzy threshold: 80%");
    m_advancedLayout->addWidget(m_fuzzyLabel, 4, 0);
    m_fuzzySlider = new QSlider(Qt::Horizontal);
    m_fuzzySlider->setRange(50, 100);
    m_fuzzySlider->setValue(80);
    m_advancedLayout->addWidget(m_fuzzySlider, 4, 1);
    
    m_mainLayout->addWidget(m_advancedGroup);
    
    // Results area
    m_resultsSplitter = new QSplitter(Qt::Vertical);
    
    // Results header
    QWidget* resultsHeader = new QWidget();
    QHBoxLayout* headerLayout = new QHBoxLayout(resultsHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    
    m_resultsLabel = new QLabel("No search results");
    headerLayout->addWidget(m_resultsLabel);
    
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_progressBar->setMaximumHeight(16);
    headerLayout->addWidget(m_progressBar);
    
    m_resultsSplitter->addWidget(resultsHeader);
    
    // Results tree
    m_resultsTree = new QTreeWidget();
    m_resultsTree->setHeaderLabels(QStringList() << "Document" << "Page" << "Context" << "Score");
    m_resultsTree->setRootIsDecorated(false);
    m_resultsTree->setAlternatingRowColors(true);
    m_resultsTree->setSortingEnabled(true);
    m_resultsTree->header()->setStretchLastSection(false);
    m_resultsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_resultsTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_resultsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    
    m_resultsSplitter->addWidget(m_resultsTree);
    m_resultsSplitter->setSizes(QList<int>() << 30 << 200);
    
    m_mainLayout->addWidget(m_resultsSplitter, 1);
}

void AdvancedSearchWidget::setupConnections()
{
    // Search controls
    connect(m_searchEdit, &QLineEdit::textChanged, this, &AdvancedSearchWidget::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &AdvancedSearchWidget::onSearchButtonClicked);
    connect(m_searchButton, &QPushButton::clicked, this, &AdvancedSearchWidget::onSearchButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &AdvancedSearchWidget::onStopButtonClicked);
    connect(m_clearButton, &QPushButton::clicked, this, &AdvancedSearchWidget::onClearButtonClicked);
    connect(m_advancedButton, &QPushButton::toggled, this, &AdvancedSearchWidget::onAdvancedToggled);
    
    // Search options
    connect(m_searchTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AdvancedSearchWidget::onSearchTypeChanged);
    connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AdvancedSearchWidget::onScopeChanged);
    
    // Advanced options
    connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, &AdvancedSearchWidget::onOptionsChanged);
    connect(m_wholeWordsCheck, &QCheckBox::toggled, this, &AdvancedSearchWidget::onOptionsChanged);
    connect(m_regexCheck, &QCheckBox::toggled, this, &AdvancedSearchWidget::onOptionsChanged);
    connect(m_backwardsCheck, &QCheckBox::toggled, this, &AdvancedSearchWidget::onOptionsChanged);
    connect(m_maxResultsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AdvancedSearchWidget::onOptionsChanged);
    
    // Fuzzy slider
    connect(m_fuzzySlider, &QSlider::valueChanged, this, [this](int value) {
        m_fuzzyLabel->setText(QString("Fuzzy threshold: %1%").arg(value));
        onOptionsChanged();
    });
    
    // Results
    connect(m_resultsTree, &QTreeWidget::itemClicked, this, &AdvancedSearchWidget::onResultItemClicked);
    connect(m_resultsTree, &QTreeWidget::itemDoubleClicked, this, &AdvancedSearchWidget::onResultItemDoubleClicked);
    
    // History
    connect(m_historyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AdvancedSearchWidget::onHistoryItemSelected);
    connect(m_historyManager, &SearchHistoryManager::historyChanged, this, [this]() {
        m_historyCombo->clear();
        m_historyCombo->addItems(m_historyManager->getQueryTexts());
    });
}

void AdvancedSearchWidget::populateSearchTypes()
{
    m_searchTypeCombo->addItem(getIconForSearchType(SearchType::PlainText), "Plain Text", static_cast<int>(SearchType::PlainText));
    m_searchTypeCombo->addItem(getIconForSearchType(SearchType::RegularExpression), "Regular Expression", static_cast<int>(SearchType::RegularExpression));
    m_searchTypeCombo->addItem(getIconForSearchType(SearchType::Wildcard), "Wildcard", static_cast<int>(SearchType::Wildcard));
    m_searchTypeCombo->addItem(getIconForSearchType(SearchType::Fuzzy), "Fuzzy Match", static_cast<int>(SearchType::Fuzzy));
    m_searchTypeCombo->addItem(getIconForSearchType(SearchType::Phonetic), "Phonetic", static_cast<int>(SearchType::Phonetic));
}

void AdvancedSearchWidget::populateSearchScopes()
{
    m_scopeCombo->addItem("Current Document", static_cast<int>(SearchScope::CurrentDocument));
    m_scopeCombo->addItem("All Open Documents", static_cast<int>(SearchScope::AllOpenDocuments));
    m_scopeCombo->addItem("Document Collection", static_cast<int>(SearchScope::DocumentCollection));
    m_scopeCombo->addItem("Annotations Only", static_cast<int>(SearchScope::Annotations));
    m_scopeCombo->addItem("Bookmarks Only", static_cast<int>(SearchScope::Bookmarks));
    m_scopeCombo->addItem("Metadata Only", static_cast<int>(SearchScope::Metadata));
}

void AdvancedSearchWidget::startSearch(const SearchQuery& query)
{
    if (m_isSearching) {
        stopSearch();
    }
    
    setQuery(query);
    executeSearch();
}

void AdvancedSearchWidget::executeSearch()
{
    SearchQuery query = getCurrentQuery();
    
    if (!validateQuery(query)) {
        QString error = getQueryValidationError(query);
        QMessageBox::warning(this, "Invalid Search Query", error);
        return;
    }
    
    m_isSearching = true;
    updateButtonStates();
    
    // Add to history
    m_historyManager->addQuery(query);
    
    // Clear previous results
    clearResults();
    
    // Show progress
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0); // Indeterminate
    
    emit searchStarted(query);
    
    // Perform search based on type
    QList<AdvancedSearchResult> results;
    
    try {
        switch (query.type) {
            case SearchType::PlainText:
                results = performTextSearch(query);
                break;
            case SearchType::RegularExpression:
                results = performRegexSearch(query);
                break;
            case SearchType::Fuzzy:
                results = performFuzzySearch(query);
                break;
            default:
                results = performTextSearch(query); // Fallback
                break;
        }
        
        // Add results
        QMutexLocker locker(&m_resultsMutex);
        m_results = results;
        locker.unlock();
        
        // Sort and display
        sortResults();
        updateResultsDisplay();
        
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Search Error", QString("Search failed: %1").arg(e.what()));
    }
    
    m_isSearching = false;
    m_progressBar->setVisible(false);
    updateButtonStates();
    
    emit searchFinished(m_results.size());
    
    qDebug() << "AdvancedSearchWidget: Search completed with" << m_results.size() << "results";
}

QIcon AdvancedSearchWidget::getIconForSearchType(SearchType type) const
{
    // Return appropriate icons for each search type
    // This would typically load from resources
    return QIcon(); // Placeholder
}

QList<AdvancedSearchResult> AdvancedSearchWidget::performTextSearch(const SearchQuery& query)
{
    QList<AdvancedSearchResult> results;

    // This would integrate with the document system to perform actual search
    // For now, return empty results as placeholder

    qDebug() << "AdvancedSearchWidget: Performing text search for:" << query.text;
    return results;
}

QList<AdvancedSearchResult> AdvancedSearchWidget::performRegexSearch(const SearchQuery& query)
{
    QList<AdvancedSearchResult> results;

    try {
        QRegularExpression regex(query.text);
        if (!regex.isValid()) {
            throw std::runtime_error("Invalid regular expression");
        }

        // This would integrate with the document system
        qDebug() << "AdvancedSearchWidget: Performing regex search for:" << query.text;

    } catch (const std::exception& e) {
        qWarning() << "AdvancedSearchWidget: Regex search error:" << e.what();
    }

    return results;
}

QList<AdvancedSearchResult> AdvancedSearchWidget::performFuzzySearch(const SearchQuery& query)
{
    QList<AdvancedSearchResult> results;

    // This would implement fuzzy string matching algorithm
    qDebug() << "AdvancedSearchWidget: Performing fuzzy search for:" << query.text
             << "threshold:" << query.fuzzyThreshold;

    return results;
}

void AdvancedSearchWidget::updateResultsDisplay()
{
    m_resultsTree->clear();

    QMutexLocker locker(&m_resultsMutex);

    if (m_results.isEmpty()) {
        m_resultsLabel->setText("No search results");
        return;
    }

    m_resultsLabel->setText(QString("Found %1 result(s)").arg(m_results.size()));

    for (const AdvancedSearchResult& result : m_results) {
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, result.documentTitle.isEmpty() ?
                     QFileInfo(result.documentPath).baseName() : result.documentTitle);
        item->setText(1, QString::number(result.pageNumber));
        item->setText(2, result.context);
        item->setText(3, QString::number(result.relevanceScore, 'f', 2));

        // Store result data
        item->setData(0, Qt::UserRole, QVariant::fromValue(result));

        m_resultsTree->addTopLevelItem(item);
    }

    // Auto-resize columns
    for (int i = 0; i < m_resultsTree->columnCount(); ++i) {
        m_resultsTree->resizeColumnToContents(i);
    }
}

void AdvancedSearchWidget::sortResults()
{
    QMutexLocker locker(&m_resultsMutex);

    std::sort(m_results.begin(), m_results.end(),
              [](const AdvancedSearchResult& a, const AdvancedSearchResult& b) {
                  return a.relevanceScore > b.relevanceScore;
              });
}

SearchQuery AdvancedSearchWidget::getCurrentQuery() const
{
    SearchQuery query;
    query.text = m_searchEdit->text();
    query.type = static_cast<SearchType>(m_searchTypeCombo->currentData().toInt());
    query.scope = static_cast<SearchScope>(m_scopeCombo->currentData().toInt());
    query.caseSensitive = m_caseSensitiveCheck->isChecked();
    query.wholeWords = m_wholeWordsCheck->isChecked();
    query.useRegex = m_regexCheck->isChecked();
    query.searchBackwards = m_backwardsCheck->isChecked();
    query.maxResults = m_maxResultsSpin->value();
    query.fuzzyThreshold = m_fuzzySlider->value() / 100.0;

    return query;
}

void AdvancedSearchWidget::setQuery(const SearchQuery& query)
{
    m_searchEdit->setText(query.text);

    // Set search type
    for (int i = 0; i < m_searchTypeCombo->count(); ++i) {
        if (m_searchTypeCombo->itemData(i).toInt() == static_cast<int>(query.type)) {
            m_searchTypeCombo->setCurrentIndex(i);
            break;
        }
    }

    // Set scope
    for (int i = 0; i < m_scopeCombo->count(); ++i) {
        if (m_scopeCombo->itemData(i).toInt() == static_cast<int>(query.scope)) {
            m_scopeCombo->setCurrentIndex(i);
            break;
        }
    }

    m_caseSensitiveCheck->setChecked(query.caseSensitive);
    m_wholeWordsCheck->setChecked(query.wholeWords);
    m_regexCheck->setChecked(query.useRegex);
    m_backwardsCheck->setChecked(query.searchBackwards);
    m_maxResultsSpin->setValue(query.maxResults);
    m_fuzzySlider->setValue(static_cast<int>(query.fuzzyThreshold * 100));
}

bool AdvancedSearchWidget::validateQuery(const SearchQuery& query) const
{
    if (query.text.isEmpty()) {
        return false;
    }

    if (query.type == SearchType::RegularExpression) {
        QRegularExpression regex(query.text);
        return regex.isValid();
    }

    return true;
}

QString AdvancedSearchWidget::getQueryValidationError(const SearchQuery& query) const
{
    if (query.text.isEmpty()) {
        return "Search text cannot be empty.";
    }

    if (query.type == SearchType::RegularExpression) {
        QRegularExpression regex(query.text);
        if (!regex.isValid()) {
            return QString("Invalid regular expression: %1").arg(regex.errorString());
        }
    }

    return QString();
}

void AdvancedSearchWidget::onSearchTextChanged()
{
    if (m_realTimeSearchEnabled && !m_searchEdit->text().isEmpty()) {
        m_realTimeTimer->start();
    } else {
        m_realTimeTimer->stop();
    }

    updateButtonStates();
}

void AdvancedSearchWidget::onSearchButtonClicked()
{
    executeSearch();
}

void AdvancedSearchWidget::onStopButtonClicked()
{
    stopSearch();
}

void AdvancedSearchWidget::onClearButtonClicked()
{
    clearResults();
}

void AdvancedSearchWidget::onAdvancedToggled(bool show)
{
    m_advancedVisible = show;
    m_advancedGroup->setVisible(show);
    updateAdvancedOptionsVisibility();
}

void AdvancedSearchWidget::onResultItemClicked()
{
    QTreeWidgetItem* item = m_resultsTree->currentItem();
    if (item) {
        AdvancedSearchResult result = item->data(0, Qt::UserRole).value<AdvancedSearchResult>();
        emit resultSelected(result);
    }
}

void AdvancedSearchWidget::onResultItemDoubleClicked()
{
    goToSelectedResult();
}

void AdvancedSearchWidget::stopSearch()
{
    if (m_isSearching) {
        m_isSearching = false;
        m_progressBar->setVisible(false);
        updateButtonStates();
        qDebug() << "AdvancedSearchWidget: Search stopped";
    }
}

void AdvancedSearchWidget::clearResults()
{
    QMutexLocker locker(&m_resultsMutex);
    m_results.clear();
    locker.unlock();

    m_resultsTree->clear();
    m_resultsLabel->setText("No search results");
}

void AdvancedSearchWidget::updateButtonStates()
{
    bool hasText = !m_searchEdit->text().isEmpty();
    bool hasResults = !m_results.isEmpty();

    m_searchButton->setEnabled(hasText && !m_isSearching);
    m_stopButton->setEnabled(m_isSearching);
    m_clearButton->setEnabled(hasResults);
}

void AdvancedSearchWidget::updateAdvancedOptionsVisibility()
{
    // Update visibility of advanced options based on search type
    SearchType type = static_cast<SearchType>(m_searchTypeCombo->currentData().toInt());

    m_fuzzySlider->setVisible(type == SearchType::Fuzzy);
    m_fuzzyLabel->setVisible(type == SearchType::Fuzzy);
    m_regexCheck->setVisible(type != SearchType::RegularExpression);
}

void AdvancedSearchWidget::goToSelectedResult()
{
    QTreeWidgetItem* item = m_resultsTree->currentItem();
    if (item) {
        AdvancedSearchResult result = item->data(0, Qt::UserRole).value<AdvancedSearchResult>();
        emit resultSelected(result);
        // Additional logic to navigate to the result would go here
    }
}

void AdvancedSearchWidget::loadSettings()
{
    if (!m_settings) return;

    m_realTimeSearchEnabled = m_settings->value("search/realTimeEnabled", true).toBool();
    m_searchDelay = m_settings->value("search/delay", 500).toInt();
    m_compactMode = m_settings->value("search/compactMode", false).toBool();
    m_advancedVisible = m_settings->value("search/advancedVisible", false).toBool();

    // Restore UI state
    m_advancedButton->setChecked(m_advancedVisible);
    m_advancedGroup->setVisible(m_advancedVisible);
}

void AdvancedSearchWidget::saveSettings()
{
    if (!m_settings) return;

    m_settings->setValue("search/realTimeEnabled", m_realTimeSearchEnabled);
    m_settings->setValue("search/delay", m_searchDelay);
    m_settings->setValue("search/compactMode", m_compactMode);
    m_settings->setValue("search/advancedVisible", m_advancedVisible);

    m_settings->sync();
}
