#pragma once

#include <QDialog>

#include "rag/domain/RagTypes.h"

class QLabel;
class QPushButton;
class QShowEvent;

namespace Rag {

class AskPdfIndexDialog : public QDialog {
    Q_OBJECT

public:
    explicit AskPdfIndexDialog(QWidget* parent = nullptr);

    void setDocument(const DocumentKey& document);
    void setSnapshot(const RagDocumentSnapshot& snapshot);
    void setStats(const RagIndexStats& stats);
    void setBusy(bool busy);
    void showError(const QString& error);
    void showInfo(const QString& message);

signals:
    void rebuildRequested();
    void deleteRequested();
    void clearAllRequested();
    void refreshRequested();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refreshView();
    static QString formatBytes(qint64 bytes);

    DocumentKey m_document;
    RagDocumentSnapshot m_snapshot;
    RagIndexStats m_stats;
    bool m_busy = false;
    QLabel* m_documentLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_detailsLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QLabel* m_globalLabel = nullptr;
    QPushButton* m_rebuildButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_clearButton = nullptr;
};

}  // namespace Rag
