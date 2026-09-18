#pragma once

#include "rag/domain/RagTypes.h"

namespace Rag {

struct PdfChunkerOptions {
    int targetCharacters = 800;
    int maxCharacters = 1200;
    int overlapCharacters = 120;
};

class PdfChunker {
public:
    static QVector<PdfChunk> chunk(const QVector<PdfPageText>& pages,
                                   const DocumentKey& document,
                                   const RagJobContext& context,
                                   const PdfChunkerOptions& options = {});
};

}  // namespace Rag
