#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "rag/domain/RagTypes.h"

namespace Rag {

class RagIndexStore {
public:
    RagIndexStore();
    ~RagIndexStore();

    RagIndexStore(const RagIndexStore&) = delete;
    RagIndexStore& operator=(const RagIndexStore&) = delete;

    bool open(const QString& databasePath, QString* error = nullptr);
    void close();
    bool isOpen() const;

    bool replaceDocument(const DocumentKey& document,
                         const QVector<PdfChunk>& chunks,
                         QString* error = nullptr);
    bool containsDocument(const DocumentKey& document) const;

    QVector<RagEvidence> search(const DocumentKey& document,
                                const QString& query, int limit = 10,
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
