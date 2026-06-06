#include "PluginManager.h"
#include <QApplication>
#include <QDebug>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QStandardPaths>
#include "utils/LoggingMacros.h"

// PluginManager Implementation
PluginManager& PluginManager::instance() {
    static PluginManager instance;
    return instance;
}

PluginManager::PluginManager()
    : QObject(nullptr),
      m_settings(nullptr),
      m_hotReloadingEnabled(false),
      m_hotReloadTimer(nullptr) {
    m_settings = new QSettings("SAST", "Readium-Plugins", this);

    QStringList defaultDirs;
    defaultDirs << QApplication::applicationDirPath() + "/plugins";
    defaultDirs << QStandardPaths::writableLocation(
                       QStandardPaths::AppDataLocation) +
                       "/plugins";
    setPluginDirectories(defaultDirs);

    m_hotReloadTimer = new QTimer(this);
    m_hotReloadTimer->setInterval(5000);
    connect(m_hotReloadTimer, &QTimer::timeout, this,
            &PluginManager::checkForPluginChanges);

    loadSettings();
}

// PluginDependencyResolver Implementation
QStringList PluginDependencyResolver::resolveDependencies(
    const QHash<QString, PluginMetadata>& plugins) {
    QStringList result;
    QHash<QString, int> visited;  // 0 = not visited, 1 = visiting, 2 = visited

    for (auto it = plugins.begin(); it != plugins.end(); ++it) {
        if (visited[it.key()] == 0) {
            visitPlugin(it.key(), plugins, visited, result);
        }
    }

    return result;
}

bool PluginDependencyResolver::hasCyclicDependencies(
    const QHash<QString, PluginMetadata>& plugins) {
    QHash<QString, int> visited;  // 0 = not visited, 1 = visiting, 2 = visited

    for (auto it = plugins.begin(); it != plugins.end(); ++it) {
        if (visited[it.key()] == 0) {
            QStringList temp;
            visitPlugin(it.key(), plugins, visited, temp);

            // Check if we encountered a cycle (visiting state)
            for (auto visitIt = visited.begin(); visitIt != visited.end();
                 ++visitIt) {
                if (visitIt.value() == 1) {
                    return true;
                }
            }
        }
    }

    return false;
}

QStringList PluginDependencyResolver::getLoadOrder(
    const QHash<QString, PluginMetadata>& plugins) {
    if (hasCyclicDependencies(plugins)) {
        LOG_WARNING("Cyclic dependencies detected in plugins");
        return plugins.keys();  // Return original order if cycles exist
    }

    return resolveDependencies(plugins);
}

void PluginDependencyResolver::visitPlugin(
    const QString& pluginName, const QHash<QString, PluginMetadata>& plugins,
    QHash<QString, int>& visited, QStringList& result) {
    if (visited[pluginName] == 2) {
        return;  // Already processed
    }

    if (visited[pluginName] == 1) {
        qWarning() << "Cyclic dependency detected involving plugin:"
                   << pluginName;
        return;
    }

    visited[pluginName] = 1;  // Mark as visiting

    if (plugins.contains(pluginName)) {
        const PluginMetadata& metadata = plugins[pluginName];

        // Visit dependencies first
        for (const QString& dependency : metadata.dependencies) {
            if (plugins.contains(dependency)) {
                visitPlugin(dependency, plugins, visited, result);
            }
        }
    }

    visited[pluginName] = 2;  // Mark as visited
    result.append(pluginName);
}

void PluginManager::setPluginDirectories(const QStringList& directories) {
    QMutexLocker lock(&m_mutex);
    m_pluginDirectories = directories;

    // Create directories if they don't exist
    for (const QString& dir : directories) {
        QDir().mkpath(dir);
    }
}

