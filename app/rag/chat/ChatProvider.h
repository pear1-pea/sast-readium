#pragma once

#include <QObject>
#include <QStringList>
#include "rag/domain/RagTypes.h"

namespace Rag {
struct ChatRequest {
    QString question;
    QString context;
    QStringList conversation;
    QVector<RagEvidence> evidence;
    int maxOutputTokens = 1024;
};
struct RagCitation {
    QString chunkId;
    int pageStart = -1;
    int pageEnd = -1;
    QString sectionPath;
    QString snippet;
};
struct RagAnswer {
    QString text;
    QVector<RagCitation> citations;
};
class ChatTask : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start() = 0;
    virtual void cancel() = 0;
signals:
    void deltaReceived(const QString& text);
    void completed(const QString& text);
    void failed(const QString& error);
    void cancelled();
};
class ChatProvider {
public:
    virtual ~ChatProvider() = default;
    virtual ChatTask* generate(const ChatRequest& request, QObject* parent) = 0;
};
}  // namespace Rag
