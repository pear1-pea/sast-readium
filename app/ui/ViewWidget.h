#pragma once


#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QImage>
#include <QPixmap>
#include <QVBoxLayout>
#include "qtmetamacros.h"
#include <QMessageBox>

class ViewWidget : public QGraphicsView {
    Q_OBJECT
public:
    ViewWidget(QWidget* parent = nullptr);
    void changeImage(const QImage& image);

public slots:
    void zoomIn();
    void zoomOut();

signals:
    void scaleChanged(double scale);

private:
    QGraphicsView* view;
    QGraphicsScene* scene;
    QGraphicsPixmapItem* pixmapItem;
    QVBoxLayout* layout;
    double currentScale;
    const double maxScale;
    const double minScale;
};