#include "StatusBar.h"
#include <QEasingCurve>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPropertyAnimation>
#include "../../managers/StyleManager.h"

StatusBar::StatusBar(QWidget* parent)
    : QStatusBar(parent), currentTotalPages(0) {
    setupUI();
    setupPageNavigation();
    setupZoomControls();
    setupLoadingProgress();

    // Theme switching is handled automatically by StyleManager factory methods
    // (createSpinBox/createComboBox/createSlider auto-register for re-theming)
}

void StatusBar::setupUI() {
    // Create file name label
    fileNameLabel = new QLabel("无文档", this);
    fileNameLabel->setMinimumWidth(150);
    fileNameLabel->setMaximumWidth(300);
    fileNameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fileNameLabel->setStyleSheet("QLabel { padding: 2px 8px; }");

    setupSeparators();

    // Add to status bar
    addPermanentWidget(fileNameLabel);
    addPermanentWidget(separatorLabel1);
}

void StatusBar::setupSeparators() {
    separatorLabel1 = new QLabel("|", this);
    separatorLabel1->setAlignment(Qt::AlignCenter);
    separatorLabel1->setStyleSheet("QLabel { color: gray; padding: 2px 4px; }");

    separatorLabel2 = new QLabel("|", this);
    separatorLabel2->setAlignment(Qt::AlignCenter);
    separatorLabel2->setStyleSheet("QLabel { color: gray; padding: 2px 4px; }");

    separatorLabel3 = new QLabel("|", this);
    separatorLabel3->setAlignment(Qt::AlignCenter);
    separatorLabel3->setStyleSheet("QLabel { color: gray; padding: 2px 4px; }");
}

void StatusBar::setupPageNavigation() {
    // First page button
    QAction* firstPageAction = new QAction("⏮", this);
    firstPageAction->setToolTip("第一页 (Ctrl+Home)");
    firstPageBtn = new QToolButton(this);
    firstPageBtn->setDefaultAction(firstPageAction);
    firstPageBtn->setFixedSize(24, 24);
    firstPageBtn->setEnabled(false);

    // Previous page button
    QAction* prevPageAction = new QAction("◀", this);
    prevPageAction->setToolTip("上一页 (Page Up)");
    prevPageBtn = new QToolButton(this);
    prevPageBtn->setDefaultAction(prevPageAction);
    prevPageBtn->setFixedSize(24, 24);
    prevPageBtn->setEnabled(false);

    // Page spinbox
    pageSpinBox = STYLE.createSpinBox(this);
    pageSpinBox->setMinimum(1);
    pageSpinBox->setMaximum(1);
    pageSpinBox->setValue(1);
    pageSpinBox->setFixedWidth(60);
    pageSpinBox->setToolTip("当前页码");
    pageSpinBox->setEnabled(false);

    // Page count label
    pageCountLabel = new QLabel("/ 1", this);
    pageCountLabel->setMinimumWidth(30);
    pageCountLabel->setAlignment(Qt::AlignCenter);

    // Next page button
    QAction* nextPageAction = new QAction("▶", this);
    nextPageAction->setToolTip("下一页 (Page Down)");
    nextPageBtn = new QToolButton(this);
    nextPageBtn->setDefaultAction(nextPageAction);
    nextPageBtn->setFixedSize(24, 24);
    nextPageBtn->setEnabled(false);

    // Last page button
    QAction* lastPageAction = new QAction("⏭", this);
    lastPageAction->setToolTip("最后一页 (Ctrl+End)");
    lastPageBtn = new QToolButton(this);
    lastPageBtn->setDefaultAction(lastPageAction);
    lastPageBtn->setFixedSize(24, 24);
    lastPageBtn->setEnabled(false);

    // Add to status bar
    addPermanentWidget(firstPageBtn);
    addPermanentWidget(prevPageBtn);
    addPermanentWidget(pageSpinBox);
    addPermanentWidget(pageCountLabel);
    addPermanentWidget(nextPageBtn);
    addPermanentWidget(lastPageBtn);

    // Connect signals
    connect(firstPageAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::firstPage); });
    connect(prevPageAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::previousPage); });
    connect(nextPageAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::nextPage); });
    connect(lastPageAction, &QAction::triggered, this,
            [this]() { emit actionTriggered(ActionMap::lastPage); });
    connect(pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &StatusBar::onPageSpinBoxChanged);
}

