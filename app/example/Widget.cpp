#include "Widget.h"
#include <QActionGroup>
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QTranslator>
#include "ui_Widget.h"

Widget::Widget(QWidget *parent) : QWidget(parent), UI(new Ui::Widget) {
    UI->setupUi(this);
    UI->retranslateUi(this);

    ActionLightTheme = UI->toolButton->addAction(tr("Light"));
    ActionDarkTheme = UI->toolButton->addAction(tr("Dark"));

    ActionLightTheme->setCheckable(true);
    ActionDarkTheme->setCheckable(true);

    auto actionGroup = new QActionGroup(this);
    actionGroup->addAction(ActionLightTheme);
    actionGroup->addAction(ActionDarkTheme);

    connect(ActionLightTheme, &QAction::triggered, this, [this](bool checked) {
        if (!checked)
            return;
        applyTheme("light");
    });

    connect(ActionDarkTheme, &QAction::triggered, this, [this](bool checked) {
        if (!checked)
            return;
        applyTheme("dark");
    });

    ActionLightTheme->setChecked(true);
    applyTheme("light");
}

void Widget::exitApp() {
    qDebug() << tr("About to exit app");
    QApplication::exit();
}

void Widget::applyEnglishLang(bool enable) {
    if (!enable)
        return;
    applyLang(0);
}

void Widget::applyChineseLang(bool enable) {
    if (!enable)
        return;
    applyLang(1);
}

void Widget::applyLang(int langId) {
    QTranslator translator;
    auto qm = langId == 0 ? "app_en.qm" : "app_zh.qm";
    auto path = QString("%1/%2").arg(qApp->applicationDirPath(), qm);

    Q_UNUSED(translator.load(qm));
    qApp->installTranslator(&translator);

    UI->retranslateUi(this);

    // 非 .ui 上的文本需要自行处理语言切换。
    // 在 .ts 文件中，出现多次的翻译，只会添加一条，并纪录其出现的多个位置。
    ActionLightTheme->setText(tr("Light"));
    ActionDarkTheme->setText(tr("Dark"));
}

void Widget::applyTheme(const QString &theme) {
    auto path =
        QString("%1/styles/%2.qss").arg(qApp->applicationDirPath(), theme);

    QFile file(path);
    if (file.exists() && file.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(file.readAll());
        file.close();

        if (!styleSheet.isEmpty()) {
            setStyleSheet(styleSheet);
            return;
        }
    }

    // 如果外部文件不可用，使用基本样式
    qWarning() << "External theme file not found or empty:" << path;
    setStyleSheet("");  // 使用默认样式
}
