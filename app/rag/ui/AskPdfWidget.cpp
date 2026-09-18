#include "AskPdfWidget.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
    m_queryEdit->setPlaceholderText(QStringLiteral("搜索当前 PDF 的证据"));
    m_queryEdit->setClearButtonEnabled(true);
    queryRow->addWidget(m_queryEdit, 1);

    m_submitButton = new QToolButton(this);
    m_submitButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_submitButton->setToolTip(QStringLiteral("搜索"));
    queryRow->addWidget(m_submitButton);

    m_evidenceList = new QListWidget(this);
    m_evidenceList->setWordWrap(true);
    m_evidenceList->setSpacing(4);
    m_evidenceList->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(m_documentLabel);
    layout->addLayout(statusRow);
    layout->addLayout(queryRow);
    layout->addWidget(m_evidenceList, 1);

    connect(m_queryEdit, &QLineEdit::returnPressed, this,
            &AskPdfWidget::submitQuery);
    connect(m_submitButton, &QToolButton::clicked, this,
            &AskPdfWidget::submitQuery);
    connect(m_rebuildButton, &QToolButton::clicked, this,
            &AskPdfWidget::rebuildIndexRequested);
    connect(m_cancelButton, &QToolButton::clicked, this,
            &AskPdfWidget::cancelIndexRequested);
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
    }
}

void AskPdfWidget::setIndexState(RagIndexState state, int chunkCount,
                                 const QString& error) {
    m_state = state;
    m_statusLabel->setText(stateText(state, chunkCount, error));
    const bool ready = state == RagIndexState::Ready;
    const bool busy = isBusyState(state);
    m_queryEdit->setEnabled(ready);
    m_submitButton->setEnabled(ready);
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
    m_queryEdit->setEnabled(!busy && m_state == RagIndexState::Ready);
    m_submitButton->setEnabled(!busy && m_state == RagIndexState::Ready);
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
                                const QString& error) {
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
