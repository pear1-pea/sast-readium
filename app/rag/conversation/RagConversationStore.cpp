#include "RagConversationStore.h"

namespace Rag {

QVector<RagConversationTurn> InMemoryConversationStore::load(
    const QString& key) {
    return m_turns.value(key);
}

void InMemoryConversationStore::save(
    const QString& key, const QVector<RagConversationTurn>& turns) {
    if (turns.isEmpty()) {
        m_turns.remove(key);
        return;
    }
    m_turns.insert(key, turns);
}

void InMemoryConversationStore::remove(const QString& key) {
    m_turns.remove(key);
}

}  // namespace Rag