void PluginManager::scanForPlugins() {
    QMutexLocker lock(&m_mutex);
    LOG_DEBUG("Scanning for plugins in directories: [{}]",
              m_pluginDirectories.join(", ").toStdString());

    m_pluginMetadata.clear();
    m_pluginErrors.clear();
    m_pluginModificationTimes.clear();
    int pluginCount = 0;

    for (const QString& directory : m_pluginDirectories) {
        QDir pluginDir(directory);
        if (!pluginDir.exists()) {
            qDebug() << "Plugin directory does not exist:" << directory;
            continue;
        }

        QDirIterator it(directory,
                        QStringList() << "*.dll"
                                      << "*.so"
                                      << "*.dylib",
                        QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString filePath = it.next();

            if (validatePlugin(filePath)) {
                QPluginLoader loader(filePath);
                PluginMetadata metadata = extractMetadata(&loader);

                if (!metadata.name.isEmpty()) {
                    metadata.filePath = filePath;
                    m_pluginMetadata[metadata.name] = metadata;
                    pluginCount++;

                    qDebug()
                        << "Found plugin:" << metadata.name << "at" << filePath;
                }
            }
        }
    }

    qDebug() << "Found" << pluginCount << "plugins";
    emit pluginsScanned(pluginCount);
}

bool PluginManager::loadPlugin(const QString& pluginName) {
    QString filePath;
    {
        QMutexLocker lock(&m_mutex);

        if (m_loadedPlugins.contains(pluginName)) {
            qDebug() << "Plugin already loaded:" << pluginName;
            return true;
        }

        // Another thread is already loading this plugin
        if (m_loadingInProgress.contains(pluginName)) {
            qDebug() << "Plugin loading in progress:" << pluginName;
            return false;
        }

        if (!m_pluginMetadata.contains(pluginName)) {
            qWarning() << "Plugin not found:" << pluginName;
            return false;
        }

        const auto& metadata = m_pluginMetadata[pluginName];
        if (!metadata.isEnabled) {
            qDebug() << "Plugin is disabled:" << pluginName;
            return false;
        }

        if (!checkDependencies(pluginName)) {
            qWarning() << "Plugin dependencies not satisfied:" << pluginName;
            return false;
        }

        filePath = metadata.filePath;
        m_loadingInProgress.insert(pluginName);
    }

    // Phase 2: IO + initialization (no lock — dlopen can block)
    QElapsedTimer timer;
    timer.start();
    auto result = loadPluginIO(filePath);

    // Phase 3: update state under lock
    {
        QMutexLocker lock(&m_mutex);
        m_loadingInProgress.remove(pluginName);

        if (result.ok) {
            m_loadedPlugins[result.pluginName] = result.plugin;
            if (m_pluginMetadata.contains(result.pluginName)) {
                m_pluginMetadata[result.pluginName].isLoaded = true;
                m_pluginMetadata[result.pluginName].loadTime = timer.elapsed();
            }
            qDebug() << "Successfully loaded plugin:" << result.pluginName
                     << "in" << timer.elapsed() << "ms";
            emit pluginLoaded(result.pluginName);
        } else {
            qWarning() << "Failed to load plugin:" << filePath
                       << result.errorString;
            m_pluginErrors[QFileInfo(filePath).baseName()].append(
                result.errorString);
        }
    }

    return result.ok;
}

PluginManager::LoadResult PluginManager::loadPluginIO(const QString& filePath) {
    QPluginLoader* loader = new QPluginLoader(filePath);

    if (!loader->load()) {
        QString err = loader->errorString();
        delete loader;
        return {false, {}, {}, err};
    }

    QObject* pluginObject = loader->instance();
    if (!pluginObject) {
        loader->unload();
        delete loader;
        return {false, {}, {}, "Failed to get plugin instance"};
    }

    IPlugin* plugin = qobject_cast<IPlugin*>(pluginObject);
    if (!plugin) {
        loader->unload();
        delete loader;
        return {false, {}, {}, "Plugin does not implement IPlugin interface"};
    }

    if (!plugin->initialize()) {
        QString name = plugin->name();
        loader->unload();
        delete loader;
        return {false, name, {}, "Plugin initialization failed"};
    }

    QString pluginName = plugin->name();
    auto sharedPlugin = QSharedPointer<IPlugin>(plugin, [loader](IPlugin* p) {
        p->shutdown();
        loader->unload();
        delete loader;
    });

    return {true, pluginName, sharedPlugin, {}};
}

