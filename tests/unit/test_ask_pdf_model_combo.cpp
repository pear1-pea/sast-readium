#include <QComboBox>
#include <QSignalSpy>
#include <QtTest>

#include "rag/ui/AskPdfModelCombo.h"

using namespace Rag;

class TestAskPdfModelCombo : public QObject {
    Q_OBJECT

private slots:
    void unknownModelKeepsRawTag();
    void typedTagWinsOverStaleSelection();
    void refreshKeepsTagOutsideTheList();
    void refreshKeepsEmptySelectionEmpty();
    void refreshRestoresKnownSelectionWithoutSignals();
};

void TestAskPdfModelCombo::unknownModelKeepsRawTag() {
    // An annotated item must still save and request the raw tag.
    QComboBox combo;
    combo.setEditable(true);
    ModelCombo::populate(&combo, {"llama3:latest"}, {"llama3:latest"});
    QCOMPARE(combo.count(), 1);
    // An empty selection must stay empty, so selecting is a user action.
    combo.setCurrentIndex(0);
    QVERIFY(combo.itemText(0).contains(QStringLiteral("能力未知")));
    QVERIFY(!combo.itemData(0, Qt::ToolTipRole).toString().isEmpty());
    QCOMPARE(ModelCombo::selectedTag(&combo), QStringLiteral("llama3:latest"));
}

void TestAskPdfModelCombo::typedTagWinsOverStaleSelection() {
    // Typing replaces the line edit text without moving currentIndex. The
    // stale item data must not override what the user typed.
    QComboBox combo;
    combo.setEditable(true);
    ModelCombo::populate(&combo, {"llama3:latest"}, {"llama3:latest"});
    combo.setCurrentIndex(0);
    combo.setEditText(QStringLiteral("custom:tag"));
    QCOMPARE(ModelCombo::selectedTag(&combo), QStringLiteral("custom:tag"));
}

void TestAskPdfModelCombo::refreshKeepsTagOutsideTheList() {
    // A saved model that the refreshed list does not contain stays selectable.
    QComboBox combo;
    combo.setEditable(true);
    combo.setEditText(QStringLiteral("custom:tag"));
    ModelCombo::populate(&combo, {"nomic-embed-text:latest"}, {});
    QCOMPARE(combo.currentIndex(), -1);
    QCOMPARE(combo.currentText(), QStringLiteral("custom:tag"));
    QCOMPARE(ModelCombo::selectedTag(&combo), QStringLiteral("custom:tag"));
}

void TestAskPdfModelCombo::refreshKeepsEmptySelectionEmpty() {
    // Refreshing must not silently choose the first model.
    QComboBox combo;
    combo.setEditable(true);
    ModelCombo::populate(&combo, {"nomic-embed-text:latest"}, {});
    QCOMPARE(combo.currentIndex(), -1);
    QVERIFY(combo.currentText().isEmpty());
    QVERIFY(ModelCombo::selectedTag(&combo).isEmpty());
}

void TestAskPdfModelCombo::refreshRestoresKnownSelectionWithoutSignals() {
    QComboBox combo;
    combo.setEditable(true);
    ModelCombo::populate(&combo, {"llama3:latest", "nomic-embed-text:latest"},
                         {});
    combo.setCurrentIndex(1);
    QCOMPARE(ModelCombo::selectedTag(&combo),
             QStringLiteral("nomic-embed-text:latest"));

    QSignalSpy editSpy(&combo, &QComboBox::editTextChanged);
    QSignalSpy indexSpy(&combo, &QComboBox::currentIndexChanged);
    ModelCombo::populate(&combo, {"nomic-embed-text:latest", "llama3:latest"},
                         {});
    QCOMPARE(combo.currentIndex(), 0);
    QCOMPARE(ModelCombo::selectedTag(&combo),
             QStringLiteral("nomic-embed-text:latest"));
    // Refreshing the list is not a user edit and must not cancel probes.
    QCOMPARE(editSpy.count(), 0);
    QCOMPARE(indexSpy.count(), 0);
}

QTEST_MAIN(TestAskPdfModelCombo)
#include "test_ask_pdf_model_combo.moc"
