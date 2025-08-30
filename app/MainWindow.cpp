#include "MainWindow.h"
#include <QApplication>
#include <QBoxLayout>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include "model/RenderModel.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    applyTheme("dark");

    initWindow();
    initModel();
    initController();
    initContent();
    initConnection();
}

MainWindow::~MainWindow() noexcept {}

// initialize
void MainWindow::initWindow() {
    resize(1280, 800);
}

void MainWindow::initContent() {
    WidgetFactory* factory = new WidgetFactory(pageController, this);

    menuBar = new MenuBar(this);
    toolBar = new ToolBar(this);
    sideBar = new SideBar(this);
    statusBar = new StatusBar(factory, this);
    viewWidget = new ViewWidget(this);

    setMenuBar(menuBar);
    addToolBar(toolBar);
    setStatusBar(statusBar);

    mainSplitter = new QSplitter(Qt::Horizontal, this);

    mainSplitter->addWidget(sideBar);
    mainSplitter->addWidget(viewWidget);

    mainSplitter->setCollapsible(0, true);
    mainSplitter->setCollapsible(1, false);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({200, 1000});

    QWidget* centralWidget = new QWidget(this);
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(mainSplitter);
    setCentralWidget(centralWidget);
}

void MainWindow::initModel() {
    renderModel = new RenderModel(this->logicalDpiX(), this->logicalDpiY());
    documentModel = new DocumentModel(renderModel);
    pageModel = new PageModel(renderModel);
}

void MainWindow::initController() {
    documentController = new DocumentController(documentModel);
    pageController = new PageController(pageModel);
}

void MainWindow::initConnection() {
    connect(menuBar, &MenuBar::themeChanged, this, &MainWindow::applyTheme);

    connect(menuBar, &MenuBar::onExecuted, documentController, &DocumentController::execute);
    connect(menuBar, &MenuBar::onExecuted, this, &MainWindow::handleActionExecuted);

    connect(renderModel, &RenderModel::renderPageDone, viewWidget, &ViewWidget::changeImage);
    connect(renderModel, &RenderModel::documentChanged, pageModel, &PageModel::updateInfo);

    connect(pageModel, &PageModel::pageUpdate, statusBar, &StatusBar::setPageInfo);
    connect(documentModel, &DocumentModel::pageUpdate, statusBar, &StatusBar::setPageInfo);

    connect(viewWidget, &ViewWidget::scaleChanged, statusBar, &StatusBar::setZoomInfo);
}

// function
void MainWindow::applyTheme(const QString& theme) {
    auto path = QString("%1/styles/%2.qss").arg(qApp->applicationDirPath(), theme);

    QFile file(path);
    file.open(QFile::ReadOnly);
    setStyleSheet(QLatin1String(file.readAll()));
}

void MainWindow::handleActionExecuted(ActionMap id) {
    switch (id) {
        case ActionMap::fullScreen:
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
            break;
        case ActionMap::zoomIn:
            viewWidget->zoomIn();
            break;
        case ActionMap::zoomOut:
            viewWidget->zoomOut();
            break;
    }
}