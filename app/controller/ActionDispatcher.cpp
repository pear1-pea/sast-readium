#include "ActionDispatcher.h"
#include <poppler/qt6/poppler-qt6.h>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStringList>
#include "../ui/dialogs/DocumentMetadataDialog.h"
#include "utils/LoggingMacros.h"

void ActionDispatcher::initializeCommandMap() {
    commandMap = {
        {ActionMap::openFile,
         [this](QWidget* ctx) {
             QStringList filePaths = QFileDialog::getOpenFileNames(
                 ctx, tr("Open PDF Files"),
                 QStandardPaths::writableLocation(
                     QStandardPaths::DocumentsLocation),
                 tr("PDF Files (*.pdf)"));
             if (!filePaths.isEmpty()) {
                 bool success = openDocuments(filePaths);
                 emit documentOperationCompleted(ActionMap::openFile, success);
             }
         }},
        {ActionMap::openFolder,
         [this](QWidget* ctx) {
             QString folderPath = QFileDialog::getExistingDirectory(
                 ctx, tr("Open Folder"),
                 QStandardPaths::writableLocation(
                     QStandardPaths::DocumentsLocation),
                 QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
             if (!folderPath.isEmpty()) {
                 QStringList pdfFiles = scanFolderForPDFs(folderPath);
                 if (!pdfFiles.isEmpty()) {
                     bool success = openDocuments(pdfFiles);
                     emit documentOperationCompleted(ActionMap::openFolder,
                                                     success);
                 } else {
                     emit documentOperationCompleted(ActionMap::openFolder,
                                                     false);
                 }
             }
         }},
        {ActionMap::save, [this](QWidget* ctx) { /*....save()....*/ }},
        {ActionMap::saveAs, [this](QWidget* ctx) { saveDocumentCopy(ctx); }},
        {ActionMap::newTab,
         [this](QWidget* ctx) {
             QString filePath = QFileDialog::getOpenFileName(
                 ctx, tr("Open PDF in New Tab"),
                 QStandardPaths::writableLocation(
                     QStandardPaths::DocumentsLocation),
                 tr("PDF Files (*.pdf)"));
             if (!filePath.isEmpty()) {
                 bool success = openDocument(filePath);
                 emit documentOperationCompleted(ActionMap::newTab, success);
             }
         }},
        {ActionMap::closeTab,
         [this](QWidget* ctx) {
             bool success = closeCurrentDocument();
             emit documentOperationCompleted(ActionMap::closeTab, success);
         }},
        {ActionMap::closeCurrentTab,
         [this](QWidget* ctx) {
             bool success = closeCurrentDocument();
             emit documentOperationCompleted(ActionMap::closeCurrentTab,
                                             success);
         }},
        {ActionMap::closeAllTabs,
         [this](QWidget* ctx) {
             bool success = true;
             while (!documentModel->isEmpty()) {
                 if (!closeDocument(0)) {
                     success = false;
                     break;
                 }
             }
             emit documentOperationCompleted(ActionMap::closeAllTabs, success);
         }},
        {ActionMap::nextTab,
         [this](QWidget* ctx) {
             int current = documentModel->getCurrentDocumentIndex();
             int count = documentModel->getDocumentCount();
             if (count > 1) {
                 int next = (current + 1) % count;
                 switchToDocument(next);
                 emit documentOperationCompleted(ActionMap::nextTab, true);
             }
         }},
        {ActionMap::prevTab,
         [this](QWidget* ctx) {
             int current = documentModel->getCurrentDocumentIndex();
             int count = documentModel->getDocumentCount();
             if (count > 1) {
                 int prev = (current - 1 + count) % count;
                 switchToDocument(prev);
                 emit documentOperationCompleted(ActionMap::prevTab, true);
             }
         }},
        // --- UI state actions are forwarded via pdfActionRequested ---
        {ActionMap::toggleSideBar,
         [this](QWidget*) {
             emit pdfActionRequested(ActionMap::toggleSideBar);
         }},
        {ActionMap::showSideBar,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::showSideBar); }},
        {ActionMap::hideSideBar,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::hideSideBar); }},
        {ActionMap::setSinglePageMode,
         [this](QWidget*) {
             emit pdfActionRequested(ActionMap::setSinglePageMode);
         }},
        {ActionMap::setContinuousScrollMode,
         [this](QWidget*) {
             emit pdfActionRequested(ActionMap::setContinuousScrollMode);
         }},
        // 页面导航操作
        {ActionMap::firstPage,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::firstPage); }},
        {ActionMap::previousPage,
         [this](QWidget*) {
             emit pdfActionRequested(ActionMap::previousPage);
         }},
        {ActionMap::nextPage,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::nextPage); }},
        {ActionMap::lastPage,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::lastPage); }},
        {ActionMap::goToPage,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::goToPage); }},
        // 缩放操作
        {ActionMap::zoomIn,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::zoomIn); }},
        {ActionMap::zoomOut,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::zoomOut); }},
        {ActionMap::fitToWidth,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::fitToWidth); }},
        {ActionMap::fitToPage,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::fitToPage); }},
        {ActionMap::fitToHeight,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::fitToHeight); }},
        // 旋转操作
        {ActionMap::rotateLeft,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::rotateLeft); }},
        {ActionMap::rotateRight,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::rotateRight); }},
        // 主题操作
        {ActionMap::toggleTheme,
         [this](QWidget*) { emit pdfActionRequested(ActionMap::toggleTheme); }},
        // 文档信息操作
        {ActionMap::showDocumentMetadata,
         [this](QWidget* ctx) { showDocumentMetadata(ctx); }},
        // 最近文件操作
        {ActionMap::openRecentFile,
         [this](QWidget*) { LOG_DEBUG("openRecentFile action triggered"); }},
        {ActionMap::clearRecentFiles,
         [this](QWidget*) {
             if (recentFilesManager) {
                 recentFilesManager->clearRecentFiles();
             }
         }},
        // 从合并分支添加的操作
        {ActionMap::saveFile, [this](QWidget*) { /*....save()....*/ }}};
}

