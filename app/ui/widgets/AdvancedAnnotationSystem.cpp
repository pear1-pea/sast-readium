#include "AdvancedAnnotationSystem.h"
#include <QApplication>
#include <QDebug>
#include <QMutexLocker>
#include <QUuid>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QColorDialog>
#include <QFontDialog>
#include <QDateTime>
#include <QRectF>
#include <QRegularExpression>
#include <QFormLayout>
#include <QFileInfo>
#include <QtGlobal>
#include <QtCore>
#include <algorithm>

// AnnotationFilter Implementation
bool AnnotationFilter::matches(const AdvancedAnnotation& annotation) const
{
    // Check type filter
    if (!types.isEmpty()) {
        QString typeStr = QString::number(static_cast<int>(annotation.type));
        if (!types.contains(typeStr)) {
            return false;
        }
    }
    
    // Check author filter
    if (!authors.isEmpty() && !authors.contains(annotation.author)) {
        return false;
    }
    
    // Check category filter
    if (!categories.isEmpty() && !categories.contains(annotation.category)) {
        return false;
    }
    
    // Check tags filter
    if (!tags.isEmpty()) {
        bool hasMatchingTag = false;
        for (const QString& tag : tags) {
            if (annotation.tags.contains(tag)) {
                hasMatchingTag = true;
                break;
            }
        }
        if (!hasMatchingTag) {
            return false;
        }
    }
    
    // Check date range
    if (fromDate.isValid() && annotation.createdTime < fromDate) {
        return false;
    }
    if (toDate.isValid() && annotation.createdTime > toDate) {
        return false;
    }
    
    // Check text content
    if (!textContent.isEmpty()) {
        Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (!annotation.content.contains(textContent, cs)) {
            return false;
        }
    }
    
    return true;
}

// AdvancedAnnotationSystem Implementation
AdvancedAnnotationSystem::AdvancedAnnotationSystem(QWidget* parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_toolbarLayout(nullptr)
    , m_mainSplitter(nullptr)
    , m_toolbar(nullptr)
    , m_toolGroup(nullptr)
    , m_styleGroup(nullptr)
    , m_colorButton(nullptr)
    , m_borderColorButton(nullptr)
    , m_fillColorButton(nullptr)
    , m_fontButton(nullptr)
    , m_borderWidthSpin(nullptr)
    , m_opacitySlider(nullptr)
    , m_opacityLabel(nullptr)
    , m_layersGroup(nullptr)
    , m_layersList(nullptr)
    , m_addLayerButton(nullptr)
    , m_deleteLayerButton(nullptr)
    , m_layerUpButton(nullptr)
    , m_layerDownButton(nullptr)
    , m_annotationsGroup(nullptr)
    , m_annotationsTree(nullptr)
    , m_filterEdit(nullptr)
    , m_filterTypeCombo(nullptr)
    , m_filterAuthorCombo(nullptr)
    , m_clearFilterButton(nullptr)
    , m_propertiesTab(nullptr)
    , m_generalTab(nullptr)
    , m_styleTab(nullptr)
    , m_advancedTab(nullptr)
    , m_contentEdit(nullptr)
    , m_authorEdit(nullptr)
    , m_createdEdit(nullptr)
    , m_modifiedEdit(nullptr)
    , m_categoryEdit(nullptr)
    , m_tagsEdit(nullptr)
    , m_visibleCheck(nullptr)
    , m_lockedCheck(nullptr)
    , m_currentTool(AdvancedAnnotationType::Text)
    , m_undoStack(nullptr)
    , m_settings(nullptr)
{
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-AnnotationSystem", this);
    
    // Initialize undo stack
    m_undoStack = new QUndoStack(this);
    
    // Setup UI
    setupUI();
    setupConnections();
    
    // Load settings
    loadSettings();
    
    // Create default layer
    createLayer("Default");
    
    // Update UI
    updateAnnotationList();
    updateLayersList();
    updateToolbar();
    
    qDebug() << "AdvancedAnnotationSystem: Initialized";
}

AdvancedAnnotationSystem::~AdvancedAnnotationSystem()
{
    saveSettings();
}

void AdvancedAnnotationSystem::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);
    
    // Setup toolbar
    setupToolbar();
    
    // Main splitter
    m_mainSplitter = new QSplitter(Qt::Horizontal);
    
    // Left panel - tools and layers
    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    
    // Style controls
    setupStyleControls();
    leftLayout->addWidget(m_styleGroup);
    
    // Layer management
    setupLayerManagement();
    leftLayout->addWidget(m_layersGroup);
    
    leftLayout->addStretch();
    m_mainSplitter->addWidget(leftPanel);
    
    // Center panel - annotation list
    QWidget* centerPanel = new QWidget();
    QVBoxLayout* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    
    setupAnnotationList();
    centerLayout->addWidget(m_annotationsGroup);
    
    m_mainSplitter->addWidget(centerPanel);
    
    // Right panel - properties
    setupPropertiesPanel();
    m_mainSplitter->addWidget(m_propertiesTab);
    
    // Set splitter sizes
    m_mainSplitter->setSizes(QList<int>() << 200 << 300 << 250);
    
    m_mainLayout->addWidget(m_mainSplitter);
}

