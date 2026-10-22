#pragma once

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include "../../model/AnnotationModel.h"

/**
 * Toolbar for annotation tools and controls
 */
class AnnotationToolbar : public QWidget {
    Q_OBJECT

public:
    explicit AnnotationToolbar(QWidget* parent = nullptr);
    ~AnnotationToolbar() = default;

    // Tool selection
    AnnotationType getCurrentTool() const { return m_currentTool; }
    void setCurrentTool(AnnotationType tool);
    bool isAnnotationMode() const { return m_annotationMode; }
    void setAnnotationMode(bool enabled);

    // Tool properties
    QColor getCurrentColor() const { return m_currentColor; }
    void setCurrentColor(const QColor& color);

    double getCurrentOpacity() const { return m_currentOpacity; }
    void setCurrentOpacity(double opacity);

    double getCurrentLineWidth() const { return m_currentLineWidth; }
    void setCurrentLineWidth(double width);

    int getCurrentFontSize() const { return m_currentFontSize; }
    void setCurrentFontSize(int size);

    QString getCurrentFontFamily() const { return m_currentFontFamily; }
    void setCurrentFontFamily(const QString& family);

    // UI state
    void setEnabled(bool enabled);
    void setUnavailableReason(const QString& reason);
    void resetToDefaults();

signals:
    void toolChanged(AnnotationType tool);
    void annotationModeChanged(bool enabled);
    void colorChanged(const QColor& color);
    void opacityChanged(double opacity);
    void lineWidthChanged(double width);
    void fontSizeChanged(int size);
    void fontFamilyChanged(const QString& family);
    void clearAllAnnotations();
    void saveAnnotations();
    void loadAnnotations();

private slots:
    void onToolButtonClicked();
    void onColorButtonClicked();
    void onOpacitySliderChanged(int value);
    void onLineWidthChanged(int value);
    void onFontSizeChanged(int size);
    void onFontFamilyChanged(const QString& family);

private:
    void setupUI();
    void setupConnections();
    void updateToolButtons();
    void updateColorButton();
    void updatePropertyControls();
    void applyTheme();

    // Tool selection
    QGroupBox* m_toolGroup;
    QHBoxLayout* m_toolLayout;
    QButtonGroup* m_toolButtonGroup;
    QToolButton* m_selectBtn;

    QToolButton* m_highlightBtn;
    QToolButton* m_noteBtn;
    QToolButton* m_freeTextBtn;
    QToolButton* m_underlineBtn;
    QToolButton* m_strikeOutBtn;
    QToolButton* m_rectangleBtn;
    QToolButton* m_circleBtn;
    QToolButton* m_lineBtn;
    QToolButton* m_arrowBtn;
    QToolButton* m_inkBtn;

    // Properties
    QGroupBox* m_propertiesGroup;
    QHBoxLayout* m_propertiesLayout;

    QToolButton* m_colorButton;
    QColorDialog* m_colorDialog;

    QLabel* m_opacityLabel;
    QSlider* m_opacitySlider;

    QLabel* m_lineWidthLabel;
    QSpinBox* m_lineWidthSpinBox;

    QLabel* m_fontSizeLabel;
    QSpinBox* m_fontSizeSpinBox;

    QLabel* m_fontFamilyLabel;
    QComboBox* m_fontFamilyCombo;

    // Actions
    QGroupBox* m_actionsGroup;
    QHBoxLayout* m_actionsLayout;

    QToolButton* m_clearAllBtn;
    QToolButton* m_saveBtn;
    QToolButton* m_loadBtn;

    // Current state
    AnnotationType m_currentTool;
    QColor m_currentColor;
    double m_currentOpacity;
    double m_currentLineWidth;
    int m_currentFontSize;
    QString m_currentFontFamily;
    bool m_annotationMode = false;
};
