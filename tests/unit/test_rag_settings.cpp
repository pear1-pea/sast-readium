#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "rag/config/RagSettings.h"

using namespace Rag;

namespace {

QString settingsPath(const QTemporaryDir& directory) {
    return directory.filePath(QStringLiteral("settings.ini"));
}

}  // namespace

class TestRagSettings : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void migrationFromEnvironment();
    void emptyVersionStaysDisabled();
    void emptyModelIsExplicit();
    void savedValuesWin();
    void noChatModelDisablesAnswers();
    void endpointNormalization();
    void withEmbeddingModelFillsVersion();
    void chatOnlySaveDoesNotEnableEmbedding();
    void urlsUseConfiguredEndpoint();
    void invalidEndpointIsRejected();
};

void TestRagSettings::init() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");
    qunsetenv("READIUM_OLLAMA_CHAT_MODEL");
}

void TestRagSettings::cleanup() { init(); }

void TestRagSettings::migrationFromEnvironment() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = settingsPath(directory);

    qputenv("READIUM_OLLAMA_EMBED_MODEL", "nomic-embed-text");
    qputenv("READIUM_OLLAMA_EMBED_VERSION", "2024-01");
    qputenv("READIUM_OLLAMA_CHAT_MODEL", "llama3");

    RagSettings loaded;
    {
        QSettings settings(path, QSettings::IniFormat);
        loaded = RagSettings::load(settings);
    }
    QCOMPARE(loaded.endpoint, RagSettings::defaultEndpoint());
    QCOMPARE(loaded.embedModel, QStringLiteral("nomic-embed-text"));
    QCOMPARE(loaded.embedVersion, QStringLiteral("2024-01"));
    QCOMPARE(loaded.chatModel, QStringLiteral("llama3"));
    QVERIFY(loaded.answerEnabled);
    QVERIFY(loaded.embeddingsEnabled());
    QVERIFY(loaded.chatEnabled());

    // The migration result is persisted, including the version.
    {
        QSettings settings(path, QSettings::IniFormat);
        QVERIFY(settings.contains(QStringLiteral("rag/ollama/embedVersion")));
        QVERIFY(settings.contains(QStringLiteral("rag/ollama/migrated")));
    }

    // Later environment changes must not override the saved migration.
    qputenv("READIUM_OLLAMA_EMBED_MODEL", "other-model");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");
    qputenv("READIUM_OLLAMA_CHAT_MODEL", "other-chat");
    QSettings settings(path, QSettings::IniFormat);
    const RagSettings reloaded = RagSettings::load(settings);
    QCOMPARE(reloaded.embedModel, QStringLiteral("nomic-embed-text"));
    QCOMPARE(reloaded.embedVersion, QStringLiteral("2024-01"));
    QCOMPARE(reloaded.chatModel, QStringLiteral("llama3"));
}

void TestRagSettings::emptyVersionStaysDisabled() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = settingsPath(directory);

    qputenv("READIUM_OLLAMA_EMBED_MODEL", "nomic-embed-text");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

    RagSettings loaded;
    {
        QSettings settings(path, QSettings::IniFormat);
        loaded = RagSettings::load(settings);
    }
    QCOMPARE(loaded.embedModel, QStringLiteral("nomic-embed-text"));
    QVERIFY(loaded.embedVersion.isEmpty());
    QVERIFY(!loaded.embeddingsEnabled());

    // An empty version is still an explicit persisted value: if the
    // environment later provides one, it must not silently enable embedding.
    {
        QSettings settings(path, QSettings::IniFormat);
        QVERIFY(settings.contains(QStringLiteral("rag/ollama/embedVersion")));
    }
    qputenv("READIUM_OLLAMA_EMBED_VERSION", "late-version");
    QSettings settings(path, QSettings::IniFormat);
    const RagSettings reloaded = RagSettings::load(settings);
    QVERIFY(reloaded.embedVersion.isEmpty());
    QVERIFY(!reloaded.embeddingsEnabled());
}

