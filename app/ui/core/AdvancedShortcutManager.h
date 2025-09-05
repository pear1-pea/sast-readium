#pragma once

#include <QObject>
#include <QShortcut>
#include <QKeySequence>
#include <QHash>
#include <QList>
#include <QSettings>
#include <QWidget>
#include <QAction>
#include <QMenu>
#include <QTimer>
#include <QMutex>
#include <functional>

/**
 * Shortcut context types
 */
enum class ShortcutContext {
    Global,             // Available everywhere
    DocumentView,       // When document is focused
    ThumbnailView,      // When thumbnail panel is focused
    SearchMode,         // When search is active
    AnnotationMode,     // When annotating
    FullscreenMode,     // When in fullscreen
    MenuContext,        // When menu is open
    DialogContext       // When dialog is open
};

/**
 * Shortcut categories for organization
 */
enum class ShortcutCategory {
    Navigation,         // Page navigation, zoom, etc.
    FileOperations,     // Open, save, export, etc.
    ViewControl,        // View modes, panels, etc.
    Editing,            // Annotations, bookmarks, etc.
    Search,             // Search operations
    Application,        // App-level commands
    Custom              // User-defined shortcuts
};

/**
 * Shortcut priority levels
 */
enum class ShortcutPriority {
    System = 10,        // System-level shortcuts
    Application = 8,    // Application shortcuts
    Context = 6,        // Context-specific shortcuts
    User = 4,           // User-defined shortcuts
    Plugin = 2          // Plugin shortcuts
};

/**
 * Shortcut action definition
 */
struct ShortcutAction {
    QString id;
    QString name;
    QString description;
    QKeySequence defaultSequence;
    QKeySequence currentSequence;
    ShortcutContext context;
    ShortcutCategory category;
    ShortcutPriority priority;
    bool isEnabled;
    bool isCustomizable;
    std::function<void()> callback;
    QAction* action;
    
    ShortcutAction()
        : context(ShortcutContext::Global)
        , category(ShortcutCategory::Application)
        , priority(ShortcutPriority::Application)
        , isEnabled(true)
        , isCustomizable(true)
        , action(nullptr) {}
};

/**
 * Shortcut conflict information
 */
struct ShortcutConflict {
    QString shortcutId1;
    QString shortcutId2;
    QKeySequence sequence;
    ShortcutContext context;
    QString resolution;
    
    ShortcutConflict() : context(ShortcutContext::Global) {}
};

/**
 * Chord sequence for multi-key shortcuts
 */
struct ChordSequence {
    QList<QKeySequence> keys;
    qint64 timeout;
    qint64 lastKeyTime;
    int currentIndex;
    
    ChordSequence() : timeout(1000), lastKeyTime(0), currentIndex(0) {}
    
    bool isComplete() const { return currentIndex >= keys.size(); }
    bool isExpired() const { 
        return QDateTime::currentMSecsSinceEpoch() - lastKeyTime > timeout; 
    }
    void reset() { currentIndex = 0; lastKeyTime = 0; }
};

/**
 * Advanced shortcut manager with context awareness
 */
class AdvancedShortcutManager : public QObject
{
    Q_OBJECT

public:
    explicit AdvancedShortcutManager(QObject* parent = nullptr);
    ~AdvancedShortcutManager();

    // Shortcut registration
    QString registerShortcut(const QString& id, const QString& name, 
                           const QKeySequence& sequence, 
                           const std::function<void()>& callback,
                           ShortcutContext context = ShortcutContext::Global,
                           ShortcutCategory category = ShortcutCategory::Application);
    
    QString registerShortcut(const ShortcutAction& action);
    bool unregisterShortcut(const QString& id);
    
    // Action integration
    void registerAction(QAction* action, const QString& id = QString(),
                       ShortcutContext context = ShortcutContext::Global);
    void unregisterAction(QAction* action);
    
