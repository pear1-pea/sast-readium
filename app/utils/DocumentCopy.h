#pragma once

#include <QString>

/**
 * Pure document-copy operations (no UI dependencies).
 *
 * Layer: Foundation (L0) — callable from any layer; callers are responsible
 * for showing dialogs (QFileDialog, QMessageBox).
 */
namespace DocumentCopy {

struct Result {
    bool success = false;
    QString errorMessage;
};

/**
 * Copy a PDF file from @p sourcePath to @p destPath with integrity checks.
 *
 * Creates parent directories if needed, verifies the copy matches the
 * original size, and cleans up on failure.
 */
Result copyFile(const QString& sourcePath, const QString& destPath);

}  // namespace DocumentCopy
