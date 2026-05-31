#include "ThemeManager.h"
#include "StyleManager.h"
#include "utils/LoggingMacros.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QLatin1String>

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {}

bool ThemeManager::loadTheme(const QString& theme) {
    if (m_currentTheme == theme) {
        LOG_DEBUG("Theme {} is already applied, skipping", theme.toStdString());
        return false;
    }

    // Try to load an external QSS file from well-known paths.
    QString path = resolveThemePath(theme);
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QFile::ReadOnly)) {
            QString styleSheet = QLatin1String(file.readAll());
            file.close();

            if (!styleSheet.isEmpty()) {
                STYLE.applyThemeStyleSheet(styleSheet);
                m_currentTheme = theme;
                LOG_DEBUG("Applied theme '{}' from {}", theme.toStdString(),
                          path.toStdString());
                emit themeApplied(theme);
                return true;
            }
            LOG_WARNING("QSS file is empty: {}", path.toStdString());
        } else {
            LOG_WARNING("Failed to open QSS file: {}", path.toStdString());
        }
    }

    // Fallback: use StyleManager's built-in styles.
    LOG_WARNING("No external QSS for theme '{}', using StyleManager fallback",
                theme.toStdString());
    QString fallbackStyleSheet = STYLE.getApplicationStyleSheet();
    STYLE.applyThemeStyleSheet(fallbackStyleSheet);
    m_currentTheme = theme;
    LOG_DEBUG("Applied fallback theme: {}", theme.toStdString());
    emit themeApplied(theme);
    emit themeLoadFailed(theme);
    return true;
}

QString ThemeManager::resolveThemePath(const QString& theme) const {
    QStringList possiblePaths = {
        QString("%1/../assets/styles/%2.qss")
            .arg(qApp->applicationDirPath(), theme),
        QString("%1/styles/%2.qss").arg(qApp->applicationDirPath(), theme),
        QString("assets/styles/%2.qss").arg(theme),
        QString("styles/%2.qss").arg(theme),
    };

    for (const QString& candidate : possiblePaths) {
        QFileInfo fi(candidate);
        if (fi.exists() && fi.isReadable()) {
            LOG_DEBUG("Found QSS file: {}", candidate.toStdString());
            return candidate;
        }
    }
    return {};
}
