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
    quint64 historyEpoch = 1;
    QString question;
    QString answerText;      // clean text for copy/history
    QString answerMarkdown;  // numbered inline citation links for rendering
    QVector<RagCitation> citations;
    QVector<RagEvidence> evidence;
    /** False when evidence was restored from a partial persisted subset. */
    bool evidenceComplete = true;
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

/**
 * History for the current prompt: only completed turns from the current
 * history epoch, in order, up to (but excluding) the current turn. Returns an
 * empty list when currentTurnId is not part of the conversation.
 */
QStringList historyForPrompt(const QVector<RagConversationTurn>& turns,
                             quint64 historyEpoch,
                             const QString& currentTurnId);

}  // namespace Rag

Q_DECLARE_METATYPE(Rag::RagConversationTurn)
