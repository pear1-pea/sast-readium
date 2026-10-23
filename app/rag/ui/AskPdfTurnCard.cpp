#include "AskPdfTurnCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace Rag {

AskPdfTurnCard::AskPdfTurnCard(const RagConversationTurn& turn, QWidget* parent)
    : QFrame(parent), m_turn(turn) {
    setObjectName(QStringLiteral("askPdfTurnCard"));
    setFrameShape(QFrame::StyledPanel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_question = new QLabel(this);
    m_question->setObjectName(QStringLiteral("turnQuestion"));
    m_question->setWordWrap(true);
    m_question->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont questionFont = m_question->font();
    questionFont.setBold(true);
    m_question->setFont(questionFont);

    m_answer = new QLabel(this);
    m_answer->setObjectName(QStringLiteral("turnAnswer"));
    m_answer->setWordWrap(true);
    m_answer->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("turnStatus"));
    m_status->setWordWrap(true);

    m_citations = new QListWidget(this);
    m_citations->setObjectName(QStringLiteral("turnCitationList"));
    m_citations->setWordWrap(true);
    m_citations->setSpacing(2);
    m_citations->setMaximumHeight(120);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    m_copyButton = new QPushButton(QStringLiteral("复制"), this);
    m_copyButton->setObjectName(QStringLiteral("turnCopyButton"));
    m_regenerateButton = new QPushButton(QStringLiteral("重新生成"), this);
    m_regenerateButton->setObjectName(QStringLiteral("turnRegenerateButton"));
    m_stopButton = new QPushButton(QStringLiteral("停止"), this);
    m_stopButton->setObjectName(QStringLiteral("turnStopButton"));
    m_retryButton = new QPushButton(QStringLiteral("重试"), this);
    m_retryButton->setObjectName(QStringLiteral("turnRetryButton"));
    actions->addWidget(m_copyButton);
    actions->addWidget(m_regenerateButton);
    actions->addWidget(m_stopButton);
    actions->addWidget(m_retryButton);
    actions->addStretch(1);

    layout->addWidget(m_question);
    layout->addWidget(m_answer);
    layout->addWidget(m_status);
    layout->addWidget(m_citations);
    layout->addLayout(actions);

    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        if (!m_turn.answerText.isEmpty()) {
            emit copyRequested(m_turn.answerText);
        }
    });
    connect(m_regenerateButton, &QPushButton::clicked, this,
            [this]() { emit regenerateRequested(m_turn.id); });
    connect(m_retryButton, &QPushButton::clicked, this,
            [this]() { emit regenerateRequested(m_turn.id); });
    connect(m_stopButton, &QPushButton::clicked, this,
            &AskPdfTurnCard::stopRequested);
    connect(m_citations, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
                if (item) {
                    emit citationActivated(item->data(Qt::UserRole).toString());
                }
            });

    refresh();
}

void AskPdfTurnCard::setTurn(const RagConversationTurn& turn) {
    m_turn = turn;
    refresh();
}

void AskPdfTurnCard::refresh() {
    m_question->setText(m_turn.question);
    m_answer->setText(m_turn.answerText);
    m_answer->setVisible(!m_turn.answerText.isEmpty());

    QString status;
    bool error = false;
    switch (m_turn.state) {
        case RagTurnState::Retrieving:
            status = QStringLiteral("正在检索证据…");
            break;
        case RagTurnState::Generating:
            if (m_turn.answerText.isEmpty()) {
                status = QStringLiteral("正在生成回答…");
            }
            break;
        case RagTurnState::Failed:
            if (m_turn.error.isEmpty()) {
                status = QStringLiteral("回答生成失败。");
            } else if (m_turn.failure == RagChatFailure::None) {
                status = m_turn.error;
            } else {
                status = QStringLiteral("回答生成失败：%1").arg(m_turn.error);
            }
            error = true;
            break;
        case RagTurnState::Cancelled:
            status = QStringLiteral("已停止，可重试。");
            break;
        case RagTurnState::Ready:
            break;
    }
    m_status->setText(status);
    m_status->setVisible(!status.isEmpty());
    m_status->setStyleSheet(error ? QStringLiteral("color: #b42318;")
                                  : QStringLiteral("color: #8a6d3b;"));

    refreshCitations();

    m_copyButton->setVisible(!m_turn.answerText.isEmpty());
    m_regenerateButton->setVisible(m_turn.state == RagTurnState::Ready);
    m_retryButton->setVisible(m_turn.state == RagTurnState::Failed ||
                              m_turn.state == RagTurnState::Cancelled);
    m_stopButton->setVisible(m_turn.state == RagTurnState::Retrieving ||
                             m_turn.state == RagTurnState::Generating);
}

void AskPdfTurnCard::refreshCitations() {
    m_citations->clear();
    for (const RagCitation& citation : m_turn.citations) {
        auto* item = new QListWidgetItem(citationLabel(citation), m_citations);
        item->setData(Qt::UserRole, citation.chunkId);
        item->setToolTip(citation.snippet);
    }
    m_citations->setVisible(!m_turn.citations.isEmpty());
}

QString AskPdfTurnCard::citationLabel(const RagCitation& citation) {
    QString label;
    if (citation.pageStart < 0) {
        label = QStringLiteral("未知页码");
    } else if (citation.pageEnd > citation.pageStart) {
        label = QStringLiteral("第 %1–%2 页")
                    .arg(citation.pageStart + 1)
                    .arg(citation.pageEnd + 1);
    } else {
        label = QStringLiteral("第 %1 页").arg(citation.pageStart + 1);
    }
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

}  // namespace Rag
