#include "Views.h"
#include "model/PageModel.h"

Views::Views(PageModel* model, PageNavigationDelegate* delegate,
             QWidget* parent)
    : QWidget(parent), _model(model), _delegate(delegate) {
    initUI();
}

void Views::initUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);

    _pageLabel =
        new QLabel("Page: " + QString::number(_model->currentPage()), this);
    connect(_delegate, &PageNavigationDelegate::pageTextChanged, _pageLabel,
            &QLabel::setText);
    connect(_model, &PageModel::pageUpdate, _delegate,
            &PageNavigationDelegate::viewUpdate);

    layout->addWidget(_pageLabel);
}
