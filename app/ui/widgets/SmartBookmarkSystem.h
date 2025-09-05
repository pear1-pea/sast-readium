#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QMenu>
#include <QContextMenuEvent>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QSplitter>
#include <QGroupBox>
#include <QTextEdit>
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QProgressBar>
#include <QTimer>
#include <QSettings>
#include <QMutex>
#include <QDateTime>
#include <QUrl>
#include <QColor>

/**
 * Bookmark types
 */
enum class BookmarkType {
    Page,               // Page bookmark
    Position,           // Specific position on page
    Selection,          // Text selection bookmark
    Annotation,         // Annotation bookmark
    Search,             // Search result bookmark
    Custom,             // Custom bookmark
    Folder              // Bookmark folder
};

/**
 * Navigation history entry
 */
struct NavigationEntry {
    QString documentPath;
    int pageNumber;
    QPointF position;
    double zoomLevel;
    qint64 timestamp;
    QString title;
    QByteArray thumbnail;
    
    NavigationEntry()
        : pageNumber(-1), zoomLevel(1.0), timestamp(0) {}
    
    bool isValid() const {
        return !documentPath.isEmpty() && pageNumber >= 0;
    }
};

/**
 * Smart bookmark data
 */
struct SmartBookmark {
    QString id;
    QString title;
    QString description;
    BookmarkType type;
    QString documentPath;
    int pageNumber;
    QPointF position;
    QRectF selectionRect;
    QString selectedText;
    QColor color;
    QDateTime createdTime;
    QDateTime lastAccessTime;
    int accessCount;
    QStringList tags;
    QVariantMap properties;
    QString parentId;
    QStringList childIds;
    bool isExpanded;
    int sortOrder;
    
    SmartBookmark()
        : type(BookmarkType::Page)
        , pageNumber(-1)
        , color(Qt::blue)
        , accessCount(0)
        , isExpanded(true)
        , sortOrder(0) {}
    
    bool isValid() const {
        return !id.isEmpty() && !documentPath.isEmpty() && pageNumber >= 0;
    }
    
    bool isFolder() const {
        return type == BookmarkType::Folder;
    }
};

/**
 * Bookmark statistics
 */
struct BookmarkStatistics {
    int totalBookmarks;
    int totalFolders;
    QHash<BookmarkType, int> typeCount;
    QHash<QString, int> documentCount;
    SmartBookmark mostAccessed;
    SmartBookmark mostRecent;
    QDateTime oldestBookmark;
    QDateTime newestBookmark;
    
    BookmarkStatistics() : totalBookmarks(0), totalFolders(0) {}
};

/**
 * Smart bookmark and navigation system
 */
class SmartBookmarkSystem : public QWidget
{
    Q_OBJECT

public:
    explicit SmartBookmarkSystem(QWidget* parent = nullptr);
    ~SmartBookmarkSystem();

    // Bookmark management
    QString addBookmark(const QString& title, const QString& documentPath, 
                       int pageNumber, BookmarkType type = BookmarkType::Page);
    QString addBookmark(const SmartBookmark& bookmark);
    bool updateBookmark(const QString& id, const SmartBookmark& bookmark);
    bool removeBookmark(const QString& id);
    SmartBookmark getBookmark(const QString& id) const;
    QList<SmartBookmark> getBookmarks(const QString& documentPath = QString()) const;
    
    // Folder management
    QString createFolder(const QString& name, const QString& parentId = QString());
    bool moveBookmark(const QString& bookmarkId, const QString& folderId);
    bool deleteFolder(const QString& folderId, bool deleteContents = false);
    QList<SmartBookmark> getFolderContents(const QString& folderId) const;
    
    // Navigation history
    void addToHistory(const NavigationEntry& entry);
    void clearHistory();
    QList<NavigationEntry> getHistory() const;
    NavigationEntry getCurrentEntry() const;
    bool canGoBack() const;
    bool canGoForward() const;
    NavigationEntry goBack();
    NavigationEntry goForward();
    
    // Search and filtering
    QList<SmartBookmark> searchBookmarks(const QString& query, bool includeContent = true) const;
    QList<SmartBookmark> filterBookmarks(BookmarkType type, const QStringList& tags = QStringList()) const;
    QStringList getAllTags() const;
    QStringList getDocuments() const;
    
    // Quick access
    QList<SmartBookmark> getRecentBookmarks(int count = 10) const;
    QList<SmartBookmark> getFrequentBookmarks(int count = 10) const;
    QList<SmartBookmark> getFavoriteBookmarks() const;
    void markAsFavorite(const QString& id, bool favorite = true);
    
    // Import/Export
    bool exportBookmarks(const QString& filePath, const QStringList& bookmarkIds = QStringList()) const;
    bool importBookmarks(const QString& filePath);
    QString exportToHTML() const;
    QByteArray exportToJSON() const;
    
