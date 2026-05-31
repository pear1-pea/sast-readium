#pragma once

#include <poppler/qt6/poppler-qt6.h>
#include <QEvent>
#include <QList>
#include <QWidget>
#include <memory>

#include "ZoomTypes.h"
#include "model/SearchModel.h"

// 页面查看模式枚举
enum class PDFViewMode {
    SinglePage,       // 单页视图
    ContinuousScroll  // 连续滚动视图
};

class PDFViewer : public QWidget {
    Q_OBJECT

public:
    PDFViewer(QWidget* parent = nullptr, bool enableStyling = true);
    ~PDFViewer();

    // 文档操作
    void setDocument(std::shared_ptr<Poppler::Document> document);
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

    // Search highlighting functionality
    void setSearchResults(const QList<SearchResult>& results);
    void clearSearchHighlights();
    void highlightCurrentSearchResult(const SearchResult& result);

    // 书签功能
    void addBookmark();
    void addBookmarkForPage(int pageNumber);
    void removeBookmark();
    void toggleBookmark();
    bool hasBookmarkForCurrentPage() const;

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
    void updateContinuousView();
    void updateContinuousViewRotation();
    void createContinuousPages();

    // 虚拟化渲染方法
    void updateVisiblePages();
    void renderVisiblePages();
    void onScrollChanged();
    void scrollToPageInContinuousView(int pageNumber);

    // 缩放相关方法
    void saveZoomSettings();
    void loadZoomSettings();

    // Search highlighting helper methods
    void updateSearchHighlightsForCurrentPage();
    void updateAllPagesSearchHighlights();

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
};
