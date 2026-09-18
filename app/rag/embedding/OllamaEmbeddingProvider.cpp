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

OllamaEmbeddingProvider::OllamaEmbeddingProvider(QString model, QString version)
    : m_model(std::move(model)), m_version(std::move(version)) {}

QVector<QVector<float>> OllamaEmbeddingProvider::embed(
    const QStringList& texts, const RagCancellationToken& cancel,
    QString* error) {
    if (error)
        error->clear();
    if (texts.isEmpty() || cancel.isCancelled())
        return {};
    QNetworkAccessManager manager;
    manager.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:11434/api/embed")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QJsonObject payload;
    payload.insert(QStringLiteral("model"), m_model);
    payload.insert(QStringLiteral("input"), QJsonArray::fromStringList(texts));
    QNetworkReply* reply =
        manager.post(request, QJsonDocument(payload).toJson());
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(30000);
    while (!reply->isFinished() && !cancel.isCancelled()) {
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (cancel.isCancelled())
        reply->abort();
    const bool ok =
        !cancel.isCancelled() && reply->error() == QNetworkReply::NoError;
    const QByteArray data = reply->readAll();
    if (!ok) {
        if (error)
            *error = cancel.isCancelled()
                         ? QStringLiteral("Embedding cancelled")
                         : reply->errorString();
        reply->deleteLater();
        return {};
    }
    reply->deleteLater();
    QJsonParseError parseError;
    const QJsonDocument response = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid embedding response");
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
