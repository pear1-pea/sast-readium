#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "rag/domain/RagTypes.h"

class DocumentModel;

namespace Rag {

class RagDocumentCoordinator : public QObject {
    Q_OBJECT

public:
    explicit RagDocumentCoordinator(DocumentModel* documentModel,
                                    QString databasePath = {},
                                    QObject* parent = nullptr);
    ~RagDocumentCoordinator() override;

    RagDocumentSnapshot currentSnapshot() const;
    QString databasePath() const { return m_databasePath; }

public slots:
    void queryCurrentDocument(const QString& query, int limit = 10);
    void rebuildCurrentDocument();
    void cancelCurrentIndexing();

signals:
    void currentDocumentStateChanged(const Rag::RagDocumentSnapshot& snapshot);
    void documentStateChanged(const Rag::RagDocumentSnapshot& snapshot);
    void evidenceReady(const QVector<Rag::RagEvidence>& evidence);
    void queryFailed(const QString& error);

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
    QString pathForIndex(int index) const;
    static QString normalizedPath(const QString& filePath);
    static QString defaultDatabasePath();

    struct Entry {
        RagDocumentSnapshot snapshot;
        RagJobContext activeJob;
    };

    DocumentModel* m_documentModel = nullptr;
    QString m_databasePath;
    QString m_currentPath;
    QHash<QString, Entry> m_entries;
    quint64 m_queryGeneration = 0;
};

}  // namespace Rag
