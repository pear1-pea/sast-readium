#pragma once

#include "rag/domain/RagTypes.h"

namespace Rag {

struct PdfChunkerOptions {
    int targetCharacters = 800;
    int maxCharacters = 1200;
    int overlapCharacters = 120;
    /**
     * When a chunk starts inside the overlap region, move its start back to the
     * nearest sentence boundary (bounded lookback) so it does not begin with a
     * truncated clause.
     */
    bool sentenceAwareOverlap = false;
};

class PdfChunker {
public:
    static QVector<PdfChunk> chunk(const QVector<PdfPageText>& pages,
                                   const DocumentKey& document,
                                   const RagJobContext& context,
                                   const PdfChunkerOptions& options = {});

    /**
     * Stable identifier for a chunking strategy.
     *
     * Stored with each indexed document so changing target/max/overlap
     * invalidates old chunks instead of mixing two strategies in one index.
     * Bump the prefix when chunk boundaries change without an option change.
     */
    static QString policyId(const PdfChunkerOptions& options = {});
};

}  // namespace Rag
