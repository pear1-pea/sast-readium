#include "AskPdfSettingsDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace Rag {
namespace {

void applyLabelStyle(QLabel* label, const QString& color) {
    label->setStyleSheet(
        color.isEmpty() ? QString() : QStringLiteral("color: %1;").arg(color));
}

}  // namespace

AskPdfSettingsDialog::AskPdfSettingsDialog(const RagSettings& settings,
                                           QWidget* parent)
    : QDialog(parent),
      m_initial(settings.normalized()),
      m_probe(new OllamaProbe(this)) {
    buildUi();

    connect(m_probe, &OllamaProbe::modelsFetched, this,
            &AskPdfSettingsDialog::onModelsFetched);
    connect(m_probe, &OllamaProbe::embeddingTested, this,
            &AskPdfSettingsDialog::onEmbeddingTested);
    connect(m_probe, &OllamaProbe::chatTested, this,
            &AskPdfSettingsDialog::onChatTested);
}

AskPdfSettingsDialog::~AskPdfSettingsDialog() { cancelProbes(); }

void AskPdfSettingsDialog::buildUi() {
    setWindowTitle(QStringLiteral("Ask PDF 本地 AI 设置"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    m_serviceStatus = new QLabel(QStringLiteral("尚未检查连接"), this);
    m_serviceStatus->setWordWrap(true);
    layout->addWidget(m_serviceStatus);

    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* endpointRow = new QHBoxLayout();
    m_endpointEdit = new QLineEdit(m_initial.endpoint, this);
    m_endpointEdit->setPlaceholderText(
        QStringLiteral("http://127.0.0.1:11434"));
    endpointRow->addWidget(m_endpointEdit, 1);
    m_refreshButton = new QPushButton(QStringLiteral("刷新模型"), this);
    endpointRow->addWidget(m_refreshButton);
    form->addRow(QStringLiteral("Ollama 地址"), endpointRow);

    m_embedCombo = new QComboBox(this);
    m_embedCombo->setEditable(true);
    m_embedCombo->setInsertPolicy(QComboBox::NoInsert);
    m_embedCombo->setEditText(m_initial.embedModel);
    form->addRow(QStringLiteral("Embedding 模型"), m_embedCombo);

    m_embeddingStatus = new QLabel(QStringLiteral("尚未检查"), this);
    m_embeddingStatus->setWordWrap(true);
    form->addRow(QString(), m_embeddingStatus);

    m_chatCombo = new QComboBox(this);
    m_chatCombo->setEditable(true);
    m_chatCombo->setInsertPolicy(QComboBox::NoInsert);
    m_chatCombo->setEditText(m_initial.chatModel);
    form->addRow(QStringLiteral("Chat 模型"), m_chatCombo);

    m_chatStatus = new QLabel(QStringLiteral("尚未检查"), this);
    m_chatStatus->setWordWrap(true);
    form->addRow(QString(), m_chatStatus);

    layout->addLayout(form);

    m_answerCheck = new QCheckBox(QStringLiteral("启用 AI 回答"), this);
    m_answerCheck->setChecked(m_initial.answerEnabled);
    layout->addWidget(m_answerCheck);

    m_hint = new QLabel(
        QStringLiteral("关闭回答生成后，仍可使用本地证据搜索；Embedding 模型"
                       "就绪时，语义检索也会继续工作。"),
        this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    auto* note = new QLabel(
        QStringLiteral("更换 Embedding 模型或地址后，已打开文档会按新配置补齐"
                       "向量，不会重新提取文本。测试连接成功只代表服务与模型"
                       "当前可用，不代表本文档的向量已经生成。"),
        this);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    auto* buttons = new QHBoxLayout();
    m_testButton = new QPushButton(QStringLiteral("测试连接"), this);
    buttons->addWidget(m_testButton);
    m_cancelButton = new QPushButton(QStringLiteral("取消检查"), this);
    m_cancelButton->hide();
    buttons->addWidget(m_cancelButton);
    buttons->addStretch(1);

    auto* box = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->addWidget(box);
    layout->addLayout(buttons);

    connect(m_testButton, &QPushButton::clicked, this,
            &AskPdfSettingsDialog::testConnection);
    connect(m_refreshButton, &QPushButton::clicked, this,
            &AskPdfSettingsDialog::refreshModels);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        cancelProbes();
        m_serviceStatus->setText(QStringLiteral("已取消检查"));
        m_embeddingStatus->setText(QStringLiteral("已取消检查"));
        m_chatStatus->setText(QStringLiteral("已取消检查"));
        applyLabelStyle(m_serviceStatus, QString());
        applyLabelStyle(m_embeddingStatus, QString());
        applyLabelStyle(m_chatStatus, QString());
    });
    connect(box, &QDialogButtonBox::accepted, this,
            &AskPdfSettingsDialog::saveSettings);
    connect(box, &QDialogButtonBox::rejected, this, [this]() {
        cancelProbes();
        reject();
    });
    connect(m_endpointEdit, &QLineEdit::textEdited, this,
            &AskPdfSettingsDialog::onEndpointEdited);
    connect(m_embedCombo, &QComboBox::editTextChanged, this, [this]() {
        if (!m_updatingModels) {
            m_embedModelTouched = true;
            cancelProbes();
            resetStatuses();
        }
    });
    connect(m_chatCombo, &QComboBox::editTextChanged, this, [this]() {
        if (!m_updatingModels) {
            cancelProbes();
            resetStatuses();
        }
    });
}

void AskPdfSettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (!m_started) {
        m_started = true;
        refreshModels();
    }
}

