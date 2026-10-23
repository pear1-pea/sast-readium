#include "AskPdfWidget.h"

#include <QClipboard>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "AskPdfConversationView.h"

namespace Rag {
namespace {

bool isIndexingState(RagIndexState state) {
    return state == RagIndexState::CheckingCache ||
           state == RagIndexState::Extracting ||
           state == RagIndexState::Chunking || state == RagIndexState::Indexing;
}

}  // namespace

AskPdfWidget::AskPdfWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_documentLabel = new QLabel(this);
    m_documentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_documentLabel->setWordWrap(true);

    auto* statusRow = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("askPdfStatus"));
    m_statusLabel->setWordWrap(true);
    statusRow->addWidget(m_statusLabel, 1);

    m_settingsButton = new QToolButton(this);
    m_settingsButton->setObjectName(QStringLiteral("askPdfSettings"));
    const QIcon settingsIcon = QIcon::fromTheme(QStringLiteral("settings"));
    if (settingsIcon.isNull()) {
        m_settingsButton->setText(QStringLiteral("⚙"));
    } else {
        m_settingsButton->setIcon(settingsIcon);
    }
    m_settingsButton->setToolTip(QStringLiteral("本地 AI 设置"));
    m_settingsButton->setAutoRaise(true);
    statusRow->addWidget(m_settingsButton);

    m_indexButton = new QToolButton(this);
    m_indexButton->setObjectName(QStringLiteral("askPdfIndex"));
    m_indexButton->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
    m_indexButton->setToolTip(QStringLiteral("索引管理"));
    m_indexButton->setAutoRaise(true);
    statusRow->addWidget(m_indexButton);

    m_rebuildButton = new QToolButton(this);
    m_rebuildButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_rebuildButton->setToolTip(QStringLiteral("重建索引"));
    statusRow->addWidget(m_rebuildButton);

    m_cancelButton = new QToolButton(this);
    m_cancelButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelButton->setToolTip(QStringLiteral("取消索引"));
    statusRow->addWidget(m_cancelButton);

    m_clearButton = new QToolButton(this);
    m_clearButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_clearButton->setToolTip(QStringLiteral("清空 Ask PDF 索引"));
    statusRow->addWidget(m_clearButton);

    m_clearConversationButton = new QToolButton(this);
    m_clearConversationButton->setObjectName(
        QStringLiteral("askPdfClearConversation"));
    m_clearConversationButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogResetButton));
    m_clearConversationButton->setToolTip(QStringLiteral("清空对话"));
    statusRow->addWidget(m_clearConversationButton);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("askPdfProgress"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->hide();

    m_statePanel = new QWidget(this);
    m_statePanel->setObjectName(QStringLiteral("askPdfStatePanel"));
    auto* stateLayout = new QVBoxLayout(m_statePanel);
    stateLayout->setContentsMargins(0, 0, 0, 0);
    stateLayout->setSpacing(4);

    m_stateTitle = new QLabel(m_statePanel);
    m_stateTitle->setObjectName(QStringLiteral("askPdfStateTitle"));
    m_stateTitle->setWordWrap(true);
    QFont titleFont = m_stateTitle->font();
    titleFont.setBold(true);
    m_stateTitle->setFont(titleFont);

    m_stateDetail = new QLabel(m_statePanel);
    m_stateDetail->setObjectName(QStringLiteral("askPdfStateDetail"));
    m_stateDetail->setWordWrap(true);
    m_stateDetail->setEnabled(false);

    auto* stateActions = new QHBoxLayout();
    stateActions->setContentsMargins(0, 0, 0, 0);
    m_statePrimary = new QPushButton(m_statePanel);
    m_statePrimary->setObjectName(QStringLiteral("askPdfStatePrimary"));
    m_stateSecondary = new QPushButton(m_statePanel);
    m_stateSecondary->setObjectName(QStringLiteral("askPdfStateSecondary"));
    stateActions->addWidget(m_statePrimary);
    stateActions->addWidget(m_stateSecondary);
    stateActions->addStretch(1);

    stateLayout->addWidget(m_stateTitle);
    stateLayout->addWidget(m_stateDetail);
    stateLayout->addLayout(stateActions);
    m_statePanel->hide();

    m_conversation = new AskPdfConversationView(this);

    auto* inputRow = new QHBoxLayout();
    m_queryEdit = new QPlainTextEdit(this);
    m_queryEdit->setObjectName(QStringLiteral("askPdfQuery"));
    m_queryEdit->setPlaceholderText(
        QStringLiteral("追问当前 PDF（Ctrl+Enter 发送）"));
    m_queryEdit->setMaximumHeight(72);
    m_queryEdit->installEventFilter(this);
    m_sendButton = new QPushButton(QStringLiteral("发送"), this);
    m_sendButton->setObjectName(QStringLiteral("askPdfSend"));
    m_stopButton = new QToolButton(this);
    m_stopButton->setObjectName(QStringLiteral("askPdfStop"));
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopButton->setToolTip(QStringLiteral("停止生成"));
    m_stopButton->hide();
    inputRow->addWidget(m_queryEdit, 1);
    inputRow->addWidget(m_sendButton);
    inputRow->addWidget(m_stopButton);

    layout->addWidget(m_documentLabel);
    layout->addLayout(statusRow);
    layout->addWidget(m_progressBar);
    layout->addWidget(m_statePanel);
    layout->addWidget(m_conversation, 1);
    layout->addLayout(inputRow);

    connect(m_sendButton, &QPushButton::clicked, this,
            &AskPdfWidget::submitQuery);
    connect(m_stopButton, &QToolButton::clicked, this,
            &AskPdfWidget::stopGenerationRequested);
    connect(m_rebuildButton, &QToolButton::clicked, this,
            &AskPdfWidget::rebuildIndexRequested);
    connect(m_cancelButton, &QToolButton::clicked, this,
            &AskPdfWidget::cancelIndexRequested);
    connect(m_clearButton, &QToolButton::clicked, this,
            &AskPdfWidget::clearIndexRequested);
    connect(m_clearConversationButton, &QToolButton::clicked, this,
            &AskPdfWidget::clearConversationRequested);
    connect(m_settingsButton, &QToolButton::clicked, this,
            &AskPdfWidget::settingsRequested);
    connect(m_indexButton, &QToolButton::clicked, this,
            &AskPdfWidget::indexManagementRequested);

    connect(m_statePrimary, &QPushButton::clicked, this, [this]() {
        switch (m_primaryAction) {
            case AskPdfStateAction::OpenSettings:
                emit settingsRequested();
                break;
            case AskPdfStateAction::RebuildIndex:
                emit rebuildIndexRequested();
                break;
            case AskPdfStateAction::FocusQuery:
                m_queryEdit->setFocus();
                m_queryEdit->selectAll();
                break;
            case AskPdfStateAction::RetryAnswer:
                emit retryAnswerRequested();
                break;
            case AskPdfStateAction::RetryQuery:
                emit retryQueryRequested();
                break;
            case AskPdfStateAction::ClearIndex:
                emit clearIndexRequested();
                break;
            case AskPdfStateAction::None:
            default:
                break;
        }
    });
    connect(m_stateSecondary, &QPushButton::clicked, this, [this]() {
        switch (m_secondaryAction) {
            case AskPdfStateAction::OpenSettings:
                emit settingsRequested();
                break;
            case AskPdfStateAction::RebuildIndex:
                emit rebuildIndexRequested();
                break;
            case AskPdfStateAction::FocusQuery:
                m_queryEdit->setFocus();
                m_queryEdit->selectAll();
                break;
            case AskPdfStateAction::ClearIndex:
                emit clearIndexRequested();
                break;
            case AskPdfStateAction::RetryAnswer:
            case AskPdfStateAction::RetryQuery:
            case AskPdfStateAction::None:
            default:
                break;
        }
    });

    connect(m_conversation, &AskPdfConversationView::copyRequested, this,
            [](const QString& text) {
                QGuiApplication::clipboard()->setText(text);
            });
    connect(m_conversation, &AskPdfConversationView::regenerateRequested, this,
            &AskPdfWidget::regenerateTurnRequested);
    connect(m_conversation, &AskPdfConversationView::stopRequested, this,
            &AskPdfWidget::stopGenerationRequested);
    connect(m_conversation, &AskPdfConversationView::citationActivated, this,
            &AskPdfWidget::citationActivated);

    setDocumentContext({});
    setIndexState(RagIndexState::NotIndexed);
}

