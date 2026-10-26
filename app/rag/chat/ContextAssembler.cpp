#include "ContextAssembler.h"
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
namespace Rag {
namespace {
// Every serialized chat message adds role and framing tokens on top of its
// content. The exact overhead depends on the model template, so this is a
// deliberately generous constant used for budgeting only.
constexpr qint64 kMessageOverheadTokens = 16;

QString citationSnippet(const QString& text) {
    QString snippet = text.simplified();
    constexpr int kMaxSnippetLength = 160;
    if (snippet.size() > kMaxSnippetLength) {
        snippet = snippet.left(kMaxSnippetLength - 3).trimmed() +
                  QStringLiteral("...");
    }
    return snippet;
}

QString evidenceBlock(const RagEvidence& item) {
    return QStringLiteral("\n[chunk_id=%1]\n[page=%2]\n")
               .arg(item.chunkId)
               .arg(item.pageStart + 1) +
           item.text + QStringLiteral("\n[/chunk]\n");
}

qint64 utf8Size(const QString& text) { return text.toUtf8().size(); }
}  // namespace

ChatRequest ContextAssembler::assemble(const QString& question,
                                       const QVector<RagEvidence>& evidence,
                                       const QStringList& conversation,
                                       int maxCharacters) {
    ChatRequest request;
    request.question = question.left(2000);
    const int pairedCount = (conversation.size() / 2) * 2;
    const int historyStart = qMax(0, pairedCount - 6);
    for (int i = historyStart; i < pairedCount; ++i)
        request.conversation.append(conversation[i].left(4000));
    int remaining = qMax(0, maxCharacters);
    QSet<QString> seen;
    for (const auto& item : evidence) {
        if (item.chunkId.isEmpty() || seen.contains(item.chunkId))
            continue;
        const QString block = evidenceBlock(item);
        // Keep source ranges and citations identical to the text sent to chat.
        // A later, smaller block may still fit after an oversized block.
        if (item.text.isEmpty() || block.size() > remaining)
            continue;
        request.context += block;
        remaining -= block.size();
        request.evidence.append(item);
        seen.insert(item.chunkId);
    }
    return request;
}

QJsonArray ContextAssembler::messages(const ChatRequest& request) {
    QJsonArray messages;
    messages.append(
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                    {QStringLiteral("content"), request.systemPrompt}});
    for (int i = 0; i < request.conversation.size(); ++i) {
        messages.append(QJsonObject{
            {QStringLiteral("role"),
             i % 2 == 0 ? QStringLiteral("user") : QStringLiteral("assistant")},
            {QStringLiteral("content"), request.conversation[i]}});
    }
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), request.question +
                                        QStringLiteral("\nEvidence:\n") +
                                        request.context}});
    return messages;
}

qint64 ContextAssembler::estimateInputTokens(const ChatRequest& request) {
    const QJsonArray serialized = messages(request);
    qint64 total = 0;
    for (const auto& message : serialized) {
        total += utf8Size(message.toObject()
                              .value(QStringLiteral("content"))
                              .toString()) +
                 kMessageOverheadTokens;
    }
    return total;
}

