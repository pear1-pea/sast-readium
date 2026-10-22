#pragma once
#include <QUrl>
#include "ChatProvider.h"
namespace Rag {
class OllamaChatProvider : public ChatProvider {
public:
    explicit OllamaChatProvider(
        QString model,
        QUrl endpoint = QUrl(QStringLiteral("http://127.0.0.1:11434/api/chat")),
        int firstByteTimeoutMs = 120000, int idleTimeoutMs = 45000);
    ChatTask* generate(const ChatRequest& request, QObject* parent) override;

private:
    QString m_model;
    QUrl m_endpoint;
    int m_firstByteTimeoutMs;
    int m_idleTimeoutMs;
};
}  // namespace Rag