void AskPdfSettingsDialog::closeEvent(QCloseEvent* event) {
    cancelProbes();
    QDialog::closeEvent(event);
}

void AskPdfSettingsDialog::onEndpointEdited() {
    cancelProbes();
    resetStatuses();
}

void AskPdfSettingsDialog::cancelProbes() {
    m_probe->cancelAll();
    m_probeQueue.clear();
    m_pendingProbes = 0;
    if (m_cancelButton) {
        m_cancelButton->hide();
    }
}

void AskPdfSettingsDialog::abortProbeQueue(const QString& reason) {
    const QStringList queued = m_probeQueue;
    for (const QString& kind : queued) {
        QLabel* label = kind == QStringLiteral("embedding") ? m_embeddingStatus
                                                            : m_chatStatus;
        label->setText(reason);
        applyLabelStyle(label, QStringLiteral("#8a6d3b"));
    }
    m_probeQueue.clear();
    m_pendingProbes = 0;
    if (m_cancelButton) {
        m_cancelButton->hide();
    }
}

void AskPdfSettingsDialog::startNextProbe() {
    if (m_probeQueue.isEmpty()) {
        return;
    }
    QString endpoint;
    if (!validEndpoint(&endpoint)) {
        abortProbeQueue(QStringLiteral("未检查：地址无效"));
        return;
    }
    const QString kind = m_probeQueue.takeFirst();
    if (kind == QStringLiteral("embedding")) {
        const QString model = m_embedCombo->currentText().trimmed();
        if (model.isEmpty()) {
            startNextProbe();
            return;
        }
        m_embeddingStatus->setText(QStringLiteral("正在检查..."));
        applyLabelStyle(m_embeddingStatus, QStringLiteral("#8a6d3b"));
        m_probe->testEmbedding(endpoint, model);
        return;
    }
    const QString model = m_chatCombo->currentText().trimmed();
    if (model.isEmpty()) {
        startNextProbe();
        return;
    }
    m_chatStatus->setText(QStringLiteral("正在检查..."));
    applyLabelStyle(m_chatStatus, QStringLiteral("#8a6d3b"));
    m_probe->testChat(endpoint, model);
}

void AskPdfSettingsDialog::beginProbes(int count) {
    m_pendingProbes = qMax(0, count);
    if (m_cancelButton) {
        m_cancelButton->setVisible(m_pendingProbes > 0);
    }
}

void AskPdfSettingsDialog::endProbe() {
    if (m_pendingProbes > 0) {
        --m_pendingProbes;
    }
    if (m_pendingProbes == 0 && m_cancelButton) {
        m_cancelButton->hide();
    }
}

