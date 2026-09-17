#include "AnnotationToolbar.h"
#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QStyle>
#include "../../managers/StyleManager.h"

AnnotationToolbar::AnnotationToolbar(QWidget* parent)
    : QWidget(parent),
      m_currentTool(AnnotationType::Highlight),
      m_currentColor(Qt::yellow),
      m_currentOpacity(0.7),
      m_currentLineWidth(2.0),
      m_currentFontSize(12),
      m_currentFontFamily("Arial"),
      m_colorDialog(nullptr) {
    setupUI();
    setupConnections();
    resetToDefaults();
    applyTheme();
    connect(&STYLE, &StyleManager::themeChanged, this,
            &AnnotationToolbar::applyTheme);
}

void AnnotationToolbar::setupUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(4);

    // Tool selection group
    m_toolGroup = new QGroupBox(this);
    m_toolGroup->setFlat(true);
    m_toolLayout = new QHBoxLayout(m_toolGroup);
    m_toolLayout->setContentsMargins(0, 0, 0, 0);
    m_toolLayout->setSpacing(2);
    m_toolButtonGroup = new QButtonGroup(this);

    m_selectBtn = new QToolButton();
    m_selectBtn->setText("选择");
    m_selectBtn->setCheckable(true);
    m_selectBtn->setChecked(true);
    m_selectBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_selectBtn->setToolTip("选择文本或页面内容");
    m_toolLayout->addWidget(m_selectBtn);

    // Create tool buttons
    m_highlightBtn = new QToolButton();
    m_highlightBtn->setText("高亮");
    m_highlightBtn->setCheckable(true);
    m_highlightBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_highlightBtn->setToolTip("文本高亮");
    m_highlightBtn->setProperty("tool",
                                static_cast<int>(AnnotationType::Highlight));

    m_noteBtn = new QToolButton();
    m_noteBtn->setText("便签");
    m_noteBtn->setCheckable(true);
    m_noteBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_noteBtn->setToolTip("添加便签");
    m_noteBtn->setProperty("tool", static_cast<int>(AnnotationType::Note));

    m_freeTextBtn = new QToolButton();
    m_freeTextBtn->setText("文本");
    m_freeTextBtn->setCheckable(true);
    m_freeTextBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_freeTextBtn->setToolTip("自由文本");
    m_freeTextBtn->setProperty("tool",
                               static_cast<int>(AnnotationType::FreeText));

    m_underlineBtn = new QToolButton();
    m_underlineBtn->setText("下划线");
    m_underlineBtn->setCheckable(true);
    m_underlineBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_underlineBtn->setToolTip("文本下划线");
    m_underlineBtn->setProperty("tool",
                                static_cast<int>(AnnotationType::Underline));

    m_strikeOutBtn = new QToolButton();
    m_strikeOutBtn->setText("删除线");
    m_strikeOutBtn->setCheckable(true);
    m_strikeOutBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_strikeOutBtn->setToolTip("文本删除线");
    m_strikeOutBtn->setProperty("tool",
                                static_cast<int>(AnnotationType::StrikeOut));

    m_rectangleBtn = new QToolButton();
    m_rectangleBtn->setText("矩形");
    m_rectangleBtn->setCheckable(true);
    m_rectangleBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_rectangleBtn->setToolTip("绘制矩形");
    m_rectangleBtn->setProperty("tool",
                                static_cast<int>(AnnotationType::Rectangle));

    m_circleBtn = new QToolButton();
    m_circleBtn->setText("圆形");
    m_circleBtn->setCheckable(true);
    m_circleBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_circleBtn->setToolTip("绘制圆形");
    m_circleBtn->setProperty("tool", static_cast<int>(AnnotationType::Circle));

    m_lineBtn = new QToolButton();
    m_lineBtn->setText("直线");
    m_lineBtn->setCheckable(true);
    m_lineBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_lineBtn->setToolTip("绘制直线");
    m_lineBtn->setProperty("tool", static_cast<int>(AnnotationType::Line));

    m_arrowBtn = new QToolButton();
    m_arrowBtn->setText("箭头");
    m_arrowBtn->setCheckable(true);
    m_arrowBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_arrowBtn->setToolTip("绘制箭头");
    m_arrowBtn->setProperty("tool", static_cast<int>(AnnotationType::Arrow));

    m_inkBtn = new QToolButton();
    m_inkBtn->setText("手绘");
    m_inkBtn->setCheckable(true);
    m_inkBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_inkBtn->setToolTip("自由手绘");
    m_inkBtn->setProperty("tool", static_cast<int>(AnnotationType::Ink));

    // Add buttons to group and layout
    QList<QToolButton*> toolButtons = {
        m_highlightBtn, m_noteBtn,      m_freeTextBtn, m_underlineBtn,
        m_strikeOutBtn, m_rectangleBtn, m_circleBtn,   m_lineBtn,
        m_arrowBtn,     m_inkBtn};

    for (QToolButton* btn : toolButtons) {
        m_toolButtonGroup->addButton(btn);
        m_toolLayout->addWidget(btn);
        btn->setMinimumHeight(28);
    }

    m_highlightBtn->setChecked(true);  // Default selection

    // Properties group
    m_propertiesGroup = new QGroupBox(this);
    m_propertiesGroup->setFlat(true);
    m_propertiesLayout = new QHBoxLayout(m_propertiesGroup);
    m_propertiesLayout->setContentsMargins(0, 0, 0, 0);
    m_propertiesLayout->setSpacing(2);

    // Color selection
    QHBoxLayout* colorLayout = new QHBoxLayout();
    colorLayout->addWidget(new QLabel("颜色:"));
    m_colorButton = new QToolButton();
    m_colorButton->setFixedSize(28, 28);
    m_colorButton->setToolTip("注释颜色");
    colorLayout->addWidget(m_colorButton);
    colorLayout->addStretch();
    m_propertiesLayout->addLayout(colorLayout);

    // Opacity control
    QHBoxLayout* opacityLayout = new QHBoxLayout();
    m_opacityLabel = new QLabel("透明度: 70%");
    opacityLayout->addWidget(m_opacityLabel);
    m_opacitySlider = STYLE.createSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(10, 100);
    m_opacitySlider->setValue(70);
    opacityLayout->addWidget(m_opacitySlider);
    m_propertiesLayout->addLayout(opacityLayout);

    // Line width control
    QHBoxLayout* lineWidthLayout = new QHBoxLayout();
    m_lineWidthLabel = new QLabel("线宽:");
    lineWidthLayout->addWidget(m_lineWidthLabel);
    m_lineWidthSpinBox = STYLE.createSpinBox(this);
    m_lineWidthSpinBox->setRange(1, 10);
    m_lineWidthSpinBox->setValue(2);
    m_lineWidthSpinBox->setSuffix(" px");
    lineWidthLayout->addWidget(m_lineWidthSpinBox);
    lineWidthLayout->addStretch();
    m_propertiesLayout->addLayout(lineWidthLayout);

    // Font size control
    QHBoxLayout* fontSizeLayout = new QHBoxLayout();
    m_fontSizeLabel = new QLabel("字号:");
    fontSizeLayout->addWidget(m_fontSizeLabel);
    m_fontSizeSpinBox = STYLE.createSpinBox(this);
    m_fontSizeSpinBox->setRange(8, 72);
    m_fontSizeSpinBox->setValue(12);
    m_fontSizeSpinBox->setSuffix(" pt");
    fontSizeLayout->addWidget(m_fontSizeSpinBox);
    fontSizeLayout->addStretch();
    m_propertiesLayout->addLayout(fontSizeLayout);

    // Font family control
    QHBoxLayout* fontFamilyLayout = new QHBoxLayout();
    m_fontFamilyLabel = new QLabel("字体:");
    fontFamilyLayout->addWidget(m_fontFamilyLabel);
    m_fontFamilyCombo = STYLE.createComboBox(this);
    m_fontFamilyCombo->addItems(QFontDatabase::families());
    m_fontFamilyCombo->setCurrentText("Arial");
    fontFamilyLayout->addWidget(m_fontFamilyCombo);
    m_propertiesLayout->addLayout(fontFamilyLayout);

    // Actions group
    m_actionsGroup = new QGroupBox(this);
    m_actionsGroup->setFlat(true);
    m_actionsLayout = new QHBoxLayout(m_actionsGroup);
    m_actionsLayout->setContentsMargins(0, 0, 0, 0);
    m_actionsLayout->setSpacing(2);

    m_clearAllBtn = new QToolButton();
    m_clearAllBtn->setText("清除");
    m_clearAllBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_clearAllBtn->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton));
    m_clearAllBtn->setToolTip("清除所有注释");

    m_saveBtn = new QToolButton();
    m_saveBtn->setText("保存");
    m_saveBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_saveBtn->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_saveBtn->setToolTip("保存批注到伴随文件");

    m_loadBtn = new QToolButton();
    m_loadBtn->setText("加载");
    m_loadBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_loadBtn->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_loadBtn->setToolTip("从文档加载注释");

    m_actionsLayout->addWidget(m_clearAllBtn);
    m_actionsLayout->addWidget(m_saveBtn);
    m_actionsLayout->addWidget(m_loadBtn);

    // Add groups to main layout
    mainLayout->addWidget(m_toolGroup, 0, Qt::AlignVCenter);
    mainLayout->addWidget(m_propertiesGroup, 0, Qt::AlignVCenter);
    mainLayout->addWidget(m_actionsGroup, 0, Qt::AlignVCenter);

    updateColorButton();
    updatePropertyControls();
}