bool PluginManager::unloadPlugin(const QString& pluginName) {
    QMutexLocker lock(&m_mutex);
    if (!m_loadedPlugins.contains(pluginName)) {
        return true;
    }

    unloadPluginInternal(pluginName);
    return true;
}

void PluginManager::unloadPluginInternal(const QString& pluginName) {
    // Remove from hash — shared_ptr custom deleter handles shutdown + unload
    m_loadedPlugins.remove(pluginName);

    // Update metadata
    if (m_pluginMetadata.contains(pluginName)) {
        m_pluginMetadata[pluginName].isLoaded = false;
    }

    qDebug() << "Unloaded plugin:" << pluginName;
    emit pluginUnloaded(pluginName);
}

void PluginManager::loadAllPlugins() {
    QMutexLocker lock(&m_mutex);
    QStringList loadOrder =
        PluginDependencyResolver::getLoadOrder(m_pluginMetadata);

    for (const QString& pluginName : loadOrder) {
        if (m_pluginMetadata[pluginName].isEnabled) {
            loadPlugin(pluginName);
        }
    }
}

void PluginManager::unloadAllPlugins() {
    QMutexLocker lock(&m_mutex);
    QStringList loadedPlugins = m_loadedPlugins.keys();

    // Unload in reverse order
    for (int i = loadedPlugins.size() - 1; i >= 0; --i) {
        unloadPluginInternal(loadedPlugins[i]);
    }
}

QStringList PluginManager::getAvailablePlugins() const {
    QMutexLocker lock(&m_mutex);
    return m_pluginMetadata.keys();
}

QStringList PluginManager::getLoadedPlugins() const {
    QMutexLocker lock(&m_mutex);
    return m_loadedPlugins.keys();
}

QStringList PluginManager::getEnabledPlugins() const {
    QMutexLocker lock(&m_mutex);
    QStringList enabled;
    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        if (it.value().isEnabled) {
            enabled.append(it.key());
        }
    }
    return enabled;
}

bool PluginManager::isPluginLoaded(const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    return m_loadedPlugins.contains(pluginName);
}

bool PluginManager::isPluginEnabled(const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    if (m_pluginMetadata.contains(pluginName)) {
        return m_pluginMetadata[pluginName].isEnabled;
    }
    return false;
}

void PluginManager::setPluginEnabled(const QString& pluginName, bool enabled) {
    QMutexLocker lock(&m_mutex);
    if (m_pluginMetadata.contains(pluginName)) {
        m_pluginMetadata[pluginName].isEnabled = enabled;

        if (enabled) {
            emit pluginEnabled(pluginName);
        } else {
            emit pluginDisabled(pluginName);
            if (m_loadedPlugins.contains(pluginName)) {
                unloadPluginInternal(pluginName);
            }
        }
    }
}

QSharedPointer<IPlugin> PluginManager::getPlugin(
    const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    return m_loadedPlugins.value(pluginName);
}

QList<QSharedPointer<IPlugin>> PluginManager::getPluginsByType(
    const QString& interfaceId) const {
    QMutexLocker lock(&m_mutex);
    QList<QSharedPointer<IPlugin>> result;

    for (const auto& plugin : m_loadedPlugins.values()) {
        if (plugin && plugin->inherits(interfaceId.toUtf8().constData())) {
            result.append(plugin);
        }
    }

    return result;
}

QList<QSharedPointer<IDocumentPlugin>> PluginManager::getDocumentPlugins()
    const {
    QMutexLocker lock(&m_mutex);
    QList<QSharedPointer<IDocumentPlugin>> result;

    for (const auto& plugin : m_loadedPlugins.values()) {
        auto docPlugin = qSharedPointerObjectCast<IDocumentPlugin>(plugin);
        if (docPlugin) {
            result.append(docPlugin);
        }
    }

    return result;
}