void AdvancedAnnotationSystem::setupToolbar()
{
    m_toolbarLayout = new QHBoxLayout();
    
    m_toolbar = new QToolBar();
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_toolbar->setIconSize(QSize(24, 24));
    
    m_toolGroup = new QActionGroup(this);
    
    // Create tool actions
    struct ToolInfo {
        AdvancedAnnotationType type;
        QString name;
        QString icon;
        QString tooltip;
    };
    
    QList<ToolInfo> tools = {
        {AdvancedAnnotationType::Text, "Text", "text", "Add text annotation"},
        {AdvancedAnnotationType::Highlight, "Highlight", "highlight", "Highlight text"},
        {AdvancedAnnotationType::Note, "Note", "note", "Add sticky note"},
        {AdvancedAnnotationType::FreeText, "Free Text", "freetext", "Add free text"},
        {AdvancedAnnotationType::Line, "Line", "line", "Draw line"},
        {AdvancedAnnotationType::Arrow, "Arrow", "arrow", "Draw arrow"},
        {AdvancedAnnotationType::Rectangle, "Rectangle", "rectangle", "Draw rectangle"},
        {AdvancedAnnotationType::Circle, "Circle", "circle", "Draw circle"},
        {AdvancedAnnotationType::Ink, "Ink", "ink", "Freehand drawing"}
    };
    
    for (const ToolInfo& tool : tools) {
        QAction* action = new QAction(tool.name, this);
        action->setCheckable(true);
        action->setToolTip(tool.tooltip);
        action->setData(static_cast<int>(tool.type));
        
        // Set first tool as default
        if (tool.type == AdvancedAnnotationType::Text) {
            action->setChecked(true);
        }
        
        m_toolGroup->addAction(action);
        m_toolbar->addAction(action);
        m_toolActions[tool.type] = action;
    }
    
    m_toolbarLayout->addWidget(m_toolbar);
    m_toolbarLayout->addStretch();
    
    m_mainLayout->addLayout(m_toolbarLayout);
}

