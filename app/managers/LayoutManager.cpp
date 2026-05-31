#include "LayoutManager.h"
#include "ui/core/RightSideBar.h"
#include "ui/core/SideBar.h"
#include "ui/core/ViewWidget.h"

#include <QBoxLayout>
#include <QSplitter>
#include <QWidget>

LayoutManager::LayoutManager(SideBar* sideBar, RightSideBar* rightSideBar,
                             ViewWidget* viewWidget, QWidget* mainViewerParent,
                             QObject* parent)
    : QObject(parent),
      m_sideBar(sideBar),
      m_rightSideBar(rightSideBar),
      m_viewWidget(viewWidget),
      m_mainViewerParent(mainViewerParent) {}

QWidget* LayoutManager::createLayout() {
    auto* mainViewerWidget = new QWidget(m_mainViewerParent);
    mainViewerWidget->setObjectName("MainViewerWidget");

    auto* mainViewerLayout = new QHBoxLayout(mainViewerWidget);
    mainViewerLayout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Horizontal, mainViewerWidget);
    m_splitter->setObjectName("MainSplitter");

    m_splitter->addWidget(m_sideBar);
    m_splitter->addWidget(m_viewWidget);
    m_splitter->addWidget(m_rightSideBar);
    m_splitter->setCollapsible(0, true);   // Left sidebar collapsible
    m_splitter->setCollapsible(1, false);  // ViewWidget not collapsible
    m_splitter->setCollapsible(2, true);   // Right sidebar collapsible
    m_splitter->setStretchFactor(1, 1);    // ViewWidget gets stretch priority

    // Initial splitter sizes based on sidebar visibility
    int leftWidth = m_sideBar->isVisible() ? m_sideBar->getPreferredWidth() : 0;
    int rightWidth =
        m_rightSideBar->isVisible() ? m_rightSideBar->getPreferredWidth() : 0;
    m_splitter->setSizes({leftWidth, 1000, rightWidth});

    mainViewerLayout->addWidget(m_splitter);

    // Persist sidebar width when splitter is dragged
    connect(m_splitter, &QSplitter::splitterMoved, this,
            &LayoutManager::onSplitterMoved);

    return mainViewerWidget;
}

void LayoutManager::toggleSideBar() { m_sideBar->toggleVisibility(true); }

void LayoutManager::showSideBar() { m_sideBar->show(true); }

void LayoutManager::hideSideBar() { m_sideBar->hide(true); }

void LayoutManager::onSplitterMoved(int pos, int index) {
    if (index == 0 && m_sideBar->isVisible()) {
        QList<int> sizes = m_splitter->sizes();
        if (!sizes.isEmpty()) {
            int newWidth = sizes[0];
            if (newWidth > 0) {
                m_sideBar->setPreferredWidth(newWidth);
                m_sideBar->saveState();
            }
        }
    }
}
