#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "rag/domain/RagTypes.h"

namespace Rag {

/**
 * One question/answer turn in the per-document conversation.
 *
 * `id` identifies the logical turn while `generation` identifies the current
 * generation attempt: regenerating a turn increments the generation so late
 * callbacks from earlier attempts can be rejected.
 */
struct RagConversationTurn {
    QString id;
    quint64 generation = 0;
    QString question;
    QString answerText;
    QVector<RagCitation> citations;
    QVector<RagEvidence> evidence;
    RagTurnState state = RagTurnState::Retrieving;
    RagChatFailure failure = RagChatFailure::None;
    QString error;
    std::optional<int> promptTokens;
};

/**
 * Flat history for ContextAssembler.
 *
 * Only completed turns with a non-empty answer participate: retrieving,
 * generating, failed and cancelled turns must never be sent to the model as if
 * they were a normal assistant reply.
 */
QStringList conversationFromTurns(const QVector<RagConversationTurn>& turns);

}  // namespace Rag

Q_DECLARE_METATYPE(Rag::RagConversationTurn)
