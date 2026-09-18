#pragma once

#include "EmbeddingProvider.h"

namespace Rag {

class OllamaEmbeddingProvider : public EmbeddingProvider {
public:
    OllamaEmbeddingProvider(QString model, QString version);
    QString modelId() const override { return m_model; }
    QString modelVersion() const override { return m_version; }
    QVector<QVector<float>> embed(const QStringList& texts,
                                  const RagCancellationToken& cancel,
                                  QString* error) override;

private:
    QString m_model;
    QString m_version;
};

}  // namespace Rag
