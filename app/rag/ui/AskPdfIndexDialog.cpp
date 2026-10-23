#include "AskPdfIndexDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include "rag/ui/AskPdfState.h"

namespace Rag {

AskPdfIndexDialog::AskPdfIndexDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Ask PDF 索引管理"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* current = new QGroupBox(QStringLiteral("当前文档"), this);
    auto* currentLayout = new QVBoxLayout(current);
    m_documentLabel = new QLabel(current);
    m_documentLabel->setWordWrap(true);
    m_statusLabel = new QLabel(current);
    m_statusLabel->setWordWrap(true);
    m_detailsLabel = new QLabel(current);
    m_detailsLabel->setWordWrap(true);
    m_messageLabel = new QLabel(current);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->hide();

    auto* actions = new QHBoxLayout();
    m_rebuildButton = new QPushButton(QStringLiteral("重建当前文档"), current);
    m_rebuildButton->setObjectName(QStringLiteral("indexRebuildButton"));
    m_deleteButton =
        new QPushButton(QStringLiteral("删除当前文档索引"), current);
    m_deleteButton->setObjectName(QStringLiteral("indexDeleteButton"));
    actions->addWidget(m_rebuildButton);
    actions->addWidget(m_deleteButton);
    actions->addStretch(1);

    currentLayout->addWidget(m_documentLabel);
    currentLayout->addWidget(m_statusLabel);
    currentLayout->addWidget(m_detailsLabel);
    currentLayout->addWidget(m_messageLabel);
    currentLayout->addLayout(actions);

    auto* global = new QGroupBox(QStringLiteral("全部索引"), this);
    auto* globalLayout = new QVBoxLayout(global);
    m_globalLabel = new QLabel(global);
    m_globalLabel->setWordWrap(true);
    m_clearButton = new QPushButton(QStringLiteral("清空全部索引"), global);
    m_clearButton->setObjectName(QStringLiteral("indexClearButton"));
    globalLayout->addWidget(m_globalLabel);
    globalLayout->addWidget(m_clearButton);

    layout->addWidget(current);
    layout->addWidget(global);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    box->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    layout->addWidget(box);

    connect(m_rebuildButton, &QPushButton::clicked, this,
            &AskPdfIndexDialog::rebuildRequested);
    connect(m_deleteButton, &QPushButton::clicked, this,
            &AskPdfIndexDialog::deleteRequested);
    connect(m_clearButton, &QPushButton::clicked, this,
            &AskPdfIndexDialog::clearAllRequested);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshView();
}

void AskPdfIndexDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    emit refreshRequested();
}

void AskPdfIndexDialog::setDocument(const DocumentKey& document) {
    m_document = document;
    refreshView();
}

void AskPdfIndexDialog::setSnapshot(const RagDocumentSnapshot& snapshot) {
    m_snapshot = snapshot;
    refreshView();
}

void AskPdfIndexDialog::setStats(const RagIndexStats& stats) {
    m_stats = stats;
    refreshView();
}

void AskPdfIndexDialog::setBusy(bool busy) {
    m_busy = busy;
    refreshView();
}

void AskPdfIndexDialog::showError(const QString& error) {
    m_messageLabel->setText(error);
    m_messageLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
    m_messageLabel->show();
}

void AskPdfIndexDialog::showInfo(const QString& message) {
    m_messageLabel->setText(message);
    m_messageLabel->setStyleSheet(QString());
    m_messageLabel->show();
}

void AskPdfIndexDialog::refreshView() {
    const QString fileName = QFileInfo(m_document.canonicalPath).fileName();
    m_documentLabel->setText(fileName.isEmpty()
                                 ? QStringLiteral("未打开 PDF")
                                 : QStringLiteral("文件：%1").arg(fileName));

    AskPdfViewInput input;
    input.hasDocument = !m_document.canonicalPath.isEmpty();
    input.indexState = m_snapshot.state;
    input.failureReason = m_snapshot.failureReason;
    input.embeddingState = m_snapshot.embeddingState;
    input.answerEnabled = true;
    input.indexError = m_snapshot.error;
    m_statusLabel->setText(askPdfStatusText(input));

    const int pages = m_snapshot.totalPages > 0 ? m_snapshot.totalPages
                                                : m_document.pageCount;
    const int chunks = m_snapshot.chunkCount > 0 ? m_snapshot.chunkCount
                                                 : m_stats.current.chunkCount;
    const int vectors = m_stats.current.vectorCount;
    const qint64 documentBytes =
        m_stats.current.textBytes + m_stats.current.vectorBytes;
    m_detailsLabel->setText(
        QStringLiteral("页数：%1  ·  文本块：%2  ·  向量：%3\n"
                       "当前文档占用（文本 + 向量）：%4")
            .arg(pages)
            .arg(chunks)
            .arg(vectors)
            .arg(formatBytes(documentBytes)));

    m_globalLabel->setText(QStringLiteral("文档数：%1  ·  索引文件占用：%2")
                               .arg(m_stats.documentCount)
                               .arg(formatBytes(m_stats.databaseBytes)));

    const bool indexing = m_snapshot.state == RagIndexState::CheckingCache ||
                          m_snapshot.state == RagIndexState::Extracting ||
                          m_snapshot.state == RagIndexState::Chunking ||
                          m_snapshot.state == RagIndexState::Indexing;
    const bool hasDocument = !m_document.canonicalPath.isEmpty();
    m_rebuildButton->setEnabled(hasDocument && !m_busy && !indexing);
    m_deleteButton->setEnabled(hasDocument && !m_busy && !indexing &&
                               (chunks > 0 || m_stats.current.present));
    m_clearButton->setEnabled(!m_busy);
}

QString AskPdfIndexDialog::formatBytes(qint64 bytes) {
    const double value = double(bytes);
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(value / 1024.0, 0, 'f', 1);
    }
    if (bytes < qint64(1024) * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(value / (1024.0 * 1024.0), 0, 'f',
                                           2);
    }
    return QStringLiteral("%1 GB").arg(value / (1024.0 * 1024.0 * 1024.0), 0,
                                       'f', 2);
}

}  // namespace Rag
