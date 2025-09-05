#include "AdvancedShortcutManager.h"
#include <QApplication>
#include <QKeyEvent>
#include <QDebug>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QMessageBox>
#include <QDateTime>

AdvancedShortcutManager::AdvancedShortcutManager(QObject* parent)
    : QObject(parent)
    , m_currentContext(ShortcutContext::Global)
    , m_contextTimer(nullptr)
    , m_chordTimer(nullptr)
    , m_chordTimeout(1000)
    , m_autoResolveConflicts(true)
    , m_contextSwitchingEnabled(true)
    , m_shortcutsEnabled(true)
    , m_settings(nullptr)
{
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-ShortcutManager", this);
    
    // Setup timers
    m_contextTimer = new QTimer(this);
    m_contextTimer->setSingleShot(true);
    m_contextTimer->setInterval(100);
    connect(m_contextTimer, &QTimer::timeout, this, &AdvancedShortcutManager::onContextTimer);
    
    m_chordTimer = new QTimer(this);
    m_chordTimer->setSingleShot(true);
    connect(m_chordTimer, &QTimer::timeout, this, &AdvancedShortcutManager::onChordTimeout);
    
    // Initialize default shortcuts
    initializeDefaultShortcuts();
    
    // Load user settings
    loadSettings();
    
    qDebug() << "AdvancedShortcutManager: Initialized with" << m_shortcuts.size() << "shortcuts";
}

AdvancedShortcutManager::~AdvancedShortcutManager()
{
    saveSettings();
}

void AdvancedShortcutManager::initializeDefaultShortcuts()
{
    // Navigation shortcuts
    registerShortcut("nav.next_page", "Next Page", QKeySequence(Qt::Key_Right), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::Navigation);
    
    registerShortcut("nav.prev_page", "Previous Page", QKeySequence(Qt::Key_Left), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::Navigation);
    
    registerShortcut("nav.first_page", "First Page", QKeySequence(Qt::CTRL + Qt::Key_Home), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::Navigation);
    
    registerShortcut("nav.last_page", "Last Page", QKeySequence(Qt::CTRL + Qt::Key_End), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::Navigation);
    
    registerShortcut("nav.goto_page", "Go to Page", QKeySequence(Qt::CTRL + Qt::Key_G), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::Navigation);
    
    // Zoom shortcuts
    registerShortcut("view.zoom_in", "Zoom In", QKeySequence(Qt::CTRL + Qt::Key_Plus), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::ViewControl);
    
    registerShortcut("view.zoom_out", "Zoom Out", QKeySequence(Qt::CTRL + Qt::Key_Minus), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::ViewControl);
    
    registerShortcut("view.zoom_fit", "Fit to Window", QKeySequence(Qt::CTRL + Qt::Key_0), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::ViewControl);
    
    registerShortcut("view.zoom_width", "Fit Width", QKeySequence(Qt::CTRL + Qt::Key_1), 
                    [](){}, ShortcutContext::DocumentView, ShortcutCategory::ViewControl);
    
    // File operations
    registerShortcut("file.open", "Open Document", QKeySequence::Open, 
                    [](){}, ShortcutContext::Global, ShortcutCategory::FileOperations);
    
    registerShortcut("file.close", "Close Document", QKeySequence::Close, 
                    [](){}, ShortcutContext::Global, ShortcutCategory::FileOperations);
    
    registerShortcut("file.save", "Save", QKeySequence::Save, 
                    [](){}, ShortcutContext::Global, ShortcutCategory::FileOperations);
    
    registerShortcut("file.print", "Print", QKeySequence::Print, 
                    [](){}, ShortcutContext::Global, ShortcutCategory::FileOperations);
    
    // Search shortcuts
    registerShortcut("search.find", "Find", QKeySequence::Find, 
                    [](){}, ShortcutContext::Global, ShortcutCategory::Search);
    
    registerShortcut("search.find_next", "Find Next", QKeySequence::FindNext, 
                    [](){}, ShortcutContext::SearchMode, ShortcutCategory::Search);
    
    registerShortcut("search.find_prev", "Find Previous", QKeySequence::FindPrevious, 
                    [](){}, ShortcutContext::SearchMode, ShortcutCategory::Search);
    
    // Application shortcuts
    registerShortcut("app.fullscreen", "Toggle Fullscreen", QKeySequence(Qt::Key_F11), 
                    [](){}, ShortcutContext::Global, ShortcutCategory::Application);
    
    registerShortcut("app.preferences", "Preferences", QKeySequence::Preferences, 
                    [](){}, ShortcutContext::Global, ShortcutCategory::Application);
    
    registerShortcut("app.quit", "Quit", QKeySequence::Quit, 
                    [](){}, ShortcutContext::Global, ShortcutCategory::Application);
    
    // View control
    registerShortcut("view.thumbnails", "Toggle Thumbnails", QKeySequence(Qt::Key_F9), 
                    [](){}, ShortcutContext::Global, ShortcutCategory::ViewControl);
    
    registerShortcut("view.bookmarks", "Toggle Bookmarks", QKeySequence(Qt::CTRL + Qt::Key_B), 
                    [](){}, ShortcutContext::Global, ShortcutCategory::ViewControl);
    
    registerShortcut("view.outline", "Toggle Outline", QKeySequence(Qt::Key_F8), 
                    [](){}, ShortcutContext::Global, ShortcutCategory::ViewControl);
}