ActionDispatcher::ActionDispatcher(DocumentModel* model,
                                   RecentFilesManager* recentFilesManager)
    : documentModel(model), recentFilesManager(recentFilesManager) {
    initializeCommandMap();
}

void ActionDispatcher::execute(ActionMap actionID, QWidget* context) {
    LOG_DEBUG("EventID: {} context: {}", static_cast<int>(actionID),
              static_cast<void*>(context));

    auto it = commandMap.find(actionID);
    if (it != commandMap.end()) {
        (*it)(context);
    } else {
        LOG_WARNING("Unknown action ID: {}", static_cast<int>(actionID));
    }
}

bool ActionDispatcher::openDocument(const QString& filePath) {
    bool success = documentModel->openFromFile(filePath);

    if (success && recentFilesManager) {
        recentFilesManager->addRecentFile(filePath);
    }

    return success;
}

bool ActionDispatcher::openDocuments(const QStringList& filePaths) {
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

    bool success = documentModel->openFromFiles(validPaths);

    if (success && recentFilesManager) {
        for (const QString& filePath : validPaths) {
            recentFilesManager->addRecentFile(filePath);
        }
    }

    return success;
}

bool ActionDispatcher::closeDocument(int index) {
    return documentModel->closeDocument(index);
}

bool ActionDispatcher::closeCurrentDocument() {
    return documentModel->closeCurrentDocument();
}

void ActionDispatcher::switchToDocument(int index) {
    documentModel->switchToDocument(index);
}

void ActionDispatcher::showDocumentMetadata(QWidget* parent) {
    if (documentModel->isEmpty()) {
        QMessageBox::information(parent, tr("提示"), tr("请先打开一个PDF文档"));
        return;
    }

    QString currentFilePath = documentModel->getCurrentFilePath();
    QString currentFileName = documentModel->getCurrentFileName();

    QString info =
        QString("文档信息:\n文件名: %1\n路径: %2")
            .arg(currentFileName.isEmpty() ? tr("未知") : currentFileName)
            .arg(currentFilePath.isEmpty() ? tr("未知") : currentFilePath);

    auto currentDoc = documentModel->getCurrentDocument();

    DocumentMetadataDialog* dialog = new DocumentMetadataDialog(parent);
    dialog->setDocument(currentDoc, currentFilePath);
    dialog->exec();
    dialog->deleteLater();
}

