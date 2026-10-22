#include "OllamaChatProvider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTimer>
#include <utility>
#include "ContextAssembler.h"
namespace Rag {
namespace {
class OllamaChatTask : public ChatTask {
public:
    OllamaChatTask(QString model, QUrl endpoint, ChatRequest request,
                   int firstByteTimeoutMs, int idleTimeoutMs, QObject* parent)
        : ChatTask(parent),
          m_model(std::move(model)),
          m_endpoint(std::move(endpoint)),
          m_request(std::move(request)),
          m_manager(this),
          m_timeout(this),
          m_firstByteTimeoutMs(qMax(1, firstByteTimeoutMs)),
          m_idleTimeoutMs(qMax(1, idleTimeoutMs)) {
        m_manager.setProxy(QNetworkProxy::NoProxy);
        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this]() {
            fail(QStringLiteral("Answer generation timed out"));
        });
    }
    void start() override {
        if (m_started || m_terminal)
            return;
        m_started = true;
        // Defense in depth: the coordinator budgets the request, but a provider
        // must never send content that cannot fit the declared window.
        const QString budgetProblem = ContextAssembler::budgetError(m_request);
        if (!budgetProblem.isEmpty()) {
            fail(budgetProblem);
            return;
        }
        const QJsonArray messages = ContextAssembler::messages(m_request);
        QNetworkRequest request(m_endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        // Declare the window explicitly so the service limit matches the budget
        // instead of depending on an Ollama version default.
        QJsonObject body{
            {"model", m_model},
            {"messages", messages},
            {"stream", true},
            {"options",
             QJsonObject{{"num_predict", m_request.maxOutputTokens},
                         {"num_ctx", m_request.contextWindowTokens}}}};
        m_reply = m_manager.post(
            request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(m_reply, &QNetworkReply::readyRead, this,
                [this]() { consume(false); });
        connect(m_reply, &QNetworkReply::finished, this, [this]() {
            if (m_terminal)
                return;
            consume(true);
            if (m_terminal)
                return;
            if (m_reply->error() != QNetworkReply::NoError)
                fail(m_reply->errorString());
            else if (!m_done)
                fail(QStringLiteral("Incomplete chat response"));
            else {
                m_terminal = true;
                m_timeout.stop();
                if (m_promptTokens > 0)
                    emit promptEvaluated(m_promptTokens);
                emit completed(m_answer);
            }
        });
        m_timeout.start(m_firstByteTimeoutMs);
    }
    void cancel() override {
        if (m_terminal)
            return;
        m_terminal = true;
        m_timeout.stop();
        if (m_reply)
            m_reply->abort();
        emit cancelled();
    }

private:
    void fail(const QString& error) {
        if (m_terminal)
            return;
        m_terminal = true;
        m_timeout.stop();
        if (m_reply && !m_reply->isFinished())
            m_reply->abort();
        emit failed(error);
    }
    void consume(bool final) {
        if (m_terminal)
            return;
        m_buffer += m_reply->readAll();
        if (m_buffer.size() > 1024 * 1024) {
            fail(QStringLiteral("Chat response too large"));
            return;
        }
        while (!m_terminal) {
            const int newline = m_buffer.indexOf('\n');
            if (newline < 0 && !final)
                break;
            if (m_buffer.isEmpty())
                break;
            const QByteArray line =
                newline < 0 ? m_buffer : m_buffer.left(newline);
            m_buffer.remove(0, newline < 0 ? m_buffer.size() : newline + 1);
            if (line.trimmed().isEmpty())
                continue;
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !json.isObject()) {
                fail(QStringLiteral("Invalid chat stream"));
                return;
            }
            const auto object = json.object();
            if (object.contains("error")) {
                fail(object.value("error").toString());
                return;
            }

            // The first complete NDJSON line is the first byte of progress.
            // Reset on every line, including reasoning chunks with empty
            // content, so long thinking phases are not treated as idle.
            m_timeout.start(m_idleTimeoutMs);

            const QString delta =
                object.value("message").toObject().value("content").toString();
            m_answer += delta;
            if (m_answer.size() > 64000) {
                fail(QStringLiteral("Answer too long"));
                return;
            }
            if (!delta.isEmpty())
                emit deltaReceived(delta);
            // The service reports the real tokenizer count on the final chunk.
            const int promptTokens =
                object.value(QStringLiteral("prompt_eval_count")).toInt();
            if (promptTokens > 0)
                m_promptTokens = promptTokens;
            m_done = m_done || object.value("done").toBool();
        }
    }
    QString m_model;
    QUrl m_endpoint;
    ChatRequest m_request;
    QNetworkAccessManager m_manager;
    QTimer m_timeout;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_buffer;
    QString m_answer;
    int m_firstByteTimeoutMs;
    int m_idleTimeoutMs;
    int m_promptTokens = 0;
    bool m_started = false;
    bool m_terminal = false;
    bool m_done = false;
};
}  // namespace
OllamaChatProvider::OllamaChatProvider(QString model, QUrl endpoint,
                                       int firstByteTimeoutMs,
                                       int idleTimeoutMs)
    : m_model(std::move(model)),
      m_endpoint(std::move(endpoint)),
      m_firstByteTimeoutMs(firstByteTimeoutMs),
      m_idleTimeoutMs(idleTimeoutMs) {}
ChatTask* OllamaChatProvider::generate(const ChatRequest& request,
                                       QObject* parent) {
    return new OllamaChatTask(m_model, m_endpoint, request,
                              m_firstByteTimeoutMs, m_idleTimeoutMs, parent);
}
}  // namespace Rag