void AskPdfWidget::setDocumentContext(const DocumentKey& document) {
    const bool changed = m_document.contentHash != document.contentHash ||
                         m_document.canonicalPath != document.canonicalPath;
    m_document = document;
    const QString fileName = QFileInfo(document.canonicalPath).fileName();
    m_documentLabel->setText(fileName.isEmpty() ? QStringLiteral("未打开 PDF")
                                                : fileName);
    if (changed) {
        clearEvidence();
        resetAnswer();
        m_hasSearched = false;
        m_queryFailed = false;
        m_answerFailed = false;
        m_queryError.clear();
        m_answerError.clear();
        refreshStatusText();
        refreshEmptyState();
        refreshProgress();
        updateControlStates();
    }
}

void AskPdfWidget::setIndexState(RagIndexState state, int chunkCount,
                                 const QString& error,
                                 RagEmbeddingState embeddingState,
                                 int embeddedChunkCount,
                                 const QString& embeddingError,
                                 RagIndexFailureReason failureReason,
                                 int processedPages, int totalPages) {
    m_state = state;
    m_failureReason = failureReason;
    m_processedPages = processedPages;
    m_totalPages = totalPages;
    m_chunkCount = chunkCount;
    m_indexError = error;
    m_embeddingState = embeddingState;
    m_embeddedChunkCount = embeddedChunkCount;
    m_embeddingError = embeddingError;
    if (state != RagIndexState::Ready) {
        clearEvidence();
        m_hasSearched = false;
        m_queryFailed = false;
        m_answerFailed = false;
        m_queryError.clear();
        m_answerError.clear();
    }
    refreshStatusText();
    refreshEmptyState();
    refreshProgress();
    updateControlStates();
}