void ActionDispatcher::saveDocumentCopy(QWidget* parent) {
    if (documentModel->isEmpty()) {
        QMessageBox::information(parent, tr("提示"), tr("请先打开一个PDF文档"));
        return;
    }

    auto currentDoc = documentModel->getCurrentDocument();
    if (!currentDoc) {
        QMessageBox::warning(parent, tr("错误"), tr("无法获取当前文档"));
        return;
    }

    QString currentFileName = documentModel->getCurrentFileName();
    QString suggestedName = currentFileName.isEmpty()
                                ? "document_copy.pdf"
                                : currentFileName + "_copy.pdf";

    QString filePath = QFileDialog::getSaveFileName(
        parent, tr("另存副本"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
            "/" + suggestedName,
        tr("PDF Files (*.pdf)"));

    if (filePath.isEmpty()) {
        return;
    }

    if (!filePath.toLower().endsWith(".pdf")) {
        filePath += ".pdf";
    }

    bool success = false;
    QString errorMessage;

    try {
        QFileInfo targetInfo(filePath);
        QDir targetDir = targetInfo.dir();

        if (!targetDir.exists()) {
            if (!targetDir.mkpath(targetDir.absolutePath())) {
                errorMessage =
                    tr("无法创建目标目录：%1").arg(targetDir.absolutePath());
                throw std::runtime_error(errorMessage.toStdString());
            }
        }

        if (!targetInfo.dir().isReadable()) {
            errorMessage =
                tr("目标目录不可访问：%1").arg(targetDir.absolutePath());
            throw std::runtime_error(errorMessage.toStdString());
        }

        QString originalPath = documentModel->getCurrentFilePath();
        if (originalPath.isEmpty()) {
            errorMessage = tr("无法获取当前文档的文件路径");
            throw std::runtime_error(errorMessage.toStdString());
        }

        if (!QFile::exists(originalPath)) {
            errorMessage = tr("原始文档文件不存在：%1").arg(originalPath);
            throw std::runtime_error(errorMessage.toStdString());
        }

        QFileInfo originalInfo(originalPath);
        if (!originalInfo.isReadable()) {
            errorMessage = tr("无法读取原始文档文件：%1").arg(originalPath);
            throw std::runtime_error(errorMessage.toStdString());
        }

        if (QFile::exists(filePath)) {
            int result = QMessageBox::question(
                parent, tr("文件已存在"),
                tr("目标文件已存在：\n%1\n\n是否要覆盖现有文件？")
                    .arg(filePath),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

            if (result != QMessageBox::Yes) {
                emit documentOperationCompleted(ActionMap::saveAs, false);
                return;
            }

            if (!QFile::remove(filePath)) {
                errorMessage = tr("无法删除现有文件：%1").arg(filePath);
                throw std::runtime_error(errorMessage.toStdString());
            }
        }

        success = QFile::copy(originalPath, filePath);

        if (!success) {
            errorMessage =
                tr("文件复制失败。可能的原因：\n- 磁盘空间不足\n- "
                   "文件权限问题\n- 目标路径无效");
            throw std::runtime_error(errorMessage.toStdString());
        }

        if (!QFile::exists(filePath)) {
            errorMessage = tr("文件复制完成但无法验证结果文件");
            throw std::runtime_error(errorMessage.toStdString());
        }

        QFileInfo originalFileInfo(originalPath);
        QFileInfo copiedFileInfo(filePath);

        if (originalFileInfo.size() != copiedFileInfo.size()) {
            errorMessage = tr("复制的文件大小不匹配，可能复制不完整");
            QFile::remove(filePath);
            throw std::runtime_error(errorMessage.toStdString());
        }

        QMessageBox::information(
            parent, tr("保存成功"),
            tr("文档副本已成功保存到：\n%1\n\n文件大小：%"
               "2\n\n注意：当前版本将原始PDF文件复制为副本。如需将当前的标注和"
               "修改嵌入到副本中，需要使用专门的PDF编辑功能。")
                .arg(filePath)
                .arg(copiedFileInfo.size()));

    } catch (const std::exception& e) {
        success = false;
        if (errorMessage.isEmpty()) {
            errorMessage = tr("保存过程中发生未知错误：%1")
                               .arg(QString::fromStdString(e.what()));
        }

        QMessageBox::critical(parent, tr("保存失败"), errorMessage);

        if (QFile::exists(filePath)) {
            QFile::remove(filePath);
        }
    } catch (...) {
        success = false;
        errorMessage = tr("保存过程中发生未知错误");

        QMessageBox::critical(parent, tr("保存失败"), errorMessage);

        if (QFile::exists(filePath)) {
            QFile::remove(filePath);
        }
    }

    emit documentOperationCompleted(ActionMap::saveAs, success);
}

QStringList ActionDispatcher::scanFolderForPDFs(const QString& folderPath) {
    QStringList pdfFiles;

    if (folderPath.isEmpty()) {
        LOG_WARNING(
            "ActionDispatcher::scanFolderForPDFs: Empty folder path "
            "provided");
        return pdfFiles;
    }

    QDir dir(folderPath);
    if (!dir.exists()) {
        LOG_WARNING(
            "ActionDispatcher::scanFolderForPDFs: Folder does not exist: {}",
            folderPath.toStdString());
        return pdfFiles;
    }

    LOG_DEBUG("ActionDispatcher: Scanning folder for PDFs: {}",
              folderPath.toStdString());

    QDirIterator it(folderPath,
                    QStringList() << "*.pdf"
                                  << "*.PDF",
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fileInfo(filePath);
        if (fileInfo.exists() && fileInfo.isReadable() && fileInfo.size() > 0) {
            pdfFiles.append(filePath);
            LOG_DEBUG("ActionDispatcher: Found PDF file: {}",
                      filePath.toStdString());
        }
    }

    LOG_DEBUG("ActionDispatcher: Found {} PDF files in folder",
              pdfFiles.size());
    return pdfFiles;
}
