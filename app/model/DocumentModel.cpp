#include "DocumentModel.h"
#include "RenderModel.h"

DocumentModel::DocumentModel(RenderModel* _renderModel): renderModel(_renderModel) {
    qDebug() << "DocumentModel created";
}

bool DocumentModel::isNULL() { return false; }

bool DocumentModel::openFromFile(const QString& filePath) {
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        qWarning() << "Invalid file path:" << filePath;
        return false;
    }

    auto _document = Poppler::Document::load(filePath);
    if (!_document) {
        qDebug() << "Failed to load document:" << filePath;
        return false;
    }
    if(document){
        document.reset();          // 使用reset而不是release,因为release只是释放了智能指针的控制权，并不会删除对象本身，导致内存泄漏
    }
    document = std::move(_document);
    document->setRenderHint(Poppler::Document::Antialiasing, true);
    document->setRenderHint(Poppler::Document::TextAntialiasing, true);
    document->setRenderHint(Poppler::Document::TextHinting, true);
    document->setRenderHint(Poppler::Document::TextSlightHinting, true);
    currentFilePath = filePath;
    qDebug() << "Opened successfully:" << filePath;

    renderModel->setDocument(document.get());
    // QImage image = renderModel->renderPage(); // 由 PageModel 负责初始渲染

    emit pageUpdate(1, renderModel->getPageCount()); // 当打开新pdf文件时立即更新page数

    return true;
}