    // Statistics and analysis
    BookmarkStatistics getStatistics() const;
    QHash<QString, int> getTagCloud() const;
    QList<SmartBookmark> getBookmarksByDate(const QDate& date) const;
    
    // UI state
    void setCurrentDocument(const QString& documentPath);
    QString getCurrentDocument() const { return m_currentDocument; }
    void setViewMode(int mode); // 0=tree, 1=list, 2=thumbnails
    int getViewMode() const { return m_viewMode; }
    
    // Settings
    void loadSettings();
    void saveSettings();

public slots:
    void showBookmarkDialog();
    void showFolderDialog();
    void showSearchDialog();
    void refreshBookmarks();
    void sortBookmarks(int criteria); // 0=name, 1=date, 2=access, 3=type
    void expandAll();
    void collapseAll();

signals:
    void bookmarkAdded(const QString& id, const SmartBookmark& bookmark);
    void bookmarkUpdated(const QString& id, const SmartBookmark& bookmark);
    void bookmarkRemoved(const QString& id);
    void bookmarkSelected(const QString& id, const SmartBookmark& bookmark);
    void bookmarkActivated(const QString& id, const SmartBookmark& bookmark);
    void folderCreated(const QString& id, const QString& name);
    void navigationRequested(const QString& documentPath, int pageNumber, const QPointF& position);
    void historyChanged();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void onBookmarkItemClicked(QTreeWidgetItem* item, int column);
    void onBookmarkItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onBookmarkItemChanged(QTreeWidgetItem* item, int column);
    void onSearchTextChanged();
    void onFilterChanged();
    void onAddBookmarkClicked();
    void onAddFolderClicked();
    void onEditBookmarkClicked();
    void onDeleteBookmarkClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onBackClicked();
    void onForwardClicked();
    void onRefreshClicked();

private:
    // UI Components
    QVBoxLayout* m_mainLayout;
    QHBoxLayout* m_toolbarLayout;
    QSplitter* m_mainSplitter;
    
    // Toolbar
    QToolBar* m_toolbar;
    QAction* m_addBookmarkAction;
    QAction* m_addFolderAction;
    QAction* m_editAction;
    QAction* m_deleteAction;
    QAction* m_moveUpAction;
    QAction* m_moveDownAction;
    QAction* m_backAction;
    QAction* m_forwardAction;
    QAction* m_refreshAction;
    
    // Search and filter
    QGroupBox* m_searchGroup;
    QLineEdit* m_searchEdit;
    QComboBox* m_filterTypeCombo;
    QComboBox* m_filterDocumentCombo;
    QComboBox* m_filterTagCombo;
    QPushButton* m_clearFilterButton;
    
    // Bookmark tree
    QTreeWidget* m_bookmarkTree;
    QLabel* m_statusLabel;
    
    // Properties panel
    QGroupBox* m_propertiesGroup;
    QLineEdit* m_titleEdit;
    QTextEdit* m_descriptionEdit;
    QComboBox* m_typeCombo;
    QLineEdit* m_tagsEdit;
    QPushButton* m_colorButton;
    QCheckBox* m_favoriteCheck;
    QLabel* m_createdLabel;
    QLabel* m_accessedLabel;
    QLabel* m_accessCountLabel;
    
    // Navigation history
    QList<NavigationEntry> m_history;
    int m_historyIndex;
    int m_maxHistorySize;
    
    // Data management
    QHash<QString, SmartBookmark> m_bookmarks;
    QHash<QString, QTreeWidgetItem*> m_itemMap;
    mutable QMutex m_dataMutex;
    
    // Current state
    QString m_currentDocument;
    QString m_selectedBookmarkId;
    int m_viewMode;
    QString m_searchQuery;
    BookmarkType m_filterType;
    QString m_filterDocument;
    QString m_filterTag;
    
    // Settings
    QSettings* m_settings;
    
    // Helper methods
    void setupUI();
    void setupToolbar();
    void setupSearchAndFilter();
    void setupBookmarkTree();
    void setupPropertiesPanel();
    void setupConnections();
    
    void updateBookmarkTree();
    void updatePropertiesPanel();
    void updateToolbar();
    void updateStatusLabel();
    void updateFilterCombos();
    
    QTreeWidgetItem* createBookmarkItem(const SmartBookmark& bookmark);
    void updateBookmarkItem(QTreeWidgetItem* item, const SmartBookmark& bookmark);
    SmartBookmark getBookmarkFromItem(QTreeWidgetItem* item) const;
    
    QString generateBookmarkId() const;
    void applyFilter();
    bool matchesFilter(const SmartBookmark& bookmark) const;
    void sortBookmarkItems(QTreeWidgetItem* parent = nullptr);
    
    // Serialization
    QVariantMap bookmarkToVariant(const SmartBookmark& bookmark) const;
    SmartBookmark variantToBookmark(const QVariantMap& data) const;
    QVariantMap navigationEntryToVariant(const NavigationEntry& entry) const;
    NavigationEntry variantToNavigationEntry(const QVariantMap& data) const;
};
