#include "OllamaProbe.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>
#include <utility>

#include "rag/config/RagSettings.h"

namespace Rag {
namespace {

/**
 * Capabilities are stable for a given model digest, so they can be reused for
 * the whole process lifetime. The endpoint is part of the key because two
 * local services can expose the same tag with different builds.
 */
QMutex& capabilityCacheMutex() {
    static QMutex mutex;
    return mutex;
}

QHash<QString, QStringList>& capabilityCache() {
    static QHash<QString, QStringList> cache;
    return cache;
}

QString capabilityKey(const QString& endpoint, const QString& model,
                      const QString& digest) {
    return endpoint + QLatin1Char('\n') + model + QLatin1Char('\n') + digest;
}

QString errorTextFromBody(const QByteArray& body) {
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QString error = document.object()
                                  .value(QStringLiteral("error"))
                                  .toString()
                                  .trimmed();
        if (!error.isEmpty()) {
            return error;
        }
    }
    return {};
}

QString joinEndpoint(const QString& endpoint, const char* path) {
    return endpoint + QLatin1String(path);
}

QByteArray readBody(QNetworkReply* reply) {
    return reply->isOpen() ? reply->readAll() : QByteArray();
}

}  // namespace

OllamaProbe::OllamaProbe(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {
    m_manager->setProxy(QNetworkProxy::NoProxy);
}

OllamaProbe::~OllamaProbe() { cancelAll(); }

void OllamaProbe::fetchModels(const QString& endpoint, int timeoutMs) {
    bool valid = false;
    const QString base = RagSettings::normalizedEndpoint(endpoint, &valid);
    const quint64 generation = ++m_serviceGeneration;
    if (!valid) {
        Result result;
        result.status = Status::Failed;
        result.message = QStringLiteral(
            "Ollama 地址无效，仅支持本机地址（127.0.0.1、localhost 或 ::1）");
        result.endpoint = endpoint;
        emit modelsFetched(result);
        return;
    }

    send(QUrl(joinEndpoint(base, "/api/version")), {}, timeoutMs,
         [this, generation, base](QNetworkReply* reply) {
             if (generation != m_serviceGeneration) {
                 return;
             }
             const QByteArray body = readBody(reply);
             Result result = classify(reply, body, base, {});
             if (!result.isReady()) {
                 emit modelsFetched(result);
                 return;
             }
             send(
                 QUrl(joinEndpoint(base, "/api/tags")), {}, 5000,
                 [this, generation, base](QNetworkReply* tagsReply) {
                     if (generation != m_serviceGeneration) {
                         return;
                     }
                     const QByteArray tagsBody = readBody(tagsReply);
                     Result tags = classify(tagsReply, tagsBody, base, {});
                     if (!tags.isReady()) {
                         emit modelsFetched(tags);
                         return;
                     }
                     const QJsonDocument document =
                         QJsonDocument::fromJson(tagsBody);
                     const QJsonArray models =
                         document.object()
                             .value(QStringLiteral("models"))
                             .toArray();
                     QVector<QPair<QString, QString>> entries;
                     for (const QJsonValue& value : models) {
                         const QJsonObject item = value.toObject();
                         QString name =
                             item.value(QStringLiteral("name")).toString();
                         if (name.isEmpty()) {
                             name =
                                 item.value(QStringLiteral("model")).toString();
                         }
                         if (!name.isEmpty()) {
                             tags.models.append(name);
                             entries.append(
                                 {name, item.value(QStringLiteral("digest"))
                                            .toString()});
                         }
                     }
                     tags.status = Status::Ready;
                     tags.message =
                         tags.models.isEmpty()
                             ? QStringLiteral("已连接，但尚未安装模型")
                             : QStringLiteral("已连接 · %1 个模型")
                                   .arg(tags.models.size());
                     classifyCapabilities(base, entries, tags, generation);
                 });
         });
}

void OllamaProbe::classifyCapabilities(
    const QString& base, const QVector<QPair<QString, QString>>& models,
    Result result, quint64 generation) {
    auto state = std::make_shared<CapabilityRequestState>();
    state->all = models;
    state->result = std::move(result);
    {
        QMutexLocker lock(&capabilityCacheMutex());
        for (const auto& entry : models) {
            const auto cached = capabilityCache().constFind(
                capabilityKey(base, entry.first, entry.second));
            if (cached != capabilityCache().constEnd()) {
                state->capabilities.insert(entry.first, cached.value());
            } else {
                state->queue.append(entry);
            }
        }
    }

    if (state->queue.isEmpty()) {
        finishCapabilities(generation, state);
        return;
    }

    requestNextCapability(base, generation, state);
    QTimer::singleShot(15000, this, [this, generation, state]() {
        // A slow or hanging /api/show must not keep the dialog "正在连接...".
        // Unresolved models are reported as unknown, not hidden.
        finishCapabilities(generation, state);
    });
}

void OllamaProbe::requestNextCapability(
    const QString& base, quint64 generation,
    const std::shared_ptr<CapabilityRequestState>& state) {
    if (!state || state->finished || generation != m_serviceGeneration) {
        return;
    }
    // Bounded concurrency keeps a large model list from opening dozens of
    // sockets at once on a local service.
    while (state->active < 3 && !state->queue.isEmpty()) {
        const auto entry = state->queue.takeFirst();
        ++state->active;
        const QJsonObject payload{{QStringLiteral("model"), entry.first}};
        send(QUrl(joinEndpoint(base, "/api/show")),
             QJsonDocument(payload).toJson(QJsonDocument::Compact), 5000,
             [this, base, generation, state, entry](QNetworkReply* reply) {
                 if (state->finished || generation != m_serviceGeneration) {
                     return;
                 }
                 --state->active;
                 const int httpStatus =
                     reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                         .toInt();
                 if (httpStatus == 200 &&
                     reply->error() == QNetworkReply::NoError) {
                     const QJsonObject object =
                         QJsonDocument::fromJson(readBody(reply)).object();
                     const QJsonArray capabilitiesArray =
                         object.value(QStringLiteral("capabilities")).toArray();
                     QStringList capabilities;
                     for (const QJsonValue& value : capabilitiesArray) {
                         const QString capability = value.toString();
                         if (!capability.isEmpty()) {
                             capabilities.append(capability);
                         }
                     }
                     if (!capabilities.isEmpty()) {
                         state->capabilities.insert(entry.first, capabilities);
                         QMutexLocker lock(&capabilityCacheMutex());
                         capabilityCache().insert(
                             capabilityKey(base, entry.first, entry.second),
                             capabilities);
                     }
                 }
                 // A failed lookup stays unresolved and is retried on the next
                 // refresh instead of being cached as "no capabilities".
                 requestNextCapability(base, generation, state);
             });
    }
    if (state->active == 0) {
        finishCapabilities(generation, state);
    }
}

void OllamaProbe::finishCapabilities(
    quint64 generation, const std::shared_ptr<CapabilityRequestState>& state) {
    if (!state || state->finished || generation != m_serviceGeneration) {
        return;
    }
    state->finished = true;
    Result out = state->result;
    out.embeddingModels.clear();
    out.chatModels.clear();
    out.unknownModels.clear();
    for (const auto& entry : state->all) {
        const QString& name = entry.first;
        const QStringList capabilities = state->capabilities.value(name);
        if (capabilities.isEmpty()) {
            out.unknownModels.append(name);
            out.embeddingModels.append(name);
            out.chatModels.append(name);
            continue;
        }
        const bool embedding =
            capabilities.contains(QStringLiteral("embedding"));
        const bool chat = capabilities.contains(QStringLiteral("completion"));
        if (embedding) {
            out.embeddingModels.append(name);
        }
        if (chat) {
            out.chatModels.append(name);
        }
        if (!embedding && !chat) {
            // A capability set we do not understand must not hide a model.
            out.unknownModels.append(name);
            out.embeddingModels.append(name);
            out.chatModels.append(name);
        }
    }
    if (!out.models.isEmpty()) {
        out.message =
            QStringLiteral("已连接 · %1 个模型（%2 个 embedding / %3 个 chat）")
                .arg(out.models.size())
                .arg(out.embeddingModels.size())
                .arg(out.chatModels.size());
    }
    emit modelsFetched(out);
}

void OllamaProbe::testEmbedding(const QString& endpoint, const QString& model,
                                int timeoutMs) {
    bool valid = false;
    const QString base = RagSettings::normalizedEndpoint(endpoint, &valid);
    const quint64 generation = ++m_embeddingGeneration;

    Result result;
    result.endpoint = base.isEmpty() ? endpoint : base;
    result.model = model.trimmed();
    if (!valid) {
        result.status = Status::Failed;
        result.message = QStringLiteral(
            "Ollama 地址无效，仅支持本机地址（127.0.0.1、localhost 或 ::1）");
        emit embeddingTested(result);
        return;
    }
    if (result.model.isEmpty()) {
        result.status = Status::ModelMissing;
        result.message = QStringLiteral("未选择 Embedding 模型");
        emit embeddingTested(result);
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), result.model);
    payload.insert(QStringLiteral("input"),
                   QJsonArray{QStringLiteral("readium")});
    send(
        QUrl(joinEndpoint(base, "/api/embed")),
        QJsonDocument(payload).toJson(QJsonDocument::Compact), timeoutMs,
        [this, generation, base, model = result.model](QNetworkReply* reply) {
            if (generation != m_embeddingGeneration) {
                return;
            }
            const QByteArray body = readBody(reply);
            Result checked = classify(reply, body, base, model);
            if (!checked.isReady()) {
                emit embeddingTested(checked);
                return;
            }
            const QJsonDocument document = QJsonDocument::fromJson(body);
            const QJsonArray embeddings =
                document.object().value(QStringLiteral("embeddings")).toArray();
            if (embeddings.isEmpty()) {
                checked.status = Status::Failed;
                checked.message = QStringLiteral("Embedding 响应无效");
            } else {
                checked.message = QStringLiteral("Embedding 可用");
            }
            emit embeddingTested(checked);
        });
}