QList<QSharedPointer<IUIPlugin>> PluginManager::getUIPlugins() const {
    QMutexLocker lock(&m_mutex);
    QList<QSharedPointer<IUIPlugin>> result;

    for (const auto& plugin : m_loadedPlugins.values()) {
        auto uiPlugin = qSharedPointerObjectCast<IUIPlugin>(plugin);
        if (uiPlugin) {
            result.append(uiPlugin);
        }
    }

    return result;
}

PluginMetadata PluginManager::getPluginMetadata(
    const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    return m_pluginMetadata.value(pluginName, PluginMetadata());
}

QHash<QString, PluginMetadata> PluginManager::getAllPluginMetadata() const {
    QMutexLocker lock(&m_mutex);
    return m_pluginMetadata;
}

PluginMetadata PluginManager::extractMetadata(QPluginLoader* loader) const {
    PluginMetadata metadata;

    QJsonObject metaData = loader->metaData().value("MetaData").toObject();

    metadata.name = metaData.value("name").toString();
    metadata.version = metaData.value("version").toString();
    metadata.description = metaData.value("description").toString();
    metadata.author = metaData.value("author").toString();

    QJsonArray deps = metaData.value("dependencies").toArray();
    for (const QJsonValue& dep : deps) {
        metadata.dependencies.append(dep.toString());
    }

    QJsonArray types = metaData.value("supportedTypes").toArray();
    for (const QJsonValue& type : types) {
        metadata.supportedTypes.append(type.toString());
    }

    QJsonArray features = metaData.value("features").toArray();
    for (const QJsonValue& feature : features) {
        metadata.features.append(feature.toString());
    }

    metadata.configuration = metaData.value("configuration").toObject();

    return metadata;
}

bool PluginManager::checkDependencies(const QString& pluginName) const {
    if (!m_pluginMetadata.contains(pluginName)) {
        return false;
    }

    const PluginMetadata& metadata = m_pluginMetadata[pluginName];

    for (const QString& dependency : metadata.dependencies) {
        if (!m_loadedPlugins.contains(dependency)) {
            return false;
        }
    }

    return true;
}

bool PluginManager::validatePlugin(const QString& filePath) const {
    QPluginLoader loader(filePath);
    QJsonObject metaData = loader.metaData();

    if (metaData.isEmpty()) {
        return false;
    }

    // Check if it has required metadata
    QJsonObject pluginMetaData = metaData.value("MetaData").toObject();
    return !pluginMetaData.value("name").toString().isEmpty();
}

QStringList PluginManager::getPluginErrors(const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    return m_pluginErrors.value(pluginName, QStringList());
}

void PluginManager::loadSettings() {
    QMutexLocker lock(&m_mutex);
    if (!m_settings)
        return;

    m_settings->beginGroup("plugins");

    // Load enabled/disabled state
    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        bool enabled = m_settings->value(it.key() + "/enabled", true).toBool();
        it.value().isEnabled = enabled;
    }

    m_settings->endGroup();
}

void PluginManager::saveSettings() {
    QMutexLocker lock(&m_mutex);
    if (!m_settings)
        return;

    m_settings->beginGroup("plugins");

    // Save enabled/disabled state
    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        m_settings->setValue(it.key() + "/enabled", it.value().isEnabled);
    }

    m_settings->endGroup();
    m_settings->sync();
}

void PluginManager::enableHotReloading(bool enabled) {
    QMutexLocker lock(&m_mutex);
    m_hotReloadingEnabled = enabled;

    if (enabled) {
        // Record current modification times
        for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
             ++it) {
            QFileInfo fileInfo(it.value().filePath);
            m_pluginModificationTimes[it.key()] =
                fileInfo.lastModified().toMSecsSinceEpoch();
        }

        m_hotReloadTimer->start();
    } else {
        m_hotReloadTimer->stop();
    }
}

