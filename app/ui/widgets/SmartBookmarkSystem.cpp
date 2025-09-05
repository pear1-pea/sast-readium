#include "SmartBookmarkSystem.h"
#include <QApplication>
#include <QDebug>
#include <QMutexLocker>
#include <QUuid>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QColorDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHeaderView>
#include <QMenu>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <algorithm>

SmartBookmarkSystem::SmartBookmarkSystem(QWidget* parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_toolbarLayout(nullptr)
    , m_mainSplitter(nullptr)
    , m_toolbar(nullptr)
    , m_addBookmarkAction(nullptr)
    , m_addFolderAction(nullptr)
    , m_editAction(nullptr)
    , m_deleteAction(nullptr)
    , m_moveUpAction(nullptr)
    , m_moveDownAction(nullptr)
    , m_backAction(nullptr)
    , m_forwardAction(nullptr)
    , m_refreshAction(nullptr)
    , m_searchGroup(nullptr)
    , m_searchEdit(nullptr)
    , m_filterTypeCombo(nullptr)
    , m_filterDocumentCombo(nullptr)
    , m_filterTagCombo(nullptr)
    , m_clearFilterButton(nullptr)
    , m_bookmarkTree(nullptr)
    , m_statusLabel(nullptr)
    , m_propertiesGroup(nullptr)
    , m_titleEdit(nullptr)
    , m_descriptionEdit(nullptr)
    , m_typeCombo(nullptr)
    , m_tagsEdit(nullptr)
    , m_colorButton(nullptr)
    , m_favoriteCheck(nullptr)
    , m_createdLabel(nullptr)
    , m_accessedLabel(nullptr)
    , m_accessCountLabel(nullptr)
    , m_historyIndex(-1)
    , m_maxHistorySize(100)
    , m_viewMode(0)
    , m_filterType(BookmarkType::Page)
    , m_settings(nullptr)
{
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-BookmarkSystem", this);
    
    // Setup UI
    setupUI();
    setupConnections();
    
    // Load settings and data
    loadSettings();
    
    // Update UI
    updateBookmarkTree();
    updateToolbar();
    updateStatusLabel();
    
    qDebug() << "SmartBookmarkSystem: Initialized";
}

SmartBookmarkSystem::~SmartBookmarkSystem()
{
    saveSettings();
}

void SmartBookmarkSystem::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);
    
    // Setup toolbar
    setupToolbar();
    
    // Main splitter
    m_mainSplitter = new QSplitter(Qt::Horizontal);
    
    // Left panel - search and bookmarks
    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    
    // Search and filter
    setupSearchAndFilter();
    leftLayout->addWidget(m_searchGroup);
    
    // Bookmark tree
    setupBookmarkTree();
    leftLayout->addWidget(m_bookmarkTree, 1);
    
    // Status label
    m_statusLabel = new QLabel("0 bookmarks");
    m_statusLabel->setStyleSheet("QLabel { color: #666; font-size: 10px; }");
    leftLayout->addWidget(m_statusLabel);
    
    m_mainSplitter->addWidget(leftPanel);
    
    // Right panel - properties
    setupPropertiesPanel();
    m_mainSplitter->addWidget(m_propertiesGroup);
    
    // Set splitter sizes
    m_mainSplitter->setSizes(QList<int>() << 400 << 250);
    
    m_mainLayout->addWidget(m_mainSplitter);
}

void SmartBookmarkSystem::setupToolbar()
{
    m_toolbarLayout = new QHBoxLayout();
    
    m_toolbar = new QToolBar();
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolbar->setIconSize(QSize(16, 16));
    
    // Navigation actions
    m_backAction = m_toolbar->addAction("Back");
    m_backAction->setEnabled(false);
    m_backAction->setToolTip("Go back in navigation history");
    
    m_forwardAction = m_toolbar->addAction("Forward");
    m_forwardAction->setEnabled(false);
    m_forwardAction->setToolTip("Go forward in navigation history");
    
    m_toolbar->addSeparator();
    
    // Bookmark actions
    m_addBookmarkAction = m_toolbar->addAction("Add Bookmark");
    m_addBookmarkAction->setToolTip("Add new bookmark");
    
    m_addFolderAction = m_toolbar->addAction("Add Folder");
    m_addFolderAction->setToolTip("Create new folder");
    
    m_toolbar->addSeparator();
    
    m_editAction = m_toolbar->addAction("Edit");
    m_editAction->setEnabled(false);
    m_editAction->setToolTip("Edit selected bookmark");
    
    m_deleteAction = m_toolbar->addAction("Delete");
    m_deleteAction->setEnabled(false);
    m_deleteAction->setToolTip("Delete selected bookmark");
    
    m_toolbar->addSeparator();
    
    m_moveUpAction = m_toolbar->addAction("Move Up");
    m_moveUpAction->setEnabled(false);
    m_moveUpAction->setToolTip("Move bookmark up");
    
    m_moveDownAction = m_toolbar->addAction("Move Down");
    m_moveDownAction->setEnabled(false);
    m_moveDownAction->setToolTip("Move bookmark down");
    
    m_toolbar->addSeparator();
    
    m_refreshAction = m_toolbar->addAction("Refresh");
    m_refreshAction->setToolTip("Refresh bookmark list");
    
    m_toolbarLayout->addWidget(m_toolbar);
    m_toolbarLayout->addStretch();
    
    m_mainLayout->addLayout(m_toolbarLayout);
}

