#include "RagSettings.h"

#include <QHostAddress>
#include <QSettings>
#include <QUrl>
#include <QtGlobal>

namespace Rag {
namespace {

QString key(const char* name) {
    return QStringLiteral("rag/ollama/") + QLatin1String(name);
}

const char* const kEndpointKey = "endpoint";
const char* const kEmbedModelKey = "embedModel";
const char* const kEmbedVersionKey = "embedVersion";
const char* const kChatModelKey = "chatModel";
const char* const kAnswerEnabledKey = "answerEnabled";
const char* const kMigratedKey = "migrated";

void writeValues(QSettings& settings, const RagSettings& values) {
    settings.setValue(key(kEndpointKey), values.endpoint);
    settings.setValue(key(kEmbedModelKey), values.embedModel);
    settings.setValue(key(kEmbedVersionKey), values.embedVersion);
    settings.setValue(key(kChatModelKey), values.chatModel);
    settings.setValue(key(kAnswerEnabledKey), values.answerEnabled);
    settings.setValue(key(kMigratedKey), true);
}

QString baseUrl(const QString& endpoint) {
    QString base = RagSettings::normalizedEndpoint(endpoint);
    if (base.isEmpty()) {
        base = RagSettings::defaultEndpoint();
    }
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base;
}

}  // namespace

const QString& RagSettings::defaultEndpoint() {
    static const QString endpoint = QStringLiteral("http://127.0.0.1:11434");
    return endpoint;
}

bool RagSettings::embeddingsEnabled() const {
    return !embedModel.trimmed().isEmpty() && !embedVersion.trimmed().isEmpty();
}

bool RagSettings::chatEnabled() const {
    return answerEnabled && !chatModel.trimmed().isEmpty();
}

RagEmbeddingIdentity RagSettings::embeddingIdentity() const {
    return {embedModel.trimmed(), embedVersion.trimmed()};
}

QString RagSettings::embedUrl() const {
    return baseUrl(endpoint) + QStringLiteral("/api/embed");
}

QString RagSettings::chatUrl() const {
    return baseUrl(endpoint) + QStringLiteral("/api/chat");
}

RagSettings RagSettings::load() {
    QSettings settings(QStringLiteral("SAST"), QStringLiteral("Readium"));
    return load(settings);
}

RagSettings RagSettings::load(QSettings& settings) {
    if (!settings.contains(key(kMigratedKey))) {
        // First run: seed from the legacy environment variables and persist the
        // result, including empty values, so future environment changes cannot
        // override what the user sees.
        RagSettings migrated;
        migrated.endpoint = defaultEndpoint();
        migrated.embedModel =
            qEnvironmentVariable("READIUM_OLLAMA_EMBED_MODEL").trimmed();
        migrated.embedVersion =
            qEnvironmentVariable("READIUM_OLLAMA_EMBED_VERSION").trimmed();
        migrated.chatModel =
            qEnvironmentVariable("READIUM_OLLAMA_CHAT_MODEL").trimmed();
        migrated.answerEnabled = !migrated.chatModel.isEmpty();
        writeValues(settings, migrated);
        settings.sync();
        return migrated;
    }

    RagSettings values;
    const QString endpoint =
        settings.value(key(kEndpointKey), defaultEndpoint()).toString();
    values.endpoint = normalizedEndpoint(endpoint);
    if (values.endpoint.isEmpty()) {
        values.endpoint = defaultEndpoint();
    }
    values.embedModel =
        settings.value(key(kEmbedModelKey)).toString().trimmed();
    values.embedVersion =
        settings.value(key(kEmbedVersionKey)).toString().trimmed();
    values.chatModel = settings.value(key(kChatModelKey)).toString().trimmed();
    values.answerEnabled =
        settings.value(key(kAnswerEnabledKey), false).toBool();
    return values;
}

bool RagSettings::save(QString* error) const {
    QSettings settings(QStringLiteral("SAST"), QStringLiteral("Readium"));
    return save(settings, error);
}

bool RagSettings::save(QSettings& settings, QString* error) const {
    if (error) {
        error->clear();
    }
    const RagSettings values = normalized();
    bool endpointValid = false;
    normalizedEndpoint(endpoint, &endpointValid);
    if (!endpointValid) {
        if (error) {
            *error = QStringLiteral(
                "Ollama 地址无效，仅支持本机地址（127.0.0.1、localhost "
                "或 ::1）");
        }
        return false;
    }
    writeValues(settings, values);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("无法保存 Ask PDF 设置");
        }
        return false;
    }
    return true;
}

RagSettings RagSettings::withEmbeddingModel(const QString& model) const {
    RagSettings values = *this;
    values.embedModel = model.trimmed();
    if (!values.embedModel.isEmpty() &&
        values.embedVersion.trimmed().isEmpty()) {
        values.embedVersion = QStringLiteral("1");
    }
    return values;
}

RagSettings RagSettings::normalized() const {
    RagSettings values = *this;
    values.endpoint = normalizedEndpoint(endpoint);
    if (values.endpoint.isEmpty()) {
        values.endpoint = defaultEndpoint();
    }
    values.embedModel = embedModel.trimmed();
    values.embedVersion = embedVersion.trimmed();
    values.chatModel = chatModel.trimmed();
    return values;
}

QString RagSettings::normalizedEndpoint(const QString& endpoint, bool* ok) {
    if (ok) {
        *ok = false;
    }
    const QString trimmed = endpoint.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QUrl url(trimmed);
    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return {};
    }
    if (!url.userInfo().isEmpty() || url.hasQuery() ||
        !url.fragment().isEmpty()) {
        return {};
    }
    const QString host = url.host().toLower();
    const bool isLocalhost = host == QStringLiteral("localhost");
    const QHostAddress address(host);
    if (!isLocalhost && !address.isLoopback()) {
        return {};
    }
    QString text = url.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment |
                                QUrl::StripTrailingSlash)
                       .toString();
    while (text.endsWith(QLatin1Char('/'))) {
        text.chop(1);
    }
    if (ok) {
        *ok = true;
    }
    return text;
}

bool RagSettings::operator==(const RagSettings& other) const {
    return endpoint == other.endpoint && embedModel == other.embedModel &&
           embedVersion == other.embedVersion && chatModel == other.chatModel &&
           answerEnabled == other.answerEnabled;
}

}  // namespace Rag
