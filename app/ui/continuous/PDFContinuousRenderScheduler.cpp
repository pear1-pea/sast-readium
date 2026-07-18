#include "PDFContinuousRenderScheduler.h"

#include <QFutureWatcher>
#include <QMutexLocker>
#include <QtConcurrent>

#include "utils/LoggingMacros.h"

PDFContinuousRenderScheduler::PDFContinuousRenderScheduler(QObject* parent)
    : QObject(parent) {}

void PDFContinuousRenderScheduler::setDocument(
    std::shared_ptr<Poppler::Document> document) {
    m_document = std::move(document);
    clearPendingRequests();
}

void PDFContinuousRenderScheduler::requestPage(
    const PDFContinuousRenderRequest& request) {
    if (!m_document) {
        LOG_DEBUG(
            "[continuous-render] drop request reason=no_document page={} "
            "generation={} current_generation={}",
            request.key.pageIndex, request.generation, m_currentGeneration);
        return;
    }

    if (request.key.pageIndex < 0 ||
        request.key.pageIndex >= m_document->numPages()) {
        LOG_DEBUG(
            "[continuous-render] drop request reason=page_out_of_range page={} "
            "pages={} generation={} current_generation={}",
            request.key.pageIndex, m_document->numPages(), request.generation,
            m_currentGeneration);
        return;
    }

    if (request.generation != m_currentGeneration) {
        LOG_DEBUG(
            "[continuous-render] drop request reason=stale_generation page={} "
            "generation={} current_generation={}",
            request.key.pageIndex, request.generation, m_currentGeneration);
        return;
    }

    const PDFContinuousPendingKey pendingKey{request.generation, request.key};
    if (m_pendingKeys.contains(pendingKey)) {
        LOG_DEBUG(
            "[continuous-render] drop request reason=pending_duplicate page={} "
            "generation={} zoom={:.3f} rotation={} dpr={:.2f}",
            request.key.pageIndex, request.generation, request.key.zoom,
            request.key.rotation, request.key.devicePixelRatio);
        return;
    }
    m_pendingKeys.insert(pendingKey);

    LOG_DEBUG(
        "[continuous-render] submit request page={} generation={} zoom={:.3f} "
        "rotation={} dpr={:.2f} target={}x{}",
        request.key.pageIndex, request.generation, request.key.zoom,
        request.key.rotation, request.key.devicePixelRatio,
        request.targetPixelSize.width(), request.targetPixelSize.height());

    auto* watcher = new QFutureWatcher<PDFContinuousRenderResult>(this);
    connect(
        watcher, &QFutureWatcher<PDFContinuousRenderResult>::finished, this,
        [this, watcher]() {
            PDFContinuousRenderResult result = watcher->result();
            watcher->deleteLater();
            const PDFContinuousPendingKey pendingKey{result.request.generation,
                                                     result.request.key};
            m_pendingKeys.remove(pendingKey);
            if (result.request.generation != m_currentGeneration) {
                LOG_DEBUG(
                    "[continuous-render] drop result reason=stale_generation "
                    "page={} generation={} current_generation={}",
                    result.request.key.pageIndex, result.request.generation,
                    m_currentGeneration);
                return;
            }
            if (result.success) {
                LOG_DEBUG(
                    "[continuous-render] result success page={} generation={} "
                    "image={}x{}",
                    result.request.key.pageIndex, result.request.generation,
                    result.image.width(), result.image.height());
            } else {
                LOG_WARNING(
                    "[continuous-render] result error page={} generation={} "
                    "error={}",
                    result.request.key.pageIndex, result.request.generation,
                    result.error.toStdString());
            }
            emit pageRendered(result);
        });

    watcher->setFuture(
        QtConcurrent::run(&PDFContinuousRenderScheduler::renderPage, m_document,
                          request, m_renderMutex));
}

void PDFContinuousRenderScheduler::requestPages(
    const QVector<PDFContinuousRenderRequest>& requests) {
    for (const PDFContinuousRenderRequest& request : requests) {
        requestPage(request);
    }
}

void PDFContinuousRenderScheduler::cancelObsoleteRequests(
    std::uint64_t currentGeneration) {
    m_currentGeneration = currentGeneration;
    m_pendingKeys.clear();
}

void PDFContinuousRenderScheduler::clearPendingRequests() {
    m_pendingKeys.clear();
    ++m_currentGeneration;
}

std::uint64_t PDFContinuousRenderScheduler::currentGeneration() const {
    return m_currentGeneration;
}

int PDFContinuousRenderScheduler::pendingRequestCount() const {
    return m_pendingKeys.size();
}

PDFContinuousRenderResult PDFContinuousRenderScheduler::renderPage(
    std::shared_ptr<Poppler::Document> document,
    const PDFContinuousRenderRequest& request,
    std::shared_ptr<QMutex> renderMutex) {
    PDFContinuousRenderResult result;
    result.request = request;

    if (!document) {
        result.error = QStringLiteral("No document");
        return result;
    }

    QMutexLocker locker(renderMutex.get());
    std::unique_ptr<Poppler::Page> page(document->page(request.key.pageIndex));
    if (!page) {
        result.error = QStringLiteral("Cannot access page");
        return result;
    }

    const double dpi = 72.0 * request.key.zoom * request.key.devicePixelRatio;
    result.image = page->renderToImage(
        dpi, dpi, -1, -1, -1, -1,
        static_cast<Poppler::Page::Rotation>(request.key.rotation / 90));
    if (result.image.isNull()) {
        result.error = QStringLiteral("Render returned empty image");
        return result;
    }

    result.image.setDevicePixelRatio(request.key.devicePixelRatio);
    result.success = true;
    return result;
}