void TestRagSettings::emptyModelIsExplicit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = settingsPath(directory);

    qputenv("READIUM_OLLAMA_EMBED_MODEL", "nomic-embed-text");
    qputenv("READIUM_OLLAMA_EMBED_VERSION", "1");
    qputenv("READIUM_OLLAMA_CHAT_MODEL", "llama3");

    QSettings settings(path, QSettings::IniFormat);
    RagSettings loaded = RagSettings::load(settings);
    QVERIFY(loaded.embeddingsEnabled());

    loaded.embedModel.clear();
    loaded.embedVersion.clear();
    loaded.chatModel.clear();
    loaded.answerEnabled = false;
    QVERIFY(loaded.save(settings));

    // Environment is still populated, but the explicit empty values win.
    const RagSettings reloaded = RagSettings::load(settings);
    QVERIFY(reloaded.embedModel.isEmpty());
    QVERIFY(reloaded.embedVersion.isEmpty());
    QVERIFY(!reloaded.embeddingsEnabled());
    QVERIFY(reloaded.chatModel.isEmpty());
    QVERIFY(!reloaded.answerEnabled);
}

void TestRagSettings::savedValuesWin() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = settingsPath(directory);

    qputenv("READIUM_OLLAMA_EMBED_MODEL", "env-model");
    qputenv("READIUM_OLLAMA_EMBED_VERSION", "env-version");

    QSettings settings(path, QSettings::IniFormat);
    RagSettings loaded = RagSettings::load(settings);

    loaded.embedModel = QStringLiteral("ui-model");
    loaded.embedVersion = QStringLiteral("1");
    loaded.endpoint = QStringLiteral("http://localhost:11434/");
    QVERIFY(loaded.save(settings));

    qputenv("READIUM_OLLAMA_EMBED_MODEL", "changed-env-model");
    const RagSettings reloaded = RagSettings::load(settings);
    QCOMPARE(reloaded.embedModel, QStringLiteral("ui-model"));
    QCOMPARE(reloaded.embedVersion, QStringLiteral("1"));
    QCOMPARE(reloaded.endpoint, QStringLiteral("http://localhost:11434"));
}

void TestRagSettings::noChatModelDisablesAnswers() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(settingsPath(directory), QSettings::IniFormat);

    const RagSettings loaded = RagSettings::load(settings);
    QVERIFY(loaded.chatModel.isEmpty());
    QVERIFY(!loaded.answerEnabled);
    QVERIFY(!loaded.chatEnabled());
    QVERIFY(!loaded.embeddingsEnabled());
}

void TestRagSettings::endpointNormalization() {
    bool ok = false;
    QCOMPARE(RagSettings::normalizedEndpoint(
                 QStringLiteral("http://127.0.0.1:11434/"), &ok),
             QStringLiteral("http://127.0.0.1:11434"));
    QVERIFY(ok);
    QCOMPARE(RagSettings::normalizedEndpoint(
                 QStringLiteral("  http://127.0.0.1:11434/base/  "), &ok),
             QStringLiteral("http://127.0.0.1:11434/base"));
    QVERIFY(ok);
    QCOMPARE(RagSettings::normalizedEndpoint(
                 QStringLiteral("https://localhost:11434"), &ok),
             QStringLiteral("https://localhost:11434"));
    QVERIFY(ok);
    QCOMPARE(RagSettings::normalizedEndpoint(
                 QStringLiteral("http://[::1]:11434/"), &ok),
             QStringLiteral("http://[::1]:11434"));
    QVERIFY(ok);

    QVERIFY(RagSettings::normalizedEndpoint(QStringLiteral(""), &ok).isEmpty());
    QVERIFY(!ok);
    QVERIFY(RagSettings::normalizedEndpoint(QStringLiteral("ftp://host"), &ok)
                .isEmpty());
    QVERIFY(!ok);
    QVERIFY(RagSettings::normalizedEndpoint(QStringLiteral("not a url"), &ok)
                .isEmpty());
    QVERIFY(!ok);

    // Endpoints outside the loopback interface are rejected: Ask PDF only
    // supports a local Ollama instance.
    QVERIFY(RagSettings::normalizedEndpoint(
                QStringLiteral("http://192.168.1.4:11434"), &ok)
                .isEmpty());
    QVERIFY(!ok);
    QVERIFY(RagSettings::normalizedEndpoint(
                QStringLiteral("http://example.com:11434"), &ok)
                .isEmpty());
    QVERIFY(!ok);
    QVERIFY(RagSettings::normalizedEndpoint(
                QStringLiteral("http://user@127.0.0.1:11434"), &ok)
                .isEmpty());
    QVERIFY(!ok);
    QVERIFY(RagSettings::normalizedEndpoint(
                QStringLiteral("http://127.0.0.1:11434/?x=1"), &ok)
                .isEmpty());
    QVERIFY(!ok);
    QVERIFY(RagSettings::normalizedEndpoint(
                QStringLiteral("http://127.0.0.1:11434/#frag"), &ok)
                .isEmpty());
    QVERIFY(!ok);
}

