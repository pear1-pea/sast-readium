#pragma once

#include <QStringList>
#include <QVector>

#include "rag/domain/RagTypes.h"

namespace Rag {

class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;
    virtual QString modelId() const = 0;
    virtual QString modelVersion() const = 0;
    virtual QVector<QVector<float>> embed(const QStringList& texts,
                                          const RagCancellationToken& cancel,
                                          QString* error) = 0;
};

}  // namespace Rag
