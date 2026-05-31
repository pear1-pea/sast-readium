#pragma once

#include <QHash>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QWidget>
#include <functional>
#include "../managers/RecentFilesManager.h"
#include "../model/DocumentModel.h"
#include "tool.hpp"

// 前向声明
class QWidget;

/**
 * Central action dispatch hub.
 *
 * Owns the commandMap (ActionMap → executable lambda) and the
 * document-operation methods (open/close/switch).  All menu-bar and
 * tool-bar actions flow through execute().
 *
 * Layer: Application (L3).
 */
class ActionDispatcher : public QObject {
    Q_OBJECT

private:
    DocumentModel* documentModel;
    RecentFilesManager* recentFilesManager;
    QHash<ActionMap, std::function<void(QWidget*)>> commandMap;
    void initializeCommandMap();

public:
    ActionDispatcher(DocumentModel* model,
                     RecentFilesManager* recentFilesManager = nullptr);
    ~ActionDispatcher() = default;
    void execute(ActionMap actionID, QWidget* context);

    // 多文档操作方法
    bool openDocument(const QString& filePath);
    bool openDocuments(const QStringList& filePaths);
    bool closeDocument(int index);
    bool closeCurrentDocument();
    void switchToDocument(int index);
    void showDocumentMetadata(QWidget* parent);
    void saveDocumentCopy(QWidget* parent);

    // 文件夹扫描功能
    QStringList scanFolderForPDFs(const QString& folderPath);

    // 获取最近文件管理器
    RecentFilesManager* getRecentFilesManager() const {
        return recentFilesManager;
    }

    // 获取文档模型
    DocumentModel* getDocumentModel() const { return documentModel; }

signals:
    void documentOperationCompleted(ActionMap action, bool success);
    void pdfActionRequested(ActionMap action);
};
