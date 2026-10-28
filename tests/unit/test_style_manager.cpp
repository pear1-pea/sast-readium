#include <QtTest/QtTest>

#include "../../app/managers/StyleManager.h"

class TestStyleManager : public QObject {
    Q_OBJECT

private slots:
    void buttonStyleHasResolvedTokens();
};

void TestStyleManager::buttonStyleHasResolvedTokens() {
    StyleManager& styleManager = StyleManager::instance();

    for (const Theme theme : {Theme::Light, Theme::Dark}) {
        styleManager.setTheme(theme);

        const QString style = styleManager.getButtonStyleSheet();

        QVERIFY2(!style.contains(QStringLiteral("@surface@")),
                 qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("@border@")),
                 qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("@radius@")),
                 qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("@text@")), qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("@hover@")), qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("@accent@")),
                 qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("@pressed@")),
                 qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("@textSecondary@")),
                 qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("background-color: 80")),
                 qPrintable(style));
        QVERIFY2(!style.contains(QStringLiteral("border-color: 32")),
                 qPrintable(style));
    }
}

QTEST_MAIN(TestStyleManager)
#include "test_style_manager.moc"
