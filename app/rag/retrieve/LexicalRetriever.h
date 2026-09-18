#pragma once

#include <QString>
#include <QVector>

#include "rag/domain/RagTypes.h"

namespace Rag {

class LexicalRetriever {
public:
    explicit LexicalRetriever(QString databasePath);

    QVector<RagEvidence> retrieve(const DocumentKey& document,
                                  const QString& query, int limit = 10,
                                  QString* error = nullptr) const;

private:
    QString m_databasePath;
};

}  // namespace Rag