void StatusBar::setupZoomControls() {
    zoomWidget = new QWidget(this);
    QHBoxLayout* zoomLayout = new QHBoxLayout(zoomWidget);
    zoomLayout->setContentsMargins(4, 0, 4, 0);
    zoomLayout->setSpacing(4);

    QAction* zoomOutAction = new QAction("-", this);
    zoomOutAction->setToolTip("缩小");
    zoomOutBtn = new QToolButton(zoomWidget);
    zoomOutBtn->setDefaultAction(zoomOutAction);
    zoomOutBtn->setFixedSize(24, 24);
    zoomOutBtn->setEnabled(false);

    zoomSlider = STYLE.createSlider(Qt::Horizontal, zoomWidget);
    zoomSlider->setRange(10, 500);
    zoomSlider->setValue(100);
    zoomSlider->setFixedWidth(100);
    zoomSlider->setEnabled(false);

    zoomPercentSpinBox = STYLE.createSpinBox(zoomWidget);
    zoomPercentSpinBox->setRange(10, 500);
    zoomPercentSpinBox->setValue(100);
    zoomPercentSpinBox->setSuffix("%");
    zoomPercentSpinBox->setFixedWidth(70);
    zoomPercentSpinBox->setEnabled(false);

    QAction* zoomInAction = new QAction("+", this);
    zoomInAction->setToolTip("放大");
    zoomInBtn = new QToolButton(zoomWidget);
    zoomInBtn->setDefaultAction(zoomInAction);
    zoomInBtn->setFixedSize(24, 24);
    zoomInBtn->setEnabled(false);

    zoomLayout->addWidget(zoomOutBtn);
    zoomLayout->addWidget(zoomSlider);
    zoomLayout->addWidget(zoomPercentSpinBox);
    zoomLayout->addWidget(zoomInBtn);

    addPermanentWidget(separatorLabel2);
    addPermanentWidget(zoomWidget);

    // Connect signals
    connect(zoomSlider, &QSlider::valueChanged, this,
            &StatusBar::onZoomSliderChanged);
    connect(zoomOutAction, &QAction::triggered, this,
            &StatusBar::zoomOutClicked);
    connect(zoomInAction, &QAction::triggered, this, &StatusBar::zoomInClicked);
}

void StatusBar::onPageSpinBoxChanged(int pageNumber) {
    emit pageJumpRequested(pageNumber - 1);  // Convert to 0-based index
}

void StatusBar::onZoomSliderChanged(int value) {
    // Sync spinbox (prevent loop)
    zoomPercentSpinBox->blockSignals(true);
    zoomPercentSpinBox->setValue(value);
    zoomPercentSpinBox->blockSignals(false);

    emit zoomChanged(value);
}

void StatusBar::setDocumentInfo(const QString& fileName, int currentPage,
                                int totalPages, int zoomPercent) {
    setFileName(fileName);
    setPageInfo(currentPage, totalPages);
    setZoomLevel(zoomPercent);
}

void StatusBar::setPageInfo(int current, int total) {
    currentTotalPages = total;

    if (total > 0) {
        // Update page spinbox
        pageSpinBox->blockSignals(true);
        pageSpinBox->setMaximum(total);
        pageSpinBox->setValue(current + 1);  // Convert to 1-based
        pageSpinBox->blockSignals(false);

        pageCountLabel->setText(QString("/ %1").arg(total));

        // Enable controls
        pageSpinBox->setEnabled(true);
        firstPageBtn->setEnabled(current > 0);
        prevPageBtn->setEnabled(current > 0);
        nextPageBtn->setEnabled(current < total - 1);
        lastPageBtn->setEnabled(current < total - 1);
    } else {
        pageSpinBox->setValue(1);
        pageSpinBox->setMaximum(1);
        pageCountLabel->setText("/ 0");
        pageSpinBox->setEnabled(false);
        firstPageBtn->setEnabled(false);
        prevPageBtn->setEnabled(false);
        nextPageBtn->setEnabled(false);
        lastPageBtn->setEnabled(false);
    }
}

void StatusBar::setZoomLevel(int percent) {
    int clamped = qBound(10, percent, 500);

    zoomSlider->blockSignals(true);
    zoomPercentSpinBox->blockSignals(true);

    zoomSlider->setValue(clamped);
    zoomPercentSpinBox->setValue(clamped);

    zoomSlider->blockSignals(false);
    zoomPercentSpinBox->blockSignals(false);

    // Enable controls
    zoomSlider->setEnabled(true);
    zoomPercentSpinBox->setEnabled(true);
    zoomOutBtn->setEnabled(clamped > 10);
    zoomInBtn->setEnabled(clamped < 500);
}

