#include "AskPdfTurnCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace Rag {
namespace {

constexpr int kCollapseCharacters = 600;
constexpr int kCollapseLines = 12;
constexpr int kPreviewCharacters = 300;

bool isLongAnswer(const QString& text) {
    return text.size() > kCollapseCharacters ||
           text.count(QLatin1Char('\n')) > kCollapseLines;
}

QString failureText(const RagConversationTurn& turn) {
    switch (turn.failure) {
        case RagChatFailure::Timeout:
            return QStringLiteral("回答生成超时，模型可能正在加载。");
        case RagChatFailure::Unreachable:
            return QStringLiteral("无法连接本地 Ollama，请确认服务已启动。");
        case RagChatFailure::ModelMissing:
            return QStringLiteral("Chat 模型不存在，请先在 Ollama 中安装。");
        case RagChatFailure::BudgetEvidence:
            return QStringLiteral("证据超出上下文预算，未能生成回答。");
        case RagChatFailure::ContextTooSmall:
            return QStringLiteral("模型上下文窗口不足，请换用更大的模型。");
        case RagChatFailure::Rejected:
        case RagChatFailure::Unknown:
            return turn.error.isEmpty()
                       ? QStringLiteral("回答生成失败。")
                       : QStringLiteral("回答生成失败：%1").arg(turn.error);
        case RagChatFailure::Cancelled:
            return QStringLiteral("已停止，可重试。");
        case RagChatFailure::None:
        default:
            return turn.error.isEmpty() ? QStringLiteral("回答生成失败。")
                                        : turn.error;
    }
}

}  // namespace

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

    m_answer = new QTextBrowser(this);
    m_answer->setObjectName(QStringLiteral("turnAnswer"));
    m_answer->setOpenLinks(false);
    m_answer->setFrameShape(QFrame::NoFrame);
    m_answer->setMaximumHeight(320);
    m_answer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_skeleton = new QLabel(QStringLiteral("▬▬▬▬▬▬▬▬▬▬▬▬\n▬▬▬▬▬▬▬▬▬▬"), this);
    m_skeleton->setObjectName(QStringLiteral("turnSkeleton"));
    m_skeleton->setEnabled(false);
    m_skeleton->hide();

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("turnStatus"));
    m_status->setWordWrap(true);

    m_citations = new QListWidget(this);
    m_citations->setObjectName(QStringLiteral("turnCitationList"));
    m_citations->setWordWrap(true);
    m_citations->setSpacing(2);
    m_citations->setMaximumHeight(120);
    m_citations->hide();

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    m_expandButton = new QPushButton(this);
    m_expandButton->setObjectName(QStringLiteral("turnExpandButton"));
    m_copyButton = new QPushButton(QStringLiteral("复制"), this);
    m_copyButton->setObjectName(QStringLiteral("turnCopyButton"));
    m_regenerateButton = new QPushButton(QStringLiteral("重新生成"), this);
    m_regenerateButton->setObjectName(QStringLiteral("turnRegenerateButton"));
    m_stopButton = new QPushButton(QStringLiteral("停止"), this);
    m_stopButton->setObjectName(QStringLiteral("turnStopButton"));
    m_retryButton = new QPushButton(QStringLiteral("重试"), this);
    m_retryButton->setObjectName(QStringLiteral("turnRetryButton"));
    m_settingsButton = new QPushButton(QStringLiteral("检查本地模型"), this);
    m_settingsButton->setObjectName(QStringLiteral("turnSettingsButton"));
    actions->addWidget(m_expandButton);
    actions->addWidget(m_copyButton);
    actions->addWidget(m_regenerateButton);
    actions->addWidget(m_stopButton);
    actions->addWidget(m_retryButton);
    actions->addWidget(m_settingsButton);
    actions->addStretch(1);

    layout->addWidget(m_question);
    layout->addWidget(m_skeleton);
    layout->addWidget(m_answer);
    layout->addWidget(m_status);
    layout->addWidget(m_citations);
    layout->addLayout(actions);

    connect(m_expandButton, &QPushButton::clicked, this, [this]() {
        m_expanded = !m_expanded;
        refreshAnswer();
    });
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
    connect(m_settingsButton, &QPushButton::clicked, this,
            &AskPdfTurnCard::settingsRequested);
    connect(m_citations, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
                if (item) {
                    emit citationActivated(item->data(Qt::UserRole).toString());
                }
            });
    connect(m_answer, &QTextBrowser::anchorClicked, this,
            &AskPdfTurnCard::onAnchorClicked);

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(60);
    connect(&m_refreshTimer, &QTimer::timeout, this, &AskPdfTurnCard::refresh);

    refresh();
}

