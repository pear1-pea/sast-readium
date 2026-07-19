#include "DocumentTabWidget.h"
#include <QApplication>
#include <QDebug>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

// DocumentTabBar Implementation
DocumentTabBar::DocumentTabBar(QWidget* parent)
    : QTabBar(parent),
      pressedCloseIndex(-1),
      hoveredCloseIndex(-1),
      addButtonPressed(false),
      addButtonHovered(false),
      dragInProgress(false) {
    setAcceptDrops(true);
    setMovable(true);
    setExpanding(false);
    setDrawBase(false);
    setMouseTracking(true);
    setElideMode(Qt::ElideRight);
    setFixedHeight(31);
}

QSize DocumentTabBar::sizeHint() const {
    QSize hint = QTabBar::sizeHint();
    hint.setHeight(31);
    hint.setWidth(qMax(hint.width() + 32, 160));
    return hint;
}

QSize DocumentTabBar::tabSizeHint(int index) const {
    Q_UNUSED(index)
    return QSize(124, 20);
}

QRect DocumentTabBar::addButtonRect() const {
    const QRect lastTab =
        count() > 0 ? tabRect(count() - 1) : QRect(18, 5, 0, 20);
    return QRect(lastTab.right() + 4, 3, 24, 24);
}

QRect DocumentTabBar::closeButtonRect(int index) const {
    const QRect tab = tabRect(index);
    const QRect pillRect(tab.left(), 5, tab.width() - 1, 20);
    const QSize closeSize(16, 16);
    return QRect(QPoint(pillRect.right() - 24,
                        pillRect.center().y() - closeSize.height() / 2 + 2),
                 closeSize);
}

int DocumentTabBar::closeButtonAt(const QPoint& pos) const {
    for (int i = 0; i < count(); ++i) {
        if (closeButtonRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void DocumentTabBar::updateHoverState(const QPoint& pos) {
    const int oldHoveredClose = hoveredCloseIndex;
    const bool oldAddHovered = addButtonHovered;

    hoveredCloseIndex = closeButtonAt(pos);
    addButtonHovered = addButtonRect().contains(pos);

    if (oldHoveredClose != hoveredCloseIndex ||
        oldAddHovered != addButtonHovered) {
        update();
    }
}

void DocumentTabBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(255, 255, 255));

    QPen basePen(QColor(210, 156, 29));
    painter.setPen(basePen);
    painter.drawLine(rect().bottomLeft(), rect().bottomRight());

    const QColor textColor(141, 93, 4);
    const QColor activeBg(255, 185, 119, 82);
    const QColor inactiveBg(255, 221, 27, 82);

    QFont tabFont = font();
    tabFont.setPointSize(13);
    painter.setFont(tabFont);
    const QFontMetrics metrics(tabFont);

    for (int i = 0; i < count(); ++i) {
        QRect pillRect = tabRect(i).adjusted(0, 0, -1, -1);
        pillRect.moveTop(5);
        pillRect.setHeight(20);

        painter.setPen(QPen(QColor(255, 255, 255), 1));
        painter.setBrush(i == currentIndex() ? activeBg : inactiveBg);
        painter.drawRoundedRect(pillRect, 10, 10);

        const QRect closeRect = closeButtonRect(i);
        const QRect textRect(pillRect.left() + 16, pillRect.top(),
                             closeRect.left() - pillRect.left() - 22,
                             pillRect.height());

        painter.setPen(textColor);
        painter.drawText(
            textRect, Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(tabText(i), Qt::ElideRight, textRect.width()));

        if (hoveredCloseIndex == i) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(141, 93, 4, 28));
            painter.drawEllipse(closeRect.adjusted(1, 1, -1, -1));
        }

        QPen closePen(textColor, 1.7, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(closePen);
        const QPoint center = closeRect.center();
        painter.drawLine(center.x() - 4, center.y() - 4, center.x() + 4,
                         center.y() + 4);
        painter.drawLine(center.x() + 4, center.y() - 4, center.x() - 4,
                         center.y() + 4);
    }

    const QRect addRect = addButtonRect();
    const QRect circleRect(addRect.left() + 3, addRect.top() + 3, 17, 17);
    painter.setPen(Qt::NoPen);
    painter.setBrush(addButtonPressed
                         ? QColor(190, 190, 190)
                         : (addButtonHovered ? QColor(205, 205, 205)
                                             : QColor(217, 217, 217)));
    painter.drawEllipse(circleRect);

    QPen plusPen(QColor(56, 56, 56), 2, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(plusPen);
    const QPoint plusCenter = circleRect.center();
    painter.drawLine(plusCenter.x() - 5, plusCenter.y(), plusCenter.x() + 5,
                     plusCenter.y());
    painter.drawLine(plusCenter.x(), plusCenter.y() - 5, plusCenter.x(),
                     plusCenter.y() + 5);
}

void DocumentTabBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragStartPosition = event->pos();
        pressedCloseIndex = closeButtonAt(event->pos());
        addButtonPressed = addButtonRect().contains(event->pos());

        if (pressedCloseIndex >= 0 || addButtonPressed) {
            update();
            event->accept();
            return;
        }
    }
    QTabBar::mousePressEvent(event);
}

void DocumentTabBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const int closeIndex = closeButtonAt(event->pos());
        const bool addClicked =
            addButtonPressed && addButtonRect().contains(event->pos());

        if (pressedCloseIndex >= 0) {
            const int indexToClose = pressedCloseIndex;
            pressedCloseIndex = -1;
            addButtonPressed = false;
            update();
            if (indexToClose == closeIndex) {
                emit tabCloseRequested(indexToClose);
            }
            event->accept();
            return;
        }

        if (addButtonPressed) {
            addButtonPressed = false;
            update();
            if (addClicked) {
                emit newTabRequested();
            }
            event->accept();
            return;
        }
    }

    QTabBar::mouseReleaseEvent(event);
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent* event) {
    updateHoverState(event->pos());

    if (pressedCloseIndex >= 0 || addButtonPressed) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    if (!(event->buttons() & Qt::LeftButton)) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    if ((event->pos() - dragStartPosition).manhattanLength() <
        QApplication::startDragDistance()) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    int tabIndex = tabAt(dragStartPosition);
    if (tabIndex == -1) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    mimeData->setData("application/x-tab-index", QByteArray::number(tabIndex));
    drag->setMimeData(mimeData);

    dragInProgress = true;
    Qt::DropAction dropAction = drag->exec(Qt::MoveAction);
    dragInProgress = false;

    QTabBar::mouseMoveEvent(event);
}

void DocumentTabBar::leaveEvent(QEvent* event) {
    hoveredCloseIndex = -1;
    addButtonHovered = false;
    update();
    QTabBar::leaveEvent(event);
}

void DocumentTabBar::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-tab-index")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DocumentTabBar::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-tab-index")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DocumentTabBar::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat("application/x-tab-index")) {
        event->ignore();
        return;
    }

    int fromIndex = event->mimeData()->data("application/x-tab-index").toInt();
    int toIndex = tabAt(event->position().toPoint());

    if (toIndex == -1) {
        toIndex = count() - 1;
    }

    if (fromIndex != toIndex && fromIndex >= 0 && toIndex >= 0) {
        emit tabMoveRequested(fromIndex, toIndex);
    }

    event->acceptProposedAction();
}

// DocumentTabWidget Implementation
DocumentTabWidget::DocumentTabWidget(QWidget* parent) : QTabWidget(parent) {
    setupTabBar();
    setTabsClosable(false);
    setMovable(true);
    setDocumentMode(true);

    connect(this, &QTabWidget::tabCloseRequested, this,
            &DocumentTabWidget::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this,
            &DocumentTabWidget::tabSwitched);
}