QString AdvancedShortcutManager::registerShortcut(const QString& id, const QString& name, 
                                                 const QKeySequence& sequence, 
                                                 const std::function<void()>& callback,
                                                 ShortcutContext context, ShortcutCategory category)
{
    ShortcutAction action;
    action.id = id;
    action.name = name;
    action.defaultSequence = sequence;
    action.currentSequence = sequence;
    action.context = context;
    action.category = category;
    action.callback = callback;
    
    return registerShortcut(action);
}

QString AdvancedShortcutManager::registerShortcut(const ShortcutAction& action)
{
    QMutexLocker locker(&m_shortcutMutex);
    
    QString id = action.id;
    if (id.isEmpty()) {
        id = generateUniqueId(action.name);
    }
    
    // Check for conflicts
    if (!action.currentSequence.isEmpty()) {
        auto conflictId = m_sequenceMap.value(action.currentSequence);
        if (!conflictId.isEmpty() && conflictId != id) {
            if (m_autoResolveConflicts) {
                // Remove conflicting shortcut
                m_sequenceMap.remove(action.currentSequence);
                qWarning() << "AdvancedShortcutManager: Resolved conflict by removing" << conflictId;
            } else {
                qWarning() << "AdvancedShortcutManager: Shortcut conflict detected for" << action.currentSequence.toString();
                return QString();
            }
        }
        
        m_sequenceMap[action.currentSequence] = id;
    }
    
    ShortcutAction newAction = action;
    newAction.id = id;
    m_shortcuts[id] = newAction;
    
    locker.unlock();
    
    // Create Qt shortcut object
    createShortcutObject(id);
    
    qDebug() << "AdvancedShortcutManager: Registered shortcut" << id << action.currentSequence.toString();
    return id;
}

bool AdvancedShortcutManager::unregisterShortcut(const QString& id)
{
    QMutexLocker locker(&m_shortcutMutex);
    
    auto it = m_shortcuts.find(id);
    if (it == m_shortcuts.end()) {
        return false;
    }
    
    // Remove from sequence map
    m_sequenceMap.remove(it.value().currentSequence);
    
    // Remove shortcut object
    removeShortcutObject(id);
    
    // Remove from shortcuts
    m_shortcuts.erase(it);
    
    qDebug() << "AdvancedShortcutManager: Unregistered shortcut" << id;
    return true;
}

void AdvancedShortcutManager::setCurrentContext(ShortcutContext context)
{
    if (m_currentContext == context) {
        return;
    }
    
    ShortcutContext oldContext = m_currentContext;
    m_currentContext = context;
    
    // Update all shortcut objects for new context
    for (const auto& action : m_shortcuts) {
        updateShortcutObject(action.id);
    }
    
    emit contextChanged(oldContext, context);
    
    qDebug() << "AdvancedShortcutManager: Context changed to" << static_cast<int>(context);
}

