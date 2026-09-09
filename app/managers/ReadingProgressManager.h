#pragma once

#include <QSettings>
#include <QString>

class ReadingProgressManager {
public:
    ReadingProgressManager() = default;

    int pageForFile(const QString& filePath) const;
    void savePageForFile(const QString& filePath, int pageNumber);
    void removeFile(const QString& filePath);

private:
    static QString normalizedPath(const QString& filePath);
    static QString keyForFile(const QString& filePath);

    mutable QSettings m_settings;
};
