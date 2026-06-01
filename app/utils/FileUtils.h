#pragma once

#include <QString>
#include <QStringList>

/**
 * File system utility functions.
 *
 * Layer: Foundation (L0) — no Qt Widgets dependency, pure free functions.
 */
namespace FileUtils {

/** Recursively scan a directory for PDF files. */
QStringList scanFolderForPDFs(const QString& folderPath);

}  // namespace FileUtils