void AdvancedAnnotationSystem::setupStyleControls()
{
    m_styleGroup = new QGroupBox("Style");
    QGridLayout* styleLayout = new QGridLayout(m_styleGroup);
    
    // Color controls
    styleLayout->addWidget(new QLabel("Color:"), 0, 0);
    m_colorButton = new QPushButton();
    m_colorButton->setFixedSize(40, 25);
    m_colorButton->setStyleSheet("background-color: yellow; border: 1px solid black;");
    styleLayout->addWidget(m_colorButton, 0, 1);
    
    styleLayout->addWidget(new QLabel("Border:"), 1, 0);
    m_borderColorButton = new QPushButton();
    m_borderColorButton->setFixedSize(40, 25);
    m_borderColorButton->setStyleSheet("background-color: black; border: 1px solid gray;");
    styleLayout->addWidget(m_borderColorButton, 1, 1);
    
    styleLayout->addWidget(new QLabel("Fill:"), 2, 0);
    m_fillColorButton = new QPushButton();
    m_fillColorButton->setFixedSize(40, 25);
    m_fillColorButton->setStyleSheet("background-color: transparent; border: 1px solid gray;");
    styleLayout->addWidget(m_fillColorButton, 2, 1);
    
    // Font control
    styleLayout->addWidget(new QLabel("Font:"), 3, 0);
    m_fontButton = new QPushButton("Arial, 12pt");
    styleLayout->addWidget(m_fontButton, 3, 1);
    
    // Border width
    styleLayout->addWidget(new QLabel("Width:"), 4, 0);
    m_borderWidthSpin = new QSpinBox();
    m_borderWidthSpin->setRange(1, 10);
    m_borderWidthSpin->setValue(1);
    styleLayout->addWidget(m_borderWidthSpin, 4, 1);
    
    // Opacity
    styleLayout->addWidget(new QLabel("Opacity:"), 5, 0);
    QHBoxLayout* opacityLayout = new QHBoxLayout();
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(10, 100);
    m_opacitySlider->setValue(70);
    m_opacityLabel = new QLabel("70%");
    m_opacityLabel->setFixedWidth(30);
    opacityLayout->addWidget(m_opacitySlider);
    opacityLayout->addWidget(m_opacityLabel);
    styleLayout->addLayout(opacityLayout, 5, 1);
}

void AdvancedAnnotationSystem::setupLayerManagement()
{
    m_layersGroup = new QGroupBox("Layers");
    QVBoxLayout* layersLayout = new QVBoxLayout(m_layersGroup);
    
    // Layer list
    m_layersList = new QListWidget();
    m_layersList->setMaximumHeight(120);
    layersLayout->addWidget(m_layersList);
    
    // Layer buttons
    QHBoxLayout* layerButtonsLayout = new QHBoxLayout();
    
    m_addLayerButton = new QPushButton("+");
    m_addLayerButton->setFixedSize(25, 25);
    m_addLayerButton->setToolTip("Add Layer");
    layerButtonsLayout->addWidget(m_addLayerButton);
    
    m_deleteLayerButton = new QPushButton("-");
    m_deleteLayerButton->setFixedSize(25, 25);
    m_deleteLayerButton->setToolTip("Delete Layer");
    layerButtonsLayout->addWidget(m_deleteLayerButton);
    
    layerButtonsLayout->addStretch();
    
    m_layerUpButton = new QPushButton("↑");
    m_layerUpButton->setFixedSize(25, 25);
    m_layerUpButton->setToolTip("Move Up");
    layerButtonsLayout->addWidget(m_layerUpButton);
    
    m_layerDownButton = new QPushButton("↓");
    m_layerDownButton->setFixedSize(25, 25);
    m_layerDownButton->setToolTip("Move Down");
    layerButtonsLayout->addWidget(m_layerDownButton);
    
    layersLayout->addLayout(layerButtonsLayout);
}

void AdvancedAnnotationSystem::setupAnnotationList()
{
    m_annotationsGroup = new QGroupBox("Annotations");
    QVBoxLayout* annotationsLayout = new QVBoxLayout(m_annotationsGroup);
    
    // Filter controls
    QHBoxLayout* filterLayout = new QHBoxLayout();
    
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("Filter annotations...");
    filterLayout->addWidget(m_filterEdit);
    
    m_filterTypeCombo = new QComboBox();
    m_filterTypeCombo->addItem("All Types", -1);
    m_filterTypeCombo->addItem("Text", static_cast<int>(AdvancedAnnotationType::Text));
    m_filterTypeCombo->addItem("Highlight", static_cast<int>(AdvancedAnnotationType::Highlight));
    m_filterTypeCombo->addItem("Note", static_cast<int>(AdvancedAnnotationType::Note));
    filterLayout->addWidget(m_filterTypeCombo);
    
    m_clearFilterButton = new QPushButton("Clear");
    m_clearFilterButton->setMaximumWidth(50);
    filterLayout->addWidget(m_clearFilterButton);
    
    annotationsLayout->addLayout(filterLayout);
    
    // Annotations tree
    m_annotationsTree = new QTreeWidget();
    m_annotationsTree->setHeaderLabels(QStringList() << "Content" << "Page" << "Author" << "Date");
    m_annotationsTree->setRootIsDecorated(false);
    m_annotationsTree->setAlternatingRowColors(true);
    m_annotationsTree->setSortingEnabled(true);
    m_annotationsTree->header()->setStretchLastSection(false);
    m_annotationsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_annotationsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_annotationsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_annotationsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    
    annotationsLayout->addWidget(m_annotationsTree);
}

