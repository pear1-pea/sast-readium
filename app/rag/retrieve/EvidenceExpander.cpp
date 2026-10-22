#include "EvidenceExpander.h"

#include <algorithm>
#include <climits>

#include "rag/index/RagIndexStore.h"

namespace Rag {
namespace {

struct PageSpan {
    int start = 0;
    int end = 0;
};

int firstCharacter(const RagEvidence& item) {
    return item.locations.isEmpty() ? 0 : item.locations.first().charStart;
}

PageSpan spanOn(const RagEvidence& item, int page) {
    PageSpan span;
    bool found = false;
    for (const auto& location : item.locations) {
        if (location.pageIndex != page)
            continue;
        if (!found) {
            span.start = location.charStart;
            span.end = location.charStart + location.charLength;
            found = true;
            continue;
        }
        span.start = qMin(span.start, location.charStart);
        span.end = qMax(span.end, location.charStart + location.charLength);
    }
    return span;
}

bool canMerge(const RagEvidence& previous, const RagEvidence& next) {
    if (previous.documentHash != next.documentHash)
        return false;
    if (next.pageStart < previous.pageEnd)
        return true;
    if (next.pageStart == previous.pageEnd) {
        return spanOn(next, next.pageStart).start <=
               spanOn(previous, previous.pageEnd).end;
    }
    // One page gap at most, and only for single-page neighbours.
    return next.pageStart == previous.pageEnd + 1 &&
           next.pageEnd == next.pageStart;
}

void addLocation(QVector<ChunkPageLocation>& locations,
                 const ChunkPageLocation& incoming) {
    for (auto& existing : locations) {
        if (existing.pageIndex != incoming.pageIndex)
            continue;
        const int existingEnd = existing.charStart + existing.charLength;
        const int incomingEnd = incoming.charStart + incoming.charLength;
        if (incoming.charStart > existingEnd ||
            incomingEnd < existing.charStart) {
            continue;
        }
        const int start = qMin(existing.charStart, incoming.charStart);
        const int end = qMax(existingEnd, incomingEnd);
        existing.charStart = start;
        existing.charLength = end - start;
        for (const auto& rect : incoming.boundingRects) {
            if (!existing.boundingRects.contains(rect))
                existing.boundingRects.append(rect);
        }
        return;
    }
    locations.append(incoming);
}

RagEvidence mergePair(const RagEvidence& previous, const RagEvidence& next) {
    RagEvidence merged = previous;
    int overlap = 0;
    if (next.pageStart == previous.pageEnd) {
        overlap = qMax(0, spanOn(previous, previous.pageEnd).end -
                              spanOn(next, next.pageStart).start);
    }
    QString text = next.text.mid(qMin(overlap, int(next.text.size())));
    if (!merged.text.isEmpty() && !text.isEmpty())
        merged.text += QStringLiteral("\n");
    merged.text += text;

    merged.pageStart = qMin(previous.pageStart, next.pageStart);
    merged.pageEnd = qMax(previous.pageEnd, next.pageEnd);
    if (merged.sectionPath.isEmpty())
        merged.sectionPath = next.sectionPath;
    for (const auto& location : next.locations)
        addLocation(merged.locations, location);

    merged.lexicalScore = qMax(previous.lexicalScore, next.lexicalScore);
    merged.vectorScore = qMax(previous.vectorScore, next.vectorScore);
    merged.rerankScore = qMax(previous.rerankScore, next.rerankScore);
    merged.finalScore = qMax(previous.finalScore, next.finalScore);
    if (previous.rank < 0 || (next.rank >= 0 && next.rank < previous.rank)) {
        merged.rank = next.rank;
        merged.chunkId = next.chunkId;
    }
    return merged;
}

void sortBySource(QVector<RagEvidence>& evidence) {
    std::stable_sort(evidence.begin(), evidence.end(),
                     [](const RagEvidence& a, const RagEvidence& b) {
                         if (a.pageStart != b.pageStart)
                             return a.pageStart < b.pageStart;
                         if (firstCharacter(a) != firstCharacter(b))
                             return firstCharacter(a) < firstCharacter(b);
                         return a.chunkId < b.chunkId;
                     });
}

}  // namespace

QVector<RagEvidence> EvidenceExpander::mergeRanges(
    const QVector<RagEvidence>& evidence) {
    QVector<RagEvidence> sorted = evidence;
    sortBySource(sorted);
    QVector<RagEvidence> merged;
    merged.reserve(sorted.size());
    for (const RagEvidence& item : sorted) {
        if (!merged.isEmpty() && canMerge(merged.last(), item)) {
            merged.last() = mergePair(merged.last(), item);
            continue;
        }
        merged.append(item);
    }
    return merged;
}

QVector<RagEvidence> EvidenceExpander::expand(
    const QVector<RagEvidence>& evidence, const RagIndexStore& store,
    const DocumentKey& document, const EvidenceExpansion& options,
    QString* error) {
    if (error)
        error->clear();
    QVector<RagEvidence> expanded;
    expanded.reserve(evidence.size());
    for (const RagEvidence& anchor : evidence) {
        if (anchor.chunkId.isEmpty() ||
            anchor.documentHash != document.contentHash) {
            expanded.append(anchor);
            continue;
        }
        QVector<RagEvidence> group;
        group.append(anchor);
        int characters = anchor.text.size();
        const auto neighbours = store.adjacentChunks(
            document, anchor, options.neighborRadius, error);
        if (error && !error->isEmpty())
            return {};
        for (const RagEvidence& neighbour : neighbours) {
            if (!options.allowAdjacentPage &&
                neighbour.pageStart != anchor.pageStart)
                continue;
            const int span = qMax(anchor.pageEnd, neighbour.pageEnd) -
                             qMin(anchor.pageStart, neighbour.pageStart);
            if (span > 1)
                continue;
            if (characters + neighbour.text.size() > options.maxCharacters)
                continue;
            characters += neighbour.text.size();
            group.append(neighbour);
        }
        // Merge the group into one item; an unmergeable neighbour (same page
        // but a real gap) stays a separate block instead of being glued.
        sortBySource(group);
        QVector<RagEvidence> merged = mergeRanges(group);
        expanded += merged;
    }
    QVector<RagEvidence> result = mergeRanges(expanded);
    std::stable_sort(result.begin(), result.end(),
                     [](const RagEvidence& a, const RagEvidence& b) {
                         const int rankA = a.rank < 0 ? INT_MAX : a.rank;
                         const int rankB = b.rank < 0 ? INT_MAX : b.rank;
                         if (rankA != rankB)
                             return rankA < rankB;
                         return a.finalScore > b.finalScore;
                     });
    return result;
}

}  // namespace Rag