void AskPdfSettingsDialog::resetStatuses() {
    m_serviceStatus->setText(QStringLiteral("尚未检查连接"));
    applyLabelStyle(m_serviceStatus, QString());
    m_embeddingStatus->setText(QStringLiteral("尚未检查"));
    applyLabelStyle(m_embeddingStatus, QString());
    m_chatStatus->setText(QStringLiteral("尚未检查"));
    applyLabelStyle(m_chatStatus, QString());
}

bool AskPdfSettingsDialog::validEndpoint(QString* normalized) const {
    bool ok = false;
    const QString endpoint =
        RagSettings::normalizedEndpoint(m_endpointEdit->text(), &ok);
    if (ok && normalized) {
        *normalized = endpoint;
    }
    return ok;
}

void AskPdfSettingsDialog::refreshModels() {
    QString endpoint;
    if (!validEndpoint(&endpoint)) {
        m_serviceStatus->setText(QStringLiteral(
            "地址无效，仅支持本机地址（127.0.0.1、localhost 或 ::1）"));
        applyLabelStyle(m_serviceStatus, QStringLiteral("#b42318"));
        return;
    }
    m_serviceStatus->setText(QStringLiteral("正在连接..."));
    applyLabelStyle(m_serviceStatus, QStringLiteral("#8a6d3b"));
    cancelProbes();
    beginProbes(1);
    m_probe->fetchModels(endpoint);
}

void AskPdfSettingsDialog::testConnection() {
    QString endpoint;
    if (!validEndpoint(&endpoint)) {
        m_serviceStatus->setText(QStringLiteral(
            "地址无效，仅支持本机地址（127.0.0.1、localhost 或 ::1）"));
        applyLabelStyle(m_serviceStatus, QStringLiteral("#b42318"));
        return;
    }

    const QString embedModel = m_embedCombo->currentText().trimmed();
    const QString chatModel = m_chatCombo->currentText().trimmed();

    cancelProbes();
    if (!embedModel.isEmpty()) {
        m_probeQueue.append(QStringLiteral("embedding"));
    }
    if (!chatModel.isEmpty()) {
        m_probeQueue.append(QStringLiteral("chat"));
    }
    beginProbes(1 + m_probeQueue.size());

    m_serviceStatus->setText(QStringLiteral("正在连接..."));
    applyLabelStyle(m_serviceStatus, QStringLiteral("#8a6d3b"));
    if (embedModel.isEmpty()) {
        m_embeddingStatus->setText(QStringLiteral("未选择 Embedding 模型"));
        applyLabelStyle(m_embeddingStatus, QStringLiteral("#8a6d3b"));
    } else {
        m_embeddingStatus->setText(QStringLiteral("等待检查"));
        applyLabelStyle(m_embeddingStatus, QStringLiteral("#8a6d3b"));
    }
    if (chatModel.isEmpty()) {
        m_chatStatus->setText(QStringLiteral("未选择 Chat 模型"));
        applyLabelStyle(m_chatStatus, QStringLiteral("#8a6d3b"));
    } else {
        m_chatStatus->setText(QStringLiteral("等待检查"));
        applyLabelStyle(m_chatStatus, QStringLiteral("#8a6d3b"));
    }

    m_probe->fetchModels(endpoint);
}

bool AskPdfSettingsDialog::resultMatchesCurrent(
    const OllamaProbe::Result& result) const {
    QString endpoint;
    if (!validEndpoint(&endpoint) ||
        RagSettings::normalizedEndpoint(result.endpoint) != endpoint) {
        return false;
    }
    if (!result.model.isEmpty() &&
        result.model != m_embedCombo->currentText().trimmed() &&
        result.model != m_chatCombo->currentText().trimmed()) {
        return false;
    }
    return true;
}

