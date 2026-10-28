#include "ReadingProgressManager.h"

#include <QCryptographicHash>
#include <QFileInfo>

namespace {
constexpr auto kSettingsGroup = "readingProgress";
constexpr auto kPageKey = "page";
}  // namespace

int ReadingProgressManager::pageForFile(const QString& filePath) const {
    if (filePath.isEmpty()) {
        return 0;
    }

    m_settings.beginGroup(kSettingsGroup);
    m_settings.beginGroup(keyForFile(filePath));
    const int pageNumber = qMax(0, m_settings.value(kPageKey, 0).toInt());
    m_settings.endGroup();
    m_settings.endGroup();
    return pageNumber;
}

void ReadingProgressManager::savePageForFile(const QString& filePath,
                                             int pageNumber) {
    if (filePath.isEmpty()) {
        return;
    }

    m_settings.beginGroup(kSettingsGroup);
    m_settings.beginGroup(keyForFile(filePath));
    m_settings.setValue(kPageKey, qMax(0, pageNumber));
    m_settings.endGroup();
    m_settings.endGroup();
    m_settings.sync();
}

void ReadingProgressManager::removeFile(const QString& filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    m_settings.beginGroup(kSettingsGroup);
    m_settings.remove(keyForFile(filePath));
    m_settings.endGroup();
    m_settings.sync();
}

QString ReadingProgressManager::normalizedPath(const QString& filePath) {
    const QFileInfo fileInfo(filePath);
    if (fileInfo.exists()) {
        return fileInfo.canonicalFilePath();
    }
    return fileInfo.absoluteFilePath();
}

QString ReadingProgressManager::keyForFile(const QString& filePath) {
    return QString::fromLatin1(
        QCryptographicHash::hash(normalizedPath(filePath).toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}