void AskPdfWidget::setAnswerEnabled(bool enabled) {
    if (m_answerEnabled == enabled) {
        return;
    }
    m_answerEnabled = enabled;
    refreshStatusText();
    refreshEmptyState();
}

void AskPdfWidget::setEmbeddingEnabled(bool enabled) {
    if (m_embeddingsEnabled == enabled) {
        return;
    }
    m_embeddingsEnabled = enabled;
    refreshProgress();
}

void AskPdfWidget::refreshStatusText() {
    if (m_clearing) {
        m_statusLabel->setText(QStringLiteral("正在清空索引..."));
        return;
    }
    AskPdfViewInput input;
    input.hasDocument = !m_document.canonicalPath.isEmpty();
    input.indexState = m_state;
    input.failureReason = m_failureReason;
    input.embeddingState = m_embeddingState;
    input.chunkCount = m_chunkCount;
    input.embeddedChunkCount = m_embeddedChunkCount;
    input.embeddingError = m_embeddingError;
    input.answerEnabled = m_answerEnabled;
    input.indexError = m_indexError;
    m_statusLabel->setText(askPdfStatusText(input));
}

void AskPdfWidget::updateControlStates() {
    const bool ready = m_state == RagIndexState::Ready;
    const bool indexing = isIndexingState(m_state);
    const bool canType = ready && !m_clearing;
    const bool canSend = canType && !m_conversationBusy;

    m_queryEdit->setEnabled(canType);
    m_sendButton->setEnabled(canSend);
    m_rebuildButton->setEnabled(!m_clearing && !indexing &&
                                !m_document.canonicalPath.isEmpty());
    m_clearButton->setEnabled(!m_clearing);
    m_clearConversationButton->setEnabled(!m_clearing);
    m_cancelButton->setVisible(indexing && !m_clearing);
    m_stopButton->setVisible(m_conversationBusy && !m_clearing);
}

void AskPdfWidget::setClearing(bool clearing) {
    if (m_clearing == clearing) {
        return;
    }
    m_clearing = clearing;
    refreshStatusText();
    refreshEmptyState();
    refreshProgress();
    updateControlStates();
}

void AskPdfWidget::setEvidence(const QVector<RagEvidence>& evidence) {
    m_evidence = evidence;
    m_hasSearched = true;
    m_queryFailed = false;
    m_queryError.clear();
    refreshEmptyState();
}

void AskPdfWidget::setBusy(bool busy) {
    m_busy = busy;
    refreshEmptyState();
    refreshProgress();
    updateControlStates();
}

