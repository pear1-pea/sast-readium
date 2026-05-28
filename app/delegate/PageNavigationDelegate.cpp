#include "PageNavigationDelegate.h"

PageNavigationDelegate::PageNavigationDelegate(QObject* parent)
    : QObject(parent) {}

void PageNavigationDelegate::viewUpdate(int pageNum) {
    emit pageTextChanged("Page: " + QString::number(pageNum));
}
