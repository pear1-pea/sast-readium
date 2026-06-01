#include "DocumentOrchestrator.h"
#include "managers/RecentFilesManager.h"
#include "model/DocumentModel.h"
#include "model/PageModel.h"
#include "model/RenderModel.h"
#include "utils/LoggingMacros.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

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

// --- Document CRUD ---

bool DocumentOrchestrator::openDocument(const QString& filePath) {
    bool success = m_documentModel->openFromFile(filePath);
    if (success) {
        addToRecentFiles(filePath);
    }
    return success;
}

bool DocumentOrchestrator::openDocuments(const QStringList& filePaths) {
    if (filePaths.isEmpty()) {
        return false;
    }

    QStringList validPaths;
    for (const QString& filePath : filePaths) {
        if (!filePath.isEmpty() && QFile::exists(filePath) &&
            filePath.toLower().endsWith(".pdf")) {
            validPaths.append(filePath);
        }
    }

    if (validPaths.isEmpty()) {
        LOG_WARNING("No valid PDF files found in the selection");
        return false;
    }

    bool success = m_documentModel->openFromFiles(validPaths);

    if (success && m_recentFiles) {
        for (const QString& filePath : validPaths) {
            m_recentFiles->addRecentFile(filePath);
        }
    }

    return success;
}

bool DocumentOrchestrator::closeDocument(int index) {
    return m_documentModel->closeDocument(index);
}

bool DocumentOrchestrator::closeCurrentDocument() {
    return m_documentModel->closeCurrentDocument();
}

void DocumentOrchestrator::switchToDocument(int index) {
    m_documentModel->switchToDocument(index);
}

// --- Private helpers ---

void DocumentOrchestrator::addToRecentFiles(const QString& filePath) {
    if (m_recentFiles) {
        m_recentFiles->addRecentFile(filePath);
    }
}
