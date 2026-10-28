#pragma once

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include "../../controller/tool.hpp"
#include "../widgets/AnnotationToolbar.h"

class ShortcutManager;

class ToolBar : public QToolBar {
    Q_OBJECT

public:
    ToolBar(QWidget* parent = nullptr);

    void setActionsEnabled(bool enabled);
    void setSidebarChecked(bool checked);
    void setViewModeIndex(int mode);
    AnnotationToolbar* annotationToolbar() const { return m_annotationToolbar; }

signals:
    void actionTriggered(ActionMap action);

private slots:
    void onViewModeChanged();

private:
    void applyShortcut(QAction* action, ActionMap actionId,
                       const QString& tooltipPrefix);
    void setupFileActions();
    void setupViewActions();
    void setupRotationActions();
    void setupAnnotationActions();
    void setupThemeActions();
    void createSeparator();
    void applyToolBarStyle();

    // File operations
    ShortcutManager* m_shortcutManager;
    QAction* openAction;
    QAction* openFolderAction;
    QAction* saveAction;

    // View operations
    QAction* toggleSidebarAction;
    QComboBox* viewModeCombo;

    // Rotation operations
    QAction* rotateLeftAction;
    QAction* rotateRightAction;

    AnnotationToolbar* m_annotationToolbar;

    // Theme operations
    QAction* themeToggleAction;
};
