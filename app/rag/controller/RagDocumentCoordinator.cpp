#include "RagDocumentCoordinator.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUuid>
#include <QtConcurrent>

#include <atomic>
#include <utility>

#include "model/DocumentModel.h"
#include "rag/chat/ContextAssembler.h"
#include "rag/chat/OllamaChatProvider.h"
#include "rag/embedding/OllamaEmbeddingProvider.h"
#include "rag/extract/PdfChunker.h"
#include "rag/extract/PdfTextExtractor.h"
#include "rag/index/RagIndexStore.h"
#include "rag/retrieve/EvidenceExpander.h"
#include "rag/retrieve/HybridRetriever.h"
#include "utils/LoggingMacros.h"

namespace Rag {
namespace {

struct IndexJobResult {
    RagJobContext context;
    RagIndexState state = RagIndexState::Failed;
    RagIndexFailureReason reason = RagIndexFailureReason::None;
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
    bool indexMissing = false;
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

RagDocumentCoordinator::RagDocumentCoordinator(
    DocumentModel* documentModel, QString databasePath, QObject* parent,
    ChatProviderFactory chatProviderFactory,
    EmbeddingProviderFactory embeddingProviderFactory, RagSettings settings)
    : QObject(parent),
      m_documentModel(documentModel),
      m_databasePath(databasePath.isEmpty() ? defaultDatabasePath()
                                            : std::move(databasePath)),
      m_chatProviderFactory(std::move(chatProviderFactory)),
      m_embeddingProviderFactory(std::move(embeddingProviderFactory)),
      m_settings(settings.normalized()) {
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
    if (m_chatTask)
        m_chatTask->cancel();
    m_queryCancellation.cancel();
    for (Entry& entry : m_entries) {
        entry.activeJob.cancellation.cancel();
        entry.embeddingCancellation.cancel();
    }
}

RagDocumentSnapshot RagDocumentCoordinator::currentSnapshot() const {
    return m_entries.value(m_currentPath).snapshot;
}

void RagDocumentCoordinator::queryCurrentDocument(const QString& query,
                                                  int limit) {
    invalidateQueries();
    m_lastQuery = query;
    m_lastEvidence.clear();
    const RagDocumentSnapshot snapshot = currentSnapshot();
    if (!snapshot.isReady()) {
        emit answerBusyChanged(false);
        emit queryFailed(QStringLiteral("The current document is not indexed"));
        return;
    }

    const quint64 queryGeneration = ++m_queryGeneration;
    emit answerBusyChanged(true);
    emit evidenceReady({});
    m_queryCancellation = {};
    const RagCancellationToken queryCancellation = m_queryCancellation;
    auto* watcher = new QFutureWatcher<QueryJobResult>(this);
    connect(watcher, &QFutureWatcher<QueryJobResult>::finished, this,
            [this, watcher, query]() {
                const QueryJobResult result = watcher->result();
                watcher->deleteLater();
                const RagDocumentSnapshot snapshot = currentSnapshot();
                if (result.generation != m_queryGeneration ||
                    snapshot.filePath != result.documentPath ||
                    snapshot.key.contentHash != result.documentHash) {
                    return;
                }
                if (result.indexMissing) {
                    m_lastEvidence.clear();
                    // Start the rebuild first: startIndexing() emits
                    // answerReset(), so the panel message must be emitted
                    // afterwards or it would be cleared immediately.
                    startIndexing(result.documentPath, true);
                    emit answerBusyChanged(false);
                    emit queryFailed(QStringLiteral("索引已丢失，正在重建"));
                    return;
                }
                if (!result.error.isEmpty()) {
                    m_lastEvidence.clear();
                    emit answerBusyChanged(false);
                    emit queryFailed(result.error);
                    return;
                }
                m_lastEvidence = result.evidence;
                emit evidenceReady(result.evidence);
                if (result.evidence.isEmpty()) {
                    // No evidence means there is nothing to answer from.
                    emit answerBusyChanged(false);
                    return;
                }
                generateAnswer(query, result.evidence);
            });

    const QString databasePath = m_databasePath;
    const RagSettings settings = m_settings;
    const bool embeddingsReady =
        snapshot.embeddingState == RagEmbeddingState::Ready &&
        settings.embeddingsEnabled() &&
        snapshot.embeddingIdentity == settings.embeddingIdentity();
    const EmbeddingProviderFactory embeddingProviderFactory =
        m_embeddingProviderFactory;
    watcher->setFuture(
        QtConcurrent::run([databasePath, document = snapshot.key,
                           documentPath = snapshot.filePath, query, limit,
                           queryGeneration, queryCancellation, embeddingsReady,
                           embeddingProviderFactory, settings]() {
            QueryJobResult result;
            result.generation = queryGeneration;
            result.documentPath = documentPath;
            result.documentHash = document.contentHash;

            RagIndexStore store;
            if (!store.open(databasePath, &result.error)) {
                return result;
            }
            QString checkError;
            const bool exists = store.containsDocument(document, &checkError);
            const int chunkCount =
                exists ? store.documentChunkCount(document, &checkError) : 0;
            if (!checkError.isEmpty()) {
                result.error = checkError;
                return result;
            }
            if (!exists || chunkCount == 0) {
                result.indexMissing = true;
                return result;
            }

            const HybridRetriever retriever(databasePath);
            constexpr int kQueryEmbeddingTimeoutMs = 4000;
            std::unique_ptr<EmbeddingProvider> provider;
            if (embeddingsReady) {
                if (embeddingProviderFactory) {
                    provider = embeddingProviderFactory(
                        settings.embedModel, settings.embedVersion,
                        settings.endpoint, kQueryEmbeddingTimeoutMs);
                } else {
                    provider = std::make_unique<OllamaEmbeddingProvider>(
                        settings.embedModel, settings.embedVersion,
                        QUrl(settings.embedUrl()), kQueryEmbeddingTimeoutMs);
                }
            }
            result.evidence = retriever.retrieve(
                store, document, query, limit, provider.get(),
                queryCancellation, &result.error);
            if (!result.error.isEmpty()) {
                return result;
            }
            result.evidence = EvidenceExpander::expand(
                result.evidence, store, document, {}, &result.error);
            if (!result.error.isEmpty()) {
                return result;
            }
            if (result.evidence.size() > limit) {
                result.evidence.resize(limit);
            }
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
    it->embeddingCancellation.cancel();
    it->embeddingCancellation = {};
    ++it->embeddingGeneration;
    it->snapshot.embeddingState = RagEmbeddingState::Disabled;
    it->snapshot.embeddingIdentity = {};
    it->snapshot.embeddedChunkCount = 0;
    it->snapshot.embeddingError.clear();
    it->snapshot.processedPages = 0;
    it->snapshot.totalPages = 0;
    invalidateQueries();
    ++it->snapshot.generation;
    it->snapshot.state = RagIndexState::Cancelled;
    it->snapshot.failureReason = RagIndexFailureReason::None;
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
        it->embeddingCancellation.cancel();
        m_entries.erase(it);
    }
    if (path == m_currentPath) {
        invalidateQueries();
        clearRetryState();
    }
}

void RagDocumentCoordinator::onCurrentDocumentChanged(int index) {
    invalidateQueries();
    clearRetryState();
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
    clearRetryState();
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
    entry.embeddingCancellation.cancel();
    entry.embeddingCancellation = {};
    ++entry.embeddingGeneration;
    if (path == m_currentPath)
        invalidateQueries();
    ++entry.snapshot.generation;
    entry.snapshot.stageSequence = 0;
    entry.snapshot.processedPages = 0;
    entry.snapshot.totalPages = 0;
    entry.lastProgressPublishMs = 0;
    entry.snapshot.filePath = path;
    entry.snapshot.key = {};
    entry.snapshot.key.canonicalPath = path;
    entry.snapshot.state = RagIndexState::Extracting;
    entry.snapshot.failureReason = RagIndexFailureReason::None;
    entry.snapshot.chunkCount = 0;
    entry.snapshot.error.clear();
    entry.conversation.clear();
    entry.snapshot.embeddingState = RagEmbeddingState::Disabled;
    entry.snapshot.embeddingIdentity = {};
    entry.snapshot.embeddedChunkCount = 0;
    entry.snapshot.embeddingError.clear();
    m_lastEvidence.clear();
    entry.activeJob = {};
    entry.activeJob.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.activeJob.generation = entry.snapshot.generation;
    {
        const QPointer<RagDocumentCoordinator> progressGuard(this);
        const QString jobId = entry.activeJob.jobId;
        const quint64 jobGeneration = entry.activeJob.generation;
        auto lastPost = std::make_shared<std::atomic<qint64>>(0);
        entry.activeJob.onProgress = [progressGuard, path, jobId, jobGeneration,
                                      lastPost](int processed, int total) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const bool final = total > 0 && processed >= total;
            if (!final) {
                const qint64 previous = lastPost->exchange(now);
                if (now - previous < 100) {
                    return;
                }
            } else {
                lastPost->store(now);
            }
            if (!progressGuard) {
                return;
            }
            QMetaObject::invokeMethod(
                progressGuard,
                [progressGuard, path, jobId, jobGeneration, processed,
                 total]() {
                    if (progressGuard) {
                        progressGuard->acceptProgress(
                            path, jobId, jobGeneration, processed, total);
                    }
                },
                Qt::QueuedConnection);
        };
    }
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
                if (!result.document.canonicalPath.isEmpty()) {
                    it->snapshot.key = result.document;
                }
                it->snapshot.state = result.state;
                it->snapshot.failureReason = result.reason;
                it->snapshot.stageSequence = 4;
                it->snapshot.chunkCount = result.chunkCount;
                it->snapshot.error = result.error;
                publish(path);
                if (result.state == RagIndexState::Ready) {
                    startEmbedding(path, result.document);
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
            result.reason = extraction.reason;
            result.error = extraction.error;
            return result;
        }
        if (!extraction.hasText) {
            result.state = RagIndexState::Unsupported;
            result.reason = RagIndexFailureReason::ScannedImage;
            result.error = QStringLiteral("No extractable text was found");
            return result;
        }

        postStage(1, RagIndexState::CheckingCache);
        RagIndexStore store;
        if (!store.open(databasePath, &result.error)) {
            result.state = RagIndexState::Failed;
            result.reason = RagIndexFailureReason::Database;
            return result;
        }
        QString checkError;
        const bool exists =
            store.containsDocument(result.document, &checkError);
        if (!checkError.isEmpty()) {
            result.state = RagIndexState::Failed;
            result.reason = RagIndexFailureReason::Database;
            result.error = checkError;
            return result;
        }
        const QString chunkerPolicy = PdfChunker::policyId();
        const bool policyMatches =
            exists && store.documentMatchesChunkerPolicy(
                          result.document, chunkerPolicy, &checkError);
        if (!checkError.isEmpty()) {
            result.state = RagIndexState::Failed;
            result.reason = RagIndexFailureReason::Database;
            result.error = checkError;
            return result;
        }
        if (!forceRebuild && policyMatches) {
            // The same content may be open through several paths. Register the
            // current path so reindexing one file does not evict the shared
            // content for another tab.
            if (!store.registerDocumentPath(result.document, &result.error)) {
                result.state = RagIndexState::Failed;
                result.reason = RagIndexFailureReason::Database;
                return result;
            }
            result.chunkCount =
                store.documentChunkCount(result.document, &result.error);
            result.state = result.chunkCount > 0 ? RagIndexState::Ready
                                                 : RagIndexState::Failed;
            if (result.state == RagIndexState::Failed) {
                result.reason = RagIndexFailureReason::Database;
                if (result.error.isEmpty()) {
                    result.error = QStringLiteral("Index contains no chunks");
                }
            }
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
            result.reason = RagIndexFailureReason::Unknown;
            result.error = QStringLiteral("No searchable chunks were produced");
            return result;
        }

        postStage(3, RagIndexState::Indexing);
        if (!store.replaceDocument(result.document, chunks, chunkerPolicy,
                                   &result.error, &context.cancellation)) {
            result.state = context.isCancelled() ? RagIndexState::Cancelled
                                                 : RagIndexState::Failed;
            if (!context.isCancelled()) {
                result.reason = RagIndexFailureReason::Database;
            }
            return result;
        }
        result.chunkCount = chunks.size();
        result.state = RagIndexState::Ready;
        return result;
    }));
}

void RagDocumentCoordinator::startEmbedding(const QString& path,
                                            const DocumentKey& document) {
    auto it = m_entries.find(path);
    if (it == m_entries.end()) {
        return;
    }
    Entry& entry = *it;

    entry.embeddingCancellation.cancel();
    entry.embeddingCancellation = {};
    const quint64 generation = ++entry.embeddingGeneration;

    const RagEmbeddingIdentity identity = m_settings.embeddingIdentity();
    if (!m_settings.embeddingsEnabled()) {
        entry.snapshot.embeddingState = RagEmbeddingState::Disabled;
        entry.snapshot.embeddingIdentity = {};
        entry.snapshot.embeddedChunkCount = 0;
        entry.snapshot.embeddingError.clear();
        publish(path);
        return;
    }

    // The identity is published together with the state so "Ready" always
    // describes a concrete (model, version) pair.
    entry.snapshot.embeddingIdentity = identity;
    entry.snapshot.embeddingState = RagEmbeddingState::Pending;
    entry.snapshot.embeddedChunkCount = 0;
    entry.snapshot.embeddingError.clear();
    publish(path);

    const QString databasePath = m_databasePath;
    const RagCancellationToken cancel = entry.embeddingCancellation;
    const QString endpoint = m_settings.endpoint;
    const EmbeddingProviderFactory factory = m_embeddingProviderFactory;
    const QPointer<RagDocumentCoordinator> guard(this);
    embeddingPool().start([guard, path, generation, document, cancel, identity,
                           endpoint, databasePath, factory]() {
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
        std::unique_ptr<EmbeddingProvider> provider;
        constexpr int kEmbeddingTimeoutMs = 60000;
        if (factory) {
            provider = factory(identity.model, identity.version, endpoint,
                               kEmbeddingTimeoutMs);
        } else {
            provider = std::make_unique<OllamaEmbeddingProvider>(
                identity.model, identity.version,
                QUrl(endpoint + QStringLiteral("/api/embed")),
                kEmbeddingTimeoutMs);
        }
        if (!provider) {
            postState(RagEmbeddingState::Failed, 0,
                      QStringLiteral("Embedding provider unavailable"));
            return;
        }

        RagIndexStore store;
        QString error;
        if (!store.open(databasePath, &error)) {
            postState(RagEmbeddingState::Failed, 0, error);
            return;
        }

        int embeddedCount = store.vectorCount(document, provider->modelId(),
                                              provider->modelVersion(), &error);
        if (!error.isEmpty()) {
            postState(RagEmbeddingState::Failed, 0, error);
            return;
        }
        postState(RagEmbeddingState::Running, embeddedCount, {});

        constexpr int batchSize = 8;
        QElapsedTimer progressTimer;
        progressTimer.start();
        qint64 lastProgressMs = 0;
        while (!cancel.isCancelled()) {
            const auto batch = store.chunksMissingVectors(
                document, provider->modelId(), provider->modelVersion(),
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
            const auto vectors = provider->embed(texts, cancel, &error);
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
            if (!store.storeVectors(document, provider->modelId(),
                                    provider->modelVersion(), indexed, cancel,
                                    &error)) {
                postState(cancel.isCancelled() ? RagEmbeddingState::Cancelled
                                               : RagEmbeddingState::Failed,
                          embeddedCount, error);
                return;
            }
            embeddedCount += batch.size();
            const qint64 now = progressTimer.elapsed();
            if (now - lastProgressMs >= 100) {
                lastProgressMs = now;
                postState(RagEmbeddingState::Running, embeddedCount, {});
            }
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
    if (it == m_entries.end() || it->embeddingGeneration != generation) {
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
    if (state == RagIndexState::Chunking || state == RagIndexState::Indexing) {
        // Page progress belongs to the previous phase.
        it->snapshot.processedPages = 0;
    }
    publish(filePath);
}

void RagDocumentCoordinator::acceptProgress(const QString& filePath,
                                            const QString& jobId,
                                            quint64 generation, int processed,
                                            int total) {
    auto it = m_entries.find(filePath);
    if (it == m_entries.end() || it->activeJob.jobId != jobId ||
        it->snapshot.generation != generation ||
        !isIndexingState(it->snapshot.state)) {
        return;
    }
    if (total > 0) {
        it->snapshot.totalPages = total;
    }
    if (processed >= it->snapshot.processedPages) {
        it->snapshot.processedPages = processed;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool final = total > 0 && processed >= total;
    if (!final && now - it->lastProgressPublishMs < 100) {
        return;
    }
    it->lastProgressPublishMs = now;
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

void RagDocumentCoordinator::abortInFlight() {
    ++m_queryGeneration;
    m_queryCancellation.cancel();
    m_queryCancellation = {};
    cancelActiveChat();
    emit answerBusyChanged(false);
}

void RagDocumentCoordinator::cancelActiveChat() {
    ++m_chatGeneration;
    if (m_chatTask) {
        m_chatTask->cancel();
        m_chatTask->deleteLater();
        m_chatTask = nullptr;
        emit answerBusyChanged(false);
    }
}

void RagDocumentCoordinator::invalidateQueries() {
    abortInFlight();
    m_validCitations.clear();
    emit answerReset();
}

void RagDocumentCoordinator::cancelAnswer() { invalidateQueries(); }

void RagDocumentCoordinator::retryAnswer() {
    if (!m_settings.answerEnabled || m_lastEvidence.isEmpty() ||
        m_lastQuery.trimmed().isEmpty()) {
        return;
    }
    cancelActiveChat();
    emit answerReset();
    emit answerBusyChanged(true);
    generateAnswer(m_lastQuery, m_lastEvidence);
}

void RagDocumentCoordinator::retryQuery() {
    if (m_lastQuery.trimmed().isEmpty()) {
        return;
    }
    queryCurrentDocument(m_lastQuery);
}

void RagDocumentCoordinator::clearRetryState() {
    m_lastQuery.clear();
    m_lastEvidence.clear();
}

void RagDocumentCoordinator::applySettings(const RagSettings& settings) {
    const RagSettings next = settings.normalized();
    if (next == m_settings) {
        return;
    }
    const RagSettings previous = m_settings;
    const bool embeddingChanged =
        next.embeddingIdentity() != previous.embeddingIdentity();
    const bool endpointChanged = next.endpoint != previous.endpoint;
    const bool chatModelChanged = next.chatModel != previous.chatModel;
    const bool answerToggled = next.answerEnabled != previous.answerEnabled;
    m_settings = next;

    // Cancellation is scoped to what the new configuration invalidates:
    // - embedding/endpoint changes invalidate the running query and answer;
    // - chat-only changes cancel the active generation but let an evidence
    //   query finish and use the new chat settings when it generates.
    if (embeddingChanged || endpointChanged) {
        abortInFlight();
    } else if (chatModelChanged || answerToggled) {
        cancelActiveChat();
    }

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry& entry = *it;
        if (chatModelChanged) {
            entry.conversation.clear();
        }
        const bool embeddingWasActive =
            entry.snapshot.embeddingState == RagEmbeddingState::Pending ||
            entry.snapshot.embeddingState == RagEmbeddingState::Running;
        if (embeddingChanged || endpointChanged) {
            entry.embeddingCancellation.cancel();
            entry.embeddingCancellation = {};
            ++entry.embeddingGeneration;
        }
        if (embeddingChanged) {
            entry.snapshot.embeddingIdentity = {};
            entry.snapshot.embeddingState = RagEmbeddingState::Disabled;
            entry.snapshot.embeddedChunkCount = 0;
            entry.snapshot.embeddingError.clear();
        } else if (endpointChanged && embeddingWasActive) {
            entry.snapshot.embeddingState = RagEmbeddingState::Pending;
            entry.snapshot.embeddedChunkCount = 0;
        }

        const bool retryEmbedding =
            embeddingChanged ||
            (endpointChanged &&
             (embeddingWasActive ||
              entry.snapshot.embeddingState == RagEmbeddingState::Failed));
        if (entry.snapshot.isReady() && retryEmbedding) {
            // Indexing uses its own generation; only the embedding task is
            // restarted. A document that is still extracting picks up the new
            // settings when its index job finishes.
            startEmbedding(it.key(), entry.snapshot.key);
        } else {
            publish(it.key());
        }
    }
}

void RagDocumentCoordinator::deleteCurrentDocumentIndex() {
    if (m_deletingCurrent || m_currentPath.isEmpty()) {
        return;
    }
    auto it = m_entries.find(m_currentPath);
    if (it == m_entries.end()) {
        return;
    }
    it->activeJob.cancellation.cancel();
    it->embeddingCancellation.cancel();
    it->embeddingCancellation = {};
    ++it->embeddingGeneration;
    ++it->snapshot.generation;
    invalidateQueries();
    clearRetryState();
    it->conversation.clear();
    it->snapshot.state = RagIndexState::NotIndexed;
    it->snapshot.failureReason = RagIndexFailureReason::None;
    it->snapshot.processedPages = 0;
    it->snapshot.totalPages = 0;
    it->snapshot.chunkCount = 0;
    it->snapshot.error.clear();
    it->snapshot.embeddingState = RagEmbeddingState::Disabled;
    it->snapshot.embeddingIdentity = {};
    it->snapshot.embeddedChunkCount = 0;
    it->snapshot.embeddingError.clear();
    it->snapshot.key.contentHash.clear();
    it->snapshot.key.pageCount = 0;
    publish(m_currentPath);

    m_deletingCurrent = true;
    const QString path = m_currentPath;
    const QString databasePath = m_databasePath;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, path]() {
                const QString error = watcher->result();
                watcher->deleteLater();
                m_deletingCurrent = false;
                if (!error.isEmpty()) {
                    LOG_WARNING("RAG: cannot delete document index: {}",
                                error.toStdString());
                    auto entry = m_entries.find(path);
                    if (entry != m_entries.end()) {
                        entry->snapshot.state = RagIndexState::Failed;
                        entry->snapshot.failureReason =
                            RagIndexFailureReason::Database;
                        entry->snapshot.error = error;
                        publish(path);
                    }
                    emit documentIndexDeleteFailed(error);
                    return;
                }
                emit documentIndexDeleted();
            });

    watcher->setFuture(QtConcurrent::run([databasePath, path]() {
        RagIndexStore store;
        QString error;
        if (!store.open(databasePath, &error)) {
            return error;
        }
        if (!store.removeDocumentPath(path, &error)) {
            return error;
        }
        return QString();
    }));
}

void RagDocumentCoordinator::requestIndexStats() {
    const QString databasePath = m_databasePath;
    const DocumentKey key = currentSnapshot().key;
    auto* watcher = new QFutureWatcher<RagIndexStats>(this);
    connect(watcher, &QFutureWatcher<RagIndexStats>::finished, this,
            [this, watcher]() {
                const RagIndexStats stats = watcher->result();
                watcher->deleteLater();
                emit indexStatsReady(stats);
            });
    watcher->setFuture(QtConcurrent::run([databasePath, key]() {
        RagIndexStats stats;
        const QFileInfo info(databasePath);
        stats.databaseBytes = info.exists() ? info.size() : 0;
        RagIndexStore store;
        QString error;
        if (!store.open(databasePath, &error)) {
            return stats;
        }
        stats.documentCount = store.documentCount(&error);
        if (!key.contentHash.isEmpty()) {
            store.documentStats(key, &stats.current, &error);
        }
        return stats;
    }));
}

void RagDocumentCoordinator::clearAllIndexes() {
    if (m_clearing) {
        return;
    }
    m_clearing = true;
    emit indexClearingChanged(true);

    invalidateQueries();
    clearRetryState();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        // Set cancellation before the async clear so an in-flight
        // replaceDocument() cannot commit after the database is cleared.
        it->activeJob.cancellation.cancel();
        it->embeddingCancellation.cancel();
        it->embeddingCancellation = {};
        ++it->embeddingGeneration;
        ++it->snapshot.generation;
    }

    const QString databasePath = m_databasePath;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(
        watcher, &QFutureWatcher<QString>::finished, this,
        [this, watcher, databasePath]() {
            const QString error = watcher->result();
            watcher->deleteLater();

            if (!error.isEmpty()) {
                LOG_WARNING("RAG: cannot clear index: {}", error.toStdString());
                for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
                    if (isIndexingState(it->snapshot.state)) {
                        it->snapshot.state = RagIndexState::Cancelled;
                        publish(it.key());
                    }
                }
                emit indexClearFailed(error);
            } else {
                for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
                    it->conversation.clear();
                    it->snapshot.state = RagIndexState::NotIndexed;
                    it->snapshot.failureReason = RagIndexFailureReason::None;
                    it->snapshot.processedPages = 0;
                    it->snapshot.totalPages = 0;
                    it->snapshot.chunkCount = 0;
                    it->snapshot.error.clear();
                    it->snapshot.embeddingState = RagEmbeddingState::Disabled;
                    it->snapshot.embeddingIdentity = {};
                    it->snapshot.embeddedChunkCount = 0;
                    it->snapshot.embeddingError.clear();
                    publish(it.key());
                }
                emit indexCleared();
                if (!m_currentPath.isEmpty()) {
                    startIndexing(m_currentPath, true);
                }
            }

            m_clearing = false;
            emit indexClearingChanged(false);
        });

    watcher->setFuture(QtConcurrent::run([databasePath]() {
        RagIndexStore store;
        QString error;
        if (!store.open(databasePath, &error)) {
            return error;
        }
        if (!store.clearAll(&error)) {
            return error;
        }
        return QString();
    }));
}

std::unique_ptr<ChatProvider> RagDocumentCoordinator::createChatProvider()
    const {
    if (!m_settings.answerEnabled)
        return {};
    if (m_chatProviderFactory)
        return m_chatProviderFactory();
    if (!m_settings.chatEnabled())
        return {};
    return std::make_unique<OllamaChatProvider>(m_settings.chatModel,
                                                QUrl(m_settings.chatUrl()));
}

void RagDocumentCoordinator::generateAnswer(
    const QString& question, const QVector<RagEvidence>& evidence) {
    auto provider = createChatProvider();
    if (!provider) {
        emit answerBusyChanged(false);
        return;
    }
    const auto snapshot = currentSnapshot();
    const ChatBudget budget;
    const auto request = ContextAssembler::assembleBudgeted(
        question, evidence, m_entries.value(m_currentPath).conversation,
        budget);
    if (!request.assemblyError.isEmpty()) {
        emit answerReset();
        emit answerFailed(request.assemblyError);
        emit answerBusyChanged(false);
        return;
    }
    if (request.evidence.isEmpty()) {
        emit answerReady({QStringLiteral("文档中没有足够的检索依据。"), {}});
        emit answerBusyChanged(false);
        return;
    }
    if (!request.skippedEvidenceIds.isEmpty() ||
        request.droppedHistoryTurns > 0) {
        LOG_DEBUG(
            "RAG: chat budget skipped {} evidence blocks, dropped {} "
            "history turns, estimated input tokens {}",
            request.skippedEvidenceIds.size(), request.droppedHistoryTurns,
            request.estimatedInputTokens);
    }
    auto* task = provider->generate(request, this);
    m_chatTask = task;
    const quint64 generation = m_queryGeneration;
    const quint64 chatGeneration = ++m_chatGeneration;
    const QString path = m_currentPath;
    connect(task, &ChatTask::deltaReceived, this,
            [this, generation, chatGeneration](const QString& text) {
                if (generation == m_queryGeneration &&
                    chatGeneration == m_chatGeneration) {
                    emit answerDelta(text);
                }
            });
    connect(task, &ChatTask::promptEvaluated, this,
            [this, generation, chatGeneration, request](int tokens) {
                if (generation != m_queryGeneration ||
                    chatGeneration != m_chatGeneration) {
                    return;
                }
                // The estimator is a byte bound; the service reports the real
                // tokenizer count. A larger actual value means the bound must
                // be tightened before the next answer.
                if (qint64(tokens) > request.estimatedInputTokens) {
                    LOG_WARNING("RAG: prompt tokens {} exceed estimate {}",
                                tokens, request.estimatedInputTokens);
                } else {
                    LOG_DEBUG("RAG: prompt tokens {} estimated {} (num_ctx {})",
                              tokens, request.estimatedInputTokens,
                              request.contextWindowTokens);
                }
            });
    connect(task, &ChatTask::completed, this,
            [this, task, generation, chatGeneration, path, request,
             snapshot](const QString& text) {
                task->deleteLater();
                if (generation != m_queryGeneration ||
                    chatGeneration != m_chatGeneration) {
                    return;
                }
                m_chatTask = nullptr;
                auto answer = CitationValidator::validate(
                    text, request.evidence, snapshot.key.contentHash);
                m_validCitations.clear();
                for (const auto& citation : answer.citations) {
                    for (const auto& item : request.evidence) {
                        if (item.chunkId == citation.chunkId &&
                            item.documentHash == snapshot.key.contentHash) {
                            m_validCitations.insert(item.chunkId, item);
                            break;
                        }
                    }
                }
                auto it = m_entries.find(path);
                if (it != m_entries.end()) {
                    it->conversation.append(request.question.left(2000));
                    it->conversation.append(answer.text.left(4000));
                    while (it->conversation.size() > 6)
                        it->conversation.removeFirst();
                }
                emit answerReady(answer);
                emit answerBusyChanged(false);
            });
    connect(task, &ChatTask::failed, this,
            [this, task, generation, chatGeneration](const QString& error) {
                task->deleteLater();
                if (generation != m_queryGeneration ||
                    chatGeneration != m_chatGeneration) {
                    return;
                }
                m_chatTask = nullptr;
                emit answerReset();
                emit answerFailed(error);
                emit answerBusyChanged(false);
            });
    task->start();
}

void RagDocumentCoordinator::activateCitation(const QString& chunkId) {
    const auto it = m_validCitations.constFind(chunkId);
    if (it == m_validCitations.constEnd())
        return;
    const auto snapshot = currentSnapshot();
    if (snapshot.key.contentHash.isEmpty() ||
        snapshot.key.contentHash != it->documentHash) {
        return;
    }
    emit citationReady(*it);
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
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString directory = QDir(appData).filePath(QStringLiteral("rag"));
    QDir().mkpath(directory);
    QFile::setPermissions(directory, QFileDevice::ReadOwner |
                                         QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner);

    const QString databasePath =
        QDir(directory).filePath(QStringLiteral("rag-index.sqlite"));
    const QString legacyPath =
        QDir(appData).filePath(QStringLiteral("rag-index.sqlite"));
    if (QFileInfo::exists(legacyPath)) {
        if (!QFileInfo::exists(databasePath)) {
            if (!QFile::rename(legacyPath, databasePath)) {
                // The index is a cache; do not keep it in the less protected
                // AppData root after the new location has been selected.
                if (!QFile::remove(legacyPath)) {
                    LOG_WARNING("RAG: cannot migrate legacy index {}",
                                legacyPath.toStdString());
                }
            }
        } else if (!QFile::remove(legacyPath)) {
            LOG_WARNING("RAG: cannot remove legacy index {}",
                        legacyPath.toStdString());
        }
    }
    return databasePath;
}

}  // namespace Rag
