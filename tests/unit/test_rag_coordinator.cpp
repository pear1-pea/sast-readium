#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>
#include <utility>

#include "model/DocumentModel.h"
#include "rag/chat/ChatProvider.h"
#include "rag/controller/RagDocumentCoordinator.h"

namespace {

class FakeChatTask : public Rag::ChatTask {
public:
    using Rag::ChatTask::ChatTask;

    void start() override {}

    void cancel() override {
        m_cancelled = true;
        emit cancelled();
    }

    bool isCancelled() const { return m_cancelled; }

    void finish(const QString& answer) {
        emit deltaReceived(QStringLiteral("partial "));
        emit completed(answer);
    }

private:
    bool m_cancelled = false;
};

struct FakeChatState {
    QPointer<FakeChatTask> task;
    Rag::ChatRequest lastRequest;
};

class FakeChatProvider : public Rag::ChatProvider {
public:
    explicit FakeChatProvider(std::shared_ptr<FakeChatState> state)
        : m_state(std::move(state)) {}

    Rag::ChatTask* generate(const Rag::ChatRequest& request,
                            QObject* parent) override {
        auto* task = new FakeChatTask(parent);
        m_state->task = task;
        m_state->lastRequest = request;
        return task;
    }

private:
    std::shared_ptr<FakeChatState> m_state;
};

QString writePdf(const QString& path, const QString& text) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(72);

    QPainter painter(&writer);
    painter.setFont(QFont(QStringLiteral("Helvetica"), 14));
    painter.drawText(QRectF(50, 50, 500, 200),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    painter.end();
    return path;
}

}  // namespace

class TestRagCoordinator : public QObject {
    Q_OBJECT

private slots:
    void staleAnswersAreDropped();
};

void TestRagCoordinator::staleAnswersAreDropped() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString documentA =
        writePdf(directory.filePath(QStringLiteral("alpha.pdf")),
                 QStringLiteral("alpha evidence from document one"));
    const QString documentB =
        writePdf(directory.filePath(QStringLiteral("beta.pdf")),
                 QStringLiteral("beta evidence from document two"));

    DocumentModel model;
    auto fake = std::make_shared<FakeChatState>();
    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        [fake]() -> std::unique_ptr<Rag::ChatProvider> {
            return std::make_unique<FakeChatProvider>(fake);
        });

    auto waitReady = [&coordinator]() {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            const Rag::RagDocumentSnapshot snapshot =
                coordinator.currentSnapshot();
            if (snapshot.isReady()) {
                return true;
            }
            if (snapshot.state == Rag::RagIndexState::Failed ||
                snapshot.state == Rag::RagIndexState::Unsupported) {
                qWarning() << "Indexing failed:" << snapshot.error;
                return false;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return false;
    };

    auto startAnswer = [&coordinator, &fake](const QString& query) {
        fake->task = nullptr;
        coordinator.queryCurrentDocument(query);
        QElapsedTimer timer;
        timer.start();
        while (!fake->task && timer.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return fake->task.data();
    };

    int answerCount = 0;
    int resetCount = 0;
    Rag::RagAnswer lastAnswer;
    connect(&coordinator, &Rag::RagDocumentCoordinator::answerReady, this,
            [&answerCount, &lastAnswer](const Rag::RagAnswer& answer) {
                ++answerCount;
                lastAnswer = answer;
            });
    connect(&coordinator, &Rag::RagDocumentCoordinator::answerReset, this,
            [&resetCount]() { ++resetCount; });

    QSignalSpy openedA(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(documentA));
    QTRY_COMPARE_WITH_TIMEOUT(openedA.count(), 1, 20000);
    QVERIFY2(waitReady(), "document A was not indexed");

    QSignalSpy openedB(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(documentB));
    QTRY_COMPARE_WITH_TIMEOUT(openedB.count(), 1, 20000);
    QVERIFY2(waitReady(), "document B was not indexed");
    QCOMPARE(model.getDocumentCount(), 2);

    QVERIFY(model.getCurrentFilePath().endsWith(QStringLiteral("beta.pdf")));
    model.switchToDocument(0);
    QVERIFY2(waitReady(), "document A was not restored");

    auto* task = startAnswer(QStringLiteral("alpha"));
    QVERIFY(task);
    QVERIFY(!fake->lastRequest.evidence.isEmpty());

    const QString chunkId = fake->lastRequest.evidence.first().chunkId;
    const int pageStart = fake->lastRequest.evidence.first().pageStart;
    task->finish(QStringLiteral("Answer [[cite:%1]]").arg(chunkId));
    QTRY_COMPARE_WITH_TIMEOUT(answerCount, 1, 5000);
    QCOMPARE(lastAnswer.citations.size(), 1);
    QCOMPARE(lastAnswer.citations.first().chunkId, chunkId);
    QCOMPARE(lastAnswer.citations.first().pageStart, pageStart);
    QVERIFY(!lastAnswer.citations.first().snippet.isEmpty());

    int citationCount = 0;
    Rag::RagEvidence activatedEvidence;
    connect(
        &coordinator, &Rag::RagDocumentCoordinator::citationReady, this,
        [&citationCount, &activatedEvidence](const Rag::RagEvidence& evidence) {
            ++citationCount;
            activatedEvidence = evidence;
        });
    coordinator.activateCitation(chunkId);
    QCOMPARE(citationCount, 1);
    QCOMPARE(activatedEvidence.chunkId, chunkId);
    QVERIFY(!activatedEvidence.locations.isEmpty());

    const int answerCountBeforeSwitch = answerCount;
    task = startAnswer(QStringLiteral("alpha"));
    QVERIFY(task);
    model.switchToDocument(1);
    task->finish(QStringLiteral("stale answer"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QCOMPARE(answerCount, answerCountBeforeSwitch);

    model.switchToDocument(0);
    QVERIFY2(waitReady(), "document A was not restored after tab switch");
    const int answerCountBeforeCancel = answerCount;
    const int resetCountBeforeCancel = resetCount;
    task = startAnswer(QStringLiteral("alpha"));
    QVERIFY(task);
    coordinator.cancelAnswer();
    QVERIFY(resetCount > resetCountBeforeCancel);
    task->finish(QStringLiteral("cancelled answer"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QCOMPARE(answerCount, answerCountBeforeCancel);

    const int answerCountBeforeClose = answerCount;
    task = startAnswer(QStringLiteral("alpha"));
    QVERIFY(task);
    QVERIFY(model.closeDocument(0));
    task->finish(QStringLiteral("closed document answer"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QCOMPARE(answerCount, answerCountBeforeClose);
}

QTEST_MAIN(TestRagCoordinator)
#include "test_rag_coordinator.moc"
