#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "rag/domain/RagConversation.h"

namespace Rag {

struct RagConversationState {
    QVector<RagConversationTurn> turns;
    quint64 historyEpoch = 1;
    QString chatModel;
};

/**
 * Persistence boundary for per-document conversations.
 *
 * v1 uses InMemoryConversationStore (same run, per document path). A later
 * iteration can add a QSettings/JSON implementation without touching the
 * coordinator or the UI.
 */
class RagConversationStore {
public:
    virtual ~RagConversationStore() = default;
    virtual RagConversationState load(const QString& key) = 0;
    virtual void save(const QString& key,
                      const RagConversationState& state) = 0;
    virtual void remove(const QString& key) = 0;
};

class JsonConversationStore : public RagConversationStore {
public:
    explicit JsonConversationStore(QString filePath);

    static QString defaultPath();

    RagConversationState load(const QString& key) override;
    void save(const QString& key, const RagConversationState& state) override;
    void remove(const QString& key) override;

private:
    void ensureLoaded();
    void write();

    QString m_path;
    QHash<QString, RagConversationState> m_documents;
    QStringList m_order;
    bool m_loaded = false;
};

class InMemoryConversationStore : public RagConversationStore {
public:
    RagConversationState load(const QString& key) override;
    void save(const QString& key, const RagConversationState& state) override;
    void remove(const QString& key) override;

private:
    QHash<QString, RagConversationState> m_documents;
};

}  // namespace Rag