void DocumentTabWidget::setupTabBar() {
    customTabBar = new DocumentTabBar(this);
    setTabBar(customTabBar);

    connect(customTabBar, &DocumentTabBar::tabMoveRequested, this,
            &DocumentTabWidget::onTabMoveRequested);
    connect(customTabBar, &DocumentTabBar::tabCloseRequested, this,
            &DocumentTabWidget::onTabCloseRequested);
    connect(customTabBar, &DocumentTabBar::newTabRequested, this,
            &DocumentTabWidget::onNewTabRequested);
}

int DocumentTabWidget::addDocumentTab(const QString& fileName,
                                      const QString& filePath) {
    QWidget* tabContent = createTabWidget(fileName, filePath);
    int index = addTab(tabContent, fileName);

    tabFilePaths[index] = filePath;

    // 设置工具提示显示完整路径
    setTabToolTip(index, filePath);

    return index;
}

void DocumentTabWidget::removeDocumentTab(int index) {
    if (index >= 0 && index < count()) {
        tabFilePaths.remove(index);

        // 更新其他标签页的索引映射
        QHash<int, QString> newTabFilePaths;
        for (auto it = tabFilePaths.begin(); it != tabFilePaths.end(); ++it) {
            int oldIndex = it.key();
            if (oldIndex > index) {
                newTabFilePaths[oldIndex - 1] = it.value();
            } else if (oldIndex < index) {
                newTabFilePaths[oldIndex] = it.value();
            }
        }
        tabFilePaths = newTabFilePaths;

        removeTab(index);

        if (count() == 0) {
            emit allTabsClosed();
        }
    }
}

void DocumentTabWidget::updateTabText(int index, const QString& fileName) {
    if (index >= 0 && index < count()) {
        setTabText(index, fileName);
    }
}

void DocumentTabWidget::setCurrentTab(int index) {
    if (index >= 0 && index < count()) {
        setCurrentIndex(index);
    }
}

void DocumentTabWidget::setTabLoadingState(int index, bool loading) {
    if (index >= 0 && index < count()) {
        QString currentText = tabText(index);

        if (loading) {
            // 如果不是已经显示加载状态，添加加载标识
            if (!currentText.contains("(加载中...)")) {
                setTabText(index, currentText + " (加载中...)");
            }
        } else {
            // 移除加载状态标识
            QString newText = currentText;
            newText.remove(" (加载中...)");
            setTabText(index, newText);
        }
    }
}

void DocumentTabWidget::moveTab(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= count() || to >= count()) {
        return;
    }

    // 保存标签页信息
    QString text = tabText(from);
    QString toolTip = tabToolTip(from);
    QWidget* widget = this->widget(from);
    QString filePath = tabFilePaths.value(from);

    // 移除原标签页
    removeTab(from);

    // 在新位置插入
    insertTab(to, widget, text);
    setTabToolTip(to, toolTip);

    // 更新文件路径映射
    tabFilePaths.remove(from);
    tabFilePaths[to] = filePath;

    // 设置为当前标签页
    setCurrentIndex(to);
}

QString DocumentTabWidget::getTabFilePath(int index) const {
    return tabFilePaths.value(index, QString());
}

int DocumentTabWidget::getTabCount() const { return count(); }

QWidget* DocumentTabWidget::createTabWidget(const QString& fileName,
                                            const QString& filePath) {
    QWidget* widget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(widget);

    QLabel* label = new QLabel(QString("PDF内容: %1").arg(fileName), widget);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    return widget;
}

void DocumentTabWidget::onTabCloseRequested(int index) {
    emit tabCloseRequested(index);
}

void DocumentTabWidget::onTabMoveRequested(int from, int to) {
    moveTab(from, to);
    emit tabMoved(from, to);
}

void DocumentTabWidget::onNewTabRequested() { emit newTabRequested(); }
