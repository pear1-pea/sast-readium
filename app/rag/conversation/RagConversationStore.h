#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include "rag/domain/RagConversation.h"

namespace Rag {

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
    virtual QVector<RagConversationTurn> load(const QString& key) = 0;
    virtual void save(const QString& key,
                      const QVector<RagConversationTurn>& turns) = 0;
    virtual void remove(const QString& key) = 0;
};

class InMemoryConversationStore : public RagConversationStore {
public:
    QVector<RagConversationTurn> load(const QString& key) override;
    void save(const QString& key,
              const QVector<RagConversationTurn>& turns) override;
    void remove(const QString& key) override;

private:
    QHash<QString, QVector<RagConversationTurn>> m_turns;
};

}  // namespace Rag
