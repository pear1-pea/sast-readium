#include "LexicalRetriever.h"

#include <utility>

#include "rag/index/RagIndexStore.h"

namespace Rag {

LexicalRetriever::LexicalRetriever(QString databasePath)
    : m_databasePath(std::move(databasePath)) {}

QVector<RagEvidence> LexicalRetriever::retrieve(const DocumentKey& document,
                                                const QString& query, int limit,
                                                QString* error) const {
    RagIndexStore store;
    if (!store.open(m_databasePath, error)) {
        return {};
    }
    return store.search(document, query, limit, error);
}

}  // namespace Rag
