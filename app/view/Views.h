#pragma once

#include <QMainWindow>
#include <QPushButton>

#include <QLabel>
#include <QMap>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "delegate/PageNavigationDelegate.h"
class Controller;
class PageModel;

class Views : public QWidget {
    Q_OBJECT
public:
    Views(PageModel* model, PageNavigationDelegate* delegate,
          QWidget* parent = nullptr);

private:
    void initUI();

    PageModel* _model;
    PageNavigationDelegate* _delegate;

    QLabel* _pageLabel;
};
