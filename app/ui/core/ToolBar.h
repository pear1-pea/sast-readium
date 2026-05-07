#pragma once

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include "../../controller/tool.hpp"

class ToolBar : public QToolBar {
    Q_OBJECT

public:
    ToolBar(QWidget* parent = nullptr);

    void setActionsEnabled(bool enabled);

signals:
    void actionTriggered(ActionMap action);

private slots:
    void onViewModeChanged();

private:
    void setupFileActions();
    void setupViewActions();
    void setupRotationActions();
    void setupThemeActions();
    void createSeparator();
    void applyToolBarStyle();

    // File operations
    QAction* openAction;
    QAction* openFolderAction;
    QAction* saveAction;

    // View operations
    QAction* toggleSidebarAction;
    QComboBox* viewModeCombo;

    // Rotation operations
    QAction* rotateLeftAction;
    QAction* rotateRightAction;

    // Theme operations
    QAction* themeToggleAction;
};