void PluginManager::checkForPluginChanges() {
    QMutexLocker lock(&m_mutex);
    if (!m_hotReloadingEnabled)
        return;

    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        QFileInfo fileInfo(it.value().filePath);
        qint64 currentModTime = fileInfo.lastModified().toMSecsSinceEpoch();
        qint64 recordedModTime = m_pluginModificationTimes.value(it.key(), 0);

        if (currentModTime > recordedModTime) {
            qDebug() << "Plugin file changed, reloading:" << it.key();

            // Unload and reload the plugin
            if (m_loadedPlugins.contains(it.key())) {
                unloadPluginInternal(it.key());
                loadPlugin(it.key());
            }

            m_pluginModificationTimes[it.key()] = currentModTime;
        }
    }
}

QJsonObject PluginManager::getPluginConfiguration(
    const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    if (m_pluginMetadata.contains(pluginName)) {
        return m_pluginMetadata[pluginName].configuration;
    }
    return QJsonObject();
}

void PluginManager::setPluginConfiguration(const QString& pluginName,
                                           const QJsonObject& config) {
    QMutexLocker lock(&m_mutex);
    if (m_pluginMetadata.contains(pluginName)) {
        m_pluginMetadata[pluginName].configuration = config;

        // Apply configuration to loaded plugin
        auto plugin = m_loadedPlugins.value(pluginName);
        if (plugin) {
            plugin->setConfiguration(config);
        }
    }
}

QStringList PluginManager::getPluginsWithFeature(const QString& feature) const {
    QMutexLocker lock(&m_mutex);
    QStringList result;

    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        if (it.value().features.contains(feature)) {
            result.append(it.key());
        }
    }

    return result;
}

QStringList PluginManager::getPluginsForFileType(
    const QString& fileType) const {
    QMutexLocker lock(&m_mutex);
    QStringList result;

    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        if (it.value().supportedTypes.contains(fileType)) {
            result.append(it.key());
        }
    }

    return result;
}

bool PluginManager::isFeatureAvailable(const QString& feature) const {
    QMutexLocker lock(&m_mutex);
    return !getPluginsWithFeature(feature).isEmpty();
}

// Additional plugin management functions
bool PluginManager::installPlugin(const QString& pluginPath) {
    QMutexLocker lock(&m_mutex);
    QFileInfo fileInfo(pluginPath);
    if (!fileInfo.exists() || !validatePlugin(pluginPath)) {
        qWarning() << "Invalid plugin file:" << pluginPath;
        return false;
    }

    // Copy plugin to plugin directory
    QString targetDir = m_pluginDirectories.first();
    QString targetPath = targetDir + "/" + fileInfo.fileName();

    if (QFile::exists(targetPath)) {
        qWarning() << "Plugin already exists:" << targetPath;
        return false;
    }

    if (!QFile::copy(pluginPath, targetPath)) {
        qWarning() << "Failed to copy plugin to:" << targetPath;
        return false;
    }

    // Rescan for plugins to pick up the new one
    scanForPlugins();

    QString pluginName = QFileInfo(targetPath).baseName();
    emit pluginInstalled(pluginName, targetPath);

    return true;
}

bool PluginManager::uninstallPlugin(const QString& pluginName) {
    QMutexLocker lock(&m_mutex);
    if (!m_pluginMetadata.contains(pluginName)) {
        return false;
    }

    // Unload plugin if it's loaded
    if (m_loadedPlugins.contains(pluginName)) {
        unloadPluginInternal(pluginName);
    }

    // Remove plugin file
    QString filePath = m_pluginMetadata[pluginName].filePath;
    if (QFile::exists(filePath)) {
        if (!QFile::remove(filePath)) {
            qWarning() << "Failed to remove plugin file:" << filePath;
            return false;
        }
    }

    // Remove from metadata
    m_pluginMetadata.remove(pluginName);

    emit pluginUninstalled(pluginName);

    return true;
}

