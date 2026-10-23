#pragma once

#include <QFrame>

#include "rag/domain/RagConversation.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace Rag {

class AskPdfTurnCard : public QFrame {
    Q_OBJECT

public:
    explicit AskPdfTurnCard(const RagConversationTurn& turn,
                            QWidget* parent = nullptr);

    void setTurn(const RagConversationTurn& turn);
    QString turnId() const { return m_turn.id; }

signals:
    void copyRequested(const QString& text);
    void regenerateRequested(const QString& turnId);
    void stopRequested();
    void citationActivated(const QString& chunkId);

private:
    void refresh();
    void refreshCitations();
    static QString citationLabel(const RagCitation& citation);

    RagConversationTurn m_turn;
    QLabel* m_question = nullptr;
    QLabel* m_answer = nullptr;
    QLabel* m_status = nullptr;
    QListWidget* m_citations = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_regenerateButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_retryButton = nullptr;
};

}  // namespace Rag
