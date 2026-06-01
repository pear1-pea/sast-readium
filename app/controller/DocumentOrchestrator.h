#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class DocumentModel;
class PageModel;
class RenderModel;
class RecentFilesManager;

/**
 * Coordinates document-lifecycle across models.
 *
 * Owns the Model↔Model wiring, exposes clean lifecycle signals, and
 * provides document CRUD operations (open/close/switch) that were
 * previously scattered between ActionDispatcher and MainWindow.
 *
 * Layer: Controller (L2).
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

    // --- Document CRUD ---
    bool openDocument(const QString& filePath);
    bool openDocuments(const QStringList& filePaths);
    bool closeDocument(int index);
    bool closeCurrentDocument();
    void switchToDocument(int index);

signals:
    /** Document was opened — relays DocumentModel::documentOpened. */
    void documentOpened(int index, const QString& fileName);
    void currentDocumentChanged(int index);
    void allDocumentsClosed();

private:
    void setupModelConnections();
    void addToRecentFiles(const QString& filePath);

    DocumentModel* m_documentModel;
    PageModel* m_pageModel;
    RenderModel* m_renderModel;
    RecentFilesManager* m_recentFiles;
};