void OllamaProbe::testChat(const QString& endpoint, const QString& model,
                           int timeoutMs) {
    bool valid = false;
    const QString base = RagSettings::normalizedEndpoint(endpoint, &valid);
    const quint64 generation = ++m_chatGeneration;

    Result result;
    result.endpoint = base.isEmpty() ? endpoint : base;
    result.model = model.trimmed();
    if (!valid) {
        result.status = Status::Failed;
        result.message = QStringLiteral(
            "Ollama 地址无效，仅支持本机地址（127.0.0.1、localhost 或 ::1）");
        emit chatTested(result);
        return;
    }
    if (result.model.isEmpty()) {
        result.status = Status::ModelMissing;
        result.message = QStringLiteral("未选择 Chat 模型");
        emit chatTested(result);
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), result.model);
    payload.insert(QStringLiteral("stream"), false);
    payload.insert(QStringLiteral("messages"),
                   QJsonArray{QJsonObject{
                       {QStringLiteral("role"), QStringLiteral("user")},
                       {QStringLiteral("content"), QStringLiteral("ping")}}});
    payload.insert(QStringLiteral("options"),
                   QJsonObject{{QStringLiteral("num_predict"), 1}});
    send(QUrl(joinEndpoint(base, "/api/chat")),
         QJsonDocument(payload).toJson(QJsonDocument::Compact), timeoutMs,
         [this, generation, base, model = result.model](QNetworkReply* reply) {
             if (generation != m_chatGeneration) {
                 return;
             }
             const QByteArray body = readBody(reply);
             Result checked = classify(reply, body, base, model);
             if (!checked.isReady()) {
                 emit chatTested(checked);
                 return;
             }
             const QJsonDocument document = QJsonDocument::fromJson(body);
             const QJsonObject object = document.object();
             const bool hasMessage =
                 object.value(QStringLiteral("message")).isObject() ||
                 object.contains(QStringLiteral("response"));
             if (!hasMessage) {
                 checked.status = Status::Failed;
                 checked.message = QStringLiteral("回答模型响应无效");
             } else {
                 checked.message = QStringLiteral("回答模型可用");
             }
             emit chatTested(checked);
         });
}

