#pragma once
#include <QJsonArray>
#include "ChatProvider.h"
namespace Rag {
/**
 * Token budget for one chat request.
 *
 * `contextWindowTokens` is the runtime window sent to the service as `num_ctx`,
 * not the model's advertised maximum. The estimator counts UTF-8 bytes plus a
 * fixed per-message overhead: a conservative upper bound for BPE tokenizers,
 * so a request that fits here fits the service window. The real token count is
 * verified after generation from the service's `prompt_eval_count`.
 */
struct ChatBudget {
    // 16K keeps several 1200-character Chinese chunks inside the byte-based
    // bound. The model's real token usage stays far below this because the
    // estimator over-counts; see ChatBudget's comment above.
    int contextWindowTokens = 16384;
    int maxOutputTokens = 1024;
    int tokenSafetyMargin = 256;
    int maxEvidenceCharacters = 12000;
    int maxHistoryTurns = 3;
};

class ContextAssembler {
public:
    static ChatRequest assemble(const QString& question,
                                const QVector<RagEvidence>& evidence,
                                const QStringList& conversation,
                                int maxCharacters = 12000);
    static ChatRequest assembleBudgeted(const QString& question,
                                        const QVector<RagEvidence>& evidence,
                                        const QStringList& conversation,
                                        const ChatBudget& budget = {});
    static QJsonArray messages(const ChatRequest& request);
    // UTF-8 byte estimate with message overhead, not a model tokenizer.
    static qint64 estimateInputTokens(const ChatRequest& request);
    static QString budgetError(const ChatRequest& request);
};
class CitationValidator {
public:
    static RagAnswer validate(const QString& answer,
                              const QVector<RagEvidence>& sentEvidence,
                              const QString& documentHash);
};
}  // namespace Rag