void TestRagSettings::withEmbeddingModelFillsVersion() {
    RagSettings settings;
    RagSettings unchanged = settings.withEmbeddingModel(QString());
    QVERIFY(unchanged.embedVersion.isEmpty());
    QVERIFY(!unchanged.embeddingsEnabled());

    RagSettings enabled = settings.withEmbeddingModel(QStringLiteral("nomic"));
    QCOMPARE(enabled.embedVersion, QStringLiteral("1"));
    QVERIFY(enabled.embeddingsEnabled());

    settings.embedVersion = QStringLiteral("custom");
    RagSettings preserved =
        settings.withEmbeddingModel(QStringLiteral("nomic"));
    QCOMPARE(preserved.embedVersion, QStringLiteral("custom"));
}

void TestRagSettings::chatOnlySaveDoesNotEnableEmbedding() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(settingsPath(directory), QSettings::IniFormat);

    // Old behaviour: a model without a version keeps embedding disabled.
    qputenv("READIUM_OLLAMA_EMBED_MODEL", "nomic-embed-text");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");
    RagSettings loaded = RagSettings::load(settings);
    QVERIFY(!loaded.embeddingsEnabled());

    // Saving unrelated fields must not fill the hidden version.
    loaded.chatModel = QStringLiteral("llama3");
    loaded.answerEnabled = true;
    loaded.endpoint = QStringLiteral("http://127.0.0.1:11435");
    QVERIFY(loaded.save(settings));
    RagSettings reloaded = RagSettings::load(settings);
    QVERIFY(reloaded.embedVersion.isEmpty());
    QVERIFY(!reloaded.embeddingsEnabled());
    QCOMPARE(reloaded.chatModel, QStringLiteral("llama3"));

    // An explicit model choice enables the hidden version.
    reloaded =
        reloaded.withEmbeddingModel(QStringLiteral("nomic-embed-text:latest"));
    QVERIFY(reloaded.embeddingsEnabled());
    QCOMPARE(reloaded.embedVersion, QStringLiteral("1"));
    QVERIFY(reloaded.save(settings));
    QCOMPARE(RagSettings::load(settings).embedVersion, QStringLiteral("1"));
}

void TestRagSettings::urlsUseConfiguredEndpoint() {
    RagSettings settings;
    settings.endpoint = QStringLiteral("http://127.0.0.1:11434/");
    QCOMPARE(settings.embedUrl(),
             QStringLiteral("http://127.0.0.1:11434/api/embed"));
    QCOMPARE(settings.chatUrl(),
             QStringLiteral("http://127.0.0.1:11434/api/chat"));

    settings.endpoint = QStringLiteral("http://localhost:1234/base");
    QCOMPARE(settings.embedUrl(),
             QStringLiteral("http://localhost:1234/base/api/embed"));
    QCOMPARE(settings.chatUrl(),
             QStringLiteral("http://localhost:1234/base/api/chat"));
}

void TestRagSettings::invalidEndpointIsRejected() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(settingsPath(directory), QSettings::IniFormat);

    RagSettings values;
    values.endpoint = QStringLiteral("ftp://example.test");
    QString error;
    QVERIFY(!values.save(settings, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!settings.contains(QStringLiteral("rag/ollama/endpoint")));

    values.endpoint = QStringLiteral("http://192.168.1.4:11434");
    QVERIFY(!values.save(settings, &error));
    QVERIFY(!settings.contains(QStringLiteral("rag/ollama/endpoint")));
}

QTEST_GUILESS_MAIN(TestRagSettings)
#include "test_rag_settings.moc"
