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
    EmbeddingProviderFactory embeddingProviderFactory, RagSettings settings,
    std::shared_ptr<RagConversationStore> conversationStore)
    : QObject(parent),
      m_documentModel(documentModel),
      m_databasePath(databasePath.isEmpty() ? defaultDatabasePath()
                                            : std::move(databasePath)),
      m_chatProviderFactory(std::move(chatProviderFactory)),
      m_embeddingProviderFactory(std::move(embeddingProviderFactory)),
      m_settings(settings.normalized()),
      m_conversationStore(conversationStore
                              ? std::move(conversationStore)
                              : std::make_shared<InMemoryConversationStore>()) {
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
    askQuestion(query, limit);
}

void RagDocumentCoordinator::askQuestion(const QString& question, int limit) {
    invalidateQueries();
    m_lastQuery = question;
    m_lastEvidence.clear();
    const RagDocumentSnapshot snapshot = currentSnapshot();
    if (!snapshot.isReady() || m_currentPath.isEmpty()) {
        emit answerBusyChanged(false);
        emit queryFailed(QStringLiteral("The current document is not indexed"));
        return;
    }
    Entry& entry = m_entries[m_currentPath];
    ensureConversation(entry, m_currentPath);
    RagConversationTurn turn;
    turn.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    turn.question = question;
    turn.historyEpoch = entry.historyEpoch;
    turn.state = RagTurnState::Retrieving;
    const int index = entry.conversation->appendTurn(std::move(turn));
    const QString turnId = entry.conversation->turns().at(index).id;
    setActiveTurn(turnId, 0);
    persistConversation(m_currentPath);
    emit conversationBusyChanged(true);
    startRetrieval(turnId, question, limit);
}

