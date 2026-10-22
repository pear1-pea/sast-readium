#pragma once

#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace Rag {

/**
 * Asynchronous, cancellable checks against a local Ollama service.
 *
 * The probe deliberately owns its own request generations. They are unrelated
 * to the index, embedding and query generations in RagDocumentCoordinator:
 * a settings check must never cancel a running index or answer.
 */
class OllamaProbe : public QObject {
    Q_OBJECT

public:
    enum class Status {
        Idle,
        Checking,
        Ready,
        Unreachable,
        Timeout,
        ModelMissing,
        Failed
    };
    Q_ENUM(Status)

    struct Result {
        Status status = Status::Idle;
        QString message;
        QString endpoint;
        QString model;
        QStringList models;

        bool isReady() const { return status == Status::Ready; }
    };

    explicit OllamaProbe(QObject* parent = nullptr);
    ~OllamaProbe() override;

    /** Fetch `/api/version` and `/api/tags`. */
    void fetchModels(const QString& endpoint, int timeoutMs = 5000);
    /** Verify that the embedding model exists and can embed one short text. */
    void testEmbedding(const QString& endpoint, const QString& model,
                       int timeoutMs = 60000);
    /** Verify that the chat model can produce one token. */
    void testChat(const QString& endpoint, const QString& model,
                  int timeoutMs = 60000);

    /** Abort every in-flight probe and drop its callback. */
    void cancelAll();

signals:
    void modelsFetched(const Rag::OllamaProbe::Result& result);
    void embeddingTested(const Rag::OllamaProbe::Result& result);
    void chatTested(const Rag::OllamaProbe::Result& result);

private:
    using Handler = std::function<void(QNetworkReply*)>;

    QNetworkReply* send(const QUrl& url, const QByteArray& payload,
                        int timeoutMs, Handler handler);
    static Result classify(QNetworkReply* reply, const QByteArray& body,
                           const QString& endpoint, const QString& model);
    static bool looksLikeMissingModel(const QByteArray& body);

    QNetworkAccessManager* m_manager = nullptr;
    quint64 m_serviceGeneration = 0;
    quint64 m_embeddingGeneration = 0;
    quint64 m_chatGeneration = 0;
    QSet<QNetworkReply*> m_active;
};

}  // namespace Rag

Q_DECLARE_METATYPE(Rag::OllamaProbe::Result)
