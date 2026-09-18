#include "RagDocumentCoordinator.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUuid>
#include <QtConcurrent>

#include <utility>

#include "model/DocumentModel.h"
#include "rag/embedding/OllamaEmbeddingProvider.h"
#include "rag/extract/PdfChunker.h"
#include "rag/extract/PdfTextExtractor.h"
#include "rag/index/RagIndexStore.h"
#include "rag/retrieve/HybridRetriever.h"

namespace Rag {
namespace {

struct IndexJobResult {
    RagJobContext context;
    RagIndexState state = RagIndexState::Failed;
    DocumentKey document;
    int chunkCount = 0;
    QString error;
};

struct QueryJobResult {
    quint64 generation = 0;
    QString documentPath;
    QString documentHash;
    QVector<RagEvidence> evidence;
    QString error;
};

bool isIndexingState(RagIndexState state) {
    return state == RagIndexState::CheckingCache ||
           state == RagIndexState::Extracting ||
           state == RagIndexState::Chunking || state == RagIndexState::Indexing;
}

QThreadPool& embeddingPool() {
    static QThreadPool pool;
    pool.setMaxThreadCount(1);
    return pool;
}

}  // namespace

RagDocumentCoordinator::RagDocumentCoordinator(DocumentModel* documentModel,
                                               QString databasePath,
                                               QObject* parent)
    : QObject(parent),
      m_documentModel(documentModel),
      m_databasePath(databasePath.isEmpty() ? defaultDatabasePath()
                                            : std::move(databasePath)) {
    if (!m_documentModel) {
        return;
    }

    connect(m_documentModel, &DocumentModel::documentOpened, this,
            [this](int index, const QString&) { onDocumentOpened(index); });
    connect(m_documentModel, &DocumentModel::documentClosed, this,
            &RagDocumentCoordinator::onDocumentClosed);
    connect(m_documentModel, &DocumentModel::currentDocumentChanged, this,
            &RagDocumentCoordinator::onCurrentDocumentChanged);
    connect(m_documentModel, &DocumentModel::allDocumentsClosed, this,
            &RagDocumentCoordinator::onAllDocumentsClosed);
}

RagDocumentCoordinator::~RagDocumentCoordinator() {
    m_queryCancellation.cancel();
    for (Entry& entry : m_entries) {
        entry.activeJob.cancellation.cancel();
    }
}

RagDocumentSnapshot RagDocumentCoordinator::currentSnapshot() const {
    return m_entries.value(m_currentPath).snapshot;
}

void RagDocumentCoordinator::queryCurrentDocument(const QString& query,
                                                  int limit) {
    invalidateQueries();
    const RagDocumentSnapshot snapshot = currentSnapshot();
    if (!snapshot.isReady()) {
        emit queryFailed(QStringLiteral("The current document is not indexed"));
        return;
    }

    const quint64 queryGeneration = ++m_queryGeneration;
    m_queryCancellation = {};
    const RagCancellationToken queryCancellation = m_queryCancellation;
    auto* watcher = new QFutureWatcher<QueryJobResult>(this);
    connect(watcher, &QFutureWatcher<QueryJobResult>::finished, this,
            [this, watcher]() {
                const QueryJobResult result = watcher->result();
                watcher->deleteLater();
                const RagDocumentSnapshot snapshot = currentSnapshot();
                if (result.generation != m_queryGeneration ||
                    snapshot.filePath != result.documentPath ||
                    snapshot.key.contentHash != result.documentHash) {
                    return;
                }
                if (!result.error.isEmpty()) {
                    emit queryFailed(result.error);
                    return;
                }
                emit evidenceReady(result.evidence);
            });

    const QString databasePath = m_databasePath;
    watcher->setFuture(
        QtConcurrent::run([databasePath, document = snapshot.key,
                           documentPath = snapshot.filePath, query, limit,
                           queryGeneration, queryCancellation]() {
            QueryJobResult result;
            result.generation = queryGeneration;
            result.documentPath = documentPath;
            result.documentHash = document.contentHash;
            const HybridRetriever retriever(databasePath);
            const QByteArray model = qgetenv("READIUM_OLLAMA_EMBED_MODEL");
            const QByteArray version = qgetenv("READIUM_OLLAMA_EMBED_VERSION");
            std::unique_ptr<OllamaEmbeddingProvider> provider;
            if (!model.isEmpty() && !version.isEmpty()) {
                provider = std::make_unique<OllamaEmbeddingProvider>(
                    QString::fromUtf8(model), QString::fromUtf8(version));
            }
            result.evidence =
                retriever.retrieve(document, query, limit, provider.get(),
                                   queryCancellation, &result.error);
            return result;
        }));
}

void RagDocumentCoordinator::rebuildCurrentDocument() {
    if (!m_currentPath.isEmpty()) {
        startIndexing(m_currentPath, true);
    }
}

void RagDocumentCoordinator::cancelCurrentIndexing() {
    auto it = m_entries.find(m_currentPath);
    if (it == m_entries.end() || !isIndexingState(it->snapshot.state)) {
        return;
    }
    it->activeJob.cancellation.cancel();
    invalidateQueries();
    ++it->snapshot.generation;
    it->snapshot.state = RagIndexState::Cancelled;
    it->snapshot.error.clear();
    publish(m_currentPath);
}

void RagDocumentCoordinator::onDocumentOpened(int index) {
    startIndexing(pathForIndex(index), false);
}

void RagDocumentCoordinator::onDocumentClosed(int index) {
    const QString path = normalizedPath(pathForIndex(index));
    auto it = m_entries.find(path);
    if (it != m_entries.end()) {
        it->activeJob.cancellation.cancel();
        m_entries.erase(it);
    }
    if (path == m_currentPath)
        invalidateQueries();
}

void RagDocumentCoordinator::onCurrentDocumentChanged(int index) {
    invalidateQueries();
    m_currentPath = normalizedPath(pathForIndex(index));
    if (m_currentPath.isEmpty()) {
        emit currentDocumentStateChanged({});
        return;
    }
    if (!m_entries.contains(m_currentPath)) {
        startIndexing(m_currentPath, false);
    } else {
        publish(m_currentPath);
    }
}

void RagDocumentCoordinator::onAllDocumentsClosed() {
    invalidateQueries();
    m_currentPath.clear();
    emit currentDocumentStateChanged({});
}

void RagDocumentCoordinator::startIndexing(const QString& filePath,
                                           bool forceRebuild) {
    const QString path = normalizedPath(filePath);
    if (path.isEmpty()) {
        return;
    }

    Entry& entry = m_entries[path];
    if (!forceRebuild &&
        (entry.snapshot.isReady() || isIndexingState(entry.snapshot.state))) {
        if (path == m_currentPath) {
            publish(path);
        }
        return;
    }

    entry.activeJob.cancellation.cancel();
    if (path == m_currentPath)
        invalidateQueries();
    ++entry.snapshot.generation;
    entry.snapshot.stageSequence = 0;
    entry.snapshot.filePath = path;
    entry.snapshot.key = {};
    entry.snapshot.key.canonicalPath = path;
    entry.snapshot.state = RagIndexState::Extracting;
    entry.snapshot.chunkCount = 0;
    entry.snapshot.error.clear();
    entry.snapshot.embeddingState = RagEmbeddingState::Disabled;
    entry.snapshot.embeddedChunkCount = 0;
    entry.snapshot.embeddingError.clear();
    entry.activeJob = {};
    entry.activeJob.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.activeJob.generation = entry.snapshot.generation;
    const RagJobContext context = entry.activeJob;
    publish(path);

    auto* watcher = new QFutureWatcher<IndexJobResult>(this);
    connect(watcher, &QFutureWatcher<IndexJobResult>::finished, this,
            [this, watcher, path]() {
                const IndexJobResult result = watcher->result();
                watcher->deleteLater();

                auto it = m_entries.find(path);
                if (it == m_entries.end() ||
                    it->activeJob.jobId != result.context.jobId ||
                    it->snapshot.generation != result.context.generation) {
                    return;
                }
                it->snapshot.key = result.document;
                it->snapshot.state = result.state;
                it->snapshot.stageSequence = 4;
                it->snapshot.chunkCount = result.chunkCount;
                it->snapshot.error = result.error;
                publish(path);
                if (result.state == RagIndexState::Ready) {
                    startEmbedding(path, result.document,
                                   result.context.generation,
                                   it->activeJob.cancellation);
                }
            });

    const QString databasePath = m_databasePath;
    const QPointer<RagDocumentCoordinator> guard(this);
    watcher->setFuture(QtConcurrent::run([guard, path, databasePath, context,
                                          forceRebuild]() {
        auto postStage = [guard, path, context](int stageSequence,
                                                RagIndexState state) {
            if (!guard) {
                return;
            }
            QMetaObject::invokeMethod(
                guard,
                [guard, path, context, stageSequence, state]() {
                    if (guard) {
                        guard->acceptStage(path, context.jobId,
                                           context.generation, stageSequence,
                                           state);
                    }
                },
                Qt::QueuedConnection);
        };

        IndexJobResult result;
        result.context = context;
        const PdfTextExtractionResult extraction =
            PdfTextExtractor::extractFile(path, context);
        result.document = extraction.document;
        if (context.isCancelled()) {
            result.state = RagIndexState::Cancelled;
            return result;
        }
        if (!extraction.success) {
            result.state = RagIndexState::Failed;
            result.error = extraction.error;
            return result;
        }
        if (!extraction.hasText) {
            result.state = RagIndexState::Unsupported;
            result.error = QStringLiteral("No extractable text was found");
            return result;
        }

        postStage(1, RagIndexState::CheckingCache);
        RagIndexStore store;
        if (!store.open(databasePath, &result.error)) {
            result.state = RagIndexState::Failed;
            return result;
        }
        if (!forceRebuild && store.containsDocument(result.document)) {
            result.chunkCount =
                store.documentChunkCount(result.document, &result.error);
            result.state = result.chunkCount >= 0 ? RagIndexState::Ready
                                                  : RagIndexState::Failed;
            return result;
        }

        postStage(2, RagIndexState::Chunking);
        const QVector<PdfChunk> chunks =
            PdfChunker::chunk(extraction.pages, result.document, context);
        if (context.isCancelled()) {
            result.state = RagIndexState::Cancelled;
            return result;
        }
        if (chunks.isEmpty()) {
            result.state = RagIndexState::Unsupported;
            result.error = QStringLiteral("No searchable chunks were produced");
            return result;
        }

        postStage(3, RagIndexState::Indexing);
        if (!store.replaceDocument(result.document, chunks, &result.error,
                                   &context.cancellation)) {
            result.state = context.isCancelled() ? RagIndexState::Cancelled
                                                 : RagIndexState::Failed;
            return result;
        }
        result.chunkCount = chunks.size();
        result.state = RagIndexState::Ready;
        return result;
    }));
}

void RagDocumentCoordinator::startEmbedding(
    const QString& path, const DocumentKey& document, quint64 generation,
    const RagCancellationToken& cancel) {
    const QByteArray model = qgetenv("READIUM_OLLAMA_EMBED_MODEL");
    const QByteArray version = qgetenv("READIUM_OLLAMA_EMBED_VERSION");
    if (model.isEmpty() || version.isEmpty()) {
        updateEmbeddingState(path, generation, RagEmbeddingState::Disabled, 0,
                             {});
        return;
    }

    const QString databasePath = m_databasePath;
    updateEmbeddingState(path, generation, RagEmbeddingState::Pending, 0, {});
    const QPointer<RagDocumentCoordinator> guard(this);
    embeddingPool().start([guard, path, generation, document, cancel, model,
                           version, databasePath]() {
        auto postState = [guard, path, generation](RagEmbeddingState state,
                                                   int embeddedCount,
                                                   const QString& error) {
            if (!guard)
                return;
            QMetaObject::invokeMethod(
                guard,
                [guard, path, generation, state, embeddedCount, error]() {
                    if (guard) {
                        guard->updateEmbeddingState(path, generation, state,
                                                    embeddedCount, error);
                    }
                },
                Qt::QueuedConnection);
        };

        postState(RagEmbeddingState::Running, 0, {});
        OllamaEmbeddingProvider provider(QString::fromUtf8(model),
                                         QString::fromUtf8(version));
        RagIndexStore store;
        QString error;
        if (!store.open(databasePath, &error)) {
            postState(RagEmbeddingState::Failed, 0, error);
            return;
        }

        constexpr int batchSize = 8;
        int embeddedCount = 0;
        while (!cancel.isCancelled()) {
            const auto batch = store.chunksMissingVectors(
                document, provider.modelId(), provider.modelVersion(),
                batchSize, &error);
            if (!error.isEmpty()) {
                postState(RagEmbeddingState::Failed, embeddedCount, error);
                return;
            }
            if (batch.isEmpty()) {
                postState(RagEmbeddingState::Ready, embeddedCount, {});
                return;
            }

            QStringList texts;
            for (const auto& chunk : batch)
                texts.append(chunk.text);
            const auto vectors = provider.embed(texts, cancel, &error);
            if (cancel.isCancelled()) {
                postState(RagEmbeddingState::Cancelled, embeddedCount,
                          QStringLiteral("Embedding cancelled"));
                return;
            }
            if (!error.isEmpty() || vectors.size() != batch.size()) {
                postState(RagEmbeddingState::Failed, embeddedCount,
                          error.isEmpty()
                              ? QStringLiteral("Invalid embedding response")
                              : error);
                return;
            }
            auto indexed = batch;
            for (int i = 0; i < indexed.size(); ++i)
                indexed[i].vector = vectors[i];
            if (!store.storeVectors(document, provider.modelId(),
                                    provider.modelVersion(), indexed, cancel,
                                    &error)) {
                postState(cancel.isCancelled() ? RagEmbeddingState::Cancelled
                                               : RagEmbeddingState::Failed,
                          embeddedCount, error);
                return;
            }
            embeddedCount += batch.size();
            postState(RagEmbeddingState::Running, embeddedCount, {});
        }
        postState(RagEmbeddingState::Cancelled, embeddedCount,
                  QStringLiteral("Embedding cancelled"));
    });
}

void RagDocumentCoordinator::updateEmbeddingState(const QString& path,
                                                  quint64 generation,
                                                  RagEmbeddingState state,
                                                  int embeddedCount,
                                                  const QString& error) {
    auto it = m_entries.find(path);
    if (it == m_entries.end() || it->snapshot.generation != generation) {
        return;
    }
    it->snapshot.embeddingState = state;
    it->snapshot.embeddedChunkCount = embeddedCount;
    it->snapshot.embeddingError = error;
    publish(path);
}

void RagDocumentCoordinator::acceptStage(const QString& filePath,
                                         const QString& jobId,
                                         quint64 generation, int stageSequence,
                                         RagIndexState state) {
    auto it = m_entries.find(filePath);
    if (it == m_entries.end() || it->activeJob.jobId != jobId ||
        it->snapshot.generation != generation ||
        stageSequence <= it->snapshot.stageSequence ||
        !isIndexingState(it->snapshot.state)) {
        return;
    }
    it->snapshot.stageSequence = stageSequence;
    it->snapshot.state = state;
    publish(filePath);
}

void RagDocumentCoordinator::publish(const QString& filePath) {
    const auto it = m_entries.constFind(filePath);
    if (it == m_entries.constEnd()) {
        return;
    }
    emit documentStateChanged(it->snapshot);
    if (filePath == m_currentPath) {
        emit currentDocumentStateChanged(it->snapshot);
    }
}

void RagDocumentCoordinator::invalidateQueries() {
    ++m_queryGeneration;
    m_queryCancellation.cancel();
    m_queryCancellation = {};
}

QString RagDocumentCoordinator::pathForIndex(int index) const {
    return m_documentModel ? m_documentModel->getDocumentFilePath(index)
                           : QString();
}

QString RagDocumentCoordinator::normalizedPath(const QString& filePath) {
    if (filePath.isEmpty()) {
        return {};
    }
    const QFileInfo info(filePath);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString RagDocumentCoordinator::defaultDatabasePath() {
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("rag-index.sqlite"));
}

}  // namespace Rag