void RagDocumentCoordinator::startRetrieval(const QString& turnId,
                                            const QString& question,
                                            int limit) {
    const RagDocumentSnapshot snapshot = currentSnapshot();
    if (!snapshot.isReady() || !isActiveTurn(turnId, 0)) {
        emit answerBusyChanged(false);
        return;
    }
    const quint64 queryGeneration = ++m_queryGeneration;
    emit answerBusyChanged(true);
    emit evidenceReady({});
    m_queryCancellation = {};
    const RagCancellationToken queryCancellation = m_queryCancellation;
    const QString path = m_currentPath;
    const QString documentHash = snapshot.key.contentHash;
    auto* watcher = new QFutureWatcher<QueryJobResult>(this);
    connect(watcher, &QFutureWatcher<QueryJobResult>::finished, this,
            [this, watcher, turnId, path, documentHash]() {
                const QueryJobResult result = watcher->result();
                watcher->deleteLater();
                if (result.generation != m_queryGeneration ||
                    !isActiveTurn(turnId, 0)) {
                    return;
                }
                auto it = m_entries.find(path);
                if (it == m_entries.end() || !it->conversation ||
                    it->snapshot.key.contentHash != documentHash) {
                    return;
                }
                RagConversationModel& model = *it->conversation;
                if (result.indexMissing) {
                    m_lastEvidence.clear();
                    model.setFailure(turnId, RagChatFailure::Unknown,
                                     QStringLiteral("索引已丢失，正在重建"));
                    model.setState(turnId, RagTurnState::Failed);
                    clearActiveTurn();
                    startIndexing(result.documentPath, true);
                    emit answerBusyChanged(false);
                    emit conversationBusyChanged(false);
                    emit queryFailed(QStringLiteral("索引已丢失，正在重建"));
                    return;
                }
                if (!result.error.isEmpty()) {
                    m_lastEvidence.clear();
                    model.setFailure(turnId, RagChatFailure::Unknown,
                                     result.error);
                    model.setState(turnId, RagTurnState::Failed);
                    clearActiveTurn();
                    emit answerBusyChanged(false);
                    emit conversationBusyChanged(false);
                    emit queryFailed(result.error);
                    return;
                }
                model.setEvidence(turnId, result.evidence);
                m_lastEvidence = result.evidence;
                emit evidenceReady(result.evidence);
                if (result.evidence.isEmpty()) {
                    model.setFailure(turnId, RagChatFailure::None,
                                     QStringLiteral("没有找到足够相关的证据"));
                    model.setState(turnId, RagTurnState::Failed);
                    clearActiveTurn();
                    persistConversation(path);
                    emit answerBusyChanged(false);
                    emit conversationBusyChanged(false);
                    return;
                }
                model.setState(turnId, RagTurnState::Generating);
                startGeneration(turnId);
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
                           documentPath = snapshot.filePath, question, limit,
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
                store, document, question, limit, provider.get(),
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
        emit conversationModelChanged(nullptr);
        return;
    }
    if (!m_entries.contains(m_currentPath)) {
        startIndexing(m_currentPath, false);
    } else {
        publish(m_currentPath);
    }
    ensureConversation(m_entries[m_currentPath], m_currentPath);
    emit conversationModelChanged(currentConversationModel());
}

void RagDocumentCoordinator::onAllDocumentsClosed() {
    invalidateQueries();
    clearRetryState();
    m_currentPath.clear();
    emit currentDocumentStateChanged({});
    emit conversationModelChanged(nullptr);
}

void RagDocumentCoordinator::startIndexing(const QString& filePath,
                                           bool forceRebuild) {
    const QString path = normalizedPath(filePath);
    if (path.isEmpty()) {
        return;
    }

    Entry& entry = m_entries[path];
    const QString previousContentHash = entry.snapshot.key.contentHash;
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
    if (forceRebuild) {
        ++entry.historyEpoch;
        if (entry.conversation) {
            entry.conversation->clear();
        }
        m_conversationStore->remove(path);
    }
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
            [this, watcher, path, previousContentHash]() {
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
                if (!previousContentHash.isEmpty() &&
                    !result.document.contentHash.isEmpty() &&
                    result.document.contentHash != previousContentHash) {
                    ++it->historyEpoch;
                    if (it->conversation) {
                        it->conversation->clear();
                    }
                    m_conversationStore->remove(path);
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
    cancelActiveTurn(RagTurnState::Cancelled);
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

void RagDocumentCoordinator::cancelActiveTurn(RagTurnState terminal) {
    if (!hasActiveTurn()) {
        return;
    }
    const QString turnId = m_activeTurnId;
    const QString path = m_currentPath;
    m_queryCancellation.cancel();
    m_queryCancellation = {};
    ++m_queryGeneration;
    cancelActiveChat();
    auto it = m_entries.find(path);
    if (it != m_entries.end() && it->conversation &&
        it->conversation->turn(turnId)) {
        it->conversation->setState(turnId, terminal);
        persistConversation(path);
    }
    clearActiveTurn();
    emit conversationBusyChanged(false);
}

bool RagDocumentCoordinator::hasActiveTurn() const {
    return !m_activeTurnId.isEmpty();
}

bool RagDocumentCoordinator::isActiveTurn(const QString& turnId,
                                          quint64 generation) const {
    return hasActiveTurn() && m_activeTurnId == turnId &&
           m_activeTurnGeneration == generation;
}

void RagDocumentCoordinator::setActiveTurn(const QString& turnId,
                                           quint64 generation) {
    m_activeTurnId = turnId;
    m_activeTurnGeneration = generation;
}

void RagDocumentCoordinator::clearActiveTurn() {
    m_activeTurnId.clear();
    m_activeTurnGeneration = 0;
}

void RagDocumentCoordinator::invalidateQueries() {
    abortInFlight();
    m_validCitations.clear();
    emit answerReset();
}

void RagDocumentCoordinator::cancelAnswer() { stopActiveTurn(); }

void RagDocumentCoordinator::stopActiveTurn() {
    if (!hasActiveTurn()) {
        invalidateQueries();
        return;
    }
    cancelActiveTurn(RagTurnState::Cancelled);
    m_validCitations.clear();
    emit answerReset();
    emit answerBusyChanged(false);
    emit conversationBusyChanged(false);
}

void RagDocumentCoordinator::clearConversation() {
    cancelActiveTurn(RagTurnState::Cancelled);
    auto it = m_entries.find(m_currentPath);
    if (it != m_entries.end()) {
        ++it->historyEpoch;
        if (it->conversation) {
            it->conversation->clear();
        }
    }
    if (!m_currentPath.isEmpty()) {
        m_conversationStore->remove(m_currentPath);
    }
    m_lastEvidence.clear();
    m_validCitations.clear();
    emit answerReset();
    emit answerBusyChanged(false);
    emit conversationBusyChanged(false);
}

RagConversationModel* RagDocumentCoordinator::currentConversationModel() const {
    auto it = m_entries.constFind(m_currentPath);
    if (it == m_entries.constEnd() || !it->conversation) {
        return nullptr;
    }
    return it->conversation.get();
}

void RagDocumentCoordinator::ensureConversation(Entry& entry,
                                                const QString& path) {
    if (entry.conversation) {
        return;
    }
    entry.conversation = std::make_shared<RagConversationModel>();
    const RagConversationState state = m_conversationStore->load(path);
    entry.historyEpoch = qMax<quint64>(1, state.historyEpoch);
    if (!state.chatModel.isEmpty() && state.chatModel != m_settings.chatModel) {
        quint64 maxEpoch = entry.historyEpoch;
        for (const RagConversationTurn& turn : state.turns) {
            maxEpoch = qMax(maxEpoch, turn.historyEpoch);
        }
        entry.historyEpoch = maxEpoch + 1;
    }
    for (const RagConversationTurn& turn : state.turns) {
        entry.conversation->appendTurn(turn);
    }
    if (!state.turns.isEmpty()) {
        persistConversation(path);
    }
}

void RagDocumentCoordinator::persistConversation(const QString& path) {
    auto it = m_entries.find(path);
    if (it == m_entries.end() || !it->conversation) {
        return;
    }
    RagConversationState state;
    state.turns = it->conversation->turns();
    state.historyEpoch = it->historyEpoch;
    state.chatModel = m_settings.chatModel;
    m_conversationStore->save(path, state);
}

void RagDocumentCoordinator::regenerateTurn(const QString& turnIdRef) {
    const QString turnId = turnIdRef;
    auto it = m_entries.find(m_currentPath);
    if (it == m_entries.end() || !it->conversation) {
        return;
    }
    const int index = it->conversation->indexOf(turnId);
    if (index < 0) {
        return;
    }
    it->conversation->truncateAfter(index);
    cancelActiveTurn(RagTurnState::Cancelled);
    const RagConversationTurn* current = it->conversation->turn(turnId);
    if (!current) {
        return;
    }
    const quint64 generation = current->generation + 1;
    const bool needsRetrieval =
        current->evidence.isEmpty() || !current->evidenceComplete;
    const QString question = current->question;
    it->conversation->updateTurn(
        turnId, [generation, needsRetrieval](RagConversationTurn& turn) {
            turn.generation = generation;
            turn.answerText.clear();
            turn.citations.clear();
            turn.failure = RagChatFailure::None;
            turn.error.clear();
            turn.promptTokens.reset();
            turn.state = needsRetrieval ? RagTurnState::Retrieving
                                        : RagTurnState::Generating;
        });
    persistConversation(m_currentPath);
    if (needsRetrieval) {
        setActiveTurn(turnId, 0);
        emit conversationBusyChanged(true);
        startRetrieval(turnId, question, 10);
        return;
    }
    setActiveTurn(turnId, generation);
    emit answerReset();
    emit answerBusyChanged(true);
    emit conversationBusyChanged(true);
    startGeneration(turnId);
}

void RagDocumentCoordinator::retryAnswer() {
    auto it = m_entries.find(m_currentPath);
    if (it == m_entries.end() || !it->conversation ||
        it->conversation->isEmpty()) {
        return;
    }
    const QString turnId = it->conversation->turns().last().id;
    regenerateTurn(turnId);
}

void RagDocumentCoordinator::retryQuery() {
    cancelActiveTurn(RagTurnState::Cancelled);
    auto it = m_entries.find(m_currentPath);
    if (it == m_entries.end() || !it->conversation ||
        it->conversation->isEmpty()) {
        if (!m_lastQuery.trimmed().isEmpty()) {
            askQuestion(m_lastQuery);
        }
        return;
    }
    const QString turnId = it->conversation->turns().last().id;
    const RagConversationTurn* current = it->conversation->turn(turnId);
    if (!current) {
        return;
    }
    const QString question = current->question;
    const quint64 historyEpoch = it->historyEpoch;
    it->conversation->updateTurn(turnId,
                                 [historyEpoch](RagConversationTurn& turn) {
                                     turn.historyEpoch = historyEpoch;
                                     turn.state = RagTurnState::Retrieving;
                                     turn.evidence.clear();
                                     turn.answerText.clear();
                                     turn.citations.clear();
                                     turn.failure = RagChatFailure::None;
                                     turn.error.clear();
                                 });
    setActiveTurn(turnId, 0);
    emit conversationBusyChanged(true);
    startRetrieval(turnId, question, 10);
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
        auto active = m_entries.find(m_currentPath);
        const RagConversationTurn* turn =
            (active != m_entries.end() && active->conversation &&
             hasActiveTurn())
                ? active->conversation->turn(m_activeTurnId)
                : nullptr;
        if (turn && turn->state == RagTurnState::Generating) {
            cancelActiveTurn(RagTurnState::Cancelled);
            emit answerBusyChanged(false);
        }
        // A retrieval in flight is allowed to finish and use the new settings.
    }

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry& entry = *it;
        if (chatModelChanged) {
            ++entry.historyEpoch;
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
    ++it->historyEpoch;
    if (it->conversation) {
        it->conversation->clear();
    }
    m_conversationStore->remove(it.key());
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
                    ++it->historyEpoch;
                    if (it->conversation) {
                        it->conversation->clear();
                    }
                    m_conversationStore->remove(it.key());
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

void RagDocumentCoordinator::startGeneration(const QString& turnId) {
    auto it = m_entries.find(m_currentPath);
    if (it == m_entries.end() || !it->conversation) {
        return;
    }
    const RagConversationTurn* current = it->conversation->turn(turnId);
    if (!current) {
        return;
    }
    const RagConversationTurn turn = *current;
    // The retrieval path registers generation 0; a regenerated/restored turn
    // can have a newer generation. Every generation entry must agree with the
    // generation checked by the task callbacks.
    setActiveTurn(turnId, turn.generation);
    auto provider = createChatProvider();
    if (!provider) {
        it->conversation->setState(turnId, RagTurnState::Ready);
        clearActiveTurn();
        persistConversation(m_currentPath);
        emit answerBusyChanged(false);
        emit conversationBusyChanged(false);
        return;
    }
    const ChatBudget budget;
    const auto request = ContextAssembler::assembleBudgeted(
        turn.question, turn.evidence,
        historyForPrompt(it->conversation->turns(), it->historyEpoch, turnId),
        budget);
    if (!request.assemblyError.isEmpty()) {
        const RagChatFailure failure =
            request.budgetIssue == ChatBudgetIssue::ContextTooSmall
                ? RagChatFailure::ContextTooSmall
                : RagChatFailure::BudgetEvidence;
        it->conversation->setFailure(turnId, failure, request.assemblyError);
        it->conversation->setState(turnId, RagTurnState::Failed);
        clearActiveTurn();
        persistConversation(m_currentPath);
        emit answerReset();
        emit answerFailed(request.assemblyError);
        emit answerBusyChanged(false);
        emit conversationBusyChanged(false);
        return;
    }
    if (request.evidence.isEmpty()) {
        it->conversation->setFailure(
            turnId, RagChatFailure::None,
            QStringLiteral("文档中没有足够的检索依据。"));
        it->conversation->setState(turnId, RagTurnState::Failed);
        clearActiveTurn();
        persistConversation(m_currentPath);
        emit answerBusyChanged(false);
        emit conversationBusyChanged(false);
        return;
    }
    auto* task = provider->generate(request, this);
    m_chatTask = task;
    const quint64 chatGeneration = ++m_chatGeneration;
    const quint64 turnGeneration = turn.generation;
    const QString path = m_currentPath;
    connect(task, &ChatTask::deltaReceived, this,
            [this, turnId, turnGeneration, chatGeneration,
             path](const QString& text) {
                if (chatGeneration != m_chatGeneration ||
                    !isActiveTurn(turnId, turnGeneration)) {
                    return;
                }
                auto entry = m_entries.find(path);
                if (entry == m_entries.end() || !entry->conversation) {
                    return;
                }
                entry->conversation->appendAnswerDelta(turnId, text);
                emit answerDelta(text);
            });
    connect(task, &ChatTask::promptEvaluated, this,
            [this, turnId, turnGeneration, chatGeneration, path](int tokens) {
                if (chatGeneration != m_chatGeneration ||
                    !isActiveTurn(turnId, turnGeneration)) {
                    return;
                }
                auto entry = m_entries.find(path);
                if (entry != m_entries.end() && entry->conversation) {
                    entry->conversation->setPromptTokens(turnId, tokens);
                }
            });
    connect(task, &ChatTask::completed, this,
            [this, task, turnId, turnGeneration, chatGeneration, path,
             request](const QString& text) {
                task->deleteLater();
                if (chatGeneration != m_chatGeneration ||
                    !isActiveTurn(turnId, turnGeneration)) {
                    return;
                }
                m_chatTask = nullptr;
                auto entry = m_entries.find(path);
                if (entry == m_entries.end() || !entry->conversation) {
                    return;
                }
                const RagDocumentSnapshot snapshot = entry->snapshot;
                auto answer = CitationValidator::validate(
                    text, request.evidence, snapshot.key.contentHash);
                entry->conversation->setAnswer(turnId, answer.plainText,
                                               answer.text, answer.citations);
                entry->conversation->setState(turnId, RagTurnState::Ready);
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
                m_lastEvidence = request.evidence;
                persistConversation(path);
                clearActiveTurn();
                emit answerReady(answer);
                emit answerBusyChanged(false);
                emit conversationBusyChanged(false);
            });
    connect(task, &ChatTask::failed, this,
            [this, task, turnId, turnGeneration, chatGeneration](
                RagChatFailure failure, const QString& error) {
                task->deleteLater();
                if (chatGeneration != m_chatGeneration ||
                    !isActiveTurn(turnId, turnGeneration)) {
                    return;
                }
                m_chatTask = nullptr;
                auto entry = m_entries.find(m_currentPath);
                if (entry != m_entries.end() && entry->conversation) {
                    if (failure == RagChatFailure::Cancelled) {
                        entry->conversation->setState(turnId,
                                                      RagTurnState::Cancelled);
                    } else {
                        entry->conversation->setFailure(turnId, failure, error);
                        entry->conversation->setState(turnId,
                                                      RagTurnState::Failed);
                    }
                    persistConversation(m_currentPath);
                }
                clearActiveTurn();
                if (failure == RagChatFailure::Cancelled) {
                    emit answerBusyChanged(false);
                    emit conversationBusyChanged(false);
                    return;
                }
                emit answerReset();
                emit answerFailed(error);
                emit answerBusyChanged(false);
                emit conversationBusyChanged(false);
            });
    task->start();
}

void RagDocumentCoordinator::activateCitation(const QString& chunkId) {
    const auto snapshot = currentSnapshot();
    if (snapshot.key.contentHash.isEmpty()) {
        return;
    }
    const auto it = m_validCitations.constFind(chunkId);
    if (it != m_validCitations.constEnd() &&
        snapshot.key.contentHash == it->documentHash) {
        emit citationReady(*it);
        return;
    }

    // Restored conversations keep only their cited evidence; fall back to it
    // so citation jumps keep working after a restart.
    auto entry = m_entries.find(m_currentPath);
    if (entry == m_entries.end() || !entry->conversation) {
        return;
    }
    for (const RagConversationTurn& turn : entry->conversation->turns()) {
        for (const RagEvidence& evidence : turn.evidence) {
            if (evidence.chunkId == chunkId &&
                evidence.documentHash == snapshot.key.contentHash) {
                emit citationReady(evidence);
                return;
            }
        }
    }
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