ChatRequest ContextAssembler::assembleBudgeted(
    const QString& question, const QVector<RagEvidence>& evidence,
    const QStringList& conversation, const ChatBudget& budget) {
    ChatRequest request;
    // The question is never truncated: a partial question would change what the
    // model is asked while citations still point at the full evidence text.
    request.question = question;
    request.maxOutputTokens = budget.maxOutputTokens;
    request.contextWindowTokens = budget.contextWindowTokens;
    request.tokenSafetyMargin = budget.tokenSafetyMargin;

    const qint64 capacity = qint64(budget.contextWindowTokens) -
                            budget.maxOutputTokens - budget.tokenSafetyMargin;
    if (budget.contextWindowTokens <= 0 || budget.maxOutputTokens <= 0 ||
        budget.tokenSafetyMargin < 0 || capacity <= 0) {
        request.estimatedInputTokens = estimateInputTokens(request);
        request.assemblyError = QStringLiteral("上下文预算配置无效");
        request.budgetIssue = ChatBudgetIssue::ContextTooSmall;
        return request;
    }

    // System prompt and the question message are always sent, so they are paid
    // for before any optional content is considered.
    qint64 remaining = capacity - estimateInputTokens(request);
    if (remaining < 0) {
        request.estimatedInputTokens = estimateInputTokens(request);
        request.assemblyError = QStringLiteral("问题超出上下文预算");
        request.budgetIssue = ChatBudgetIssue::ContextTooSmall;
        return request;
    }

    QSet<QString> seen;
    int evidenceCharacters = 0;
    const int evidenceLimit = qMax(0, budget.maxEvidenceCharacters);
    for (const auto& item : evidence) {
        if (item.chunkId.isEmpty() || seen.contains(item.chunkId))
            continue;
        seen.insert(item.chunkId);
        if (item.text.isEmpty())
            continue;
        const QString block = evidenceBlock(item);
        const qint64 blockTokens = utf8Size(block);
        if (evidenceCharacters + block.size() > evidenceLimit ||
            blockTokens > remaining) {
            request.skippedEvidenceIds.append(item.chunkId);
            continue;
        }
        request.context += block;
        request.evidence.append(item);
        evidenceCharacters += block.size();
        remaining -= blockTokens;
    }

    // History is optional context and therefore lower priority than evidence.
    // Only a contiguous run of the most recent complete turns is kept; a turn
    // is never cut in half, so the model never sees a dangling question.
    const int pairs = conversation.size() / 2;
    const int turns = qMin(pairs, qMax(0, budget.maxHistoryTurns));
    QVector<QPair<QString, QString>> kept;
    for (int i = pairs - 1; i >= pairs - turns; --i) {
        const QString& user = conversation.at(2 * i);
        const QString& assistant = conversation.at(2 * i + 1);
        const qint64 cost =
            utf8Size(user) + utf8Size(assistant) + 2 * kMessageOverheadTokens;
        if (cost > remaining)
            break;
        kept.prepend({user, assistant});
        remaining -= cost;
    }
    for (const auto& turn : kept) {
        request.conversation.append(turn.first);
        request.conversation.append(turn.second);
    }
    request.droppedHistoryTurns = qMax(0, pairs - kept.size());

    request.estimatedInputTokens = estimateInputTokens(request);
    if (request.evidence.isEmpty() && !evidence.isEmpty()) {
        request.assemblyError =
            QStringLiteral("证据超出上下文预算，未能放入任何片段");
        request.budgetIssue = ChatBudgetIssue::EvidenceOverflow;
    }
    return request;
}

QString ContextAssembler::budgetError(const ChatRequest& request) {
    if (!request.assemblyError.isEmpty())
        return request.assemblyError;
    if (request.contextWindowTokens <= 0 || request.maxOutputTokens <= 0 ||
        request.tokenSafetyMargin < 0)
        return QStringLiteral("上下文预算配置无效");
    const qint64 total = estimateInputTokens(request) +
                         request.maxOutputTokens + request.tokenSafetyMargin;
    if (total > request.contextWindowTokens)
        return QStringLiteral("请求超出上下文预算");
    return {};
}

RagAnswer CitationValidator::validate(const QString& answer,
                                      const QVector<RagEvidence>& sentEvidence,
                                      const QString& documentHash) {
    QHash<QString, RagEvidence> allowed;
    for (const auto& item : sentEvidence) {
        if (item.documentHash == documentHash && !item.chunkId.isEmpty())
            allowed.insert(item.chunkId, item);
    }
    RagAnswer result;
    QHash<QString, int> numbers;
    const QRegularExpression pattern(
        QStringLiteral("\\[\\[cite:([^\\]\\r\\n]+)\\]\\]"));
    auto iterator = pattern.globalMatch(answer);
    int offset = 0;
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        const QString prefix =
            answer.mid(offset, match.capturedStart() - offset);
        result.text += prefix;
        result.plainText += prefix;

        const QString id = match.captured(1);
        const auto evidence = allowed.constFind(id);
        if (evidence != allowed.constEnd()) {
            int number = 0;
            const auto existing = numbers.constFind(id);
            if (existing != numbers.constEnd()) {
                number = existing.value();
            } else {
                number = result.citations.size() + 1;
                numbers.insert(id, number);
                RagCitation citation;
                citation.chunkId = id;
                citation.pageStart = evidence->pageStart;
                citation.pageEnd = evidence->pageEnd;
                citation.sectionPath = evidence->sectionPath;
                citation.snippet = citationSnippet(evidence->text);
                result.citations.append(citation);
            }
            result.text +=
                QStringLiteral("[%1](readium-cite:%2)").arg(number).arg(id);
        }
        offset = match.capturedEnd();
    }
    const QString tail = answer.mid(offset);
    result.text += tail;
    result.plainText += tail;
    result.text = result.text.trimmed();
    result.plainText = result.plainText.trimmed();
    return result;
}
}  // namespace Rag