void AskPdfWidget::setConversationBusy(bool busy) {
    m_conversationBusy = busy;
    m_stopButton->setVisible(busy && !m_clearing);
    refreshEmptyState();
    updateControlStates();
}

void AskPdfWidget::setConversationModel(RagConversationModel* model) {
    m_conversation->setModel(model);
}

void AskPdfWidget::resetAnswer() {
    m_answerFailed = false;
    m_answerError.clear();
    refreshEmptyState();
}

void AskPdfWidget::setAnswerError(const QString& error) {
    resetAnswer();
    m_queryFailed = false;
    m_queryError.clear();
    m_answerFailed = true;
    m_answerError = error;
    refreshEmptyState();
}

void AskPdfWidget::setQueryError(const QString& error) {
    m_hasSearched = true;
    m_queryFailed = true;
    m_queryError = error;
    m_answerFailed = false;
    m_answerError.clear();
    clearEvidence();
    refreshEmptyState();
}

void AskPdfWidget::clearEvidence() { m_evidence.clear(); }

void AskPdfWidget::submitQuery() {
    const QString query = m_queryEdit->toPlainText().trimmed();
    if (query.isEmpty() || m_state != RagIndexState::Ready ||
        m_conversationBusy || m_clearing) {
        return;
    }
    m_queryFailed = false;
    m_answerFailed = false;
    m_queryError.clear();
    m_answerError.clear();
    clearEvidence();
    m_queryEdit->clear();
    refreshEmptyState();
    emit questionSubmitted(query);
}

bool AskPdfWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_queryEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return ||
             keyEvent->key() == Qt::Key_Enter) &&
            (keyEvent->modifiers() & Qt::ControlModifier)) {
            submitQuery();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AskPdfWidget::refreshEmptyState() {
    if (!m_statePanel) {
        return;
    }
    if (m_clearing) {
        m_statePanel->hide();
        return;
    }

    AskPdfViewInput input;
    input.hasDocument = !m_document.canonicalPath.isEmpty();
    input.indexState = m_state;
    input.failureReason = m_failureReason;
    input.embeddingState = m_embeddingState;
    input.chunkCount = m_chunkCount;
    input.embeddedChunkCount = m_embeddedChunkCount;
    input.embeddingError = m_embeddingError;
    input.busy = m_busy || m_conversationBusy;
    input.hasSearched = m_hasSearched;
    input.hasEvidence = !m_evidence.isEmpty();
    input.answerEnabled = m_answerEnabled;
    input.queryFailed = m_queryFailed;
    input.answerFailed = m_answerFailed;
    input.queryError = m_queryError;
    input.answerError = m_answerError;
    input.indexError = m_indexError;

    const AskPdfStateView view = askPdfStateView(input);
    if (!view.visible) {
        m_statePanel->hide();
        return;
    }

    m_stateTitle->setText(view.title);
    m_stateDetail->setText(view.detail);
    m_stateDetail->setVisible(!view.detail.isEmpty());
    m_primaryAction = view.primaryAction;
    m_secondaryAction = view.secondaryAction;
    m_statePrimary->setText(view.primaryText);
    m_statePrimary->setVisible(!view.primaryText.isEmpty());
    m_stateSecondary->setText(view.secondaryText);
    m_stateSecondary->setVisible(!view.secondaryText.isEmpty());
    m_statePanel->show();
}

void AskPdfWidget::refreshProgress() {
    if (!m_progressBar) {
        return;
    }
    AskPdfProgressInput input;
    input.indexState = m_state;
    input.embeddingState = m_embeddingState;
    input.processedPages = m_processedPages;
    input.totalPages = m_totalPages;
    input.chunkCount = m_chunkCount;
    input.embeddedChunkCount = m_embeddedChunkCount;
    input.embeddingsEnabled = m_embeddingsEnabled;

    const AskPdfProgressView view = askPdfProgressView(input);
    if (!view.visible) {
        m_progressBar->hide();
        return;
    }
    if (view.indeterminate) {
        m_progressBar->setRange(0, 0);
        m_progressBar->setFormat(view.text);
    } else {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(view.percent);
        m_progressBar->setFormat(QStringLiteral("%1  %p%").arg(view.text));
    }
    m_progressBar->show();
}

}  // namespace Rag
