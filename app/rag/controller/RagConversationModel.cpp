#include "RagConversationModel.h"

#include <utility>

namespace Rag {

RagConversationModel::RagConversationModel(QObject* parent)
    : QAbstractListModel(parent) {}

int RagConversationModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_turns.size();
}

QVariant RagConversationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_turns.size()) {
        return {};
    }
    const RagConversationTurn& turn = m_turns.at(index.row());
    switch (role) {
        case TurnIdRole:
            return turn.id;
        case QuestionRole:
            return turn.question;
        case AnswerRole:
            return turn.answerText;
        case CitationsRole:
            return QVariant::fromValue(turn.citations);
        case EvidenceRole:
            return QVariant::fromValue(turn.evidence);
        case StateRole:
            return static_cast<int>(turn.state);
        case FailureRole:
            return static_cast<int>(turn.failure);
        case ErrorRole:
            return turn.error;
        case GenerationRole:
            return QVariant::fromValue(turn.generation);
        case PromptTokensRole:
            return turn.promptTokens.has_value() ? QVariant(*turn.promptTokens)
                                                 : QVariant();
        default:
            return {};
    }
}

QHash<int, QByteArray> RagConversationModel::roleNames() const {
    return {{TurnIdRole, "turnId"},         {QuestionRole, "question"},
            {AnswerRole, "answer"},         {CitationsRole, "citations"},
            {EvidenceRole, "evidence"},     {StateRole, "state"},
            {FailureRole, "failure"},       {ErrorRole, "error"},
            {GenerationRole, "generation"}, {PromptTokensRole, "promptTokens"}};
}

int RagConversationModel::indexOf(const QString& turnId) const {
    for (int i = 0; i < m_turns.size(); ++i) {
        if (m_turns.at(i).id == turnId) {
            return i;
        }
    }
    return -1;
}

const RagConversationTurn* RagConversationModel::turn(
    const QString& turnId) const {
    const int index = indexOf(turnId);
    return index < 0 ? nullptr : &m_turns.at(index);
}

int RagConversationModel::appendTurn(RagConversationTurn turn) {
    const int row = m_turns.size();
    beginInsertRows({}, row, row);
    m_turns.append(std::move(turn));
    endInsertRows();
    return row;
}

void RagConversationModel::updateTurn(
    const QString& turnId,
    const std::function<void(RagConversationTurn&)>& mutate) {
    const int row = indexOf(turnId);
    if (row < 0 || !mutate) {
        return;
    }
    mutate(m_turns[row]);
    const QModelIndex modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex);
}

void RagConversationModel::setState(const QString& turnId, RagTurnState state) {
    updateTurn(turnId,
               [state](RagConversationTurn& turn) { turn.state = state; });
    emit turnStateChanged(turnId, state);
}

void RagConversationModel::setEvidence(const QString& turnId,
                                       QVector<RagEvidence> evidence) {
    updateTurn(turnId, [evidence = std::move(evidence)](
                           RagConversationTurn& turn) mutable {
        turn.evidence = std::move(evidence);
    });
}

void RagConversationModel::appendAnswerDelta(const QString& turnId,
                                             const QString& delta) {
    if (delta.isEmpty()) {
        return;
    }
    const int row = indexOf(turnId);
    if (row < 0) {
        return;
    }
    m_turns[row].answerText += delta;
    const QModelIndex modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {AnswerRole});
}

void RagConversationModel::setAnswer(const QString& turnId, const QString& text,
                                     QVector<RagCitation> citations) {
    updateTurn(turnId, [&text, &citations](RagConversationTurn& turn) {
        turn.answerText = text;
        turn.citations = citations;
        turn.failure = RagChatFailure::None;
        turn.error.clear();
    });
}

void RagConversationModel::setFailure(const QString& turnId,
                                      RagChatFailure failure,
                                      const QString& error) {
    updateTurn(turnId, [failure, &error](RagConversationTurn& turn) {
        turn.failure = failure;
        turn.error = error;
    });
}

void RagConversationModel::setPromptTokens(const QString& turnId, int tokens) {
    updateTurn(turnId, [tokens](RagConversationTurn& turn) {
        turn.promptTokens = tokens;
    });
}

void RagConversationModel::truncateAfter(int index) {
    if (index < -1 || index >= m_turns.size() - 1) {
        return;
    }
    const int first = index + 1;
    beginRemoveRows({}, first, m_turns.size() - 1);
    m_turns.resize(first);
    endRemoveRows();
}

void RagConversationModel::keepOnly(const QString& turnId) {
    const int index = indexOf(turnId);
    if (index < 0) {
        clear();
        return;
    }
    if (m_turns.size() == 1) {
        return;
    }
    beginResetModel();
    RagConversationTurn turn = m_turns.at(index);
    m_turns.clear();
    m_turns.append(std::move(turn));
    endResetModel();
}

void RagConversationModel::clear() {
    if (m_turns.isEmpty()) {
        return;
    }
    beginResetModel();
    m_turns.clear();
    endResetModel();
}

}  // namespace Rag