void AdvancedShortcutManager::pushContext(ShortcutContext context)
{
    m_contextStack.push_back(m_currentContext);
    setCurrentContext(context);
}

void AdvancedShortcutManager::popContext()
{
    if (!m_contextStack.isEmpty()) {
        ShortcutContext previousContext = m_contextStack.takeLast();
        setCurrentContext(previousContext);
    }
}

bool AdvancedShortcutManager::setShortcut(const QString& id, const QKeySequence& sequence)
{
    QMutexLocker locker(&m_shortcutMutex);
    
    auto it = m_shortcuts.find(id);
    if (it == m_shortcuts.end()) {
        return false;
    }
    
    // Remove old sequence mapping
    m_sequenceMap.remove(it.value().currentSequence);
    
    // Check for conflicts with new sequence
    if (!sequence.isEmpty()) {
        auto conflictId = m_sequenceMap.value(sequence);
        if (!conflictId.isEmpty()) {
            if (m_autoResolveConflicts) {
                // Remove conflicting shortcut
                auto conflictIt = m_shortcuts.find(conflictId);
                if (conflictIt != m_shortcuts.end()) {
                    conflictIt.value().currentSequence = QKeySequence();
                    updateShortcutObject(conflictId);
                }
                m_sequenceMap.remove(sequence);
            } else {
                qWarning() << "AdvancedShortcutManager: Cannot set shortcut due to conflict";
                return false;
            }
        }
        
        m_sequenceMap[sequence] = id;
    }
    
    QKeySequence oldSequence = it.value().currentSequence;
    it.value().currentSequence = sequence;
    
    locker.unlock();
    
    // Update shortcut object
    updateShortcutObject(id);
    
    emit shortcutChanged(id, oldSequence, sequence);
    
    qDebug() << "AdvancedShortcutManager: Updated shortcut" << id << "to" << sequence.toString();
    return true;
}

void AdvancedShortcutManager::createShortcutObject(const QString& id)
{
    QMutexLocker locker(&m_shortcutMutex);
    
    auto it = m_shortcuts.find(id);
    if (it == m_shortcuts.end()) {
        return;
    }
    
    ShortcutAction& action = it.value();
    
    if (action.action) {
        delete action.action;
    }
    
    action.action = new QShortcut(this);
    action.action->setKey(action.currentSequence);
    action.action->setEnabled(action.isEnabled && isShortcutValidInContext(id, m_currentContext));
    
    connect(action.action, &QShortcut::activated, this, [this, id]() {
        QMutexLocker locker(&m_shortcutMutex);
        auto it = m_shortcuts.find(id);
        if (it != m_shortcuts.end() && it.value().callback) {
            locker.unlock();
            it.value().callback();
            emit shortcutActivated(id);
        }
    });
}

void AdvancedShortcutManager::updateShortcutObject(const QString& id)
{
    QMutexLocker locker(&m_shortcutMutex);
    
    auto it = m_shortcuts.find(id);
    if (it == m_shortcuts.end() || !it.value().action) {
        return;
    }
    
    ShortcutAction& action = it.value();
    action.action->setKey(action.currentSequence);
    action.action->setEnabled(action.isEnabled && isShortcutValidInContext(id, m_currentContext));
}

void AdvancedShortcutManager::removeShortcutObject(const QString& id)
{
    QMutexLocker locker(&m_shortcutMutex);
    
    auto it = m_shortcuts.find(id);
    if (it != m_shortcuts.end() && it.value().action) {
        delete it.value().action;
        it.value().action = nullptr;
    }
}

bool AdvancedShortcutManager::isShortcutValidInContext(const QString& id, ShortcutContext context) const
{
    auto it = m_shortcuts.find(id);
    if (it == m_shortcuts.end()) {
        return false;
    }
    
    const ShortcutAction& action = it.value();
    
    // Global shortcuts are always valid
    if (action.context == ShortcutContext::Global) {
        return true;
    }
    
    // Check if current context matches or is compatible
    return action.context == context || isContextActive(action.context);
}