void SmartBookmarkSystem::setupSearchAndFilter()
{
    m_searchGroup = new QGroupBox("Search & Filter");
    QVBoxLayout* searchLayout = new QVBoxLayout(m_searchGroup);
    
    // Search field
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search bookmarks...");
    searchLayout->addWidget(m_searchEdit);
    
    // Filter controls
    QHBoxLayout* filterLayout = new QHBoxLayout();
    
    // Type filter
    m_filterTypeCombo = new QComboBox();
    m_filterTypeCombo->addItem("All Types", -1);
    m_filterTypeCombo->addItem("Page", static_cast<int>(BookmarkType::Page));
    m_filterTypeCombo->addItem("Position", static_cast<int>(BookmarkType::Position));
    m_filterTypeCombo->addItem("Selection", static_cast<int>(BookmarkType::Selection));
    m_filterTypeCombo->addItem("Folder", static_cast<int>(BookmarkType::Folder));
    filterLayout->addWidget(m_filterTypeCombo);
    
    // Document filter
    m_filterDocumentCombo = new QComboBox();
    m_filterDocumentCombo->addItem("All Documents", "");
    filterLayout->addWidget(m_filterDocumentCombo);
    
    // Tag filter
    m_filterTagCombo = new QComboBox();
    m_filterTagCombo->addItem("All Tags", "");
    filterLayout->addWidget(m_filterTagCombo);
    
    // Clear filter button
    m_clearFilterButton = new QPushButton("Clear");
    m_clearFilterButton->setMaximumWidth(50);
    filterLayout->addWidget(m_clearFilterButton);
    
    searchLayout->addLayout(filterLayout);
}

void SmartBookmarkSystem::setupBookmarkTree()
{
    m_bookmarkTree = new QTreeWidget();
    m_bookmarkTree->setHeaderLabels(QStringList() << "Title" << "Page" << "Document" << "Created");
    m_bookmarkTree->setRootIsDecorated(true);
    m_bookmarkTree->setAlternatingRowColors(true);
    m_bookmarkTree->setSortingEnabled(true);
    m_bookmarkTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_bookmarkTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    // Configure columns
    m_bookmarkTree->header()->setStretchLastSection(false);
    m_bookmarkTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_bookmarkTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_bookmarkTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_bookmarkTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    
    // Enable context menu
    m_bookmarkTree->setContextMenuPolicy(Qt::CustomContextMenu);
}

