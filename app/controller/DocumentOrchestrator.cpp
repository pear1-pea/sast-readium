#include "DocumentOrchestrator.h"
#include "managers/RecentFilesManager.h"
#include "model/DocumentModel.h"
#include "model/PageModel.h"
#include "model/RenderModel.h"

DocumentOrchestrator::DocumentOrchestrator(DocumentModel* docModel,
                                           PageModel* pageModel,
                                           RenderModel* renderModel,
                                           RecentFilesManager* recentFiles,
                                           QObject* parent)
    : QObject(parent),
      m_documentModel(docModel),
      m_pageModel(pageModel),
      m_renderModel(renderModel),
      m_recentFiles(recentFiles) {
    setupModelConnections();
}

void DocumentOrchestrator::setupModelConnections() {
    if (!m_documentModel || !m_pageModel || !m_renderModel)
        return;

    // PageModel stays in sync with RenderModel's document.
    connect(m_renderModel, &RenderModel::documentChanged, m_pageModel,
            &PageModel::updateInfo);

    // Relay life-cycle signals so consumers can bind to one source.
    connect(m_documentModel, &DocumentModel::documentOpened, this,
            &DocumentOrchestrator::documentOpened);
    connect(m_documentModel, &DocumentModel::currentDocumentChanged, this,
            &DocumentOrchestrator::currentDocumentChanged);
    connect(m_documentModel, &DocumentModel::allDocumentsClosed, this,
            &DocumentOrchestrator::allDocumentsClosed);
}
