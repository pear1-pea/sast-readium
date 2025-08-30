#include "RenderModel.h"
#include "qimage.h"
#include "qlogging.h"

RenderModel::RenderModel(double dpiX, double dpiY,
                         Poppler::Document *_document,
                         QObject *parent)
    : document(_document), QObject(parent), dpiX(dpiX), dpiY(dpiY) {}

QImage RenderModel::renderPage(int pageNum, double xres, double yres, int x, int y, int w, int h) {
    if(!document){
        qDebug() << "Document not loaded";
        return QImage();
    }
    std::unique_ptr<Poppler::Page> pdfPage = document->page(pageNum);
    if (!pdfPage) {
        qDebug() << "Page not found";
        return QImage();
    }
    QImage image = pdfPage->renderToImage(dpiX*2, dpiY*2);
    if(image.isNull()){
        qDebug() << "Failed to render page";
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

void RenderModel::setDocument(Poppler::Document* _document) {
    if (!_document) {
        return;
    }
    // document.reset(_document);       //  这里不能用reset，因为_document是外部传入的智能指针，
                                        //  app\model\DocumentModel.cpp已经reset()过了
    document = _document;               //  直接赋值防止重复reset导致崩溃
    emit documentChanged(document);
}
