#pragma once
#include "ChatProvider.h"
namespace Rag {
class ContextAssembler {
public:
    static ChatRequest assemble(const QString& question,
                                const QVector<RagEvidence>& evidence,
                                const QStringList& conversation,
                                int maxCharacters = 12000);
};
class CitationValidator {
public:
    static RagAnswer validate(const QString& answer,
                              const QVector<RagEvidence>& sentEvidence,
                              const QString& documentHash);
};
}  // namespace Rag
