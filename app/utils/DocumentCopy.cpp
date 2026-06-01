#include "DocumentCopy.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace DocumentCopy {

Result copyFile(const QString& sourcePath, const QString& destPath) {
    Result result;

    if (sourcePath.isEmpty()) {
        result.errorMessage = QStringLiteral("Source path is empty");
        return result;
    }
    if (destPath.isEmpty()) {
        result.errorMessage = QStringLiteral("Destination path is empty");
        return result;
    }

    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        result.errorMessage =
            QStringLiteral("Source file does not exist: %1").arg(sourcePath);
        return result;
    }
    if (!sourceInfo.isReadable()) {
        result.errorMessage =
            QStringLiteral("Source file is not readable: %1").arg(sourcePath);
        return result;
    }

    // Create target directory if needed
    QDir targetDir = sourceInfo.dir();
    if (!targetDir.exists()) {
        if (!targetDir.mkpath(targetDir.absolutePath())) {
            result.errorMessage =
                QStringLiteral("Failed to create target directory: %1")
                    .arg(targetDir.absolutePath());
            return result;
        }
    }

    // Remove existing file at destination
    if (QFile::exists(destPath)) {
        if (!QFile::remove(destPath)) {
            result.errorMessage =
                QStringLiteral("Failed to remove existing file: %1")
                    .arg(destPath);
            return result;
        }
    }

    // Perform the copy
    if (!QFile::copy(sourcePath, destPath)) {
        result.errorMessage =
            QStringLiteral(
                "Failed to copy file (disk full / permission denied): "
                "%1 → %2")
                .arg(sourcePath, destPath);
        return result;
    }

    // Verify destination exists
    if (!QFile::exists(destPath)) {
        result.errorMessage =
            QStringLiteral("Copy completed but destination not found: %1")
                .arg(destPath);
        return result;
    }

    // Verify size integrity
    QFileInfo copiedInfo(destPath);
    if (sourceInfo.size() != copiedInfo.size()) {
        // Size mismatch — clean up the partial copy
        QFile::remove(destPath);
        result.errorMessage =
            QStringLiteral("File size mismatch after copy (%1 vs %2 bytes)")
                .arg(sourceInfo.size())
                .arg(copiedInfo.size());
        return result;
    }

    result.success = true;
    return result;
}

}  // namespace DocumentCopy
