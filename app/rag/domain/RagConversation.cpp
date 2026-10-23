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

}  // namespace Rag
