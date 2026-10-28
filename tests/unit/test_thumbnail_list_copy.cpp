#include <QAction>
#include <QKeySequence>
#include <QtTest>

#include "ui/thumbnail/ThumbnailListView.h"

class TestThumbnailListCopy : public QObject {
    Q_OBJECT

private slots:
    void copyShortcutIsScopedToTheList();
};

void TestThumbnailListCopy::copyShortcutIsScopedToTheList() {
    ThumbnailListView view;
    bool found = false;
    for (QAction* action : view.actions()) {
        if (action->shortcut() != QKeySequence(QKeySequence::Copy)) {
            continue;
        }
        found = true;
        // The shortcut must not steal Ctrl/Cmd+C from other widgets in the
        // window, and it must be owned by the list so it fires on focus.
        QCOMPARE(action->shortcutContext(), Qt::WidgetWithChildrenShortcut);
    }
    QVERIFY(found);
}

QTEST_MAIN(TestThumbnailListCopy)
#include "test_thumbnail_list_copy.moc"
