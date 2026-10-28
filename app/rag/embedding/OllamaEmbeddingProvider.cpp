#include "OllamaEmbeddingProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <utility>

namespace Rag {

OllamaEmbeddingProvider::OllamaEmbeddingProvider(QString model, QString version,
                                                 int timeoutMs)
    : OllamaEmbeddingProvider(
          std::move(model), std::move(version),
          QUrl(QStringLiteral("http://127.0.0.1:11434/api/embed")), timeoutMs) {
}

OllamaEmbeddingProvider::OllamaEmbeddingProvider(QString model, QString version,
                                                 QUrl endpoint, int timeoutMs)
    : m_model(std::move(model)),
      m_version(std::move(version)),
      m_endpoint(std::move(endpoint)),
      m_timeoutMs(qMax(1, timeoutMs)) {}

QVector<QVector<float>> OllamaEmbeddingProvider::embed(
    const QStringList& texts, const RagCancellationToken& cancel,
    QString* error) {
    if (error)
        error->clear();
    if (cancel.isCancelled()) {
        if (error)
            *error = QStringLiteral("Embedding cancelled");
        return {};
    }
    if (texts.isEmpty())
        return {};
    QNetworkAccessManager manager;
    manager.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QJsonObject payload;
    payload.insert(QStringLiteral("model"), m_model);
    payload.insert(QStringLiteral("input"), QJsonArray::fromStringList(texts));
    payload.insert(QStringLiteral("truncate"), false);
    QNetworkReply* reply =
        manager.post(request, QJsonDocument(payload).toJson());
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeout, &QTimer::timeout, reply, [&timedOut, reply]() {
        timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(m_timeoutMs);
    while (!reply->isFinished() && !cancel.isCancelled()) {
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (cancel.isCancelled())
        reply->abort();
    timeout.stop();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->isOpen() ? reply->readAll() : QByteArray();
    QJsonParseError parseError;
    const QJsonDocument response = QJsonDocument::fromJson(data, &parseError);
    const QString serverError =
        response.object().value(QStringLiteral("error")).toString();
    const bool ok = !cancel.isCancelled() && !timedOut && status < 400 &&
                    reply->error() == QNetworkReply::NoError;
    if (!ok) {
        if (error) {
            if (cancel.isCancelled())
                *error = QStringLiteral("Embedding cancelled");
            else if (timedOut)
                *error = QStringLiteral("Embedding request timed out");
            else if (status >= 400)
                *error =
                    QStringLiteral("Embedding request failed (HTTP %1): %2")
                        .arg(status)
                        .arg(serverError.isEmpty() ? reply->errorString()
                                                   : serverError);
            else
                *error = QStringLiteral("Embedding network error: %1")
                             .arg(reply->errorString());
        }
        reply->deleteLater();
        return {};
    }
    reply->deleteLater();
    if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid embedding response");
        return {};
    }
    if (response.object().contains(QStringLiteral("error"))) {
        if (error)
            *error = serverError.isEmpty()
                         ? QStringLiteral("Embedding server error")
                         : QStringLiteral("Embedding server error: %1")
                               .arg(serverError);
        return {};
    }
    const QJsonArray embeddings =
        response.object().value(QStringLiteral("embeddings")).toArray();
    if (embeddings.size() != texts.size()) {
        if (error)
            *error = QStringLiteral("Invalid embedding response");
        return {};
    }
    QVector<QVector<float>> vectors;
    int dimension = 0;
    for (const QJsonValue& item : embeddings) {
        if (!item.isArray())
            break;
        const QJsonArray values = item.toArray();
        if (values.isEmpty() || (dimension && values.size() != dimension))
            break;
        dimension = values.size();
        QVector<float> vector;
        vector.reserve(dimension);
        for (const QJsonValue& value : values) {
            if (!value.isDouble() || !std::isfinite(value.toDouble()))
                break;
            vector.append(static_cast<float>(value.toDouble()));
        }
        if (vector.size() != dimension)
            break;
        vectors.append(std::move(vector));
    }
    if (vectors.size() != texts.size()) {
        if (error)
            *error = QStringLiteral("Invalid embedding dimensions");
        return {};
    }
    return vectors;
}

}  // namespace Rag
