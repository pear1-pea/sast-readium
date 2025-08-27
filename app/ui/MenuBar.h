#pragma once

#include <QMenuBar>
#include "controller/tool.hpp"

class MenuBar : public QMenuBar {
    Q_OBJECT

public:
    MenuBar(QWidget* parent = nullptr);

signals:
    void themeChanged(const QString& theme);
    void onExecuted(ActionMap id, QWidget* context = nullptr);

private:
    void createFileMenu();
    void createViewMenu();
    void createThemeMenu();
};