bool PluginManager::updatePlugin(const QString& pluginName,
                                 const QString& newPluginPath) {
    QMutexLocker lock(&m_mutex);
    if (!m_pluginMetadata.contains(pluginName)) {
        return false;
    }

    // Validate new plugin
    if (!validatePlugin(newPluginPath)) {
        return false;
    }

    // Unload current plugin
    bool wasLoaded = m_loadedPlugins.contains(pluginName);
    if (wasLoaded) {
        unloadPluginInternal(pluginName);
    }

    // Replace plugin file
    QString oldPath = m_pluginMetadata[pluginName].filePath;
    if (QFile::exists(oldPath)) {
        QFile::remove(oldPath);
    }

    if (!QFile::copy(newPluginPath, oldPath)) {
        qWarning() << "Failed to update plugin file:" << oldPath;
        return false;
    }

    // Update metadata
    QPluginLoader loader(oldPath);
    PluginMetadata newMetadata = extractMetadata(&loader);
    newMetadata.filePath = oldPath;
    m_pluginMetadata[pluginName] = newMetadata;

    // Reload if it was loaded before
    if (wasLoaded) {
        loadPlugin(pluginName);
    }

    emit pluginUpdated(pluginName);

    return true;
}

QStringList PluginManager::getPluginDependencies(
    const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    if (m_pluginMetadata.contains(pluginName)) {
        return m_pluginMetadata[pluginName].dependencies;
    }
    return QStringList();
}

QStringList PluginManager::getPluginsDependingOn(
    const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    QStringList dependents;

    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        if (it.value().dependencies.contains(pluginName)) {
            dependents.append(it.key());
        }
    }

    return dependents;
}

bool PluginManager::canUnloadPlugin(const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    // Check if other loaded plugins depend on this one
    QStringList dependents = getPluginsDependingOn(pluginName);

    for (const QString& dependent : dependents) {
        if (m_loadedPlugins.contains(dependent)) {
            return false;
        }
    }

    return true;
}

void PluginManager::reloadPlugin(const QString& pluginName) {
    QMutexLocker lock(&m_mutex);
    if (m_loadedPlugins.contains(pluginName)) {
        unloadPluginInternal(pluginName);
    }
    loadPlugin(pluginName);
}

void PluginManager::reloadAllPlugins() {
    QMutexLocker lock(&m_mutex);
    QStringList loadedPlugins = m_loadedPlugins.keys();

    // Unload all plugins
    for (int i = loadedPlugins.size() - 1; i >= 0; --i) {
        unloadPluginInternal(loadedPlugins[i]);
    }

    // Rescan for plugins (in case files changed)
    m_pluginMetadata.clear();
    m_pluginErrors.clear();
    m_pluginModificationTimes.clear();
    int pluginCount = 0;

    for (const QString& directory : m_pluginDirectories) {
        QDir pluginDir(directory);
        if (!pluginDir.exists())
            continue;

        QDirIterator it(directory,
                        QStringList() << "*.dll"
                                      << "*.so"
                                      << "*.dylib",
                        QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString filePath = it.next();
            if (QPluginLoader(filePath).metaData().isEmpty())
                continue;

            QPluginLoader loader(filePath);
            PluginMetadata metadata = extractMetadata(&loader);
            if (!metadata.name.isEmpty()) {
                metadata.filePath = filePath;
                m_pluginMetadata[metadata.name] = metadata;
                pluginCount++;
            }
        }
    }

    emit pluginsScanned(pluginCount);

    // Reload previously loaded plugins
    for (const QString& pluginName : loadedPlugins) {
        if (m_pluginMetadata.contains(pluginName) &&
            m_pluginMetadata[pluginName].isEnabled) {
            loadPlugin(pluginName);
        }
    }
}

QJsonObject PluginManager::getPluginInfo(const QString& pluginName) const {
    QMutexLocker lock(&m_mutex);
    QJsonObject info;

    if (!m_pluginMetadata.contains(pluginName)) {
        return info;
    }

    const PluginMetadata& metadata = m_pluginMetadata[pluginName];

    info["name"] = metadata.name;
    info["version"] = metadata.version;
    info["description"] = metadata.description;
    info["author"] = metadata.author;
    info["filePath"] = metadata.filePath;
    info["isEnabled"] = metadata.isEnabled;
    info["isLoaded"] = metadata.isLoaded;
    info["loadTime"] = metadata.loadTime;

    QJsonArray depsArray;
    for (const QString& dep : metadata.dependencies) {
        depsArray.append(dep);
    }
    info["dependencies"] = depsArray;

    QJsonArray typesArray;
    for (const QString& type : metadata.supportedTypes) {
        typesArray.append(type);
    }
    info["supportedTypes"] = typesArray;

    QJsonArray featuresArray;
    for (const QString& feature : metadata.features) {
        featuresArray.append(feature);
    }
    info["features"] = featuresArray;

    info["configuration"] = metadata.configuration;

    return info;
}

