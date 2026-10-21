#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "rag/chat/ChatProvider.h"
#include "rag/domain/RagTypes.h"

class DocumentModel;

namespace Rag {

class RagDocumentCoordinator : public QObject {
    Q_OBJECT

public:
    using ChatProviderFactory = std::function<std::unique_ptr<ChatProvider>()>;

    explicit RagDocumentCoordinator(
        DocumentModel* documentModel, QString databasePath = {},
        QObject* parent = nullptr,
        ChatProviderFactory chatProviderFactory = {});
    ~RagDocumentCoordinator() override;

    RagDocumentSnapshot currentSnapshot() const;
    QString databasePath() const { return m_databasePath; }

public slots:
    void queryCurrentDocument(const QString& query, int limit = 10);
    void rebuildCurrentDocument();
    void cancelCurrentIndexing();
    void cancelAnswer();
    void activateCitation(const QString& chunkId);

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

private:
    void onDocumentOpened(int index);
    void onDocumentClosed(int index);
    void onCurrentDocumentChanged(int index);
    void onAllDocumentsClosed();
    void startIndexing(const QString& filePath, bool forceRebuild);
    void acceptStage(const QString& filePath, const QString& jobId,
                     quint64 generation, int stageSequence,
                     RagIndexState state);
    void publish(const QString& filePath);
    void startEmbedding(const QString& path, const DocumentKey& document,
                        quint64 generation, const RagCancellationToken& cancel);
    void updateEmbeddingState(const QString& path, quint64 generation,
                              RagEmbeddingState state, int embeddedCount,
                              const QString& error);
    void invalidateQueries();
    void generateAnswer(const QString& question,
                        const QVector<RagEvidence>& evidence);
    std::unique_ptr<ChatProvider> createChatProvider() const;
    QString pathForIndex(int index) const;
    static QString normalizedPath(const QString& filePath);
    static QString defaultDatabasePath();

    struct Entry {
        RagDocumentSnapshot snapshot;
        RagJobContext activeJob;
        QStringList conversation;
    };

    DocumentModel* m_documentModel = nullptr;
    QString m_databasePath;
    QString m_currentPath;
    QHash<QString, Entry> m_entries;
    quint64 m_queryGeneration = 0;
    RagCancellationToken m_queryCancellation;
    QPointer<ChatTask> m_chatTask;
    QHash<QString, RagEvidence> m_validCitations;
    ChatProviderFactory m_chatProviderFactory;
};

}  // namespace Rag
