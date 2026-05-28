#pragma once

#include <QObject>
#include <QString>

class PageNavigationDelegate : public QObject {
    Q_OBJECT

public:
    explicit PageNavigationDelegate(QObject* parent = nullptr);
    ~PageNavigationDelegate() override = default;

signals:
    void pageTextChanged(const QString& text);

public slots:
    void viewUpdate(int pageNum);
};