void AnnotationToolbar::setupConnections() {
    // Tool selection
    connect(m_toolButtonGroup,
            QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this,
            &AnnotationToolbar::onToolButtonClicked);
    connect(m_selectBtn, &QToolButton::clicked, this,
            [this]() { setAnnotationMode(false); });

    // Color selection
    connect(m_colorButton, &QToolButton::clicked, this,
            &AnnotationToolbar::onColorButtonClicked);

    // Property controls
    connect(m_opacitySlider, &QSlider::valueChanged, this,
            &AnnotationToolbar::onOpacitySliderChanged);
    connect(m_lineWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AnnotationToolbar::onLineWidthChanged);
    connect(m_fontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AnnotationToolbar::onFontSizeChanged);
    connect(m_fontFamilyCombo, &QComboBox::currentTextChanged, this,
            &AnnotationToolbar::onFontFamilyChanged);

    // Actions
    connect(m_clearAllBtn, &QToolButton::clicked, this,
            &AnnotationToolbar::clearAllAnnotations);
    connect(m_saveBtn, &QToolButton::clicked, this,
            &AnnotationToolbar::saveAnnotations);
    connect(m_loadBtn, &QToolButton::clicked, this,
            &AnnotationToolbar::loadAnnotations);
}

void AnnotationToolbar::setCurrentTool(AnnotationType tool) {
    if (m_currentTool != tool) {
        m_currentTool = tool;
        updateToolButtons();
        updatePropertyControls();
        emit toolChanged(tool);
    }
}

