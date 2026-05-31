#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * Application-level theme manager.
 *
 * Resolves theme QSS files from well-known paths and applies them via
 * StyleManager.  Falls back to StyleManager's built-in styles when no
 * external QSS file is found.
 *
 * Layer: Application (L3) — owns the "where does the QSS come from" logic.
 */
class ThemeManager : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override = default;

    /** Apply a named theme ("light", "dark", "sepia", …).
     *  Returns true if the theme was actually applied (different from current).
     */
    bool loadTheme(const QString& theme);

    /** The currently active theme name. */
    QString currentTheme() const { return m_currentTheme; }

    /** List of themes that could be loaded (by naming convention). */
    QStringList availableThemes() const { return {"light", "dark"}; }

signals:
    /** Emitted whenever a new theme is applied. */
    void themeApplied(const QString& theme);

    /** Emitted when no external QSS file was found and fallback was used. */
    void themeLoadFailed(const QString& theme);

private:
    QString resolveThemePath(const QString& theme) const;

    QString m_currentTheme;
};
