#include "ShortcutManager.h"

#include <QDebug>

ShortcutManager& ShortcutManager::instance() {
    static ShortcutManager manager;
    return manager;
}

ShortcutManager::ShortcutManager(QObject* parent) : QObject(parent) {}

QAction* ShortcutManager::actionFor(ActionMap action) const {
    const auto it = m_actions.constFind(static_cast<int>(action));
    return it == m_actions.constEnd() ? nullptr : it.value();
}

QList<QAction*> ShortcutManager::actions() const {
    QList<QAction*> result;
    result.reserve(m_actions.size());
    for (QAction* action : m_actions) {
        result.push_back(action);
    }
    return result;
}

QList<QKeySequence> ShortcutManager::shortcutsFor(ActionMap action) const {
    return m_shortcutsByAction.value(static_cast<int>(action));
}

bool ShortcutManager::hasConflict(const QKeySequence& shortcut,
                                  Scope scope) const {
    if (shortcut.isEmpty()) {
        return false;
    }
    if (scope != Scope::Global) {
        return false;
    }
    return m_globalShortcutOwners.contains(
        shortcut.toString(QKeySequence::PortableText));
}

QAction* ShortcutManager::registerAction(const ShortcutDefinition& definition) {
    const auto existingIt =
        m_actions.constFind(static_cast<int>(definition.action));
    QAction* existing =
        existingIt == m_actions.constEnd() ? nullptr : existingIt.value();
    QList<QKeySequence> shortcuts;
    auto appendShortcut = [&](const QKeySequence& shortcut) {
        if (shortcut.isEmpty()) {
            return;
        }
        if (registerShortcut(shortcut, definition.action, definition.scope)) {
            shortcuts.append(shortcut);
        }
    };

    appendShortcut(definition.primaryShortcut);
    for (const QKeySequence& shortcut : definition.alternateShortcuts) {
        appendShortcut(shortcut);
    }

    if (existing) {
        if (!definition.text.isEmpty()) {
            existing->setText(definition.text);
        }
        if (!definition.tooltip.isEmpty()) {
            existing->setToolTip(definition.tooltip);
        }
        existing->setCheckable(definition.checkable);

        QList<QKeySequence> merged =
            m_shortcutsByAction.value(static_cast<int>(definition.action));
        for (const QKeySequence& shortcut : shortcuts) {
            if (!merged.contains(shortcut)) {
                merged.append(shortcut);
            }
        }
        if (!merged.isEmpty()) {
            existing->setShortcuts(merged);
        }
        m_shortcutsByAction.insert(static_cast<int>(definition.action), merged);
        return existing;
    }

    auto* action = new QAction(definition.text, this);
    if (!definition.tooltip.isEmpty()) {
        action->setToolTip(definition.tooltip);
    }
    action->setCheckable(definition.checkable);
    action->setShortcutContext(Qt::ApplicationShortcut);

    const auto triggerAction = [this, actionId = definition.action]() {
        emit actionTriggered(actionId);
    };
    connect(action, &QAction::triggered, this, triggerAction);

    if (!shortcuts.isEmpty()) {
        action->setShortcuts(shortcuts);
    }

    m_shortcutsByAction.insert(static_cast<int>(definition.action), shortcuts);
    m_actions.insert(static_cast<int>(definition.action), action);
    return action;
}

