#pragma once

#include <QLabel>
#include <QStatusBar>
#include <QMessageBox>
#include "../factory/WidgetFactory.h"

class StatusBar : public QStatusBar {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);
    StatusBar(WidgetFactory* factory, QWidget* parent = nullptr);

public slots:
    // interface
    void setPageInfo(int current, int total);
    void setZoomInfo(double scale);
    void setMessage(const QString& message);

private:
    QLabel* pageLabel;
    QLabel* zoomLabel;
};