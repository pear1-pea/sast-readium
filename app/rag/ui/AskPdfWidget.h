#pragma once

#include <QVector>
#include <QWidget>

#include "rag/chat/ChatProvider.h"
#include "rag/domain/RagTypes.h"
#include "rag/ui/AskPdfProgress.h"
#include "rag/ui/AskPdfState.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QToolButton;

namespace Rag {

class AskPdfConversationView;
class RagConversationModel;

class AskPdfWidget : public QWidget {
    Q_OBJECT

public:
    explicit AskPdfWidget(QWidget* parent = nullptr);

    void setDocumentContext(const DocumentKey& document);
    void setIndexState(
        RagIndexState state, int chunkCount = 0, const QString& error = {},
        RagEmbeddingState embeddingState = RagEmbeddingState::Disabled,
        int embeddedChunkCount = 0, const QString& embeddingError = {},
        RagIndexFailureReason failureReason = RagIndexFailureReason::None,
        int processedPages = 0, int totalPages = 0);
    void setClearing(bool clearing);
    void setAnswerEnabled(bool enabled);
    void setEmbeddingEnabled(bool enabled);
    void setConversationModel(RagConversationModel* model);
    void setConversationBusy(bool busy);
    void setReturnToPositionAvailable(bool available);

signals:
    void questionSubmitted(const QString& question);
    void evidenceActivated(const Rag::RagEvidence& evidence);
    void rebuildIndexRequested();
    void cancelIndexRequested();
    void clearIndexRequested();
    void citationActivated(const QString& chunkId);
    void settingsRequested();
    void indexManagementRequested();
    void regenerateTurnRequested(const QString& turnId);
    void stopGenerationRequested();
    void clearConversationRequested();
    void retryAnswerRequested();
    void retryQueryRequested();
    void returnToPositionRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void submitQuery();
    void updateControlStates();
    void refreshStatusText();
    void refreshEmptyState();
    void refreshProgress();

    DocumentKey m_document;
    RagIndexState m_state = RagIndexState::NotIndexed;
    RagIndexFailureReason m_failureReason = RagIndexFailureReason::None;
    int m_processedPages = 0;
    int m_totalPages = 0;
    int m_chunkCount = 0;
    QString m_indexError;
    RagEmbeddingState m_embeddingState = RagEmbeddingState::Disabled;
    int m_embeddedChunkCount = 0;
    QString m_embeddingError;
    bool m_answerEnabled = true;
    bool m_embeddingsEnabled = false;
    bool m_conversationBusy = false;
    bool m_clearing = false;

    QLabel* m_documentLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QWidget* m_statePanel = nullptr;
    QLabel* m_stateTitle = nullptr;
    QLabel* m_stateDetail = nullptr;
    QPushButton* m_statePrimary = nullptr;
    QPushButton* m_stateSecondary = nullptr;
    AskPdfStateAction m_primaryAction = AskPdfStateAction::None;
    AskPdfStateAction m_secondaryAction = AskPdfStateAction::None;
    AskPdfConversationView* m_conversation = nullptr;
    QPlainTextEdit* m_queryEdit = nullptr;
    QPushButton* m_sendButton = nullptr;
    QToolButton* m_stopButton = nullptr;
    QToolButton* m_clearConversationButton = nullptr;
    QToolButton* m_rebuildButton = nullptr;
    QToolButton* m_cancelButton = nullptr;
    QToolButton* m_clearButton = nullptr;
    QToolButton* m_settingsButton = nullptr;
    QToolButton* m_indexButton = nullptr;
    QToolButton* m_returnButton = nullptr;
};

}  // namespace Rag