void AskPdfSettingsDialog::setStatus(QLabel* label,
                                     const OllamaProbe::Result& result,
                                     const QString& readyText) {
    switch (result.status) {
        case OllamaProbe::Status::Ready:
            label->setText(readyText);
            applyLabelStyle(label, QStringLiteral("#1a7f37"));
            break;
        case OllamaProbe::Status::Unreachable:
        case OllamaProbe::Status::Timeout:
        case OllamaProbe::Status::ModelMissing:
        case OllamaProbe::Status::Failed:
            label->setText(result.message);
            applyLabelStyle(label, QStringLiteral("#b42318"));
            break;
        case OllamaProbe::Status::Checking:
            label->setText(QStringLiteral("正在检查..."));
            applyLabelStyle(label, QStringLiteral("#8a6d3b"));
            break;
        case OllamaProbe::Status::Idle:
        default:
            label->setText(QStringLiteral("尚未检查"));
            applyLabelStyle(label, QString());
            break;
    }
}

void AskPdfSettingsDialog::onModelsFetched(const OllamaProbe::Result& result) {
    endProbe();
    QString endpoint;
    if (!validEndpoint(&endpoint) ||
        RagSettings::normalizedEndpoint(result.endpoint) != endpoint) {
        return;
    }
    if (result.isReady()) {
        setStatus(m_serviceStatus, result, result.message);
        if (!result.models.isEmpty()) {
            m_updatingModels = true;
            setModelItems(m_embedCombo, result.models);
            setModelItems(m_chatCombo, result.models);
            m_updatingModels = false;
        }
        startNextProbe();
    } else {
        setStatus(m_serviceStatus, result, {});
        abortProbeQueue(QStringLiteral("未检查：服务不可用"));
    }
}

void AskPdfSettingsDialog::onEmbeddingTested(
    const OllamaProbe::Result& result) {
    endProbe();
    if (!resultMatchesCurrent(result) ||
        result.model != m_embedCombo->currentText().trimmed()) {
        return;
    }
    setStatus(m_embeddingStatus, result, result.message);
    startNextProbe();
}

void AskPdfSettingsDialog::onChatTested(const OllamaProbe::Result& result) {
    endProbe();
    if (!resultMatchesCurrent(result) ||
        result.model != m_chatCombo->currentText().trimmed()) {
        return;
    }
    setStatus(m_chatStatus, result, result.message);
}

void AskPdfSettingsDialog::setModelItems(QComboBox* combo,
                                         const QStringList& models) {
    if (!combo) {
        return;
    }
    const QString current = combo->currentText();
    combo->clear();
    combo->addItems(models);
    if (current.trimmed().isEmpty()) {
        // QComboBox selects the first inserted item; an empty selection must
        // stay empty so refreshing the list never changes the configuration.
        combo->setCurrentIndex(-1);
        combo->setEditText(QString());
    } else {
        combo->setEditText(current);
    }
}

void AskPdfSettingsDialog::saveSettings() {
    QString endpoint;
    if (!validEndpoint(&endpoint)) {
        QMessageBox::warning(
            this, QStringLiteral("地址无效"),
            QStringLiteral("请填写本机 Ollama 地址，例如 "
                           "http://127.0.0.1:11434；不支持远程主机。"));
        m_endpointEdit->setFocus();
        return;
    }

    RagSettings next = m_initial;
    next.endpoint = endpoint;
    next.embedModel = m_embedCombo->currentText().trimmed();
    next.chatModel = m_chatCombo->currentText().trimmed();
    next.answerEnabled = m_answerCheck->isChecked();

    if (next.answerEnabled && next.chatModel.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("缺少 Chat 模型"),
            QStringLiteral("启用 AI 回答前请先选择 Chat 模型。"));
        m_chatCombo->setFocus();
        return;
    }

    // A migrated user may have a model without a version (embedding disabled
    // by the old rules). Only an explicit model choice enables the hidden
    // version, and only for a non-empty model.
    if (m_embedModelTouched && !next.embedModel.isEmpty() &&
        next.embedVersion.isEmpty()) {
        next.embedVersion = QStringLiteral("1");
    }

    next = next.normalized();
    QString error;
    if (!next.save(&error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    emit settingsApplied(next);
    accept();
}

}  // namespace Rag
