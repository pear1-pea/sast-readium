#pragma once

#include <poppler/qt6/poppler-qt6.h>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVector>

#include "PDFContinuousImageCache.h"

class PDFContinuousRenderScheduler : public QObject {
    Q_OBJECT

public:
    explicit PDFContinuousRenderScheduler(QObject* parent = nullptr);

    void setDocumentSource(const QString& filePath, int pageCount);
    void clearDocumentSource();
    void requestPage(const PDFContinuousRenderRequest& request);
    void requestPages(const QVector<PDFContinuousRenderRequest>& requests);
    void cancelObsoleteRequests(std::uint64_t currentGeneration);
    void clearPendingRequests();
    std::uint64_t currentGeneration() const;
    std::uint64_t documentToken() const;
    int pendingRequestCount() const;

signals:
    void pageRendered(const PDFContinuousRenderResult& result);

private:
    static PDFContinuousRenderResult renderPage(
        const QString& documentPath, std::uint64_t documentToken,
        const PDFContinuousRenderRequest& request);
    void dispatchPendingRequests();

    QString m_documentPath;
    int m_pageCount = 0;
    QSet<PDFContinuousPendingKey> m_pendingKeys;
    QSet<PDFContinuousPendingKey> m_inFlightKeys;
    QVector<PDFContinuousRenderRequest> m_pendingRequests;
    std::uint64_t m_currentGeneration = 0;
    std::uint64_t m_documentToken = 0;
    QThreadPool m_renderPool;
    static constexpr int kMaxInFlightRenders = 2;
};
