#pragma once

#include <poppler/qt6/poppler-qt6.h>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>
#include "AsyncDocumentLoader.h"
#include "qtmetamacros.h"

class RenderModel;

struct DocumentInfo {
    QString filePath;
    QString fileName;
    std::shared_ptr<Poppler::Document> document;

    DocumentInfo(const QString& path, std::shared_ptr<Poppler::Document> doc)
        : filePath(path), document(std::move(doc)) {
        fileName = QFileInfo(path).baseName();
    }
};

class DocumentModel : public QObject {
    Q_OBJECT

private:
    std::vector<std::shared_ptr<DocumentInfo>> documents;
    int currentDocumentIndex;

    // 异步加载器
    AsyncDocumentLoader* asyncLoader;

    // 正在加载中的文件路径（防止重复加载）
    QSet<QString> m_loadingPaths;

    // 多文档加载队列
    QStringList pendingFiles;

private slots:
    void onDocumentLoaded(Poppler::Document* document, const QString& filePath);

public:
    DocumentModel();
    explicit DocumentModel(RenderModel* /*unused*/) : DocumentModel() {}
    ~DocumentModel() = default;

    // 多文档管理
    bool openFromFile(const QString& filePath);
    bool openFromFiles(const QStringList& filePaths);
    bool closeDocument(int index);
    bool closeCurrentDocument();
    void switchToDocument(int index);

    // 查询方法
    int getDocumentCount() const;
    int getCurrentDocumentIndex() const;
    QString getCurrentFilePath() const;
    QString getCurrentFileName() const;
    QString getDocumentFileName(int index) const;
    QString getDocumentFilePath(int index) const;
    std::shared_ptr<Poppler::Document> getCurrentDocument() const;
    std::shared_ptr<Poppler::Document> getDocument(int index) const;
    bool isEmpty() const;
    bool isValidIndex(int index) const;

signals:
    void documentOpened(int index, const QString& fileName);
    void documentClosed(int index);
    void currentDocumentChanged(int index);
    void allDocumentsClosed();

    // 异步加载相关信号
    void loadingProgressChanged(int progress);
    void loadingMessageChanged(const QString& message);
    void loadingStarted(const QString& filePath);
    void loadingFailed(const QString& error, const QString& filePath);

    // 从合并分支添加的信号
    void renderPageDone(QImage image);
    void pageUpdate(int currentPage, int totalPages);
};
