#pragma once

#include <poppler/qt6/poppler-qt6.h>
#include <QMutex>
#include <QObject>
#include <QSet>

#include <memory>

#include "PDFContinuousImageCache.h"

class PDFContinuousRenderScheduler : public QObject {
    Q_OBJECT

public:
    explicit PDFContinuousRenderScheduler(QObject* parent = nullptr);

    void setDocument(std::shared_ptr<Poppler::Document> document);
    void requestPage(const PDFContinuousRenderRequest& request);
    void requestPages(const QVector<PDFContinuousRenderRequest>& requests);
    void cancelObsoleteRequests(std::uint64_t currentGeneration);
    void clearPendingRequests();
    std::uint64_t currentGeneration() const;
    int pendingRequestCount() const;

signals:
    void pageRendered(const PDFContinuousRenderResult& result);

private:
    static PDFContinuousRenderResult renderPage(
        std::shared_ptr<Poppler::Document> document,
        const PDFContinuousRenderRequest& request,
        std::shared_ptr<QMutex> renderMutex);

    std::shared_ptr<Poppler::Document> m_document;
    QSet<PDFContinuousPendingKey> m_pendingKeys;
    std::uint64_t m_currentGeneration = 0;
    std::shared_ptr<QMutex> m_renderMutex = std::make_shared<QMutex>();
};
