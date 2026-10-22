#pragma once

#include <QUrl>

#include "EmbeddingProvider.h"

namespace Rag {

class OllamaEmbeddingProvider : public EmbeddingProvider {
public:
    OllamaEmbeddingProvider(QString model, QString version,
                            int timeoutMs = 30000);
    OllamaEmbeddingProvider(QString model, QString version, QUrl endpoint,
                            int timeoutMs = 30000);
    QString modelId() const override { return m_model; }
    QString modelVersion() const override { return m_version; }
    QVector<QVector<float>> embed(const QStringList& texts,
                                  const RagCancellationToken& cancel,
                                  QString* error) override;

private:
    QString m_model;
    QString m_version;
    QUrl m_endpoint;
    int m_timeoutMs;
};

}  // namespace Rag
