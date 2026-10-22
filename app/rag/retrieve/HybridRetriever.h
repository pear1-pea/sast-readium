#pragma once

#include "rag/embedding/EmbeddingProvider.h"
#include "rag/index/RagIndexStore.h"

namespace Rag {

class HybridRetriever {
public:
    explicit HybridRetriever(QString databasePath);
    QVector<RagEvidence> retrieve(const DocumentKey& document,
                                  const QString& query, int limit,
                                  EmbeddingProvider* provider,
                                  const RagCancellationToken& cancel,
                                  QString* error = nullptr) const;
    QVector<RagEvidence> retrieve(RagIndexStore& store,
                                  const DocumentKey& document,
                                  const QString& query, int limit,
                                  EmbeddingProvider* provider,
                                  const RagCancellationToken& cancel,
                                  QString* error = nullptr) const;

private:
    QString m_databasePath;
};

}  // namespace Rag
