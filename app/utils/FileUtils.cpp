#include "FileUtils.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>

namespace FileUtils {

QStringList scanFolderForPDFs(const QString& folderPath) {
    QStringList pdfFiles;

    if (folderPath.isEmpty()) {
        return pdfFiles;
    }

    QDir dir(folderPath);
    if (!dir.exists()) {
        return pdfFiles;
    }

    QDirIterator it(folderPath,
                    QStringList() << "*.pdf"
                                  << "*.PDF",
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fileInfo(filePath);
        if (fileInfo.exists() && fileInfo.isReadable() && fileInfo.size() > 0) {
            pdfFiles.append(filePath);
        }
    }

    return pdfFiles;
}

}  // namespace FileUtils
