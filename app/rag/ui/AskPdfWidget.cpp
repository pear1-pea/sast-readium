#include "AskPdfWidget.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace Rag {
namespace {

bool isBusyState(RagIndexState state) {
    return state == RagIndexState::CheckingCache ||
           state == RagIndexState::Extracting ||
           state == RagIndexState::Chunking || state == RagIndexState::Indexing;
}

QString evidenceLabel(const RagEvidence& evidence) {
    QString text = evidence.text.simplified();
    constexpr int kPreviewLength = 180;
    if (text.size() > kPreviewLength) {
        text = text.left(kPreviewLength - 3) + QStringLiteral("...");
    }
    return QStringLiteral("第 %1 页\n%2").arg(evidence.pageStart + 1).arg(text);
}

QString citationPageLabel(const RagCitation& citation) {
    if (citation.pageStart < 0) {
        return QStringLiteral("未知页码");
    }
    if (citation.pageEnd > citation.pageStart) {
        return QStringLiteral("第 %1–%2 页")
            .arg(citation.pageStart + 1)
            .arg(citation.pageEnd + 1);
    }
    return QStringLiteral("第 %1 页").arg(citation.pageStart + 1);
}

QString citationLabel(const RagCitation& citation) {
    QString label = citationPageLabel(citation);
    const QString section = citation.sectionPath.trimmed();
    if (!section.isEmpty()) {
        label += QStringLiteral(" · ") + section;
    }
    const QString snippet = citation.snippet.simplified();
    if (!snippet.isEmpty()) {
        label += QStringLiteral("\n") + snippet;
    }
    return label;
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

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("askPdfProgress"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->hide();

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

    auto* queryRow = new QHBoxLayout();
    m_queryEdit = new QLineEdit(this);
    m_queryEdit->setPlaceholderText(QStringLiteral("询问当前 PDF"));
    m_queryEdit->setClearButtonEnabled(true);
    queryRow->addWidget(m_queryEdit, 1);

    m_submitButton = new QToolButton(this);
    m_submitButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_submitButton->setToolTip(QStringLiteral("搜索"));
    queryRow->addWidget(m_submitButton);
    m_stopButton = new QToolButton(this);
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopButton->setToolTip(QStringLiteral("停止回答"));
    m_stopButton->hide();
    queryRow->addWidget(m_stopButton);

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

    m_answerEdit = new QPlainTextEdit(this);
    m_answerEdit->setReadOnly(true);
    m_answerEdit->setMaximumHeight(240);
    m_answerEdit->hide();
    m_citationList = new QListWidget(this);
    m_citationList->setObjectName(QStringLiteral("citationList"));
    m_citationList->setWordWrap(true);
    m_citationList->setSpacing(4);
    m_citationList->setMaximumHeight(160);
    m_citationList->hide();

    m_evidenceList = new QListWidget(this);
    m_evidenceList->setWordWrap(true);
    m_evidenceList->setSpacing(4);
    m_evidenceList->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(m_documentLabel);
    layout->addLayout(statusRow);
    layout->addWidget(m_progressBar);
    layout->addLayout(queryRow);
    layout->addWidget(m_statePanel);
    layout->addWidget(m_answerEdit);
    layout->addWidget(m_citationList);
    layout->addWidget(m_evidenceList, 1);

    connect(m_queryEdit, &QLineEdit::returnPressed, this,
            &AskPdfWidget::submitQuery);
    connect(m_submitButton, &QToolButton::clicked, this,
            &AskPdfWidget::submitQuery);
    connect(m_rebuildButton, &QToolButton::clicked, this,
            &AskPdfWidget::rebuildIndexRequested);
    connect(m_cancelButton, &QToolButton::clicked, this,
            &AskPdfWidget::cancelIndexRequested);
    connect(m_clearButton, &QToolButton::clicked, this,
            &AskPdfWidget::clearIndexRequested);
    connect(m_settingsButton, &QToolButton::clicked, this,
            &AskPdfWidget::settingsRequested);
    connect(m_indexButton, &QToolButton::clicked, this,
            &AskPdfWidget::indexManagementRequested);
    connect(m_stopButton, &QToolButton::clicked, this,
            &AskPdfWidget::cancelAnswerRequested);
    connect(m_statePrimary, &QPushButton::clicked, this,
            [this]() { runStateAction(m_primaryAction); });
    connect(m_stateSecondary, &QPushButton::clicked, this,
            [this]() { runStateAction(m_secondaryAction); });
    connect(m_citationList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
                emit citationActivated(item->data(Qt::UserRole).toString());
            });
    connect(m_citationList, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) {
                emit citationActivated(item->data(Qt::UserRole).toString());
            });
    connect(m_evidenceList, &QListWidget::itemActivated, this,
            &AskPdfWidget::activateItem);
    connect(m_evidenceList, &QListWidget::itemClicked, this,
            &AskPdfWidget::activateItem);

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
        refreshEmptyState();
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

void AskPdfWidget::setEmbeddingEnabled(bool enabled) {
    if (m_embeddingsEnabled == enabled) {
        return;
    }
    m_embeddingsEnabled = enabled;
    refreshProgress();
}

void AskPdfWidget::setAnswerEnabled(bool enabled) {
    if (m_answerEnabled == enabled) {
        return;
    }
    m_answerEnabled = enabled;
    refreshStatusText();
    refreshEmptyState();
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
    const bool indexing = isBusyState(m_state);
    const bool canQuery = ready && !m_busy && !m_clearing;

    m_queryEdit->setEnabled(canQuery);
    m_submitButton->setEnabled(canQuery);
    m_rebuildButton->setEnabled(!m_clearing && !indexing &&
                                !m_document.canonicalPath.isEmpty());
    m_clearButton->setEnabled(!m_clearing);
    m_cancelButton->setVisible(indexing && !m_clearing);
}

void AskPdfWidget::setClearing(bool clearing) {
    if (m_clearing == clearing) {
        return;
    }
    m_clearing = clearing;
    refreshStatusText();
    refreshEmptyState();
    updateControlStates();
}

void AskPdfWidget::setEvidence(const QVector<RagEvidence>& evidence) {
    m_evidence = evidence;
    m_hasSearched = true;
    m_queryFailed = false;
    m_queryError.clear();
    m_evidenceList->clear();
    for (const RagEvidence& item : m_evidence) {
        auto* listItem = new QListWidgetItem(evidenceLabel(item));
        listItem->setData(Qt::UserRole, item.rank);
        listItem->setToolTip(item.text);
        m_evidenceList->addItem(listItem);
    }
    refreshEmptyState();
}

void AskPdfWidget::setBusy(bool busy) {
    m_busy = busy;
    m_stopButton->setVisible(busy);
    refreshEmptyState();
    refreshProgress();
    updateControlStates();
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

void AskPdfWidget::resetAnswer() {
    m_answerEdit->clear();
    m_answerEdit->hide();
    m_citationList->clear();
    m_citationList->hide();
    m_answerFailed = false;
    m_answerError.clear();
    refreshEmptyState();
}
void AskPdfWidget::appendAnswer(const QString& text) {
    m_answerFailed = false;
    m_answerEdit->show();
    auto cursor = m_answerEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    m_answerEdit->setTextCursor(cursor);
}
void AskPdfWidget::setAnswer(const RagAnswer& answer) {
    m_answerFailed = false;
    m_answerError.clear();
    m_answerEdit->setPlainText(answer.text);
    m_answerEdit->show();
    m_citationList->clear();
    for (const auto& citation : answer.citations) {
        auto* item = new QListWidgetItem(citationLabel(citation));
        item->setData(Qt::UserRole, citation.chunkId);
        item->setToolTip(citation.snippet);
        m_citationList->addItem(item);
    }
    m_citationList->setVisible(!answer.citations.isEmpty());
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

void AskPdfWidget::clearEvidence() {
    m_evidence.clear();
    m_evidenceList->clear();
}

void AskPdfWidget::submitQuery() {
    const QString query = m_queryEdit->text().trimmed();
    if (!query.isEmpty() && m_state == RagIndexState::Ready) {
        m_hasSearched = true;
        m_queryFailed = false;
        m_answerFailed = false;
        m_queryError.clear();
        m_answerError.clear();
        clearEvidence();
        refreshEmptyState();
        emit querySubmitted(query);
    }
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
    input.busy = m_busy;
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

void AskPdfWidget::runStateAction(AskPdfStateAction action) {
    switch (action) {
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
}

void AskPdfWidget::activateItem(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    const int row = m_evidenceList->row(item);
    if (row >= 0 && row < m_evidence.size()) {
        emit evidenceActivated(m_evidence.at(row));
    }
}

}  // namespace Rag