void ShortcutManager::registerDefaults() {
    if (m_defaultsRegistered) {
        return;
    }

    clear();

    registerAction({ActionMap::openFile, tr("打开"), tr("打开PDF文件"),
                    QKeySequence("Ctrl+O")});
    registerAction({ActionMap::openFolder, tr("打开文件夹"), tr("打开文件夹"),
                    QKeySequence("Ctrl+Shift+O")});
    registerAction(
        {ActionMap::save, tr("保存"), tr("保存文件"), QKeySequence("Ctrl+S")});
    registerAction({ActionMap::saveAs, tr("另存副本"), tr("另存副本"),
                    QKeySequence("Ctrl+Shift+S")});
    registerAction({ActionMap::showDocumentMetadata,
                    tr("文档属性"),
                    tr("显示文档属性"),
                    QKeySequence("Ctrl+I"),
                    {QKeySequence("Alt+Enter")}});
    registerAction({ActionMap::newTab, tr("新建标签页"), tr("新建标签页"),
                    QKeySequence("Ctrl+T")});
    registerAction({ActionMap::closeCurrentTab, tr("关闭标签页"),
                    tr("关闭当前标签页"), QKeySequence("Ctrl+W")});
    registerAction({ActionMap::closeAllTabs, tr("关闭所有标签页"),
                    tr("关闭所有标签页"), QKeySequence("Ctrl+Shift+W")});
    registerAction({ActionMap::nextTab, tr("下一个标签页"),
                    tr("切换到下一个标签页"), QKeySequence("Ctrl+Tab")});
    registerAction({ActionMap::prevTab, tr("上一个标签页"),
                    tr("切换到上一个标签页"), QKeySequence("Ctrl+Shift+Tab")});
    registerAction({ActionMap::toggleSideBar,
                    tr("切换侧边栏"),
                    tr("切换侧边栏显示"),
                    QKeySequence("F9"),
                    {},
                    Scope::Global,
                    true});
    registerAction({ActionMap::setSinglePageMode, tr("单页视图"),
                    tr("切换到单页视图"), QKeySequence("Ctrl+1")});
    registerAction({ActionMap::setContinuousScrollMode, tr("连续滚动"),
                    tr("切换到连续滚动视图"), QKeySequence("Ctrl+2")});
    registerAction({ActionMap::firstPage, tr("第一页"), tr("跳到第一页"),
                    QKeySequence("Ctrl+Home")});
    registerAction({ActionMap::previousPage, tr("上一页"), tr("上一页"),
                    QKeySequence("Page Up")});
    registerAction({ActionMap::nextPage, tr("下一页"), tr("下一页"),
                    QKeySequence("Page Down")});
    registerAction({ActionMap::lastPage, tr("最后一页"), tr("跳到最后一页"),
                    QKeySequence("Ctrl+End")});
    registerAction({ActionMap::zoomIn,
                    tr("放大"),
                    tr("放大"),
                    QKeySequence("Ctrl++"),
                    {QKeySequence("Ctrl+=")}});
    registerAction(
        {ActionMap::zoomOut, tr("缩小"), tr("缩小"), QKeySequence("Ctrl+-")});
    registerAction({ActionMap::fitToWidth, tr("适应宽度"), tr("适应宽度"),
                    QKeySequence("Ctrl+Shift+1")});
    registerAction({ActionMap::fitToPage, tr("适应页面"), tr("适应页面"),
                    QKeySequence("Ctrl+0")});
    registerAction({ActionMap::fitToHeight, tr("适应高度"), tr("适应高度"),
                    QKeySequence("Ctrl+Shift+2")});
    registerAction({ActionMap::rotateLeft, tr("向左旋转"), tr("向左旋转90度"),
                    QKeySequence("Ctrl+L")});
    registerAction({ActionMap::rotateRight, tr("向右旋转"), tr("向右旋转90度"),
                    QKeySequence("Ctrl+R")});
    registerAction({ActionMap::toggleTheme, tr("切换主题"), tr("切换主题"),
                    QKeySequence("Ctrl+Shift+T")});
    registerAction({ActionMap::showSearch, tr("搜索"), tr("打开搜索面板"),
                    QKeySequence("Ctrl+F")});
    registerAction({ActionMap::findNext, tr("查找下一个"), tr("查找下一个"),
                    QKeySequence("F3")});
    registerAction({ActionMap::findPrevious, tr("查找上一个"), tr("查找上一个"),
                    QKeySequence("Shift+F3")});
    registerAction(
        {ActionMap::fullScreen, tr("全屏"), tr("全屏"), QKeySequence("F11")});

    m_defaultsRegistered = true;
}

void ShortcutManager::clear() {
    qDeleteAll(m_actions);
    m_actions.clear();
    m_shortcutsByAction.clear();
    m_globalShortcutOwners.clear();
    m_defaultsRegistered = false;
}

bool ShortcutManager::registerShortcut(const QKeySequence& shortcut,
                                       ActionMap action, Scope scope) {
    if (shortcut.isEmpty()) {
        return false;
    }

    if (scope == Scope::Global) {
        const QString key = shortcut.toString(QKeySequence::PortableText);
        const auto owner = m_globalShortcutOwners.constFind(key);
        if (owner != m_globalShortcutOwners.constEnd() &&
            owner.value() != action) {
            qWarning() << "Shortcut conflict:" << shortcut.toString()
                       << "already used by" << static_cast<int>(owner.value());
            return false;
        }
        m_globalShortcutOwners.insert(key, action);
    }

    return true;
}