void SmartBookmarkSystem::setupPropertiesPanel()
{
    m_propertiesGroup = new QGroupBox("Properties");
    QFormLayout* propertiesLayout = new QFormLayout(m_propertiesGroup);
    
    // Title
    m_titleEdit = new QLineEdit();
    propertiesLayout->addRow("Title:", m_titleEdit);
    
    // Description
    m_descriptionEdit = new QTextEdit();
    m_descriptionEdit->setMaximumHeight(80);
    propertiesLayout->addRow("Description:", m_descriptionEdit);
    
    // Type
    m_typeCombo = new QComboBox();
    m_typeCombo->addItem("Page", static_cast<int>(BookmarkType::Page));
    m_typeCombo->addItem("Position", static_cast<int>(BookmarkType::Position));
    m_typeCombo->addItem("Selection", static_cast<int>(BookmarkType::Selection));
    m_typeCombo->addItem("Folder", static_cast<int>(BookmarkType::Folder));
    propertiesLayout->addRow("Type:", m_typeCombo);
    
    // Tags
    m_tagsEdit = new QLineEdit();
    m_tagsEdit->setPlaceholderText("tag1, tag2, tag3");
    propertiesLayout->addRow("Tags:", m_tagsEdit);
    
    // Color
    m_colorButton = new QPushButton();
    m_colorButton->setFixedSize(40, 25);
    m_colorButton->setStyleSheet("background-color: blue; border: 1px solid black;");
    propertiesLayout->addRow("Color:", m_colorButton);
    
    // Favorite
    m_favoriteCheck = new QCheckBox();
    propertiesLayout->addRow("Favorite:", m_favoriteCheck);
    
    // Information labels
    m_createdLabel = new QLabel();
    m_createdLabel->setStyleSheet("QLabel { color: #666; font-size: 10px; }");
    propertiesLayout->addRow("Created:", m_createdLabel);
    
    m_accessedLabel = new QLabel();
    m_accessedLabel->setStyleSheet("QLabel { color: #666; font-size: 10px; }");
    propertiesLayout->addRow("Last Accessed:", m_accessedLabel);
    
    m_accessCountLabel = new QLabel();
    m_accessCountLabel->setStyleSheet("QLabel { color: #666; font-size: 10px; }");
    propertiesLayout->addRow("Access Count:", m_accessCountLabel);
}

void SmartBookmarkSystem::setupConnections()
{
    // Toolbar actions
    connect(m_addBookmarkAction, &QAction::triggered, this, &SmartBookmarkSystem::onAddBookmarkClicked);
    connect(m_addFolderAction, &QAction::triggered, this, &SmartBookmarkSystem::onAddFolderClicked);
    connect(m_editAction, &QAction::triggered, this, &SmartBookmarkSystem::onEditBookmarkClicked);
    connect(m_deleteAction, &QAction::triggered, this, &SmartBookmarkSystem::onDeleteBookmarkClicked);
    connect(m_moveUpAction, &QAction::triggered, this, &SmartBookmarkSystem::onMoveUpClicked);
    connect(m_moveDownAction, &QAction::triggered, this, &SmartBookmarkSystem::onMoveDownClicked);
    connect(m_backAction, &QAction::triggered, this, &SmartBookmarkSystem::onBackClicked);
    connect(m_forwardAction, &QAction::triggered, this, &SmartBookmarkSystem::onForwardClicked);
    connect(m_refreshAction, &QAction::triggered, this, &SmartBookmarkSystem::onRefreshClicked);
    
    // Search and filter
    connect(m_searchEdit, &QLineEdit::textChanged, this, &SmartBookmarkSystem::onSearchTextChanged);
    connect(m_filterTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SmartBookmarkSystem::onFilterChanged);
    connect(m_filterDocumentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SmartBookmarkSystem::onFilterChanged);
    connect(m_filterTagCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SmartBookmarkSystem::onFilterChanged);
    connect(m_clearFilterButton, &QPushButton::clicked, this, [this]() {
        m_searchEdit->clear();
        m_filterTypeCombo->setCurrentIndex(0);
        m_filterDocumentCombo->setCurrentIndex(0);
        m_filterTagCombo->setCurrentIndex(0);
        applyFilter();
    });
    
    // Bookmark tree
    connect(m_bookmarkTree, &QTreeWidget::itemClicked, this, &SmartBookmarkSystem::onBookmarkItemClicked);
    connect(m_bookmarkTree, &QTreeWidget::itemDoubleClicked, this, &SmartBookmarkSystem::onBookmarkItemDoubleClicked);
    connect(m_bookmarkTree, &QTreeWidget::itemChanged, this, &SmartBookmarkSystem::onBookmarkItemChanged);
    
    // Properties
    connect(m_titleEdit, &QLineEdit::textChanged, this, [this]() {
        if (!m_selectedBookmarkId.isEmpty()) {
            SmartBookmark bookmark = getBookmark(m_selectedBookmarkId);
            bookmark.title = m_titleEdit->text();
            updateBookmark(m_selectedBookmarkId, bookmark);
        }
    });
    
    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        QColor color = QColorDialog::getColor(Qt::blue, this);
        if (color.isValid()) {
            m_colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(color.name()));
            if (!m_selectedBookmarkId.isEmpty()) {
                SmartBookmark bookmark = getBookmark(m_selectedBookmarkId);
                bookmark.color = color;
                updateBookmark(m_selectedBookmarkId, bookmark);
            }
        }
    });
}

