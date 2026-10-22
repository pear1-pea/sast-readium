#pragma once

#include <QVector>
#include <QWidget>

#include "rag/chat/ChatProvider.h"
#include "rag/domain/RagTypes.h"
#include "rag/ui/AskPdfState.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
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
        int embeddedChunkCount = 0, const QString& embeddingError = {},
        RagIndexFailureReason failureReason = RagIndexFailureReason::None);
    void setEvidence(const QVector<RagEvidence>& evidence);
    void setBusy(bool busy);
    void setClearing(bool clearing);
    void resetAnswer();
    void appendAnswer(const QString& text);
    void setAnswer(const RagAnswer& answer);
    void setAnswerError(const QString& error);
    void setQueryError(const QString& error);
    void setAnswerEnabled(bool enabled);

signals:
    void querySubmitted(const QString& query);
    void evidenceActivated(const Rag::RagEvidence& evidence);
    void rebuildIndexRequested();
    void cancelIndexRequested();
    void cancelAnswerRequested();
    void clearIndexRequested();
    void citationActivated(const QString& chunkId);
    void settingsRequested();
    void retryAnswerRequested();
    void retryQueryRequested();

private:
    void submitQuery();
    void activateItem(QListWidgetItem* item);
    void updateControlStates();
    void refreshStatusText();
    void refreshEmptyState();
    void clearEvidence();
    void runStateAction(AskPdfStateAction action);
    static QString stateText(RagIndexState state, int chunkCount,
                             const QString& error,
                             RagEmbeddingState embeddingState,
                             int embeddedChunkCount,
                             const QString& embeddingError, bool answerEnabled);

    DocumentKey m_document;
    RagIndexState m_state = RagIndexState::NotIndexed;
    RagIndexFailureReason m_failureReason = RagIndexFailureReason::None;
    int m_chunkCount = 0;
    QString m_indexError;
    RagEmbeddingState m_embeddingState = RagEmbeddingState::Disabled;
    int m_embeddedChunkCount = 0;
    QString m_embeddingError;
    bool m_answerEnabled = true;
    bool m_hasSearched = false;
    bool m_queryFailed = false;
    bool m_answerFailed = false;
    QString m_queryError;
    QString m_answerError;
    QVector<RagEvidence> m_evidence;
    QLabel* m_documentLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QWidget* m_statePanel = nullptr;
    QLabel* m_stateTitle = nullptr;
    QLabel* m_stateDetail = nullptr;
    QPushButton* m_statePrimary = nullptr;
    QPushButton* m_stateSecondary = nullptr;
    AskPdfStateAction m_primaryAction = AskPdfStateAction::None;
    AskPdfStateAction m_secondaryAction = AskPdfStateAction::None;
    QLineEdit* m_queryEdit = nullptr;
    QToolButton* m_submitButton = nullptr;
    QToolButton* m_rebuildButton = nullptr;
    QToolButton* m_cancelButton = nullptr;
    QToolButton* m_clearButton = nullptr;
    QToolButton* m_settingsButton = nullptr;
    QListWidget* m_evidenceList = nullptr;
    QPlainTextEdit* m_answerEdit = nullptr;
    QListWidget* m_citationList = nullptr;
    QToolButton* m_stopButton = nullptr;
    bool m_busy = false;
    bool m_clearing = false;
};

}  // namespace Rag
