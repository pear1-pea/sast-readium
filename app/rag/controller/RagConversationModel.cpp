#include "RagConversationModel.h"

#include <utility>

namespace Rag {
namespace {
constexpr int kMaxTurns = 20;
constexpr int kMaxCharacters = 200000;
constexpr int kMaxTurnCharacters = 20000;
constexpr int kMaxPendingMarkerCharacters = 512;

/** A trailing '[' fragment that can still grow into a citation marker. */
bool isPartialMarker(const QString& text) {
    return QStringLiteral("[[cite:").startsWith(text) ||
           QStringLiteral("[cite:").startsWith(text);
}

/**
 * Hide `[[cite:...]]` markers while the answer streams.
 *
 * A marker can be split across deltas, so an incomplete marker (or a trailing
 * '[' that may become one) is kept in `pending` until the next delta. Both the
 * double-bracket form from the prompt and the single-bracket form some models
 * produce are removed; otherwise a model that echoes the literal
 * `[cite:chunk_id]` placeholder would leak it into the visible answer.
 */
QString filterStreamDelta(QString& pending, const QString& delta) {
    QString buffer = pending + delta;
    pending.clear();
    QString visible;
    int index = 0;
    while (index < buffer.size()) {
        const int open = buffer.indexOf(QLatin1Char('['), index);
        if (open < 0) {
            visible += buffer.mid(index);
            return visible;
        }
        visible += buffer.mid(index, open - index);

        const QString rest = buffer.mid(open);
        int markerEnd = -1;
        if (rest.startsWith(QStringLiteral("[[cite:"))) {
            const int close = rest.indexOf(QStringLiteral("]]"), 2);
            if (close >= 0) {
                markerEnd = open + close + 2;
            }
        } else if (rest.startsWith(QStringLiteral("[cite:"))) {
            const int close = rest.indexOf(QLatin1Char(']'), 6);
            if (close >= 0) {
                markerEnd = open + close + 1;
            }
        } else if (!isPartialMarker(rest)) {
            // A plain '[' that cannot start a citation marker is text.
            visible += QLatin1Char('[');
            index = open + 1;
            continue;
        }

        if (markerEnd < 0) {
            // Incomplete marker: hold it (and any '[' that may still grow into
            // one) until the next delta.
            pending = rest;
            if (pending.size() > kMaxPendingMarkerCharacters) {
                // Malformed or never-closed marker: stop holding the text.
                visible += pending;
                pending.clear();
            }
            return visible;
        }
        index = markerEnd;
    }
    return visible;
}
}  // namespace

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
        case AnswerMarkdownRole:
            return turn.answerMarkdown;
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
    while (!m_turns.isEmpty()) {
        int characters = 0;
        for (const RagConversationTurn& existing : m_turns) {
            characters += existing.question.size() + existing.answerText.size();
        }
        const bool tooManyTurns = m_turns.size() >= kMaxTurns;
        const bool tooManyCharacters =
            characters + turn.question.size() + turn.answerText.size() >
            kMaxCharacters;
        if (!tooManyTurns && !tooManyCharacters) {
            break;
        }
        beginRemoveRows({}, 0, 0);
        m_turns.removeFirst();
        endRemoveRows();
    }
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
        turn.evidenceComplete = true;
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
    QString& text = m_turns[row].answerText;
    const int remaining = kMaxTurnCharacters - text.size();
    if (remaining <= 0) {
        m_streamPending.remove(turnId);
        return;
    }
    const QString visible = filterStreamDelta(m_streamPending[turnId], delta);
    if (visible.isEmpty()) {
        return;
    }
    text += visible.left(remaining);
    if (visible.size() >= remaining) {
        m_streamPending.remove(turnId);
    }
    const QModelIndex modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {AnswerRole});
}

void RagConversationModel::beginAnswer(const QString& turnId) {
    m_streamPending.remove(turnId);
    updateTurn(turnId, [](RagConversationTurn& turn) {
        turn.answerText.clear();
        turn.answerMarkdown.clear();
        turn.citations.clear();
    });
}

void RagConversationModel::setAnswer(const QString& turnId, const QString& text,
                                     const QString& markdown,
                                     QVector<RagCitation> citations) {
    m_streamPending.remove(turnId);
    updateTurn(turnId,
               [&text, &markdown, &citations](RagConversationTurn& turn) {
                   turn.answerText = text;
                   turn.answerMarkdown = markdown;
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
    for (int i = first; i < m_turns.size(); ++i) {
        m_streamPending.remove(m_turns.at(i).id);
    }
    beginRemoveRows({}, first, m_turns.size() - 1);
    m_turns.resize(first);
    endRemoveRows();
}

void RagConversationModel::clear() {
    m_streamPending.clear();
    if (m_turns.isEmpty()) {
        return;
    }
    beginResetModel();
    m_turns.clear();
    endResetModel();
}

}  // namespace Rag
