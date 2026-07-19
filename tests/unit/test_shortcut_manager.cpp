#include <QtTest/QtTest>

#include "../../app/managers/ShortcutManager.h"

class TestShortcutManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testDefaultActionsExist();
    void testConflictDetection();
    void testRegisterActionWithMultipleShortcuts();
};

void TestShortcutManager::initTestCase() {
    ShortcutManager::instance().clear();
    ShortcutManager::instance().registerDefaults();
}

void TestShortcutManager::testDefaultActionsExist() {
    ShortcutManager& manager = ShortcutManager::instance();
    QVERIFY(manager.actionFor(ActionMap::openFile) != nullptr);
    QVERIFY(manager.actionFor(ActionMap::setSinglePageMode) != nullptr);
    QVERIFY(manager.actionFor(ActionMap::showSearch) != nullptr);
    QVERIFY(!manager.shortcutsFor(ActionMap::openFile).isEmpty());
}

void TestShortcutManager::testConflictDetection() {
    ShortcutManager& manager = ShortcutManager::instance();
    QVERIFY(manager.hasConflict(QKeySequence("Ctrl+O")));
    QVERIFY(manager.hasConflict(QKeySequence("Ctrl+1")));
    QVERIFY(!manager.hasConflict(QKeySequence("Ctrl+Shift+1"),
                                 ShortcutManager::Scope::Local));
    QVERIFY(!manager.hasConflict(QKeySequence("Ctrl+Shift+Alt+9")));
}

void TestShortcutManager::testRegisterActionWithMultipleShortcuts() {
    ShortcutManager manager;
    const auto* action = manager.registerAction({ActionMap::goToPage,
                                                 QStringLiteral("跳转页"),
                                                 QStringLiteral("跳转页"),
                                                 QKeySequence("Ctrl+G"),
                                                 {QKeySequence("F4")}});
    QVERIFY(action != nullptr);
    const QList<QKeySequence> shortcuts =
        manager.shortcutsFor(ActionMap::goToPage);
    QCOMPARE(shortcuts.size(), 2);
    QVERIFY(shortcuts.contains(QKeySequence("Ctrl+G")));
    QVERIFY(shortcuts.contains(QKeySequence("F4")));
}

QTEST_MAIN(TestShortcutManager)
#include "test_shortcut_manager.moc"
