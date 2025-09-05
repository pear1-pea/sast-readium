#pragma once

#include <QWidget>

namespace Ui {
class Widget;
}  // namespace Ui

class Widget : public QWidget {
    Q_OBJECT
public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() = default;

private slots:
    void exitApp();
    void applyEnglishLang(bool);
    void applyChineseLang(bool);

    void applyTheme(const QString &theme);

private:
    // 0: english; 1: 中文
    void applyLang(int langId);

private:
    Ui::Widget *UI;

    QAction *ActionLightTheme;
    QAction *ActionDarkTheme;
};