void AnnotationToolbar::setAnnotationMode(bool enabled) {
    if (m_annotationMode == enabled) {
        updateToolButtons();
        return;
    }

    m_annotationMode = enabled;
    updateToolButtons();
    emit annotationModeChanged(enabled);
}

void AnnotationToolbar::setCurrentColor(const QColor& color) {
    if (m_currentColor != color) {
        m_currentColor = color;
        updateColorButton();
        emit colorChanged(color);
    }
}

void AnnotationToolbar::setCurrentOpacity(double opacity) {
    if (m_currentOpacity != opacity) {
        m_currentOpacity = opacity;
        m_opacitySlider->setValue(static_cast<int>(opacity * 100));
        m_opacityLabel->setText(
            QString("透明度: %1%").arg(static_cast<int>(opacity * 100)));
        emit opacityChanged(opacity);
    }
}

void AnnotationToolbar::setCurrentLineWidth(double width) {
    if (m_currentLineWidth != width) {
        m_currentLineWidth = width;
        m_lineWidthSpinBox->setValue(static_cast<int>(width));
        emit lineWidthChanged(width);
    }
}

void AnnotationToolbar::setCurrentFontSize(int size) {
    if (m_currentFontSize != size) {
        m_currentFontSize = size;
        m_fontSizeSpinBox->setValue(size);
        emit fontSizeChanged(size);
    }
}

