#pragma once

#include <QString>

#include "rag/domain/RagTypes.h"

namespace Rag {

struct AskPdfProgressInput {
    RagIndexState indexState = RagIndexState::NotIndexed;
    RagEmbeddingState embeddingState = RagEmbeddingState::Disabled;
    int processedPages = 0;
    int totalPages = 0;
    int chunkCount = 0;
    int embeddedChunkCount = 0;
    bool embeddingsEnabled = false;
};

struct AskPdfProgressView {
    bool visible = false;
    bool indeterminate = false;
    int percent = 0;
    QString text;
};

AskPdfProgressView askPdfProgressView(const AskPdfProgressInput& input);

}  // namespace Rag