void AdvancedAnnotationSystem::setupPropertiesPanel()
{
    m_propertiesTab = new QTabWidget();
    
    // General tab
    m_generalTab = new QWidget();
    QFormLayout* generalLayout = new QFormLayout(m_generalTab);
    
    m_contentEdit = new QLineEdit();
    generalLayout->addRow("Content:", m_contentEdit);
    
    m_authorEdit = new QLineEdit();
    generalLayout->addRow("Author:", m_authorEdit);
    
    m_createdEdit = new QDateTimeEdit();
    m_createdEdit->setReadOnly(true);
    generalLayout->addRow("Created:", m_createdEdit);
    
    m_modifiedEdit = new QDateTimeEdit();
    m_modifiedEdit->setReadOnly(true);
    generalLayout->addRow("Modified:", m_modifiedEdit);
    
    m_categoryEdit = new QLineEdit();
    generalLayout->addRow("Category:", m_categoryEdit);
    
    m_tagsEdit = new QLineEdit();
    m_tagsEdit->setPlaceholderText("tag1, tag2, tag3");
    generalLayout->addRow("Tags:", m_tagsEdit);
    
    m_visibleCheck = new QCheckBox();
    generalLayout->addRow("Visible:", m_visibleCheck);
    
    m_lockedCheck = new QCheckBox();
    generalLayout->addRow("Locked:", m_lockedCheck);
    
    m_propertiesTab->addTab(m_generalTab, "General");
    
    // Style tab
    m_styleTab = new QWidget();
    // Style controls would be duplicated here for selected annotation
    m_propertiesTab->addTab(m_styleTab, "Style");
    
    // Advanced tab
    m_advancedTab = new QWidget();
    // Advanced properties would go here
    m_propertiesTab->addTab(m_advancedTab, "Advanced");
}

void AdvancedAnnotationSystem::setupConnections()
{
    // Tool actions
    connect(m_toolGroup, &QActionGroup::triggered, this, &AdvancedAnnotationSystem::onToolActionTriggered);
    
    // Style controls
    connect(m_colorButton, &QPushButton::clicked, this, &AdvancedAnnotationSystem::onColorChanged);
    connect(m_borderColorButton, &QPushButton::clicked, this, &AdvancedAnnotationSystem::onColorChanged);
    connect(m_fillColorButton, &QPushButton::clicked, this, &AdvancedAnnotationSystem::onColorChanged);
    connect(m_fontButton, &QPushButton::clicked, this, &AdvancedAnnotationSystem::onFontChanged);
    connect(m_borderWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AdvancedAnnotationSystem::onStyleChanged);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityLabel->setText(QString("%1%").arg(value));
        onStyleChanged();
    });
    
    // Layer management
    connect(m_layersList, &QListWidget::currentRowChanged, this, &AdvancedAnnotationSystem::onLayerSelectionChanged);
    connect(m_addLayerButton, &QPushButton::clicked, this, [this]() {
        createLayer(QString("Layer %1").arg(m_layers.size() + 1));
    });
    
    // Annotation list
    connect(m_annotationsTree, &QTreeWidget::itemSelectionChanged, this, &AdvancedAnnotationSystem::onAnnotationListItemChanged);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &AdvancedAnnotationSystem::onFilterChanged);
    connect(m_filterTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AdvancedAnnotationSystem::onFilterChanged);
    connect(m_clearFilterButton, &QPushButton::clicked, this, [this]() {
        m_filterEdit->clear();
        m_filterTypeCombo->setCurrentIndex(0);
        clearFilter();
    });
    
    // Properties
    connect(m_contentEdit, &QLineEdit::textChanged, this, &AdvancedAnnotationSystem::onPropertiesChanged);
    connect(m_authorEdit, &QLineEdit::textChanged, this, &AdvancedAnnotationSystem::onPropertiesChanged);
    connect(m_categoryEdit, &QLineEdit::textChanged, this, &AdvancedAnnotationSystem::onPropertiesChanged);
    connect(m_tagsEdit, &QLineEdit::textChanged, this, &AdvancedAnnotationSystem::onPropertiesChanged);
    connect(m_visibleCheck, &QCheckBox::toggled, this, &AdvancedAnnotationSystem::onPropertiesChanged);
    connect(m_lockedCheck, &QCheckBox::toggled, this, &AdvancedAnnotationSystem::onPropertiesChanged);
}

