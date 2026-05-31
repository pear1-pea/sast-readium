#include <QSignalSpy>
#include <QtTest/QtTest>
#include "../../app/controller/SearchController.h"

class TestSearchController : public QObject {
    Q_OBJECT

private:
    static QList<SearchResult> makeResults();

private slots:
    void testDefaultState();
    void testSetResults();
    void testClear();
    void testIsEmpty();
    void testResultsForPage();
    void testResultsByPage();
    void testHighlightCurrent();
    void testCurrentResultIndex();
    void testResultsChangedSignal();
};

QList<SearchResult> TestSearchController::makeResults() {
    QList<SearchResult> results;
    results.append(SearchResult(0, "apple", "ctx0", QRectF(), 0, 5));
    results.append(SearchResult(0, "banana", "ctx1", QRectF(), 6, 6));
    results.append(SearchResult(1, "cherry", "ctx2", QRectF(), 0, 6));
    return results;
}

void TestSearchController::testDefaultState() {
    SearchController ctrl;
    QVERIFY(ctrl.isEmpty());
    QCOMPARE(ctrl.currentResultIndex(), -1);
}

void TestSearchController::testSetResults() {
    SearchController ctrl;
    ctrl.setResults(makeResults());

    QVERIFY(!ctrl.isEmpty());
    QCOMPARE(ctrl.currentResultIndex(), -1);
}

void TestSearchController::testClear() {
    SearchController ctrl;
    ctrl.setResults(makeResults());
    QVERIFY(!ctrl.isEmpty());

    ctrl.clear();
    QVERIFY(ctrl.isEmpty());
    QCOMPARE(ctrl.currentResultIndex(), -1);
}

void TestSearchController::testIsEmpty() {
    SearchController ctrl;
    QVERIFY(ctrl.isEmpty());

    ctrl.setResults(makeResults());
    QVERIFY(!ctrl.isEmpty());

    ctrl.clear();
    QVERIFY(ctrl.isEmpty());
}

void TestSearchController::testResultsForPage() {
    SearchController ctrl;
    ctrl.setResults(makeResults());

    auto page0 = ctrl.resultsForPage(0);
    QCOMPARE(page0.size(), 2);

    auto page1 = ctrl.resultsForPage(1);
    QCOMPARE(page1.size(), 1);
    QCOMPARE(page1[0].text, "cherry");

    auto page2 = ctrl.resultsForPage(2);
    QCOMPARE(page2.size(), 0);
}

void TestSearchController::testResultsByPage() {
    SearchController ctrl;
    ctrl.setResults(makeResults());

    auto grouped = ctrl.resultsByPage();
    QCOMPARE(grouped.size(), 2);           // pages 0 and 1
    QCOMPARE(grouped.value(0).size(), 2);  // apple, banana
    QCOMPARE(grouped.value(1).size(), 1);  // cherry
}

void TestSearchController::testHighlightCurrent() {
    SearchController ctrl;
    ctrl.setResults(makeResults());

    SearchResult target(0, "banana", "ctx1", QRectF(), 6, 6);
    ctrl.highlightCurrent(target);

    QCOMPARE(ctrl.currentResultIndex(), 1);

    // Verify resultsForPage marks isCurrentResult
    auto page0 = ctrl.resultsForPage(0);
    QCOMPARE(page0.size(), 2);
    QVERIFY(!page0[0].isCurrentResult);  // apple is not current
    QVERIFY(page0[1].isCurrentResult);   // banana is current
}

void TestSearchController::testCurrentResultIndex() {
    SearchController ctrl;
    QCOMPARE(ctrl.currentResultIndex(), -1);

    ctrl.setResults(makeResults());
    QCOMPARE(ctrl.currentResultIndex(), -1);

    SearchResult target(1, "cherry", "ctx2", QRectF(), 0, 6);
    ctrl.highlightCurrent(target);
    QCOMPARE(ctrl.currentResultIndex(), 2);
}

void TestSearchController::testResultsChangedSignal() {
    SearchController ctrl;
    QSignalSpy spy(&ctrl, &SearchController::resultsChanged);

    ctrl.setResults(makeResults());
    QCOMPARE(spy.count(), 1);

    ctrl.clear();
    QCOMPARE(spy.count(), 2);

    ctrl.highlightCurrent(SearchResult(0, "apple", "ctx0", QRectF(), 0, 5));
    QCOMPARE(spy.count(), 3);
}

QTEST_MAIN(TestSearchController)
#include "test_search_controller.moc"