void StatusBar::setZoomLevel(double factor) {
    int percent = static_cast<int>(factor * 100 + 0.5);
    setZoomLevel(percent);
}

void StatusBar::setFileName(const QString& fileName) {
    if (fileName.isEmpty()) {
        fileNameLabel->setText("无文档");
    } else {
        QString displayName = formatFileName(fileName);
        fileNameLabel->setText(displayName);
        fileNameLabel->setToolTip(fileName);  // Full path as tooltip
    }
}

void StatusBar::setMessage(const QString& message) {
    showMessage(message, 3000);
}

void StatusBar::clearDocumentInfo() {
    fileNameLabel->setText("无文档");
    fileNameLabel->setToolTip("");

    pageSpinBox->setValue(1);
    pageSpinBox->setMaximum(1);
    pageSpinBox->setEnabled(false);
    pageCountLabel->setText("/ 0");

    firstPageBtn->setEnabled(false);
    prevPageBtn->setEnabled(false);
    nextPageBtn->setEnabled(false);
    lastPageBtn->setEnabled(false);

    zoomSlider->setValue(100);
    zoomPercentSpinBox->setValue(100);
    zoomSlider->setEnabled(false);
    zoomPercentSpinBox->setEnabled(false);
    zoomOutBtn->setEnabled(false);
    zoomInBtn->setEnabled(false);

    currentTotalPages = 0;
}

QString StatusBar::formatFileName(const QString& fullPath) const {
    if (fullPath.isEmpty()) {
        return "无文档";
    }

    QFileInfo fileInfo(fullPath);
    QString baseName = fileInfo.baseName();

    // Truncate if filename is too long
    QFontMetrics metrics(fileNameLabel->font());
    int maxWidth = fileNameLabel->maximumWidth() - 16;  // Leave padding space

    if (metrics.horizontalAdvance(baseName) > maxWidth) {
        baseName = metrics.elidedText(baseName, Qt::ElideMiddle, maxWidth);
    }

    return baseName;
}

void StatusBar::setupLoadingProgress() {
    // Create loading progress bar
    loadingProgressBar = new QProgressBar(this);
    loadingProgressBar->setMinimumWidth(200);
    loadingProgressBar->setMaximumWidth(300);
    loadingProgressBar->setMinimum(0);
    loadingProgressBar->setMaximum(100);
    loadingProgressBar->setValue(0);
    loadingProgressBar->setVisible(false);
    loadingProgressBar->setStyleSheet(
        "QProgressBar {"
        "    border: 1px solid #ccc;"
        "    border-radius: 3px;"
        "    text-align: center;"
        "    font-size: 11px;"
        "}"
        "QProgressBar::chunk {"
        "    background-color: #4CAF50;"
        "    border-radius: 2px;"
        "}");

    // Create loading message label
    loadingMessageLabel = new QLabel(this);
    loadingMessageLabel->setVisible(false);
    loadingMessageLabel->setStyleSheet(
        "QLabel { padding: 2px 8px; color: #666; }");

    // Create progress animation
    progressAnimation =
        new QPropertyAnimation(loadingProgressBar, "value", this);
    progressAnimation->setDuration(300);
    progressAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // Add to status bar (at leftmost position)
    insertPermanentWidget(0, loadingMessageLabel);
    insertPermanentWidget(1, loadingProgressBar);
}

void StatusBar::showLoadingProgress(const QString& message) {
    loadingMessageLabel->setText(message);
    loadingMessageLabel->setVisible(true);
    loadingProgressBar->setValue(0);
    loadingProgressBar->setVisible(true);

    // Hide other controls to save space
    fileNameLabel->setVisible(false);
    separatorLabel1->setVisible(false);
}

void StatusBar::updateLoadingProgress(int progress) {
    progress = qBound(0, progress, 100);

    // Update progress with animation
    progressAnimation->stop();
    progressAnimation->setStartValue(loadingProgressBar->value());
    progressAnimation->setEndValue(progress);
    progressAnimation->start();
}

void StatusBar::setLoadingMessage(const QString& message) {
    if (loadingMessageLabel->isVisible()) {
        loadingMessageLabel->setText(message);
    }
}

void StatusBar::hideLoadingProgress() {
    loadingProgressBar->setVisible(false);
    loadingMessageLabel->setVisible(false);

    // Restore other controls display
    fileNameLabel->setVisible(true);
    separatorLabel1->setVisible(true);
}
