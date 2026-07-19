#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include "../controller/tool.hpp"

class ShortcutManager : public QObject {
    Q_OBJECT

public:
    enum class Scope {
        Global,
        Local,
    };

    struct ShortcutDefinition {
        ActionMap action = ActionMap::openFile;
        QString text;
        QString tooltip;
        QKeySequence primaryShortcut;
        QVector<QKeySequence> alternateShortcuts;
        Scope scope = Scope::Global;
        bool checkable = false;
    };

    static ShortcutManager& instance();

    ShortcutManager(QObject* parent = nullptr);

    QAction* actionFor(ActionMap action) const;
    QList<QAction*> actions() const;
    QList<QKeySequence> shortcutsFor(ActionMap action) const;
    bool hasConflict(const QKeySequence& shortcut,
                     Scope scope = Scope::Global) const;

    QAction* registerAction(const ShortcutDefinition& definition);
    void registerDefaults();
    void clear();

signals:
    void actionTriggered(ActionMap action);

private:
    bool registerShortcut(const QKeySequence& shortcut, ActionMap action,
                          Scope scope);

    QHash<int, QAction*> m_actions;
    QHash<int, QList<QKeySequence>> m_shortcutsByAction;
    QHash<QString, ActionMap> m_globalShortcutOwners;
    bool m_defaultsRegistered = false;
};
