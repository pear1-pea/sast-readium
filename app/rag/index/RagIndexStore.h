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
    /** As above, recording the chunking strategy that produced the chunks. */
    bool replaceDocument(const DocumentKey& document,
                         const QVector<PdfChunk>& chunks,
                         const QString& chunkerPolicy, QString* error = nullptr,
                         const RagCancellationToken* cancellation = nullptr);
    bool registerDocumentPath(const DocumentKey& document,
                              QString* error = nullptr);
    bool clearAll(QString* error = nullptr);
    bool containsDocument(const DocumentKey& document,
                          QString* error = nullptr) const;
    /** True when the stored index was built with exactly this policy. */
    bool documentMatchesChunkerPolicy(const DocumentKey& document,
                                      const QString& policy,
                                      QString* error = nullptr) const;
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
    /**
     * Up to `radius` chunks before and after `anchor` in reading order
     * (page, character offset), excluding the anchor itself.
     */
    QVector<RagEvidence> adjacentChunks(const DocumentKey& document,
                                        const RagEvidence& anchor, int radius,
                                        QString* error = nullptr) const;

private:
    bool initializeSchema(QString* error);
    bool createSchema(QString* error);
    bool migrateSchemaFromVersionZero(QString* error);
    bool ensureChunkerPolicyColumn(QString* error);
    bool deleteContentForHash(const QString& documentHash, QString* error);
    static QString ftsQueryFor(const QString& query);
    static QString locationsToJson(const QVector<ChunkPageLocation>& locations);
    static QVector<ChunkPageLocation> locationsFromJson(const QString& json);
    static void setError(QString* error, const QString& message);

    QSqlDatabase m_database;
    QString m_connectionName;
};

}  // namespace Rag
