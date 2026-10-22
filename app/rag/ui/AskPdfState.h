#pragma once

#include <QString>

#include "rag/domain/RagTypes.h"

namespace Rag {

enum class AskPdfViewState {
    NoDocument,
    Indexing,
    IndexCancelled,
    IndexUnsupported,
    IndexFailed,
    ReadyIdle,
    Searching,
    NoEvidence,
    EvidenceOnly,
    SemanticUnavailable,
    QueryFailed,
    AnswerFailed
};

enum class AskPdfStateAction {
    None,
    OpenSettings,
    RebuildIndex,
    FocusQuery,
    RetryAnswer,
    RetryQuery,
    ClearIndex
};

struct AskPdfViewInput {
    bool hasDocument = false;
    RagIndexState indexState = RagIndexState::NotIndexed;
    RagIndexFailureReason failureReason = RagIndexFailureReason::None;
    RagEmbeddingState embeddingState = RagEmbeddingState::Disabled;
    bool busy = false;
    bool hasSearched = false;
    bool hasEvidence = false;
    bool answerEnabled = true;
    bool queryFailed = false;
    bool answerFailed = false;
    QString queryError;
    QString answerError;
    QString indexError;
};

struct AskPdfStateView {
    AskPdfViewState state = AskPdfViewState::ReadyIdle;
    bool visible = false;
    QString title;
    QString detail;
    QString primaryText;
    AskPdfStateAction primaryAction = AskPdfStateAction::None;
    QString secondaryText;
    AskPdfStateAction secondaryAction = AskPdfStateAction::None;
};

AskPdfViewState askPdfViewState(const AskPdfViewInput& input);
AskPdfStateView askPdfStateView(const AskPdfViewInput& input);

}  // namespace Rag
