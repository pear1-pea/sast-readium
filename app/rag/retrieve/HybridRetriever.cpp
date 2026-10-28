#include "HybridRetriever.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Rag {
namespace {

double cosine(const QVector<float>& left, const QVector<float>& right) {
    if (left.size() != right.size() || left.isEmpty())
        return -1.0;
    double dot = 0, a = 0, b = 0;
    for (int i = 0; i < left.size(); ++i) {
        dot += double(left[i]) * right[i];
        a += double(left[i]) * left[i];
        b += double(right[i]) * right[i];
    }
    return a > 0 && b > 0 ? dot / std::sqrt(a * b) : -1.0;
}

double coverage(const QString& text, const QStringList& terms) {
    if (terms.isEmpty())
        return 0;
    int matches = 0;
    for (const QString& term : terms) {
        if (text.contains(term, Qt::CaseInsensitive))
            ++matches;
    }
    return double(matches) / terms.size();
}

}  // namespace

HybridRetriever::HybridRetriever(QString databasePath)
    : m_databasePath(std::move(databasePath)) {}

QVector<RagEvidence> HybridRetriever::retrieve(
    const DocumentKey& document, const QString& query, int limit,
    EmbeddingProvider* provider, const RagCancellationToken& cancel,
    QString* error) const {
    if (error)
        error->clear();
    if (limit <= 0 || query.trimmed().isEmpty())
        return {};
    RagIndexStore store;
    if (!store.open(m_databasePath, error))
        return {};
    return retrieve(store, document, query, limit, provider, cancel, error);
}

QVector<RagEvidence> HybridRetriever::retrieve(
    RagIndexStore& store, const DocumentKey& document, const QString& query,
    int limit, EmbeddingProvider* provider, const RagCancellationToken& cancel,
    QString* error) const {
    if (error)
        error->clear();
    if (limit <= 0 || query.trimmed().isEmpty())
        return {};
    const int pool = qMax(10, qMin(200, limit * 4));
    QVector<RagEvidence> lexical = store.search(document, query, pool, error);
    if (error && !error->isEmpty())
        return {};
    if (cancel.isCancelled())
        return {};
    QVector<RagIndexStore::VectorChunk> vectors;
    if (provider) {
        QString vectorError;
        vectors = store.loadVectors(document, provider->modelId(),
                                    provider->modelVersion(), &vectorError);
    }
    QVector<QPair<QString, double>> ranked;
    if (!vectors.isEmpty() && provider) {
        QString embedError;
        const auto queryVectors = provider->embed({query}, cancel, &embedError);
        if (!queryVectors.isEmpty() && !cancel.isCancelled()) {
            for (const auto& chunk : vectors) {
                const double score = cosine(queryVectors.first(), chunk.vector);
                if (std::isfinite(score) && score > -1.0)
                    ranked.append({chunk.chunkId, score});
            }
            std::sort(ranked.begin(), ranked.end(),
                      [](const auto& a, const auto& b) {
                          return a.second > b.second;
                      });
            ranked.resize(qMin(pool, int(ranked.size())));
        }
    }
    QHash<QString, RagEvidence> candidates;
    for (int i = 0; i < lexical.size(); ++i) {
        RagEvidence item = lexical.at(i);
        item.finalScore = 1.0 / (60 + i + 1);
        candidates.insert(item.chunkId, std::move(item));
    }
    if (!ranked.isEmpty()) {
        QStringList ids;
        for (const auto& entry : ranked)
            ids.append(entry.first);
        const auto extras = store.evidenceByIds(document, ids, error);
        if (error && !error->isEmpty())
            return {};
        for (const RagEvidence& item : extras) {
            if (!candidates.contains(item.chunkId))
                candidates.insert(item.chunkId, item);
        }
        for (int i = 0; i < ranked.size(); ++i) {
            auto it = candidates.find(ranked[i].first);
            if (it == candidates.end())
                continue;
            it->vectorScore = ranked[i].second;
            it->finalScore += 1.0 / (60 + i + 1);
        }
    }
    QStringList terms = query.split(QRegularExpression(QStringLiteral("\\s+")),
                                    Qt::SkipEmptyParts);
    QVector<RagEvidence> results;
    results.reserve(candidates.size());
    for (auto it = candidates.cbegin(); it != candidates.cend(); ++it) {
        RagEvidence item = it.value();
        const double title = coverage(item.sectionPath, terms);
        const double text = coverage(item.text, terms);
        item.rerankScore = 0.002 * title + 0.001 * text -
                           0.0002 * qMax(0, item.text.size() - 1200) / 1200.0;
        item.finalScore += item.rerankScore;
        results.append(std::move(item));
    }
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        if (a.finalScore != b.finalScore)
            return a.finalScore > b.finalScore;
        return a.chunkId < b.chunkId;
    });
    QVector<RagEvidence> selected;
    QHash<int, int> pages;
    while (!results.isEmpty() && selected.size() < limit) {
        int best = 0;
        double score = -1e9;
        for (int i = 0; i < results.size(); ++i) {
            const double adjusted = results[i].finalScore -
                                    0.001 * pages.value(results[i].pageStart);
            if (adjusted > score) {
                score = adjusted;
                best = i;
            }
        }
        RagEvidence item = results.takeAt(best);
        item.finalScore = score;
        item.rank = selected.size();
        ++pages[item.pageStart];
        selected.append(std::move(item));
    }
    return selected;
}

}  // namespace Rag
