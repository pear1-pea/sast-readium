#pragma once

#include <QHash>
#include <QPointer>
#include <QScrollArea>

class QVBoxLayout;
class QWidget;

namespace Rag {

class AskPdfTurnCard;
class RagConversationModel;

class AskPdfConversationView : public QScrollArea {
    Q_OBJECT

public:
    explicit AskPdfConversationView(QWidget* parent = nullptr);

    void setModel(RagConversationModel* model);
    void scrollToBottom();

signals:
    void copyRequested(const QString& text);
    void regenerateRequested(const QString& turnId);
    void stopRequested();
    void citationActivated(const QString& chunkId);

private:
    void rebuild();
    void appendRow(int row);
    void updateRow(int row);
    AskPdfTurnCard* createCard(int row);
    void updateStickyFromScroll(int value);

    QWidget* m_container = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QPointer<RagConversationModel> m_model;
    QHash<QString, AskPdfTurnCard*> m_cards;
    bool m_stickyToBottom = true;
};

}  // namespace Rag