QString SmartBookmarkSystem::addBookmark(const QString& title, const QString& documentPath, 
                                        int pageNumber, BookmarkType type)
{
    SmartBookmark bookmark;
    bookmark.id = generateBookmarkId();
    bookmark.title = title;
    bookmark.documentPath = documentPath;
    bookmark.pageNumber = pageNumber;
    bookmark.type = type;
    bookmark.createdTime = QDateTime::currentDateTime();
    bookmark.lastAccessTime = bookmark.createdTime;
    
    return addBookmark(bookmark);
}

QString SmartBookmarkSystem::addBookmark(const SmartBookmark& bookmark)
{
    if (!bookmark.isValid()) {
        qWarning() << "SmartBookmarkSystem: Invalid bookmark";
        return QString();
    }
    
    QMutexLocker locker(&m_dataMutex);
    
    SmartBookmark newBookmark = bookmark;
    if (newBookmark.id.isEmpty()) {
        newBookmark.id = generateBookmarkId();
    }
    
    m_bookmarks[newBookmark.id] = newBookmark;
    locker.unlock();
    
    updateBookmarkTree();
    updateFilterCombos();
    updateStatusLabel();
    
    emit bookmarkAdded(newBookmark.id, newBookmark);
    
    qDebug() << "SmartBookmarkSystem: Added bookmark" << newBookmark.id << newBookmark.title;
    return newBookmark.id;
}

bool SmartBookmarkSystem::updateBookmark(const QString& id, const SmartBookmark& bookmark)
{
    QMutexLocker locker(&m_dataMutex);
    
    auto it = m_bookmarks.find(id);
    if (it == m_bookmarks.end()) {
        return false;
    }
    
    SmartBookmark updatedBookmark = bookmark;
    updatedBookmark.id = id;
    it.value() = updatedBookmark;
    
    locker.unlock();
    
    updateBookmarkTree();
    updatePropertiesPanel();
    
    emit bookmarkUpdated(id, updatedBookmark);
    
    return true;
}

QString SmartBookmarkSystem::generateBookmarkId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void SmartBookmarkSystem::updateBookmarkTree()
{
    m_bookmarkTree->clear();
    m_itemMap.clear();
    
    QMutexLocker locker(&m_dataMutex);
    
    // Create root items for folders first
    QList<SmartBookmark> folders;
    QList<SmartBookmark> bookmarks;
    
    for (const SmartBookmark& bookmark : m_bookmarks) {
        if (matchesFilter(bookmark)) {
            if (bookmark.isFolder()) {
                folders.append(bookmark);
            } else {
                bookmarks.append(bookmark);
            }
        }
    }
    
    // Sort folders and bookmarks
    std::sort(folders.begin(), folders.end(), [](const SmartBookmark& a, const SmartBookmark& b) {
        return a.sortOrder < b.sortOrder || (a.sortOrder == b.sortOrder && a.title < b.title);
    });
    
    std::sort(bookmarks.begin(), bookmarks.end(), [](const SmartBookmark& a, const SmartBookmark& b) {
        return a.sortOrder < b.sortOrder || (a.sortOrder == b.sortOrder && a.title < b.title);
    });
    
    // Add folders first
    for (const SmartBookmark& folder : folders) {
        if (folder.parentId.isEmpty()) {
            QTreeWidgetItem* item = createBookmarkItem(folder);
            m_bookmarkTree->addTopLevelItem(item);
            m_itemMap[folder.id] = item;
        }
    }
    
    // Add bookmarks
    for (const SmartBookmark& bookmark : bookmarks) {
        if (bookmark.parentId.isEmpty()) {
            QTreeWidgetItem* item = createBookmarkItem(bookmark);
            m_bookmarkTree->addTopLevelItem(item);
            m_itemMap[bookmark.id] = item;
        }
    }
    
    // Add child items
    for (const SmartBookmark& bookmark : m_bookmarks) {
        if (!bookmark.parentId.isEmpty() && matchesFilter(bookmark)) {
            QTreeWidgetItem* parentItem = m_itemMap.value(bookmark.parentId, nullptr);
            if (parentItem) {
                QTreeWidgetItem* item = createBookmarkItem(bookmark);
                parentItem->addChild(item);
                m_itemMap[bookmark.id] = item;
            }
        }
    }
    
    // Expand folders
    for (const SmartBookmark& folder : folders) {
        QTreeWidgetItem* item = m_itemMap.value(folder.id, nullptr);
        if (item) {
            item->setExpanded(folder.isExpanded);
        }
    }
}

