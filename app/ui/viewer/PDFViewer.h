#pragma once

#include <poppler/qt6/poppler-qt6.h>
#include <QAbstractScrollArea>
#include <QColor>
#include <QEvent>
#include <QList>
#include <QWidget>
#include <memory>
#include "rag/domain/EvidenceHighlightRequest.h"

#include "ZoomTypes.h"
#include "model/AnnotationModel.h"
#include "model/SearchModel.h"

class QDragEnterEvent;
class QDropEvent;

// 页面查看模式枚举
// 顺序必须与 ToolBar 的 viewModeCombo 索引一致 (0/1/2/3)，
// ViewWidget::setCurrentViewMode 依赖 static_cast<PDFViewMode>(index)。
enum class PDFViewMode {
    SinglePage,        // 单页视图 (index 0)
    ContinuousScroll,  // 连续滚动视图 (index 1)
    DoublePage,        // 双页对开视图 (index 2)
    GridView           // 多页网格视图 (index 3)
};

class PDFViewer : public QWidget {
    Q_OBJECT

public:
    PDFViewer(QWidget* parent = nullptr, bool enableStyling = true);
    ~PDFViewer();

    // 文档操作
    void setDocument(std::shared_ptr<Poppler::Document> document,
                     const QString& filePath);
    void clearDocument();

    // 页面导航
    void goToPage(int pageNumber);
    void nextPage();
    void previousPage();
    void firstPage();
    void lastPage();
    bool goToPageWithValidation(int pageNumber, bool showMessage = true);

    // 缩放操作
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToWidth();
    void zoomToHeight();
    void setZoom(double factor);
    void setZoomWithType(double factor, ZoomType type);
    void setZoomFromPercentage(int percentage);

    // 旋转操作
    void rotateLeft();
    void rotateRight();
    void resetRotation();
    void setRotation(int degrees);

    // 主题切换
    void toggleTheme();
    void updateThemeUI();

    // 搜索功能
    void showSearch();
    void hideSearch();
    void toggleSearch();
    void findNext();
    void findPrevious();
    void clearSearch();
    void copySelectedText();

    // Search highlighting functionality
    void setSearchResults(const QList<SearchResult>& results);
    void clearSearchHighlights();
    void highlightCurrentSearchResult(const SearchResult& result);
    void setEvidenceHighlights(
        const QVector<Rag::EvidenceHighlightRequest>& highlights);
    void clearEvidenceHighlights();

    // 书签功能
    void addBookmark();
    void addBookmarkForPage(int pageNumber);
    void removeBookmark();
    void toggleBookmark();
    bool hasBookmarkForCurrentPage() const;

    // Annotation tools
    AnnotationModel* annotationModel() const;
    void setAnnotationStoragePath(const QString& storagePath);
    bool saveAnnotations();
    bool loadAnnotations();
    void clearAnnotations();
    void setAnnotationTool(AnnotationType tool);
    void setAnnotationMode(bool enabled);
    void setAnnotationColor(const QColor& color);
    void setAnnotationOpacity(double opacity);
    void setAnnotationLineWidth(double width);
    void setAnnotationFont(const QString& family, int size);

    // 查看模式操作
    void setViewMode(PDFViewMode mode);
    PDFViewMode getViewMode() const;

    // 获取状态
    int getCurrentPage() const;
    int getPageCount() const;
    double getCurrentZoom() const;
    bool hasDocument() const;

    // 缓存管理
    void setCacheSize(int maxCostMB);
    int getCacheSize() const;
    void clearCache();

    // 消息显示
    void setMessage(const QString& message);

protected:
    void setupUI();
    void setupConnections();
    void setupShortcuts();
    void updatePageDisplay();
    bool eventFilter(QObject* object, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    // 查看模式相关方法
    void setupViewModes();
    void switchToSinglePageMode();
    void switchToContinuousMode();
    void updateContinuousView(const char* reason);
    void updateContinuousViewRotation();
    void createContinuousPages(const char* reason);
    void rebuildContinuousCanvasBlueprint(const char* reason);
    void handleContinuousResize();
    void updateContinuousCanvasGeometry();

    // 虚拟化渲染方法
    void updateVisiblePages();
    void renderVisiblePages();
    void onScrollChanged();
    void scrollToPageInContinuousView(int pageNumber);
    void finalizeContinuousModeInitialization(int pageNumber,
                                              int attemptsRemaining = 8);

    // 缩放相关方法
    void saveZoomSettings();
    void loadZoomSettings();

    // Search highlighting helper methods
    void updateSearchHighlightsForCurrentPage();
    void updateAllPagesSearchHighlights();
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onScaleChanged(double scale);

    // 搜索相关槽函数
    void onSearchRequested(const QString& query, const SearchOptions& options);
    void onSearchResultSelected(const SearchResult& result);
    void onNavigateToSearchResult(int pageNumber, const QRectF& rect);

private:
    struct Private;
    std::unique_ptr<Private> d;

signals:
    void pageChanged(int pageNumber);
    void zoomChanged(double factor);
    void documentChanged(bool hasDocument);
    void viewModeChanged(PDFViewMode mode);
    void rotationChanged(int degrees);
    void sidebarToggleRequested();
    void searchRequested(const QString& text);
    void bookmarkRequested(int pageNumber);
    void fullscreenToggled(bool fullscreen);
    void fileDropped(const QString& filePath);
    void filesDropped(const QStringList& filePaths);
};