    // Context management
    void setCurrentContext(ShortcutContext context);
    ShortcutContext getCurrentContext() const { return m_currentContext; }
    void pushContext(ShortcutContext context);
    void popContext();
    
    // Shortcut modification
    bool setShortcut(const QString& id, const QKeySequence& sequence);
    bool resetShortcut(const QString& id);
    bool enableShortcut(const QString& id, bool enabled);
    
    // Chord sequences
    QString registerChordSequence(const QString& id, const QString& name,
                                 const QList<QKeySequence>& sequence,
                                 const std::function<void()>& callback,
                                 ShortcutContext context = ShortcutContext::Global);
    
    // Query and information
    QList<ShortcutAction> getShortcuts(ShortcutContext context = ShortcutContext::Global) const;
    QList<ShortcutAction> getShortcutsByCategory(ShortcutCategory category) const;
    ShortcutAction getShortcut(const QString& id) const;
    bool hasShortcut(const QString& id) const;
    
    // Conflict detection and resolution
    QList<ShortcutConflict> detectConflicts() const;
    bool hasConflicts() const;
    void resolveConflict(const ShortcutConflict& conflict, const QString& resolution);
    
    // Import/Export
    void exportShortcuts(const QString& filePath) const;
    bool importShortcuts(const QString& filePath);
    void resetToDefaults();
    
    // Widget integration
    void installEventFilter(QWidget* widget);
    void removeEventFilter(QWidget* widget);
    
    // Help and documentation
    QString getShortcutHelp(ShortcutContext context = ShortcutContext::Global) const;
    QStringList getAvailableSequences() const;
    
    // Settings
    void loadSettings();
    void saveSettings();
    
    // Configuration
    void setChordTimeout(int milliseconds);
    void setConflictResolution(bool autoResolve);
    void enableContextSwitching(bool enable);

public slots:
    void showShortcutEditor();
    void showShortcutHelp();
    void enableAllShortcuts(bool enabled);

signals:
    void shortcutActivated(const QString& id);
    void contextChanged(ShortcutContext oldContext, ShortcutContext newContext);
    void conflictDetected(const ShortcutConflict& conflict);
    void shortcutChanged(const QString& id, const QKeySequence& oldSequence, const QKeySequence& newSequence);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onShortcutActivated();
    void onChordTimeout();
    void onContextTimer();

private:
    // Core data
    QHash<QString, ShortcutAction> m_shortcuts;
    QHash<QKeySequence, QString> m_sequenceMap;
    QHash<QAction*, QString> m_actionMap;
    mutable QMutex m_shortcutMutex;
    
    // Context management
    ShortcutContext m_currentContext;
    QList<ShortcutContext> m_contextStack;
    QTimer* m_contextTimer;
    
    // Chord sequences
    QHash<QString, ChordSequence> m_chordSequences;
    QHash<QString, ChordSequence> m_activeChords;
    QTimer* m_chordTimer;
    int m_chordTimeout;
    
    // Configuration
    bool m_autoResolveConflicts;
    bool m_contextSwitchingEnabled;
    bool m_shortcutsEnabled;
    
    // Event filtering
    QList<QWidget*> m_filteredWidgets;
    
    // Settings
    QSettings* m_settings;
    
    // Helper methods
    void initializeDefaultShortcuts();
    void createShortcutObject(const QString& id);
    void updateShortcutObject(const QString& id);
    void removeShortcutObject(const QString& id);
    
    bool isShortcutValidInContext(const QString& id, ShortcutContext context) const;
    bool processKeySequence(const QKeySequence& sequence);
    bool processChordSequence(const QKeySequence& sequence);
    
    void detectAndResolveConflicts();
    QString generateUniqueId(const QString& baseName) const;
    
    // Context helpers
    bool isContextActive(ShortcutContext context) const;
    ShortcutContext detectCurrentContext() const;
    
    // Serialization helpers
    QVariantMap shortcutToVariant(const ShortcutAction& action) const;
    ShortcutAction variantToShortcut(const QVariantMap& data) const;
};