QString AdvancedAnnotationSystem::createAnnotation(AdvancedAnnotationType type, int pageNumber, const QRectF& rect)
{
    QString id = generateAnnotationId();

    AdvancedAnnotation annotation;
    annotation.id = id;
    annotation.type = type;
    annotation.pageNumber = pageNumber;
    annotation.boundingRect = rect;
    annotation.author = QApplication::applicationName();
    annotation.createdTime = QDateTime::currentDateTime();
    annotation.modifiedTime = annotation.createdTime;
    annotation.style = m_currentStyle;
    annotation.layerIndex = 0;

    // Set default content based on type
    switch (type) {
        case AdvancedAnnotationType::Text:
            annotation.content = "Text annotation";
            break;
        case AdvancedAnnotationType::Note:
            annotation.content = "Note";
            break;
        case AdvancedAnnotationType::FreeText:
            annotation.content = "Free text";
            break;
        default:
            annotation.content = QString("Annotation %1").arg(static_cast<int>(type));
            break;
    }

    QMutexLocker locker(&m_dataMutex);
    m_annotations[id] = annotation;
    locker.unlock();

    updateAnnotationList();

    emit annotationCreated(id, annotation);

    qDebug() << "AdvancedAnnotationSystem: Created annotation" << id << "type:" << static_cast<int>(type);
    return id;
}

bool AdvancedAnnotationSystem::updateAnnotation(const QString& id, const AdvancedAnnotation& annotation)
{
    QMutexLocker locker(&m_dataMutex);

    auto it = m_annotations.find(id);
    if (it == m_annotations.end()) {
        return false;
    }

    AdvancedAnnotation updatedAnnotation = annotation;
    updatedAnnotation.id = id;
    updatedAnnotation.modifiedTime = QDateTime::currentDateTime();

    it.value() = updatedAnnotation;
    locker.unlock();

    updateAnnotationList();
    updatePropertiesPanel();

    emit annotationUpdated(id, updatedAnnotation);

    return true;
}

bool AdvancedAnnotationSystem::deleteAnnotation(const QString& id)
{
    QMutexLocker locker(&m_dataMutex);

    auto it = m_annotations.find(id);
    if (it == m_annotations.end()) {
        return false;
    }

    m_annotations.erase(it);

    // Remove from selection
    m_selectedAnnotations.removeAll(id);

    locker.unlock();

    updateAnnotationList();

    emit annotationDeleted(id);

    qDebug() << "AdvancedAnnotationSystem: Deleted annotation" << id;
    return true;
}

AdvancedAnnotation AdvancedAnnotationSystem::getAnnotation(const QString& id) const
{
    QMutexLocker locker(&m_dataMutex);
    return m_annotations.value(id);
}

QList<AdvancedAnnotation> AdvancedAnnotationSystem::getAnnotations(int pageNumber) const
{
    QMutexLocker locker(&m_dataMutex);

    QList<AdvancedAnnotation> result;
    for (const auto& annotation : m_annotations) {
        if (pageNumber == -1 || annotation.pageNumber == pageNumber) {
            if (matchesFilter(annotation)) {
                result.append(annotation);
            }
        }
    }

    return result;
}

QString AdvancedAnnotationSystem::createLayer(const QString& name)
{
    QString id = generateLayerId();

    AnnotationLayer layer;
    layer.id = id;
    layer.name = name;
    layer.zOrder = m_layers.size();

    QMutexLocker locker(&m_dataMutex);
    m_layers[id] = layer;

    if (m_currentLayerId.isEmpty()) {
        m_currentLayerId = id;
    }

    locker.unlock();

    updateLayersList();

    qDebug() << "AdvancedAnnotationSystem: Created layer" << id << name;
    return id;
}

