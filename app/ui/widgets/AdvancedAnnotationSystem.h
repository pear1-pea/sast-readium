#pragma once

#include <QWidget>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QFontDialog>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QTextEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QTabWidget>
#include <QSettings>
#include <QMutex>
#include <QTimer>
#include <QUndoStack>
#include <QUndoCommand>

/**
 * Enhanced annotation types
 */
enum class AdvancedAnnotationType {
    Text,               // Text annotations
    Highlight,          // Text highlighting
    Underline,          // Text underline
    Strikethrough,      // Text strikethrough
    Note,               // Sticky notes
    FreeText,           // Free text annotations
    Line,               // Line drawings
    Arrow,              // Arrow annotations
    Rectangle,          // Rectangle shapes
    Circle,             // Circle shapes
    Polygon,            // Polygon shapes
    Ink,                // Freehand ink annotations
    Stamp,              // Stamp annotations
    Image,              // Image annotations
    Link,               // Hyperlink annotations
    Bookmark,           // Bookmark annotations
    Custom              // Custom annotation types
};

/**
 * Annotation style properties
 */
struct AnnotationStyle {
    QColor color;
    QColor borderColor;
    QColor fillColor;
    int borderWidth;
    Qt::PenStyle borderStyle;
    QFont font;
    double opacity;
    bool hasShadow;
    QColor shadowColor;
    int shadowOffset;
    
    AnnotationStyle()
        : color(Qt::yellow)
        , borderColor(Qt::black)
        , fillColor(Qt::transparent)
        , borderWidth(1)
        , borderStyle(Qt::SolidLine)
        , opacity(0.7)
        , hasShadow(false)
        , shadowColor(Qt::gray)
        , shadowOffset(2) {}
};

/**
 * Enhanced annotation data
 */
struct AdvancedAnnotation {
    QString id;
    AdvancedAnnotationType type;
    int pageNumber;
    QRectF boundingRect;
    QString content;
    QString author;
    QDateTime createdTime;
    QDateTime modifiedTime;
    AnnotationStyle style;
    QVariantMap properties;
    bool isVisible;
    bool isLocked;
    int layerIndex;
    QString category;
    QStringList tags;
    
    AdvancedAnnotation()
        : type(AdvancedAnnotationType::Text)
        , pageNumber(-1)
        , isVisible(true)
        , isLocked(false)
        , layerIndex(0) {}
    
    bool isValid() const {
        return !id.isEmpty() && pageNumber >= 0;
    }
};

/**
 * Annotation layer for organization
 */
struct AnnotationLayer {
    QString id;
    QString name;
    bool isVisible;
    bool isLocked;
    double opacity;
    int zOrder;
    QList<QString> annotationIds;
    
    AnnotationLayer()
        : isVisible(true), isLocked(false), opacity(1.0), zOrder(0) {}
};

/**
 * Annotation filter criteria
 */
struct AnnotationFilter {
    QStringList types;
    QStringList authors;
    QStringList categories;
    QStringList tags;
    QDateTime fromDate;
    QDateTime toDate;
    QString textContent;
    bool caseSensitive;
    
    AnnotationFilter() : caseSensitive(false) {}
    
    bool matches(const AdvancedAnnotation& annotation) const;
};

/**
 * Annotation statistics
 */
struct AnnotationStatistics {
    int totalAnnotations;
    QHash<AdvancedAnnotationType, int> typeCount;
    QHash<QString, int> authorCount;
    QHash<QString, int> categoryCount;
    QHash<int, int> pageCount;
    QDateTime oldestAnnotation;
    QDateTime newestAnnotation;
    
    AnnotationStatistics() : totalAnnotations(0) {}
};

/**
 * Advanced annotation system with comprehensive features
 */
class AdvancedAnnotationSystem : public QWidget
{
    Q_OBJECT

public:
    explicit AdvancedAnnotationSystem(QWidget* parent = nullptr);
    ~AdvancedAnnotationSystem();

    // Annotation management
    QString createAnnotation(AdvancedAnnotationType type, int pageNumber, const QRectF& rect);
    bool updateAnnotation(const QString& id, const AdvancedAnnotation& annotation);
    bool deleteAnnotation(const QString& id);
    AdvancedAnnotation getAnnotation(const QString& id) const;
    QList<AdvancedAnnotation> getAnnotations(int pageNumber = -1) const;
    
    // Layer management
    QString createLayer(const QString& name);
    bool deleteLayer(const QString& layerId);
    bool moveAnnotationToLayer(const QString& annotationId, const QString& layerId);
    QList<AnnotationLayer> getLayers() const;
    
    // Selection and editing
    void selectAnnotation(const QString& id);
    void selectAnnotations(const QStringList& ids);
    QStringList getSelectedAnnotations() const;
    void clearSelection();
    
    // Style management
    void setAnnotationStyle(const QString& id, const AnnotationStyle& style);
    AnnotationStyle getAnnotationStyle(const QString& id) const;
    void applyStyleToSelected(const AnnotationStyle& style);
    
    // Filtering and search
    void setFilter(const AnnotationFilter& filter);
    AnnotationFilter getFilter() const;
    void clearFilter();
    QList<AdvancedAnnotation> searchAnnotations(const QString& text, bool caseSensitive = false) const;
    
