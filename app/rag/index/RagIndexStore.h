#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "rag/domain/RagTypes.h"

namespace Rag {

class RagIndexStore {
public:
    struct VectorChunk {
        QString chunkId;
        QString chunkHash;
        QString text;
        QVector<float> vector;
    };
    RagIndexStore();
    ~RagIndexStore();

    RagIndexStore(const RagIndexStore&) = delete;
    RagIndexStore& operator=(const RagIndexStore&) = delete;

    bool open(const QString& databasePath, QString* error = nullptr);
    void close();
    bool isOpen() const;

    bool replaceDocument(const DocumentKey& document,
                         const QVector<PdfChunk>& chunks,
                         QString* error = nullptr,
                         const RagCancellationToken* cancellation = nullptr);
    bool containsDocument(const DocumentKey& document) const;
    int documentChunkCount(const DocumentKey& document,
                           QString* error = nullptr) const;

    QVector<RagEvidence> search(const DocumentKey& document,
                                const QString& query, int limit = 10,
                                QString* error = nullptr) const;
    QVector<VectorChunk> chunksMissingVectors(const DocumentKey& document,
                                              const QString& model,
                                              const QString& version, int limit,
                                              QString* error = nullptr) const;
    bool storeVectors(const DocumentKey& document, const QString& model,
                      const QString& version,
                      const QVector<VectorChunk>& chunks,
                      const RagCancellationToken& cancel,
                      QString* error = nullptr);
    QVector<VectorChunk> loadVectors(const DocumentKey& document,
                                     const QString& model,
                                     const QString& version,
                                     QString* error = nullptr) const;
    bool invalidateVectors(const DocumentKey& document, const QString& model,
                           const QString& version, QString* error = nullptr);
    QVector<RagEvidence> evidenceByIds(const DocumentKey& document,
                                       const QStringList& ids,
                                       QString* error = nullptr) const;

private:
    bool initializeSchema(QString* error);
    static QString ftsQueryFor(const QString& query);
    static QString locationsToJson(const QVector<ChunkPageLocation>& locations);
    static QVector<ChunkPageLocation> locationsFromJson(const QString& json);
    static void setError(QString* error, const QString& message);

    QSqlDatabase m_database;
    QString m_connectionName;
};

}  // namespace Rag
