#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

#include <functional>

#include "rag/domain/RagConversation.h"

namespace Rag {

/**
 * Qt model for the current document's conversation.
 *
 * It owns the turn vector and is the single place that emits structural
 * notifications. Business state changes go through the coordinator, which
 * calls the mutators here.
 */
class RagConversationModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        TurnIdRole = Qt::UserRole + 1,
        QuestionRole,
        AnswerRole,
        AnswerMarkdownRole,
        CitationsRole,
        EvidenceRole,
        StateRole,
        FailureRole,
        ErrorRole,
        GenerationRole,
        PromptTokensRole
    };

    explicit RagConversationModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QVector<RagConversationTurn>& turns() const { return m_turns; }
    bool isEmpty() const { return m_turns.isEmpty(); }
    int count() const { return m_turns.size(); }
    int indexOf(const QString& turnId) const;
    const RagConversationTurn* turn(const QString& turnId) const;

    int appendTurn(RagConversationTurn turn);
    void updateTurn(const QString& turnId,
                    const std::function<void(RagConversationTurn&)>& mutate);
    void setState(const QString& turnId, RagTurnState state);
    void setEvidence(const QString& turnId, QVector<RagEvidence> evidence);
    void appendAnswerDelta(const QString& turnId, const QString& delta);
    /**
     * Start a fresh answer: drop the previous text, citations and any partial
     * `[[cite:` fragment still held from an interrupted stream. Regenerating
     * must not prefix the new answer with the old marker fragment.
     */
    void beginAnswer(const QString& turnId);
    void setAnswer(const QString& turnId, const QString& text,
                   const QString& markdown, QVector<RagCitation> citations);
    void setFailure(const QString& turnId, RagChatFailure failure,
                    const QString& error);
    void setPromptTokens(const QString& turnId, int tokens);
    void truncateAfter(int index);
    void clear();

signals:
    void turnStateChanged(const QString& turnId, Rag::RagTurnState state);

private:
    QVector<RagConversationTurn> m_turns;
    /** Trailing partial `[[cite:` marker held back while streaming. */
    QHash<QString, QString> m_streamPending;
};

}  // namespace Rag
