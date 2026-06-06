#include "DocumentModel.h"
#include <QFileInfo>
#include "utils/LoggingMacros.h"

DocumentModel::DocumentModel() : currentDocumentIndex(-1) {
    asyncLoader = new AsyncDocumentLoader(this);

    connect(asyncLoader, &AsyncDocumentLoader::documentLoaded, this,
            &DocumentModel::onDocumentLoaded);
    connect(asyncLoader, &AsyncDocumentLoader::loadingProgressChanged, this,
            &DocumentModel::loadingProgressChanged);
    connect(asyncLoader, &AsyncDocumentLoader::loadingMessageChanged, this,
            &DocumentModel::loadingMessageChanged);
    connect(asyncLoader, &AsyncDocumentLoader::loadingFailed, this,
            &DocumentModel::loadingFailed);
}

bool DocumentModel::openFromFile(const QString& filePath) {
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        qWarning() << "Invalid file path:" << filePath;
        emit loadingFailed("文件路径无效", filePath);
        return false;
    }

    // 检查文档是否已经打开
    for (size_t i = 0; i < documents.size(); ++i) {
        if (documents[i]->filePath == filePath) {
            switchToDocument(static_cast<int>(i));
            return true;
        }
    }

    // 检查文档是否正在异步加载中
    if (m_loadingPaths.contains(filePath)) {
        LOG_DEBUG("File is already being loaded: {}", filePath.toStdString());
        return true;
    }

    m_loadingPaths.insert(filePath);
    emit loadingStarted(filePath);
    asyncLoader->loadDocument(filePath);

    return true;  // 异步加载，立即返回true
}

bool DocumentModel::openFromFiles(const QStringList& filePaths) {
    if (filePaths.isEmpty()) {
        return false;
    }

    // 过滤掉已经打开或正在加载中的文档
    QStringList newFilePaths;
    for (const QString& filePath : filePaths) {
        if (filePath.isEmpty() || !QFile::exists(filePath)) {
            continue;
        }

        bool alreadyOpen = false;
        for (size_t i = 0; i < documents.size(); ++i) {
            if (documents[i]->filePath == filePath) {
                alreadyOpen = true;
                break;
            }
        }

        if (!alreadyOpen && !m_loadingPaths.contains(filePath)) {
            newFilePaths.append(filePath);
        }
    }

    if (newFilePaths.isEmpty()) {
        // 如果没有新文档需要打开，切换到第一个已存在的文档
        if (!filePaths.isEmpty()) {
            for (size_t i = 0; i < documents.size(); ++i) {
                if (documents[i]->filePath == filePaths.first()) {
                    switchToDocument(static_cast<int>(i));
                    break;
                }
            }
        }
        return true;
    }

    // 优化加载策略：先加载第一个文档
    QString firstFile = newFilePaths.first();
    m_loadingPaths.insert(firstFile);
    emit loadingStarted(firstFile);
    asyncLoader->loadDocument(firstFile);

    // 如果有多个文档，暂时简化实现：逐个加载其他文档
    if (newFilePaths.size() > 1) {
        QStringList remainingFiles = newFilePaths.mid(1);
        for (const QString& path : remainingFiles) {
            m_loadingPaths.insert(path);
        }
        pendingFiles = remainingFiles;
    }

    return true;
}

void DocumentModel::onDocumentLoaded(Poppler::Document* document,
                                     const QString& filePath) {
    m_loadingPaths.remove(filePath);

    if (!document) {
        emit loadingFailed("文档加载失败", filePath);
        // 继续加载下一个队列中的文档
        if (!pendingFiles.isEmpty()) {
            QString nextFile = pendingFiles.takeFirst();
            emit loadingStarted(nextFile);
            asyncLoader->loadDocument(nextFile);
        }
        return;
    }

    // 创建文档信息
    auto docInfo = std::make_shared<DocumentInfo>(
        filePath, std::shared_ptr<Poppler::Document>(document));
    documents.push_back(std::move(docInfo));

    int newIndex = static_cast<int>(documents.size() - 1);
    currentDocumentIndex = newIndex;

    LOG_INFO("Async loaded successfully: {}", filePath.toStdString());
    emit documentOpened(newIndex, documents[newIndex]->fileName);
    emit currentDocumentChanged(newIndex);

    // 检查是否还有待加载的文件
    if (!pendingFiles.isEmpty()) {
        QString nextFile = pendingFiles.takeFirst();
        LOG_DEBUG("Loading next file from queue: {}", nextFile.toStdString());
        emit loadingStarted(nextFile);
        asyncLoader->loadDocument(nextFile);
    }
}

bool DocumentModel::closeDocument(int index) {
    if (!isValidIndex(index)) {
        return false;
    }

    // 清理加载状态：防止异步回调操作已关闭的文档
    const QString filePath = documents[index]->filePath;
    m_loadingPaths.remove(filePath);
    pendingFiles.removeAll(filePath);
    if (asyncLoader->currentFilePath() == filePath) {
        asyncLoader->cancelLoading();
    }

    // 在删除之前发出信号，此时 vector 状态一致，listener 可安全访问
    emit documentClosed(index);

    documents.erase(documents.begin() + index);

    // 调整当前文档索引
    if (documents.empty()) {
        currentDocumentIndex = -1;
        emit allDocumentsClosed();
    } else if (index <= currentDocumentIndex) {
        if (currentDocumentIndex >= static_cast<int>(documents.size())) {
            currentDocumentIndex = static_cast<int>(documents.size()) - 1;
        }
        emit currentDocumentChanged(currentDocumentIndex);
    }

    return true;
}

bool DocumentModel::closeCurrentDocument() {
    return closeDocument(currentDocumentIndex);
}

void DocumentModel::switchToDocument(int index) {
    if (isValidIndex(index) && index != currentDocumentIndex) {
        currentDocumentIndex = index;
        emit currentDocumentChanged(index);
    }
}

int DocumentModel::getDocumentCount() const {
    return static_cast<int>(documents.size());
}

int DocumentModel::getCurrentDocumentIndex() const {
    return currentDocumentIndex;
}

QString DocumentModel::getCurrentFilePath() const {
    if (isValidIndex(currentDocumentIndex)) {
        return documents[currentDocumentIndex]->filePath;
    }
    return QString();
}

QString DocumentModel::getCurrentFileName() const {
    if (isValidIndex(currentDocumentIndex)) {
        return documents[currentDocumentIndex]->fileName;
    }
    return QString();
}

QString DocumentModel::getDocumentFileName(int index) const {
    if (isValidIndex(index)) {
        return documents[index]->fileName;
    }
    return QString();
}

QString DocumentModel::getDocumentFilePath(int index) const {
    if (isValidIndex(index)) {
        return documents[index]->filePath;
    }
    return QString();
}

std::shared_ptr<Poppler::Document> DocumentModel::getCurrentDocument() const {
    if (isValidIndex(currentDocumentIndex)) {
        return documents[currentDocumentIndex]->document;
    }
    return nullptr;
}

std::shared_ptr<Poppler::Document> DocumentModel::getDocument(int index) const {
    if (isValidIndex(index)) {
        return documents[index]->document;
    }
    return nullptr;
}

bool DocumentModel::isEmpty() const { return documents.empty(); }

bool DocumentModel::isValidIndex(int index) const {
    return index >= 0 && index < static_cast<int>(documents.size());
}