void AdvancedAnnotationSystem::selectAnnotation(const QString& id)
{
    m_selectedAnnotations.clear();
    m_selectedAnnotations.append(id);

    updatePropertiesPanel();

    emit annotationSelected(id);
    emit selectionChanged(m_selectedAnnotations);
}

void AdvancedAnnotationSystem::setCurrentTool(AdvancedAnnotationType tool)
{
    if (m_currentTool == tool) {
        return;
    }

    m_currentTool = tool;

    // Update toolbar
    QAction* action = m_toolActions.value(tool, nullptr);
    if (action) {
        action->setChecked(true);
    }

    emit toolChanged(tool);

    qDebug() << "AdvancedAnnotationSystem: Tool changed to" << static_cast<int>(tool);
}

void AdvancedAnnotationSystem::updateAnnotationList()
{
    m_annotationsTree->clear();

    QList<AdvancedAnnotation> annotations = getAnnotations();

    for (const AdvancedAnnotation& annotation : annotations) {
        QTreeWidgetItem* item = new QTreeWidgetItem();

        // Truncate long content
        QString content = annotation.content;
        if (content.length() > 50) {
            content = content.left(47) + "...";
        }

        item->setText(0, content);
        item->setText(1, QString::number(annotation.pageNumber + 1));
        item->setText(2, annotation.author);
        item->setText(3, annotation.createdTime.toString("MM/dd hh:mm"));

        // Store annotation ID
        item->setData(0, Qt::UserRole, annotation.id);

        // Set icon based on type
        // item->setIcon(0, getIconForAnnotationType(annotation.type));

        // Highlight selected annotations
        if (m_selectedAnnotations.contains(annotation.id)) {
            item->setSelected(true);
        }

        m_annotationsTree->addTopLevelItem(item);
    }

    // Sort by creation time (newest first)
    m_annotationsTree->sortItems(3, Qt::DescendingOrder);
}

void AdvancedAnnotationSystem::updateLayersList()
{
    m_layersList->clear();

    QMutexLocker locker(&m_dataMutex);

    // Sort layers by z-order
    QList<AnnotationLayer> layers = m_layers.values();
    std::sort(layers.begin(), layers.end(),
              [](const AnnotationLayer& a, const AnnotationLayer& b) {
                  return a.zOrder > b.zOrder; // Higher z-order first
              });

    for (const AnnotationLayer& layer : layers) {
        QListWidgetItem* item = new QListWidgetItem(layer.name);
        item->setData(Qt::UserRole, layer.id);
        item->setCheckState(layer.isVisible ? Qt::Checked : Qt::Unchecked);

        if (layer.id == m_currentLayerId) {
            item->setSelected(true);
        }

        m_layersList->addItem(item);
    }
}

void AdvancedAnnotationSystem::updatePropertiesPanel()
{
    if (m_selectedAnnotations.isEmpty()) {
        // Clear properties
        m_contentEdit->clear();
        m_authorEdit->clear();
        m_createdEdit->setDateTime(QDateTime());
        m_modifiedEdit->setDateTime(QDateTime());
        m_categoryEdit->clear();
        m_tagsEdit->clear();
        m_visibleCheck->setChecked(false);
        m_lockedCheck->setChecked(false);
        return;
    }

    // Show properties of first selected annotation
    QString id = m_selectedAnnotations.first();
    AdvancedAnnotation annotation = getAnnotation(id);

    if (!annotation.isValid()) {
        return;
    }

    m_contentEdit->setText(annotation.content);
    m_authorEdit->setText(annotation.author);
    m_createdEdit->setDateTime(annotation.createdTime);
    m_modifiedEdit->setDateTime(annotation.modifiedTime);
    m_categoryEdit->setText(annotation.category);
    m_tagsEdit->setText(annotation.tags.join(", "));
    m_visibleCheck->setChecked(annotation.isVisible);
    m_lockedCheck->setChecked(annotation.isLocked);
}

