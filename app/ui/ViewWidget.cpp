
#include "ViewWidget.h"
#include <QLabel>
#include <QVBoxLayout>
#include "qmessagebox.h"

ViewWidget::ViewWidget(QWidget* parent) : QGraphicsView(parent),
currentScale(1.0), maxScale(4.0), minScale(0.25)  {
    scene = new QGraphicsScene(this);
    this->setScene(scene);
    this->setRenderHint(QPainter::Antialiasing);
    this->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    this->setDragMode(QGraphicsView::ScrollHandDrag);
}


void ViewWidget::changeImage(const QImage& image) {
    if(image.isNull()){
        QMessageBox::warning(this, "Error", "无法渲染页面");
        return;
    }
    QGraphicsPixmapItem* item = new QGraphicsPixmapItem(QPixmap::fromImage(image));
    item->setTransformationMode(Qt::SmoothTransformation);
    scene->clear();
    scene->addItem(item);

    this->fitInView(item, Qt::KeepAspectRatio);
    emit scaleChanged(currentScale);
}

void ViewWidget::zoomIn() {
    double newScale = currentScale * 1.25;
    if (newScale > maxScale) {
        newScale = maxScale;
    }
    scale(newScale / currentScale, newScale / currentScale);
    currentScale = newScale;
    emit scaleChanged(currentScale);
}

void ViewWidget::zoomOut() {
    double newScale = currentScale * 0.8;
    if (newScale < minScale) {
        newScale = minScale;
    }
    scale(newScale / currentScale, newScale / currentScale);
    currentScale = newScale;
    emit scaleChanged(currentScale);
}