void AskPdfTurnCard::setTurn(const RagConversationTurn& turn) {
    const RagTurnState previous = m_turn.state;
    m_turn = turn;
    const bool sameStreamingState = previous == RagTurnState::Generating &&
                                    turn.state == RagTurnState::Generating;
    if (sameStreamingState) {
        if (!m_refreshTimer.isActive()) {
            m_refreshTimer.start();
        }
        return;
    }
    m_refreshTimer.stop();
    refresh();
}

void AskPdfTurnCard::refresh() {
    m_question->setText(m_turn.question);

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
            status = failureText(m_turn);
            error = true;
            break;
        case RagTurnState::Cancelled:
            status = QStringLiteral("已停止，可重试。");
            break;
        case RagTurnState::Ready:
            break;
    }
    const bool loading = m_turn.state == RagTurnState::Retrieving ||
                         (m_turn.state == RagTurnState::Generating &&
                          m_turn.answerText.isEmpty());
    m_skeleton->setVisible(loading);

    m_status->setText(status);
    m_status->setVisible(!status.isEmpty());
    m_status->setStyleSheet(error ? QStringLiteral("color: #b42318;")
                                  : QStringLiteral("color: #8a6d3b;"));

    refreshAnswer();
    refreshCitations();

    const bool hasAnswer = !m_turn.answerText.isEmpty();
    m_copyButton->setVisible(hasAnswer);
    m_regenerateButton->setVisible(m_turn.state == RagTurnState::Ready);
    m_retryButton->setVisible(m_turn.state == RagTurnState::Failed ||
                              m_turn.state == RagTurnState::Cancelled);
    m_stopButton->setVisible(m_turn.state == RagTurnState::Retrieving ||
                             m_turn.state == RagTurnState::Generating);
    m_settingsButton->setVisible(
        m_turn.failure == RagChatFailure::Unreachable ||
        m_turn.failure == RagChatFailure::ModelMissing ||
        m_turn.failure == RagChatFailure::BudgetEvidence ||
        m_turn.failure == RagChatFailure::ContextTooSmall);
}

void AskPdfTurnCard::refreshAnswer() {
    const QString plain = m_turn.answerText;
    if (plain.isEmpty()) {
        m_answer->hide();
        m_expandButton->hide();
        return;
    }

    const bool longAnswer = isLongAnswer(plain);
    const bool markdownReady =
        m_turn.state == RagTurnState::Ready && !m_turn.answerMarkdown.isEmpty();
    const bool showMarkdown = markdownReady && (m_expanded || !longAnswer);

    m_answer->show();
    if (showMarkdown) {
        m_answer->setMarkdown(m_turn.answerMarkdown);
    } else if (longAnswer && !m_expanded &&
               m_turn.state == RagTurnState::Ready) {
        m_answer->setPlainText(plain.left(kPreviewCharacters) +
                               QStringLiteral("…"));
    } else {
        m_answer->setPlainText(plain);
    }

    m_expandButton->setVisible(longAnswer &&
                               m_turn.state == RagTurnState::Ready);
    m_expandButton->setText(m_expanded ? QStringLiteral("收起")
                                       : QStringLiteral("展开全文"));
}

void AskPdfTurnCard::refreshCitations() {
    QStringList ids;
    ids.reserve(m_turn.citations.size());
    for (const RagCitation& citation : m_turn.citations) {
        ids.append(citation.chunkId);
    }
    if (ids == m_citationIds) {
        return;
    }
    m_citationIds = ids;
    m_citations->clear();
    for (const RagCitation& citation : m_turn.citations) {
        auto* item = new QListWidgetItem(citationLabel(citation), m_citations);
        item->setData(Qt::UserRole, citation.chunkId);
        item->setToolTip(citation.snippet);
    }
    m_citations->setVisible(!m_turn.citations.isEmpty());
}

void AskPdfTurnCard::onAnchorClicked(const QUrl& url) {
    const QString scheme = QStringLiteral("readium-cite:");
    const QString target = url.toString();
    if (!target.startsWith(scheme)) {
        return;
    }
    const QString chunkId =
        QUrl::fromPercentEncoding(target.mid(scheme.size()).toUtf8());
    for (const RagCitation& citation : m_turn.citations) {
        if (citation.chunkId == chunkId) {
            emit citationActivated(chunkId);
            return;
        }
    }
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
