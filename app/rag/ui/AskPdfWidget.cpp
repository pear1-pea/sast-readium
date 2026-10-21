#include "AskPdfWidget.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
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
    m_statusLabel->setWordWrap(true);
    statusRow->addWidget(m_statusLabel, 1);

    m_rebuildButton = new QToolButton(this);
    m_rebuildButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_rebuildButton->setToolTip(QStringLiteral("重建索引"));
    statusRow->addWidget(m_rebuildButton);

    m_cancelButton = new QToolButton(this);
    m_cancelButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelButton->setToolTip(QStringLiteral("取消索引"));
    statusRow->addWidget(m_cancelButton);

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
    layout->addLayout(queryRow);
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
    connect(m_stopButton, &QToolButton::clicked, this,
            &AskPdfWidget::cancelAnswerRequested);
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
        setEvidence({});
        resetAnswer();
    }
}

void AskPdfWidget::setIndexState(RagIndexState state, int chunkCount,
                                 const QString& error,
                                 RagEmbeddingState embeddingState,
                                 int embeddedChunkCount,
                                 const QString& embeddingError) {
    m_state = state;
    m_statusLabel->setText(stateText(state, chunkCount, error, embeddingState,
                                     embeddedChunkCount, embeddingError));
    const bool ready = state == RagIndexState::Ready;
    const bool busy = isBusyState(state);
    m_queryEdit->setEnabled(ready && !m_busy);
    m_submitButton->setEnabled(ready && !m_busy);
    m_rebuildButton->setEnabled(!m_document.canonicalPath.isEmpty() && !busy);
    m_cancelButton->setVisible(busy);
}

void AskPdfWidget::setEvidence(const QVector<RagEvidence>& evidence) {
    m_evidence = evidence;
    m_evidenceList->clear();
    for (const RagEvidence& item : m_evidence) {
        auto* listItem = new QListWidgetItem(evidenceLabel(item));
        listItem->setData(Qt::UserRole, item.rank);
        listItem->setToolTip(item.text);
        m_evidenceList->addItem(listItem);
    }
}

void AskPdfWidget::setBusy(bool busy) {
    m_busy = busy;
    m_stopButton->setVisible(busy);
    m_queryEdit->setEnabled(!busy && m_state == RagIndexState::Ready);
    m_submitButton->setEnabled(!busy && m_state == RagIndexState::Ready);
}

void AskPdfWidget::resetAnswer() {
    m_answerEdit->clear();
    m_answerEdit->hide();
    m_citationList->clear();
    m_citationList->hide();
}
void AskPdfWidget::appendAnswer(const QString& text) {
    m_answerEdit->show();
    auto cursor = m_answerEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    m_answerEdit->setTextCursor(cursor);
}
void AskPdfWidget::setAnswer(const RagAnswer& answer) {
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
}
void AskPdfWidget::setAnswerError(const QString& error) {
    resetAnswer();
    m_answerEdit->setPlainText(QStringLiteral("回答生成失败：%1").arg(error));
    m_answerEdit->show();
}

void AskPdfWidget::submitQuery() {
    const QString query = m_queryEdit->text().trimmed();
    if (!query.isEmpty() && m_state == RagIndexState::Ready) {
        emit querySubmitted(query);
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

QString AskPdfWidget::stateText(RagIndexState state, int chunkCount,
                                const QString& error,
                                RagEmbeddingState embeddingState,
                                int embeddedChunkCount,
                                const QString& embeddingError) {
    switch (state) {
        case RagIndexState::CheckingCache:
            return QStringLiteral("正在检查索引缓存...");
        case RagIndexState::Extracting:
            return QStringLiteral("正在提取 PDF 文本...");
        case RagIndexState::Chunking:
            return QStringLiteral("正在切分文本...");
        case RagIndexState::Indexing:
            return QStringLiteral("正在建立搜索索引...");
        case RagIndexState::Ready:
            if (embeddingState == RagEmbeddingState::Pending ||
                embeddingState == RagEmbeddingState::Running) {
                return QStringLiteral("索引就绪，正在准备语义检索 %1/%2")
                    .arg(embeddedChunkCount)
                    .arg(chunkCount);
            }
            if (embeddingState == RagEmbeddingState::Failed &&
                !embeddingError.isEmpty()) {
                return QStringLiteral("索引就绪，语义检索失败：%1")
                    .arg(embeddingError);
            }
            return QStringLiteral("索引就绪，%1 个证据片段").arg(chunkCount);
        case RagIndexState::Cancelled:
            return QStringLiteral("索引已取消");
        case RagIndexState::Failed:
            return error.isEmpty() ? QStringLiteral("索引失败") : error;
        case RagIndexState::Unsupported:
            return error.isEmpty() ? QStringLiteral("当前 PDF 无可提取文本")
                                   : error;
        case RagIndexState::NotIndexed:
        default:
            return QStringLiteral("等待建立索引");
    }
}

}  // namespace Rag