bool AdvancedShortcutManager::isContextActive(ShortcutContext context) const
{
    // Check if the given context is currently active
    // This would depend on the application state
    
    switch (context) {
        case ShortcutContext::Global:
            return true;
            
        case ShortcutContext::DocumentView:
            // Would check if document view has focus
            return m_currentContext == ShortcutContext::DocumentView;
            
        case ShortcutContext::SearchMode:
            return m_currentContext == ShortcutContext::SearchMode;
            
        case ShortcutContext::FullscreenMode:
            return m_currentContext == ShortcutContext::FullscreenMode;
            
        default:
            return m_currentContext == context;
    }
}

QString AdvancedShortcutManager::generateUniqueId(const QString& baseName) const
{
    QString cleanName = baseName.toLower().replace(' ', '_').replace(QRegExp("[^a-z0-9_]"), "");
    QString id = cleanName;
    int counter = 1;
    
    while (m_shortcuts.contains(id)) {
        id = QString("%1_%2").arg(cleanName).arg(counter++);
    }
    
    return id;
}

void AdvancedShortcutManager::loadSettings()
{
    if (!m_settings) return;
    
    m_chordTimeout = m_settings->value("shortcuts/chordTimeout", 1000).toInt();
    m_autoResolveConflicts = m_settings->value("shortcuts/autoResolveConflicts", true).toBool();
    m_contextSwitchingEnabled = m_settings->value("shortcuts/contextSwitchingEnabled", true).toBool();
    
    // Load custom shortcuts
    m_settings->beginGroup("customShortcuts");
    QStringList keys = m_settings->childKeys();
    
    for (const QString& key : keys) {
        QVariantMap data = m_settings->value(key).toMap();
        if (!data.isEmpty()) {
            ShortcutAction action = variantToShortcut(data);
            if (!action.id.isEmpty()) {
                m_shortcuts[action.id] = action;
                if (!action.currentSequence.isEmpty()) {
                    m_sequenceMap[action.currentSequence] = action.id;
                }
            }
        }
    }
    
    m_settings->endGroup();
}

void AdvancedShortcutManager::saveSettings()
{
    if (!m_settings) return;
    
    m_settings->setValue("shortcuts/chordTimeout", m_chordTimeout);
    m_settings->setValue("shortcuts/autoResolveConflicts", m_autoResolveConflicts);
    m_settings->setValue("shortcuts/contextSwitchingEnabled", m_contextSwitchingEnabled);
    
    // Save custom shortcuts
    m_settings->beginGroup("customShortcuts");
    m_settings->remove(""); // Clear existing
    
    QMutexLocker locker(&m_shortcutMutex);
    for (const auto& action : m_shortcuts) {
        if (action.isCustomizable && action.currentSequence != action.defaultSequence) {
            QVariantMap data = shortcutToVariant(action);
            m_settings->setValue(action.id, data);
        }
    }
    
    m_settings->endGroup();
    m_settings->sync();
}

QVariantMap AdvancedShortcutManager::shortcutToVariant(const ShortcutAction& action) const
{
    QVariantMap data;
    data["id"] = action.id;
    data["name"] = action.name;
    data["description"] = action.description;
    data["defaultSequence"] = action.defaultSequence.toString();
    data["currentSequence"] = action.currentSequence.toString();
    data["context"] = static_cast<int>(action.context);
    data["category"] = static_cast<int>(action.category);
    data["priority"] = static_cast<int>(action.priority);
    data["isEnabled"] = action.isEnabled;
    data["isCustomizable"] = action.isCustomizable;
    
    return data;
}

ShortcutAction AdvancedShortcutManager::variantToShortcut(const QVariantMap& data) const
{
    ShortcutAction action;
    action.id = data["id"].toString();
    action.name = data["name"].toString();
    action.description = data["description"].toString();
    action.defaultSequence = QKeySequence(data["defaultSequence"].toString());
    action.currentSequence = QKeySequence(data["currentSequence"].toString());
    action.context = static_cast<ShortcutContext>(data["context"].toInt());
    action.category = static_cast<ShortcutCategory>(data["category"].toInt());
    action.priority = static_cast<ShortcutPriority>(data["priority"].toInt());
    action.isEnabled = data["isEnabled"].toBool();
    action.isCustomizable = data["isCustomizable"].toBool();
    
    return action;
}
