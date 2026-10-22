#pragma once

#include <QDialog>
#include <QStringList>

#include "rag/config/RagSettings.h"
#include "rag/probe/OllamaProbe.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;

namespace Rag {

class AskPdfSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AskPdfSettingsDialog(const RagSettings& settings,
                                  QWidget* parent = nullptr);
    ~AskPdfSettingsDialog() override;

signals:
    void settingsApplied(const Rag::RagSettings& settings);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void refreshModels();
    void testConnection();
    void saveSettings();
    void cancelProbes();
    void beginProbes(int count);
    void endProbe();
    void startNextProbe();
    void abortProbeQueue(const QString& reason);
    void onEndpointEdited();
    void onModelsFetched(const OllamaProbe::Result& result);
    void onEmbeddingTested(const OllamaProbe::Result& result);
    void onChatTested(const OllamaProbe::Result& result);

    bool validEndpoint(QString* normalized = nullptr) const;
    bool resultMatchesCurrent(const OllamaProbe::Result& result) const;
    void setStatus(QLabel* label, const OllamaProbe::Result& result,
                   const QString& readyText);
    void resetStatuses();
    static void setModelItems(QComboBox* combo, const QStringList& models);

    RagSettings m_initial;
    OllamaProbe* m_probe = nullptr;
    QLineEdit* m_endpointEdit = nullptr;
    QComboBox* m_embedCombo = nullptr;
    QComboBox* m_chatCombo = nullptr;
    QCheckBox* m_answerCheck = nullptr;
    QLabel* m_serviceStatus = nullptr;
    QLabel* m_embeddingStatus = nullptr;
    QLabel* m_chatStatus = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_testButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QStringList m_probeQueue;
    int m_pendingProbes = 0;
    bool m_updatingModels = false;
    bool m_embedModelTouched = false;
    bool m_started = false;
};

}  // namespace Rag
