#include "AskPdfConversationView.h"

#include <QScrollBar>
#include <QVBoxLayout>

#include "AskPdfTurnCard.h"
#include "rag/controller/RagConversationModel.h"

namespace Rag {

AskPdfConversationView::AskPdfConversationView(QWidget* parent)
    : QScrollArea(parent) {
    setObjectName(QStringLiteral("askPdfConversation"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget(this);
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(6);
    m_layout->addStretch(1);
    setWidget(m_container);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &AskPdfConversationView::updateStickyFromScroll);
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this](int, int max) {
                if (m_stickyToBottom) {
                    verticalScrollBar()->setValue(max);
                }
            });
}

void AskPdfConversationView::setModel(RagConversationModel* model) {
    if (m_model == model) {
        return;
    }
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex&, int first, int last) {
                    for (int row = first; row <= last; ++row) {
                        appendRow(row);
                    }
                });
        connect(m_model, &QAbstractItemModel::rowsRemoved, this,
                [this]() { rebuild(); });
        connect(m_model, &QAbstractItemModel::modelReset, this,
                [this]() { rebuild(); });
        connect(m_model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& topLeft, const QModelIndex&,
                       const QList<int>&) { updateRow(topLeft.row()); });
    }
    rebuild();
}

void AskPdfConversationView::rebuild() {
    for (AskPdfTurnCard* card : m_cards) {
        if (card) {
            m_layout->removeWidget(card);
            card->hide();
            card->deleteLater();
        }
    }
    m_cards.clear();
    if (!m_model) {
        return;
    }
    for (int row = 0; row < m_model->count(); ++row) {
        appendRow(row);
    }
    scrollToBottom();
}

AskPdfTurnCard* AskPdfConversationView::createCard(int row) {
    if (!m_model || row < 0 || row >= m_model->count()) {
        return nullptr;
    }
    const RagConversationTurn& turn = m_model->turns().at(row);
    auto* card = new AskPdfTurnCard(turn, m_container);
    m_cards.insert(turn.id, card);
    connect(card, &AskPdfTurnCard::copyRequested, this,
            &AskPdfConversationView::copyRequested);
    connect(card, &AskPdfTurnCard::regenerateRequested, this,
            &AskPdfConversationView::regenerateRequested);
    connect(card, &AskPdfTurnCard::stopRequested, this,
            &AskPdfConversationView::stopRequested);
    connect(card, &AskPdfTurnCard::citationActivated, this,
            &AskPdfConversationView::citationActivated);
    connect(card, &AskPdfTurnCard::settingsRequested, this,
            &AskPdfConversationView::settingsRequested);
    return card;
}

void AskPdfConversationView::appendRow(int row) {
    if (!m_model || row < 0 || row >= m_model->count()) {
        return;
    }
    const QString turnId = m_model->turns().at(row).id;
    if (m_cards.contains(turnId)) {
        updateRow(row);
        return;
    }
    AskPdfTurnCard* card = createCard(row);
    if (!card) {
        return;
    }
    m_layout->insertWidget(m_layout->count() - 1, card);
    if (m_stickyToBottom) {
        scrollToBottom();
    }
}

void AskPdfConversationView::updateRow(int row) {
    if (!m_model || row < 0 || row >= m_model->count()) {
        return;
    }
    const RagConversationTurn& turn = m_model->turns().at(row);
    AskPdfTurnCard* card = m_cards.value(turn.id);
    if (card) {
        card->setTurn(turn);
        if (m_stickyToBottom) {
            scrollToBottom();
        }
    }
}

void AskPdfConversationView::updateStickyFromScroll(int value) {
    m_stickyToBottom = value >= verticalScrollBar()->maximum() - 4;
}

void AskPdfConversationView::scrollToBottom() {
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

}  // namespace Rag