    // Import/Export
    bool exportAnnotations(const QString& filePath, const QStringList& annotationIds = QStringList()) const;
    bool importAnnotations(const QString& filePath);
    QString exportToText() const;
    QString exportToHTML() const;
    QByteArray exportToPDF() const;
    
    // Statistics and analysis
    AnnotationStatistics getStatistics() const;
    QHash<QString, int> getTagCloud() const;
    QList<AdvancedAnnotation> getRecentAnnotations(int count = 10) const;
    
    // Undo/Redo
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    void clearUndoStack();
    
    // Settings
    void loadSettings();
    void saveSettings();
    
    // UI state
    void setCurrentTool(AdvancedAnnotationType tool);
    AdvancedAnnotationType getCurrentTool() const { return m_currentTool; }
    void setToolbarVisible(bool visible);
    void setPropertiesVisible(bool visible);

public slots:
    void showAnnotationProperties();
    void hideAnnotationProperties();
    void toggleAnnotationVisibility(bool visible);
    void lockSelectedAnnotations(bool locked);
    void groupSelectedAnnotations();
    void ungroupSelectedAnnotations();
    void alignSelectedAnnotations(Qt::Alignment alignment);
    void distributeSelectedAnnotations(Qt::Orientation orientation);

signals:
    void annotationCreated(const QString& id, const AdvancedAnnotation& annotation);
    void annotationUpdated(const QString& id, const AdvancedAnnotation& annotation);
    void annotationDeleted(const QString& id);
    void annotationSelected(const QString& id);
    void selectionChanged(const QStringList& selectedIds);
    void toolChanged(AdvancedAnnotationType tool);
    void layerChanged(const QString& layerId);
    void filterChanged(const AnnotationFilter& filter);

private slots:
    void onToolActionTriggered();
    void onColorChanged();
    void onFontChanged();
    void onStyleChanged();
    void onLayerSelectionChanged();
    void onAnnotationListItemChanged();
    void onFilterChanged();
    void onPropertiesChanged();

private:
    // UI Components
    QVBoxLayout* m_mainLayout;
    QHBoxLayout* m_toolbarLayout;
    QSplitter* m_mainSplitter;
    
    // Toolbar
    QToolBar* m_toolbar;
    QActionGroup* m_toolGroup;
    QHash<AdvancedAnnotationType, QAction*> m_toolActions;
    
    // Style controls
    QGroupBox* m_styleGroup;
    QPushButton* m_colorButton;
    QPushButton* m_borderColorButton;
    QPushButton* m_fillColorButton;
    QPushButton* m_fontButton;
    QSpinBox* m_borderWidthSpin;
    QSlider* m_opacitySlider;
    QLabel* m_opacityLabel;
    
    // Layer management
    QGroupBox* m_layersGroup;
    QListWidget* m_layersList;
    QPushButton* m_addLayerButton;
    QPushButton* m_deleteLayerButton;
    QPushButton* m_layerUpButton;
    QPushButton* m_layerDownButton;
    
    // Annotation list
    QGroupBox* m_annotationsGroup;
    QTreeWidget* m_annotationsTree;
    QLineEdit* m_filterEdit;
    QComboBox* m_filterTypeCombo;
    QComboBox* m_filterAuthorCombo;
    QPushButton* m_clearFilterButton;
    
    // Properties panel
    QTabWidget* m_propertiesTab;
    QWidget* m_generalTab;
    QWidget* m_styleTab;
    QWidget* m_advancedTab;
    
    // General properties
    QLineEdit* m_contentEdit;
    QLineEdit* m_authorEdit;
    QDateTimeEdit* m_createdEdit;
    QDateTimeEdit* m_modifiedEdit;
    QLineEdit* m_categoryEdit;
    QLineEdit* m_tagsEdit;
    QCheckBox* m_visibleCheck;
    QCheckBox* m_lockedCheck;
    
    // Data management
    QHash<QString, AdvancedAnnotation> m_annotations;
    QHash<QString, AnnotationLayer> m_layers;
    QStringList m_selectedAnnotations;
    AnnotationFilter m_currentFilter;
    mutable QMutex m_dataMutex;
    
    // Current state
    AdvancedAnnotationType m_currentTool;
    AnnotationStyle m_currentStyle;
    QString m_currentLayerId;
    
    // Undo/Redo
    QUndoStack* m_undoStack;
    
    // Settings
    QSettings* m_settings;
    
    // Helper methods
    void setupUI();
    void setupToolbar();
    void setupStyleControls();
    void setupLayerManagement();
    void setupAnnotationList();
    void setupPropertiesPanel();
    void setupConnections();
    
    void updateAnnotationList();
    void updateLayersList();
    void updatePropertiesPanel();
    void updateStyleControls();
    void updateToolbar();
    
    QString generateAnnotationId() const;
    QString generateLayerId() const;
    void applyFilter();
    bool matchesFilter(const AdvancedAnnotation& annotation) const;
    
    // Serialization
    QVariantMap annotationToVariant(const AdvancedAnnotation& annotation) const;
    AdvancedAnnotation variantToAnnotation(const QVariantMap& data) const;
    QVariantMap layerToVariant(const AnnotationLayer& layer) const;
    AnnotationLayer variantToLayer(const QVariantMap& data) const;
};
