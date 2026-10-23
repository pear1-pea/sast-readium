#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "rag/chat/ChatProvider.h"
#include "rag/config/RagSettings.h"
#include "rag/domain/RagTypes.h"
#include "rag/embedding/EmbeddingProvider.h"

class DocumentModel;

namespace Rag {

class RagDocumentCoordinator : public QObject {
    Q_OBJECT

public:
    using ChatProviderFactory = std::function<std::unique_ptr<ChatProvider>()>;
    using EmbeddingProviderFactory =
        std::function<std::unique_ptr<EmbeddingProvider>(
            const QString& model, const QString& version,
            const QString& endpoint, int timeoutMs)>;

    explicit RagDocumentCoordinator(
        DocumentModel* documentModel, QString databasePath = {},
        QObject* parent = nullptr, ChatProviderFactory chatProviderFactory = {},
        EmbeddingProviderFactory embeddingProviderFactory = {},
        RagSettings settings = {});
    ~RagDocumentCoordinator() override;

    RagDocumentSnapshot currentSnapshot() const;
    QString databasePath() const { return m_databasePath; }
    RagSettings settings() const { return m_settings; }

public slots:
    void queryCurrentDocument(const QString& query, int limit = 10);
    void rebuildCurrentDocument();
    void cancelCurrentIndexing();
    void cancelAnswer();
    void clearAllIndexes();
    void activateCitation(const QString& chunkId);
    void applySettings(const RagSettings& settings);
    void retryAnswer();
    void retryQuery();
    void deleteCurrentDocumentIndex();
    void requestIndexStats();

signals:
    void currentDocumentStateChanged(const Rag::RagDocumentSnapshot& snapshot);
    void documentStateChanged(const Rag::RagDocumentSnapshot& snapshot);
    void evidenceReady(const QVector<Rag::RagEvidence>& evidence);
    void queryFailed(const QString& error);
    void answerReset();
    void answerDelta(const QString& text);
    void answerReady(const Rag::RagAnswer& answer);
    void answerFailed(const QString& error);
    void answerBusyChanged(bool busy);
    void citationReady(const Rag::RagEvidence& evidence);
    void indexClearingChanged(bool clearing);
    void indexCleared();
    void indexClearFailed(const QString& error);
    void documentIndexDeleted();
    void documentIndexDeleteFailed(const QString& error);
    void indexStatsReady(const Rag::RagIndexStats& stats);

private:
    void onDocumentOpened(int index);
    void onDocumentClosed(int index);
    void onCurrentDocumentChanged(int index);
    void onAllDocumentsClosed();
    void startIndexing(const QString& filePath, bool forceRebuild);
    void acceptStage(const QString& filePath, const QString& jobId,
                     quint64 generation, int stageSequence,
                     RagIndexState state);
    void acceptProgress(const QString& filePath, const QString& jobId,
                        quint64 generation, int processed, int total);
    void publish(const QString& filePath);
    void startEmbedding(const QString& path, const DocumentKey& document);
    void updateEmbeddingState(const QString& path, quint64 generation,
                              RagEmbeddingState state, int embeddedCount,
                              const QString& error);
    void abortInFlight();
    void cancelActiveChat();
    void invalidateQueries();
    void generateAnswer(const QString& question,
                        const QVector<RagEvidence>& evidence);
    std::unique_ptr<ChatProvider> createChatProvider() const;
    QString pathForIndex(int index) const;
    void clearRetryState();
    static QString normalizedPath(const QString& filePath);
    static QString defaultDatabasePath();

    struct Entry {
        RagDocumentSnapshot snapshot;
        RagJobContext activeJob;
        quint64 embeddingGeneration = 0;
        RagCancellationToken embeddingCancellation;
        qint64 lastProgressPublishMs = 0;
        QStringList conversation;
    };

    DocumentModel* m_documentModel = nullptr;
    QString m_databasePath;
    QString m_currentPath;
    QHash<QString, Entry> m_entries;
    quint64 m_queryGeneration = 0;
    quint64 m_chatGeneration = 0;
    RagCancellationToken m_queryCancellation;
    QPointer<ChatTask> m_chatTask;
    QString m_lastQuery;
    QVector<RagEvidence> m_lastEvidence;
    QHash<QString, RagEvidence> m_validCitations;
    ChatProviderFactory m_chatProviderFactory;
    EmbeddingProviderFactory m_embeddingProviderFactory;
    RagSettings m_settings;
    bool m_clearing = false;
    bool m_deletingCurrent = false;
};

}  // namespace Rag
