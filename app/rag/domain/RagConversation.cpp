#include "RagConversation.h"

namespace Rag {

QStringList conversationFromTurns(const QVector<RagConversationTurn>& turns) {
    QStringList conversation;
    for (const RagConversationTurn& turn : turns) {
        if (turn.state != RagTurnState::Ready) {
            continue;
        }
        const QString question = turn.question.trimmed();
        const QString answer = turn.answerText.trimmed();
        if (question.isEmpty() || answer.isEmpty()) {
            continue;
        }
        conversation.append(question.left(2000));
        conversation.append(answer.left(4000));
    }
    return conversation;
}

QStringList historyForPrompt(const QVector<RagConversationTurn>& turns,
                             quint64 historyEpoch,
                             const QString& currentTurnId) {
    QVector<RagConversationTurn> eligible;
    bool foundCurrent = currentTurnId.isEmpty();
    for (const RagConversationTurn& turn : turns) {
        if (!currentTurnId.isEmpty() && turn.id == currentTurnId) {
            foundCurrent = true;
            break;
        }
        if (turn.historyEpoch == historyEpoch) {
            eligible.append(turn);
        }
    }
    if (!foundCurrent) {
        return {};
    }
    return conversationFromTurns(eligible);
}

}  // namespace Rag
