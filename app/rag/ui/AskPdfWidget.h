#pragma once

#include <QVector>
#include <QWidget>

#include "rag/chat/ChatProvider.h"
#include "rag/domain/RagTypes.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;
class QPlainTextEdit;

namespace Rag {

class AskPdfWidget : public QWidget {
    Q_OBJECT

public:
    explicit AskPdfWidget(QWidget* parent = nullptr);

    void setDocumentContext(const DocumentKey& document);
    void setIndexState(
        RagIndexState state, int chunkCount = 0, const QString& error = {},
        RagEmbeddingState embeddingState = RagEmbeddingState::Disabled,
        int embeddedChunkCount = 0, const QString& embeddingError = {});
    void setEvidence(const QVector<RagEvidence>& evidence);
    void setBusy(bool busy);
    void resetAnswer();
    void appendAnswer(const QString& text);
    void setAnswer(const RagAnswer& answer);
    void setAnswerError(const QString& error);

signals:
    void querySubmitted(const QString& query);
    void evidenceActivated(const Rag::RagEvidence& evidence);
    void rebuildIndexRequested();
    void cancelIndexRequested();
    void cancelAnswerRequested();
    void citationActivated(const QString& chunkId);

private:
    void submitQuery();
    void activateItem(QListWidgetItem* item);
    static QString stateText(RagIndexState state, int chunkCount,
                             const QString& error,
                             RagEmbeddingState embeddingState,
                             int embeddedChunkCount,
                             const QString& embeddingError);

    DocumentKey m_document;
    RagIndexState m_state = RagIndexState::NotIndexed;
    QVector<RagEvidence> m_evidence;
    QLabel* m_documentLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLineEdit* m_queryEdit = nullptr;
    QToolButton* m_submitButton = nullptr;
    QToolButton* m_rebuildButton = nullptr;
    QToolButton* m_cancelButton = nullptr;
    QListWidget* m_evidenceList = nullptr;
    QPlainTextEdit* m_answerEdit = nullptr;
    QListWidget* m_citationList = nullptr;
    QToolButton* m_stopButton = nullptr;
    bool m_busy = false;
};

}  // namespace Rag
