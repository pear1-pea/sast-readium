#include <QtTest/QtTest>

#include <algorithm>
#include <vector>

#include "../../app/ui/thumbnail/ThumbnailGenerator.h"

class TestThumbnailPriority : public QObject {
    Q_OBJECT

private slots:
    void testLowerPriorityValueComesFirst();
    void testEarlierRequestWinsWhenPriorityMatches();
};

void TestThumbnailPriority::testLowerPriorityValueComesFirst() {
    std::vector<ThumbnailGenerator::GenerationRequest> requests{
        {12, QSize(120, 143), 1.0, 1},
        {10, QSize(120, 143), 1.0, 0},
        {11, QSize(120, 143), 1.0, 5},
    };

    std::sort(requests.begin(), requests.end());

    QCOMPARE(requests[0].pageNumber, 10);
    QCOMPARE(requests[0].priority, 0);
    QCOMPARE(requests[1].pageNumber, 12);
    QCOMPARE(requests[1].priority, 1);
    QCOMPARE(requests[2].pageNumber, 11);
    QCOMPARE(requests[2].priority, 5);
}

void TestThumbnailPriority::testEarlierRequestWinsWhenPriorityMatches() {
    ThumbnailGenerator::GenerationRequest first(10, QSize(120, 143), 1.0, 0);
    ThumbnailGenerator::GenerationRequest second(11, QSize(120, 143), 1.0, 0);
    first.timestamp = 100;
    second.timestamp = 200;

    QVERIFY(first < second);
    QVERIFY(!(second < first));
}

QTEST_MAIN(TestThumbnailPriority)
#include "test_thumbnail_priority.moc"
