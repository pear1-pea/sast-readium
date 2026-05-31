#pragma once

#include <QObject>
#include <QString>

class DocumentModel;
class PageModel;
class RenderModel;
class RecentFilesManager;

/**
 * Coordinates document-lifecycle across models.
 *
 * Owns the Model↔Model wiring and exposes clean lifecycle signals.
 * View-level routing (StatusBar, ToolBar, MenuBar, SideBar) is handled
 * by the Assembly layer (AppBootstrap / MainWindow).
 *
 * Layer: Application (L3).
 */
class DocumentOrchestrator : public QObject {
    Q_OBJECT

public:
    DocumentOrchestrator(DocumentModel* docModel, PageModel* pageModel,
                         RenderModel* renderModel,
                         RecentFilesManager* recentFiles,
                         QObject* parent = nullptr);
    ~DocumentOrchestrator() override = default;

    // --- Accessors ---
    DocumentModel* documentModel() const { return m_documentModel; }
    PageModel* pageModel() const { return m_pageModel; }
    RenderModel* renderModel() const { return m_renderModel; }
    RecentFilesManager* recentFilesManager() const { return m_recentFiles; }

signals:
    /** Document was opened — relays DocumentModel::documentOpened. */
    void documentOpened(int index, const QString& fileName);
    void currentDocumentChanged(int index);
    void allDocumentsClosed();

private:
    void setupModelConnections();

    DocumentModel* m_documentModel;
    PageModel* m_pageModel;
    RenderModel* m_renderModel;
    RecentFilesManager* m_recentFiles;
};