void AnnotationToolbar::setCurrentFontFamily(const QString& family) {
    if (m_currentFontFamily != family) {
        m_currentFontFamily = family;
        m_fontFamilyCombo->setCurrentText(family);
        emit fontFamilyChanged(family);
    }
}

void AnnotationToolbar::setEnabled(bool enabled) {
    QWidget::setEnabled(enabled);
}

void AnnotationToolbar::resetToDefaults() {
    setCurrentTool(AnnotationType::Highlight);
    setAnnotationMode(false);
    setCurrentColor(Qt::yellow);
    setCurrentOpacity(0.7);
    setCurrentLineWidth(2.0);
    setCurrentFontSize(12);
    setCurrentFontFamily("Arial");
}

void AnnotationToolbar::onToolButtonClicked() {
    QToolButton* button = qobject_cast<QToolButton*>(sender());
    if (button) {
        int toolValue = button->property("tool").toInt();
        setCurrentTool(static_cast<AnnotationType>(toolValue));
        setAnnotationMode(true);
    }
}

void AnnotationToolbar::onColorButtonClicked() {
    if (!m_colorDialog) {
        m_colorDialog = new QColorDialog(this);
        m_colorDialog->setOption(QColorDialog::ShowAlphaChannel, false);
    }

    m_colorDialog->setCurrentColor(m_currentColor);
    if (m_colorDialog->exec() == QDialog::Accepted) {
        setCurrentColor(m_colorDialog->currentColor());
    }
}

void AnnotationToolbar::onOpacitySliderChanged(int value) {
    double opacity = value / 100.0;
    setCurrentOpacity(opacity);
}

void AnnotationToolbar::onLineWidthChanged(int value) {
    setCurrentLineWidth(static_cast<double>(value));
}

void AnnotationToolbar::onFontSizeChanged(int size) {
    setCurrentFontSize(size);
}

void AnnotationToolbar::onFontFamilyChanged(const QString& family) {
    setCurrentFontFamily(family);
}

void AnnotationToolbar::updateToolButtons() {
    if (m_selectBtn) {
        m_selectBtn->setChecked(!m_annotationMode);
    }

    // Find and check the button corresponding to current tool
    for (QAbstractButton* button : m_toolButtonGroup->buttons()) {
        QToolButton* pushButton = qobject_cast<QToolButton*>(button);
        if (pushButton) {
            int toolValue = pushButton->property("tool").toInt();
            pushButton->setChecked(m_annotationMode &&
                                   static_cast<AnnotationType>(toolValue) ==
                                       m_currentTool);
        }
    }
}

void AnnotationToolbar::updateColorButton() {
    QString colorStyle = QString(
                             "QToolButton { background-color: %1; border: 1px "
                             "solid %2; border-radius: %3px; }")
                             .arg(m_currentColor.name())
                             .arg(STYLE.borderColor().name())
                             .arg(STYLE.borderRadius());
    m_colorButton->setStyleSheet(colorStyle);
}

void AnnotationToolbar::updatePropertyControls() {
    // Show/hide controls based on current tool
    bool showLineWidth = (m_currentTool == AnnotationType::Rectangle ||
                          m_currentTool == AnnotationType::Circle ||
                          m_currentTool == AnnotationType::Line ||
                          m_currentTool == AnnotationType::Arrow ||
                          m_currentTool == AnnotationType::Ink);

    bool showFontControls = (m_currentTool == AnnotationType::FreeText ||
                             m_currentTool == AnnotationType::Note);

    m_lineWidthLabel->setVisible(showLineWidth);
    m_lineWidthSpinBox->setVisible(showLineWidth);

    m_fontSizeLabel->setVisible(showFontControls);
    m_fontSizeSpinBox->setVisible(showFontControls);
    m_fontFamilyLabel->setVisible(showFontControls);
    m_fontFamilyCombo->setVisible(showFontControls);
}

void AnnotationToolbar::applyTheme() {
    const QString buttonStyle = STYLE.getButtonStyleSheet();
    for (QToolButton* button : findChildren<QToolButton*>()) {
        if (button != m_colorButton) {
            button->setStyleSheet(buttonStyle);
        }
    }
    updateColorButton();
}