QTreeWidgetItem* SmartBookmarkSystem::createBookmarkItem(const SmartBookmark& bookmark)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    updateBookmarkItem(item, bookmark);
    return item;
}

void SmartBookmarkSystem::updateBookmarkItem(QTreeWidgetItem* item, const SmartBookmark& bookmark)
{
    if (!item) return;
    
    item->setText(0, bookmark.title);
    item->setText(1, bookmark.isFolder() ? "" : QString::number(bookmark.pageNumber + 1));
    item->setText(2, bookmark.isFolder() ? "" : QFileInfo(bookmark.documentPath).baseName());
    item->setText(3, bookmark.createdTime.toString("MM/dd/yy"));
    
    // Store bookmark ID
    item->setData(0, Qt::UserRole, bookmark.id);
    
    // Set icon and color
    if (bookmark.isFolder()) {
        // Set folder icon
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    } else {
        // Set bookmark icon with color
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        item->setForeground(0, QBrush(bookmark.color));
    }
    
    // Set tooltip
    QString tooltip = QString("Title: %1\nType: %2\nCreated: %3\nAccessed: %4 times")
                     .arg(bookmark.title)
                     .arg(static_cast<int>(bookmark.type))
                     .arg(bookmark.createdTime.toString())
                     .arg(bookmark.accessCount);
    
    if (!bookmark.description.isEmpty()) {
        tooltip += QString("\nDescription: %1").arg(bookmark.description);
    }
    
    if (!bookmark.tags.isEmpty()) {
        tooltip += QString("\nTags: %1").arg(bookmark.tags.join(", "));
    }
    
    item->setToolTip(0, tooltip);
}

void SmartBookmarkSystem::loadSettings()
{
    if (!m_settings) return;
    
    m_maxHistorySize = m_settings->value("navigation/maxHistorySize", 100).toInt();
    m_viewMode = m_settings->value("ui/viewMode", 0).toInt();
    
    // Load bookmarks
    m_settings->beginGroup("bookmarks");
    QStringList keys = m_settings->childKeys();
    
    QMutexLocker locker(&m_dataMutex);
    for (const QString& key : keys) {
        QVariantMap data = m_settings->value(key).toMap();
        if (!data.isEmpty()) {
            SmartBookmark bookmark = variantToBookmark(data);
            if (bookmark.isValid()) {
                m_bookmarks[bookmark.id] = bookmark;
            }
        }
    }
    m_settings->endGroup();
    
    // Load navigation history
    m_settings->beginGroup("history");
    int historySize = m_settings->beginReadArray("entries");
    for (int i = 0; i < historySize; ++i) {
        m_settings->setArrayIndex(i);
        QVariantMap data = m_settings->value("entry").toMap();
        if (!data.isEmpty()) {
            NavigationEntry entry = variantToNavigationEntry(data);
            if (entry.isValid()) {
                m_history.append(entry);
            }
        }
    }
    m_settings->endReadArray();
    m_settings->endGroup();
    
    m_historyIndex = m_settings->value("navigation/historyIndex", -1).toInt();
}

void SmartBookmarkSystem::saveSettings()
{
    if (!m_settings) return;
    
    m_settings->setValue("navigation/maxHistorySize", m_maxHistorySize);
    m_settings->setValue("ui/viewMode", m_viewMode);
    m_settings->setValue("navigation/historyIndex", m_historyIndex);
    
    // Save bookmarks
    m_settings->beginGroup("bookmarks");
    m_settings->remove(""); // Clear existing
    
    QMutexLocker locker(&m_dataMutex);
    for (const SmartBookmark& bookmark : m_bookmarks) {
        QVariantMap data = bookmarkToVariant(bookmark);
        m_settings->setValue(bookmark.id, data);
    }
    m_settings->endGroup();
    
    // Save navigation history
    m_settings->beginGroup("history");
    m_settings->beginWriteArray("entries");
    for (int i = 0; i < m_history.size(); ++i) {
        m_settings->setArrayIndex(i);
        QVariantMap data = navigationEntryToVariant(m_history[i]);
        m_settings->setValue("entry", data);
    }
    m_settings->endWriteArray();
    m_settings->endGroup();
    
    m_settings->sync();
}