QString AdvancedAnnotationSystem::generateAnnotationId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString AdvancedAnnotationSystem::generateLayerId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool AdvancedAnnotationSystem::matchesFilter(const AdvancedAnnotation& annotation) const
{
    return m_currentFilter.matches(annotation);
}

void AdvancedAnnotationSystem::onToolActionTriggered()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        AdvancedAnnotationType tool = static_cast<AdvancedAnnotationType>(action->data().toInt());
        setCurrentTool(tool);
    }
}

void AdvancedAnnotationSystem::onColorChanged()
{
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button) return;

    QColor currentColor = Qt::yellow; // Default

    // Parse current color from stylesheet
    QString style = button->styleSheet();
    QRegExp colorRegex("background-color:\\s*(\\w+|#[0-9a-fA-F]{6})");
    if (colorRegex.indexIn(style) != -1) {
        currentColor = QColor(colorRegex.cap(1));
    }

    QColor color = QColorDialog::getColor(currentColor, this);
    if (color.isValid()) {
        button->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(color.name()));

        // Update current style
        if (button == m_colorButton) {
            m_currentStyle.color = color;
        } else if (button == m_borderColorButton) {
            m_currentStyle.borderColor = color;
        } else if (button == m_fillColorButton) {
            m_currentStyle.fillColor = color;
        }

        onStyleChanged();
    }
}

void AdvancedAnnotationSystem::onFontChanged()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, m_currentStyle.font, this);
    if (ok) {
        m_currentStyle.font = font;
        m_fontButton->setText(QString("%1, %2pt").arg(font.family()).arg(font.pointSize()));
        onStyleChanged();
    }
}

void AdvancedAnnotationSystem::onStyleChanged()
{
    // Update current style from controls
    m_currentStyle.borderWidth = m_borderWidthSpin->value();
    m_currentStyle.opacity = m_opacitySlider->value() / 100.0;

    // Apply to selected annotations if any
    if (!m_selectedAnnotations.isEmpty()) {
        applyStyleToSelected(m_currentStyle);
    }
}

void AdvancedAnnotationSystem::applyStyleToSelected(const AnnotationStyle& style)
{
    for (const QString& id : m_selectedAnnotations) {
        setAnnotationStyle(id, style);
    }
}

void AdvancedAnnotationSystem::setAnnotationStyle(const QString& id, const AnnotationStyle& style)
{
    QMutexLocker locker(&m_dataMutex);

    auto it = m_annotations.find(id);
    if (it != m_annotations.end()) {
        it.value().style = style;
        it.value().modifiedTime = QDateTime::currentDateTime();

        emit annotationUpdated(id, it.value());
    }
}

void AdvancedAnnotationSystem::loadSettings()
{
    if (!m_settings) return;

    // Load current style
    m_currentStyle.color = QColor(m_settings->value("style/color", "yellow").toString());
    m_currentStyle.borderColor = QColor(m_settings->value("style/borderColor", "black").toString());
    m_currentStyle.fillColor = QColor(m_settings->value("style/fillColor", "transparent").toString());
    m_currentStyle.borderWidth = m_settings->value("style/borderWidth", 1).toInt();
    m_currentStyle.opacity = m_settings->value("style/opacity", 0.7).toDouble();

    // Update UI controls
    m_colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(m_currentStyle.color.name()));
    m_borderColorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(m_currentStyle.borderColor.name()));
    m_fillColorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(m_currentStyle.fillColor.name()));
    m_borderWidthSpin->setValue(m_currentStyle.borderWidth);
    m_opacitySlider->setValue(static_cast<int>(m_currentStyle.opacity * 100));
    m_opacityLabel->setText(QString("%1%").arg(static_cast<int>(m_currentStyle.opacity * 100)));
}

void AdvancedAnnotationSystem::saveSettings()
{
    if (!m_settings) return;

    m_settings->setValue("style/color", m_currentStyle.color.name());
    m_settings->setValue("style/borderColor", m_currentStyle.borderColor.name());
    m_settings->setValue("style/fillColor", m_currentStyle.fillColor.name());
    m_settings->setValue("style/borderWidth", m_currentStyle.borderWidth);
    m_settings->setValue("style/opacity", m_currentStyle.opacity);

    m_settings->sync();
}