void PluginManager::exportPluginList(const QString& filePath) const {
    QMutexLocker lock(&m_mutex);
    QJsonObject root;
    QJsonArray pluginsArray;

    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        QJsonObject pluginObj = getPluginInfo(it.key());
        pluginsArray.append(pluginObj);
    }

    root["plugins"] = pluginsArray;
    root["totalPlugins"] = m_pluginMetadata.size();
    root["loadedPlugins"] = m_loadedPlugins.size();
    root["enabledPlugins"] = getEnabledPlugins().size();
    root["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(root);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        emit pluginListExported(filePath);
    }
}

void PluginManager::createPluginReport() const {
    QMutexLocker lock(&m_mutex);
    QString report;
    QTextStream stream(&report);

    stream << "Plugin Manager Report\n";
    stream << "====================\n\n";

    stream << "Summary:\n";
    stream << "  Total plugins: " << m_pluginMetadata.size() << "\n";
    stream << "  Loaded plugins: " << m_loadedPlugins.size() << "\n";
    stream << "  Enabled plugins: " << getEnabledPlugins().size() << "\n\n";

    stream << "Plugin Details:\n";
    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        const PluginMetadata& metadata = it.value();

        stream << "  " << metadata.name << " (" << metadata.version << ")\n";
        stream << "    Author: " << metadata.author << "\n";
        stream << "    Description: " << metadata.description << "\n";
        stream << "    Status: "
               << (metadata.isLoaded ? "Loaded" : "Not Loaded");
        stream << " / " << (metadata.isEnabled ? "Enabled" : "Disabled")
               << "\n";
        stream << "    File: " << metadata.filePath << "\n";

        if (!metadata.dependencies.isEmpty()) {
            stream << "    Dependencies: " << metadata.dependencies.join(", ")
                   << "\n";
        }

        if (!metadata.features.isEmpty()) {
            stream << "    Features: " << metadata.features.join(", ") << "\n";
        }

        stream << "\n";
    }

    // Save report
    QString fileName =
        QString("plugin_report_%1.txt")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(report.toUtf8());
        emit pluginReportCreated(fileName);
    }
}

bool PluginManager::backupPluginConfiguration(const QString& filePath) const {
    QMutexLocker lock(&m_mutex);
    QJsonObject backup;
    QJsonArray pluginsArray;

    for (auto it = m_pluginMetadata.begin(); it != m_pluginMetadata.end();
         ++it) {
        QJsonObject pluginObj;
        pluginObj["name"] = it.key();
        pluginObj["enabled"] = it.value().isEnabled;
        pluginObj["configuration"] = it.value().configuration;
        pluginsArray.append(pluginObj);
    }

    backup["plugins"] = pluginsArray;
    backup["backupTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    backup["version"] = "1.0";

    QJsonDocument doc(backup);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        emit pluginConfigurationBackedUp(filePath);
        return true;
    }

    return false;
}

bool PluginManager::restorePluginConfiguration(const QString& filePath) {
    QMutexLocker lock(&m_mutex);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject backup = doc.object();

    QJsonArray pluginsArray = backup["plugins"].toArray();

    for (const QJsonValue& value : pluginsArray) {
        QJsonObject pluginObj = value.toObject();
        QString pluginName = pluginObj["name"].toString();

        if (m_pluginMetadata.contains(pluginName)) {
            bool enabled = pluginObj["enabled"].toBool();
            QJsonObject config = pluginObj["configuration"].toObject();

            setPluginEnabled(pluginName, enabled);
            setPluginConfiguration(pluginName, config);
        }
    }

    saveSettings();
    emit pluginConfigurationRestored(filePath);

    return true;
}
