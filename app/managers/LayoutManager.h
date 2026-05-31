#pragma once

#include <QObject>

class QSplitter;
class QWidget;
class SideBar;
class RightSideBar;
class ViewWidget;

/**
 * Manages the main viewer layout: splitter, sidebar visibility,
 * and sidebar-width persistence.
 *
 * Layer: Application (L3).
 */
class LayoutManager : public QObject {
    Q_OBJECT

public:
    LayoutManager(SideBar* sideBar, RightSideBar* rightSideBar,
                  ViewWidget* viewWidget, QWidget* mainViewerParent,
                  QObject* parent = nullptr);
    ~LayoutManager() override = default;

    /** Creates and returns the main viewer widget containing the splitter. */
    QWidget* createLayout();

    /** The splitter managed by this layout. */
    QSplitter* splitter() const { return m_splitter; }

public slots:
    void toggleSideBar();
    void showSideBar();
    void hideSideBar();

signals:
    void sideBarVisibilityChanged(bool visible);

private slots:
    void onSplitterMoved(int pos, int index);

private:
    SideBar* m_sideBar;
    RightSideBar* m_rightSideBar;
    ViewWidget* m_viewWidget;
    QWidget* m_mainViewerParent;
    QSplitter* m_splitter = nullptr;
};