void OllamaProbe::cancelAll() {
    ++m_serviceGeneration;
    ++m_embeddingGeneration;
    ++m_chatGeneration;
    const QSet<QNetworkReply*> active = m_active;
    for (QNetworkReply* reply : active) {
        if (reply && !reply->isFinished()) {
            reply->abort();
        }
    }
}

QNetworkReply* OllamaProbe::send(const QUrl& url, const QByteArray& payload,
                                 int timeoutMs, Handler handler) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* reply = nullptr;
    if (payload.isEmpty()) {
        reply = m_manager->get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
        reply = m_manager->post(request, payload);
    }
    m_active.insert(reply);
    QTimer::singleShot(qMax(1, timeoutMs), reply, [reply]() {
        if (!reply->isFinished()) {
            reply->setProperty("readiumTimedOut", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, handler = std::move(handler)]() {
                m_active.remove(reply);
                handler(reply);
                reply->deleteLater();
            });
    return reply;
}

OllamaProbe::Result OllamaProbe::classify(QNetworkReply* reply,
                                          const QByteArray& body,
                                          const QString& endpoint,
                                          const QString& model) {
    Result result;
    result.endpoint = endpoint;
    result.model = model;
    result.status = Status::Ready;

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();

    // A 404 only means "model missing" when a concrete model was being checked
    // and the body actually names a missing model. A plain 404 (wrong address
    // or API path) is an address error.
    if (!model.isEmpty() && looksLikeMissingModel(body)) {
        result.status = Status::ModelMissing;
        result.message =
            QStringLiteral("模型 %1 不存在，请先在 Ollama 中安装").arg(model);
        return result;
    }
    if (httpStatus >= 400) {
        result.status = Status::Failed;
        const QString detail = errorTextFromBody(body);
        result.message =
            detail.isEmpty()
                ? QStringLiteral("Ollama 返回 HTTP %1，请检查地址或 API 路径")
                      .arg(httpStatus)
                : detail;
        return result;
    }

    if (networkError == QNetworkReply::OperationCanceledError) {
        if (reply->property("readiumTimedOut").toBool()) {
            result.status = Status::Timeout;
            result.message =
                QStringLiteral("请求超时，模型可能正在加载，请稍后重试");
        } else {
            result.status = Status::Failed;
            result.message = QStringLiteral("请求已取消");
        }
        return result;
    }
    if (networkError != QNetworkReply::NoError) {
        switch (networkError) {
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::NetworkSessionFailedError:
                result.status = Status::Unreachable;
                result.message = QStringLiteral(
                    "无法连接 Ollama，请确认服务已启动且地址可访问");
                break;
            default:
                result.status = Status::Failed;
                result.message = reply->errorString();
                break;
        }
        return result;
    }

    const QString bodyError = errorTextFromBody(body);
    if (!bodyError.isEmpty()) {
        result.status =
            looksLikeMissingModel(body) ? Status::ModelMissing : Status::Failed;
        result.message = bodyError;
    }
    return result;
}

bool OllamaProbe::looksLikeMissingModel(const QByteArray& body) {
    const QString text = QString::fromUtf8(body).toLower();
    if (!text.contains(QStringLiteral("model"))) {
        return false;
    }
    return text.contains(QStringLiteral("not found")) ||
           text.contains(QStringLiteral("try pulling")) ||
           text.contains(QStringLiteral("no such model"));
}

}  // namespace Rag
