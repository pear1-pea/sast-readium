#include "RenderModel.h"
#include "qimage.h"
#include "qlogging.h"
#include "utils/LoggingMacros.h"

RenderModel::RenderModel(double dpiX, double dpiY,
                         std::shared_ptr<Poppler::Document> _document,
                         QObject* parent)
    : document(std::move(_document)), QObject(parent), dpiX(dpiX), dpiY(dpiY) {}

QImage RenderModel::renderPage(int pageNum, double xres, double yres, int x,
                               int y, int w, int h) {
    if (!document) {
        LOG_WARNING("Document not loaded");
        return QImage();
    }
    std::unique_ptr<Poppler::Page> pdfPage = document->page(pageNum);
    if (!pdfPage) {
        LOG_WARNING("Page not found: {}", pageNum);
        return QImage();
    }
    QImage image = pdfPage->renderToImage(dpiX * 2, dpiY * 2);
    if (image.isNull()) {
        LOG_ERROR("Failed to render page: {}", pageNum);
        return QImage();
    }
    emit renderPageDone(image);
    return image;
}

int RenderModel::getPageCount() {
    if (!document) {
        return 0;
    }
    return document->numPages();
}

void RenderModel::setDocument(std::shared_ptr<Poppler::Document> _document) {
    document = std::move(_document);
    emit documentChanged(document);
}
