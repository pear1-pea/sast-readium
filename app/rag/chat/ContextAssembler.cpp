#include "ContextAssembler.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
namespace Rag {
namespace {
QString citationSnippet(const QString& text) {
    QString snippet = text.simplified();
    constexpr int kMaxSnippetLength = 160;
    if (snippet.size() > kMaxSnippetLength) {
        snippet = snippet.left(kMaxSnippetLength - 3).trimmed() +
                  QStringLiteral("...");
    }
    return snippet;
}
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
        const QString header = QStringLiteral("\n[chunk_id=%1]\n[page=%2]\n")
                                   .arg(item.chunkId)
                                   .arg(item.pageStart + 1);
        const QString footer = QStringLiteral("\n[/chunk]\n");
        const int available = remaining - header.size() - footer.size();
        if (available <= 0)
            break;
        const QString text = item.text.left(available);
        if (text.isEmpty())
            continue;
        const QString block = header + text + footer;
        request.context += block;
        remaining -= block.size();
        request.evidence.append(item);
        seen.insert(item.chunkId);
    }
    return request;
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
    QSet<QString> cited;
    const QRegularExpression pattern(
        QStringLiteral("\\[\\[cite:([^\\]\\r\\n]+)\\]\\]"));
    auto iterator = pattern.globalMatch(answer);
    int offset = 0;
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        result.text += answer.mid(offset, match.capturedStart() - offset);
        const QString id = match.captured(1);
        const auto evidence = allowed.constFind(id);
        if (evidence != allowed.constEnd() && !cited.contains(id)) {
            RagCitation citation;
            citation.chunkId = id;
            citation.pageStart = evidence->pageStart;
            citation.pageEnd = evidence->pageEnd;
            citation.sectionPath = evidence->sectionPath;
            citation.snippet = citationSnippet(evidence->text);
            result.citations.append(citation);
            cited.insert(id);
        }
        offset = match.capturedEnd();
    }
    result.text += answer.mid(offset);
    result.text = result.text.trimmed();
    return result;
}
}  // namespace Rag
