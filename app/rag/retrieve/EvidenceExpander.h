#pragma once

#include "rag/domain/RagTypes.h"

namespace Rag {

class RagIndexStore;

/** Limits for adding surrounding context to a retrieved chunk. */
struct EvidenceExpansion {
    int neighborRadius = 1;
    /** Cap on the merged text of one expanded evidence item, in QString chars.
     */
    int maxCharacters = 2400;
    bool allowAdjacentPage = true;
};

/**
 * Neighbour expansion and source-range merging for retrieved evidence.
 *
 * Retrieval still ranks small chunks. Expansion adds the chunks immediately
 * before and after a hit in reading order, then overlapping or touching source
 * ranges are merged so one condition or step is not split across duplicate
 * blocks. Merged text keeps the original chunk id of the highest-ranked
 * member, so citations and page highlights still resolve.
 */
class EvidenceExpander {
public:
    static QVector<RagEvidence> expand(const QVector<RagEvidence>& evidence,
                                       const RagIndexStore& store,
                                       const DocumentKey& document,
                                       const EvidenceExpansion& options = {},
                                       QString* error = nullptr);

    /** Merge evidence whose source ranges overlap or touch. */
    static QVector<RagEvidence> mergeRanges(
        const QVector<RagEvidence>& evidence);
};

}  // namespace Rag
