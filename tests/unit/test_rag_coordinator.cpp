#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QMutex>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>
#include <memory>
#include <utility>

#include "model/DocumentModel.h"
#include "rag/chat/ChatProvider.h"
#include "rag/config/RagSettings.h"
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

    void fail(const QString& error) { emit failed(error); }

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

struct FakeEmbeddingState {
    QHash<QString, int> embedCalls;
    QStringList models;
    QString endpoint;
};

class FakeEmbeddingProvider : public Rag::EmbeddingProvider {
public:
    FakeEmbeddingProvider(QString model, QString version,
                          std::shared_ptr<FakeEmbeddingState> state)
        : m_model(std::move(model)),
          m_version(std::move(version)),
          m_state(std::move(state)) {}

    QString modelId() const override { return m_model; }
    QString modelVersion() const override { return m_version; }

    QVector<QVector<float>> embed(const QStringList& texts,
                                  const Rag::RagCancellationToken&,
                                  QString*) override {
        ++m_state->embedCalls[m_model];
        QVector<QVector<float>> vectors;
        vectors.reserve(texts.size());
        for (int i = 0; i < texts.size(); ++i) {
            vectors.append({1.0f, 0.0f, 0.0f});
        }
        return vectors;
    }

private:
    QString m_model;
    QString m_version;
    std::shared_ptr<FakeEmbeddingState> m_state;
};

struct GatedEmbeddingState {
    QSemaphore gate;
    std::atomic_bool blockFirst{true};
    QMutex mutex;
    QStringList endpoints;
};

class GatedEmbeddingProvider : public Rag::EmbeddingProvider {
public:
    GatedEmbeddingProvider(QString model, QString version, QString endpoint,
                           std::shared_ptr<GatedEmbeddingState> state)
        : m_model(std::move(model)),
          m_version(std::move(version)),
          m_endpoint(std::move(endpoint)),
          m_state(std::move(state)) {}

    QString modelId() const override { return m_model; }
    QString modelVersion() const override { return m_version; }

    QVector<QVector<float>> embed(const QStringList& texts,
                                  const Rag::RagCancellationToken&,
                                  QString*) override {
        {
            QMutexLocker lock(&m_state->mutex);
            m_state->endpoints.append(m_endpoint);
        }
        if (m_state->blockFirst.exchange(false)) {
            m_state->gate.tryAcquire(1, 5000);
        }
        QVector<QVector<float>> vectors;
        vectors.reserve(texts.size());
        for (int i = 0; i < texts.size(); ++i) {
            vectors.append({1.0f, 0.0f, 0.0f});
        }
        return vectors;
    }

private:
    QString m_model;
    QString m_version;
    QString m_endpoint;
    std::shared_ptr<GatedEmbeddingState> m_state;
};

Rag::RagSettings settingsWith(QString chatModel, bool answerEnabled,
                              QString embedModel = {},
                              QString embedVersion = QStringLiteral("1")) {
    Rag::RagSettings settings;
    settings.chatModel = std::move(chatModel);
    settings.answerEnabled = answerEnabled;
    settings.embedModel = std::move(embedModel);
    settings.embedVersion = std::move(embedVersion);
    return settings;
}

}  // namespace

class TestRagCoordinator : public QObject {
    Q_OBJECT

private slots:
    void staleAnswersAreDropped();
    void clearFailureIsReported();
    void embeddingSettingsFollowModel();
    void chatSettingsKeepAnswerCitations();
    void endpointChangeRestartsActiveEmbedding();
    void answerDisabledStillReturnsEvidence();
    void zeroEvidenceDoesNotGenerateAnswer();
    void retryAnswerUsesLastEvidence();
    void retryQueryRerunsSearch();
    void embeddingFailureKeepsLexicalEvidence();
};

void TestRagCoordinator::staleAnswersAreDropped() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

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
    int embeddingFactoryCalls = 0;
    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        [fake]() -> std::unique_ptr<Rag::ChatProvider> {
            return std::make_unique<FakeChatProvider>(fake);
        },
        [&embeddingFactoryCalls](
            const QString&, const QString&, const QString&,
            int) -> std::unique_ptr<Rag::EmbeddingProvider> {
            ++embeddingFactoryCalls;
            return nullptr;
        },
        settingsWith(QStringLiteral("fake-chat"), true));

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

    // Embedding state is Disabled throughout this fixture; the query path must
    // not attempt to create an embedding provider.
    QCOMPARE(embeddingFactoryCalls, 0);
}

void TestRagCoordinator::clearFailureIsReported() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString invalidDatabase = directory.filePath("database-dir");
    QVERIFY(QDir().mkpath(invalidDatabase));

    Rag::RagDocumentCoordinator coordinator(nullptr, invalidDatabase);
    QSignalSpy failed(&coordinator,
                      &Rag::RagDocumentCoordinator::indexClearFailed);
    QSignalSpy cleared(&coordinator,
                       &Rag::RagDocumentCoordinator::indexCleared);

    coordinator.clearAllIndexes();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QCOMPARE(cleared.count(), 0);
    QVERIFY(!failed.first().first().toString().isEmpty());
}

void TestRagCoordinator::embeddingSettingsFollowModel() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf =
        writePdf(directory.filePath(QStringLiteral("embed.pdf")),
                 QStringLiteral("alpha evidence for embeddings"));

    DocumentModel model;
    auto embeddingState = std::make_shared<FakeEmbeddingState>();
    Rag::RagSettings settings =
        settingsWith({}, false, QStringLiteral("model-a"));

    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        Rag::RagDocumentCoordinator::ChatProviderFactory{},
        [embeddingState](const QString& modelId, const QString& version,
                         const QString& endpoint,
                         int) -> std::unique_ptr<Rag::EmbeddingProvider> {
            embeddingState->models.append(modelId);
            embeddingState->endpoint = endpoint;
            return std::make_unique<FakeEmbeddingProvider>(modelId, version,
                                                           embeddingState);
        },
        settings);

    auto waitFor = [](auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (predicate()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return false;
    };
    auto waitIndex = [&]() {
        return waitFor([&coordinator]() {
            return coordinator.currentSnapshot().isReady();
        });
    };
    auto waitEmbedding = [&](const QString& modelId) {
        return waitFor([&coordinator, &modelId]() {
            const auto snapshot = coordinator.currentSnapshot();
            return snapshot.embeddingState == Rag::RagEmbeddingState::Ready &&
                   snapshot.embeddingIdentity.model == modelId;
        });
    };

    QSignalSpy opened(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(pdf));
    QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 20000);
    QVERIFY(waitIndex());
    QVERIFY(waitEmbedding(QStringLiteral("model-a")));

    auto snapshot = coordinator.currentSnapshot();
    QCOMPARE(snapshot.embeddingIdentity.model, QStringLiteral("model-a"));
    QCOMPARE(snapshot.embeddingIdentity.version, QStringLiteral("1"));
    QCOMPARE(embeddingState->endpoint,
             QStringLiteral("http://127.0.0.1:11434"));
    QVERIFY(embeddingState->embedCalls.value(QStringLiteral("model-a")) > 0);
    const int callsForA =
        embeddingState->embedCalls.value(QStringLiteral("model-a"));

    Rag::RagSettings next = settings;
    next.embedModel = QStringLiteral("model-b");
    coordinator.applySettings(next);
    QVERIFY(waitEmbedding(QStringLiteral("model-b")));
    QVERIFY(embeddingState->embedCalls.value(QStringLiteral("model-b")) > 0);

    next.embedModel = QStringLiteral("model-a");
    coordinator.applySettings(next);
    QVERIFY(waitEmbedding(QStringLiteral("model-a")));
    // Switching back reuses the vectors already stored for (model-a, 1).
    QCOMPARE(embeddingState->embedCalls.value(QStringLiteral("model-a")),
             callsForA);

    // An endpoint change keeps the same identity and reuses the vectors
    // without starting another embedding pass.
    const int factoryCallsBefore = embeddingState->models.size();
    next.endpoint = QStringLiteral("http://127.0.0.1:11435");
    coordinator.applySettings(next);
    QTest::qWait(150);
    QCOMPARE(coordinator.currentSnapshot().embeddingState,
             Rag::RagEmbeddingState::Ready);
    QCOMPARE(coordinator.currentSnapshot().embeddingIdentity.model,
             QStringLiteral("model-a"));
    QCOMPARE(embeddingState->models.size(), factoryCallsBefore);
    QCOMPARE(embeddingState->embedCalls.value(QStringLiteral("model-a")),
             callsForA);

    next.embedModel.clear();
    next.embedVersion.clear();
    coordinator.applySettings(next);
    QVERIFY(waitFor([&coordinator]() {
        return coordinator.currentSnapshot().embeddingState ==
               Rag::RagEmbeddingState::Disabled;
    }));
    QVERIFY(coordinator.currentSnapshot().embeddingIdentity.isEmpty());
}

void TestRagCoordinator::chatSettingsKeepAnswerCitations() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf =
        writePdf(directory.filePath(QStringLiteral("chat.pdf")),
                 QStringLiteral("alpha evidence for chat settings"));

    DocumentModel model;
    auto fake = std::make_shared<FakeChatState>();
    Rag::RagSettings settings = settingsWith(QStringLiteral("fake-chat"), true);

    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        [fake]() -> std::unique_ptr<Rag::ChatProvider> {
            return std::make_unique<FakeChatProvider>(fake);
        },
        {}, settings);

    auto waitFor = [](auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (predicate()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return false;
    };

    QSignalSpy opened(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(pdf));
    QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 20000);
    QVERIFY(waitFor(
        [&coordinator]() { return coordinator.currentSnapshot().isReady(); }));

    int answerCount = 0;
    QString chunkId;
    Rag::RagEvidence citation;
    connect(&coordinator, &Rag::RagDocumentCoordinator::answerReady, this,
            [&answerCount](const Rag::RagAnswer&) { ++answerCount; });

    fake->task = nullptr;
    coordinator.queryCurrentDocument(QStringLiteral("alpha"));
    QVERIFY(waitFor([&fake]() { return !fake->task.isNull(); }));
    QVERIFY(!fake->lastRequest.evidence.isEmpty());
    chunkId = fake->lastRequest.evidence.first().chunkId;
    fake->task->finish(QStringLiteral("Answer [[cite:%1]]").arg(chunkId));
    QTRY_COMPARE_WITH_TIMEOUT(answerCount, 1, 5000);

    int resetCount = 0;
    int citationCount = 0;
    connect(&coordinator, &Rag::RagDocumentCoordinator::answerReset, this,
            [&resetCount]() { ++resetCount; });
    connect(&coordinator, &Rag::RagDocumentCoordinator::citationReady, this,
            [&citationCount](const Rag::RagEvidence& evidence) {
                ++citationCount;
                Q_UNUSED(evidence);
            });

    // A chat-only change keeps the completed answer and its citations.
    Rag::RagSettings next = settings;
    next.chatModel = QStringLiteral("another-chat");
    coordinator.applySettings(next);
    QCOMPARE(resetCount, 0);
    coordinator.activateCitation(chunkId);
    QCOMPARE(citationCount, 1);

    // A chat-only change must not cancel an evidence query already running,
    // and the new query gets an empty conversation context.
    fake->task = nullptr;
    coordinator.queryCurrentDocument(QStringLiteral("alpha"));
    const int resetAfterQueryStart = resetCount;
    Rag::RagSettings other = next;
    other.chatModel = QStringLiteral("third-chat");
    coordinator.applySettings(other);
    QCOMPARE(resetCount, resetAfterQueryStart);
    QVERIFY(waitFor([&fake]() { return !fake->task.isNull(); }));
    QVERIFY(fake->lastRequest.conversation.isEmpty());
}

void TestRagCoordinator::endpointChangeRestartsActiveEmbedding() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf =
        writePdf(directory.filePath(QStringLiteral("endpoint.pdf")),
                 QStringLiteral("alpha evidence for endpoint switch"));

    DocumentModel model;
    auto state = std::make_shared<GatedEmbeddingState>();
    Rag::RagSettings settings =
        settingsWith({}, false, QStringLiteral("model-a"));

    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        Rag::RagDocumentCoordinator::ChatProviderFactory{},
        [state](const QString& modelId, const QString& version,
                const QString& endpoint,
                int) -> std::unique_ptr<Rag::EmbeddingProvider> {
            return std::make_unique<GatedEmbeddingProvider>(modelId, version,
                                                            endpoint, state);
        },
        settings);

    auto waitFor = [](auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (predicate()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return false;
    };

    QSignalSpy opened(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(pdf));
    QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 20000);
    QVERIFY(waitFor(
        [&coordinator]() { return coordinator.currentSnapshot().isReady(); }));
    QVERIFY(waitFor([&state]() {
        QMutexLocker lock(&state->mutex);
        return state->endpoints.size() == 1;
    }));

    Rag::RagSettings next = settings;
    next.endpoint = QStringLiteral("http://127.0.0.1:11435");
    coordinator.applySettings(next);
    state->gate.release();

    QVERIFY(waitFor([&coordinator, &state]() {
        QMutexLocker lock(&state->mutex);
        return coordinator.currentSnapshot().embeddingState ==
                   Rag::RagEmbeddingState::Ready &&
               state->endpoints.contains(
                   QStringLiteral("http://127.0.0.1:11435"));
    }));
    QStringList endpoints;
    {
        QMutexLocker lock(&state->mutex);
        endpoints = state->endpoints;
    }
    QVERIFY(endpoints.first() == QStringLiteral("http://127.0.0.1:11434"));
    QCOMPARE(coordinator.currentSnapshot().embeddingIdentity.model,
             QStringLiteral("model-a"));
}

void TestRagCoordinator::answerDisabledStillReturnsEvidence() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf =
        writePdf(directory.filePath(QStringLiteral("evidence.pdf")),
                 QStringLiteral("alpha evidence without an answer"));

    DocumentModel model;
    auto fake = std::make_shared<FakeChatState>();
    Rag::RagSettings settings = settingsWith(QStringLiteral("fake-chat"), true);

    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        [fake]() -> std::unique_ptr<Rag::ChatProvider> {
            return std::make_unique<FakeChatProvider>(fake);
        },
        {}, settings);

    auto waitFor = [](auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (predicate()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return false;
    };

    QSignalSpy opened(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(pdf));
    QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 20000);
    QVERIFY(waitFor(
        [&coordinator]() { return coordinator.currentSnapshot().isReady(); }));

    Rag::RagSettings next = settings;
    next.answerEnabled = false;
    coordinator.applySettings(next);

    int evidenceCount = 0;
    connect(&coordinator, &Rag::RagDocumentCoordinator::evidenceReady, this,
            [&evidenceCount](const QVector<Rag::RagEvidence>& evidence) {
                if (!evidence.isEmpty()) {
                    ++evidenceCount;
                }
            });

    fake->task = nullptr;
    coordinator.queryCurrentDocument(QStringLiteral("alpha"));
    QVERIFY(waitFor([&evidenceCount]() { return evidenceCount == 1; }));
    QVERIFY(fake->task.isNull());
}

void TestRagCoordinator::embeddingFailureKeepsLexicalEvidence() {
    class OverContextEmbedding : public Rag::EmbeddingProvider {
    public:
        QString modelId() const override { return QStringLiteral("fixture"); }
        QString modelVersion() const override { return QStringLiteral("1"); }
        QVector<QVector<float>> embed(const QStringList&,
                                      const Rag::RagCancellationToken&,
                                      QString* error) override {
            if (error)
                *error = QStringLiteral(
                    "Embedding request failed (HTTP 400): input length exceeds "
                    "maximum context length");
            return {};
        }
    };

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf =
        writePdf(directory.filePath(QStringLiteral("embedding-failed.pdf")),
                 QStringLiteral("alpha evidence remains available"));
    DocumentModel model;
    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr, {},
        [](const QString&, const QString&, const QString&,
           int) -> std::unique_ptr<Rag::EmbeddingProvider> {
            return std::make_unique<OverContextEmbedding>();
        },
        settingsWith({}, false, QStringLiteral("fixture")));

    QVERIFY(model.openFromFile(pdf));
    QTRY_COMPARE_WITH_TIMEOUT(coordinator.currentSnapshot().embeddingState,
                              Rag::RagEmbeddingState::Failed, 20000);
    const auto snapshot = coordinator.currentSnapshot();
    QVERIFY(snapshot.isReady());
    QCOMPARE(snapshot.embeddedChunkCount, 0);
    QVERIFY(snapshot.embeddingError.contains("maximum context length"));

    QVector<Rag::RagEvidence> found;
    connect(&coordinator, &Rag::RagDocumentCoordinator::evidenceReady, this,
            [&found](const QVector<Rag::RagEvidence>& evidence) {
                found = evidence;
            });
    QSignalSpy failed(&coordinator, &Rag::RagDocumentCoordinator::queryFailed);
    coordinator.queryCurrentDocument(QStringLiteral("alpha"));
    QTRY_VERIFY_WITH_TIMEOUT(!found.isEmpty(), 15000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(found.first().text.contains("alpha"));
    QCOMPARE(coordinator.currentSnapshot().embeddingState,
             Rag::RagEmbeddingState::Failed);
}

void TestRagCoordinator::zeroEvidenceDoesNotGenerateAnswer() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf =
        writePdf(directory.filePath(QStringLiteral("empty.pdf")),
                 QStringLiteral("alpha evidence for the empty query"));

    DocumentModel model;
    auto fake = std::make_shared<FakeChatState>();
    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        [fake]() -> std::unique_ptr<Rag::ChatProvider> {
            return std::make_unique<FakeChatProvider>(fake);
        },
        {}, settingsWith(QStringLiteral("fake-chat"), true));

    auto waitFor = [](auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (predicate()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return false;
    };

    QSignalSpy opened(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(pdf));
    QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 20000);
    QVERIFY(waitFor(
        [&coordinator]() { return coordinator.currentSnapshot().isReady(); }));

    bool busy = false;
    connect(&coordinator, &Rag::RagDocumentCoordinator::answerBusyChanged, this,
            [&busy](bool value) { busy = value; });
    QVector<Rag::RagEvidence> lastEvidence;
    connect(&coordinator, &Rag::RagDocumentCoordinator::evidenceReady, this,
            [&lastEvidence](const QVector<Rag::RagEvidence>& evidence) {
                lastEvidence = evidence;
            });

    fake->task = nullptr;
    coordinator.queryCurrentDocument(QStringLiteral("zzzzzznotfound"));
    QVERIFY(waitFor([&busy]() { return !busy; }));
    QVERIFY(lastEvidence.isEmpty());
    QVERIFY(fake->task.isNull());
}

void TestRagCoordinator::retryAnswerUsesLastEvidence() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf =
        writePdf(directory.filePath(QStringLiteral("retry.pdf")),
                 QStringLiteral("alpha evidence for retrying answers"));

    DocumentModel model;
    auto fake = std::make_shared<FakeChatState>();
    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        [fake]() -> std::unique_ptr<Rag::ChatProvider> {
            return std::make_unique<FakeChatProvider>(fake);
        },
        {}, settingsWith(QStringLiteral("fake-chat"), true));

    auto waitFor = [](auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (predicate()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return false;
    };

    QSignalSpy opened(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(pdf));
    QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 20000);
    QVERIFY(waitFor(
        [&coordinator]() { return coordinator.currentSnapshot().isReady(); }));

    fake->task = nullptr;
    coordinator.queryCurrentDocument(QStringLiteral("alpha"));
    QVERIFY(waitFor([&fake]() { return !fake->task.isNull(); }));
    QVERIFY(!fake->lastRequest.evidence.isEmpty());
    const QString chunkId = fake->lastRequest.evidence.first().chunkId;

    int failedCount = 0;
    connect(&coordinator, &Rag::RagDocumentCoordinator::answerFailed, this,
            [&failedCount](const QString&) { ++failedCount; });
    fake->task->fail(QStringLiteral("boom"));
    QTRY_COMPARE_WITH_TIMEOUT(failedCount, 1, 5000);

    fake->task = nullptr;
    coordinator.retryAnswer();
    QVERIFY(waitFor([&fake]() { return !fake->task.isNull(); }));
    QCOMPARE(fake->lastRequest.evidence.first().chunkId, chunkId);
}

void TestRagCoordinator::retryQueryRerunsSearch() {
    qunsetenv("READIUM_OLLAMA_EMBED_MODEL");
    qunsetenv("READIUM_OLLAMA_EMBED_VERSION");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf =
        writePdf(directory.filePath(QStringLiteral("requery.pdf")),
                 QStringLiteral("alpha evidence for retrying queries"));

    DocumentModel model;
    auto fake = std::make_shared<FakeChatState>();
    Rag::RagDocumentCoordinator coordinator(
        &model, directory.filePath(QStringLiteral("rag.sqlite")), nullptr,
        [fake]() -> std::unique_ptr<Rag::ChatProvider> {
            return std::make_unique<FakeChatProvider>(fake);
        },
        {}, settingsWith(QStringLiteral("fake-chat"), true));

    auto waitFor = [](auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (predicate()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        return false;
    };

    QSignalSpy opened(&model, &DocumentModel::documentOpened);
    QVERIFY(model.openFromFile(pdf));
    QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 20000);
    QVERIFY(waitFor(
        [&coordinator]() { return coordinator.currentSnapshot().isReady(); }));

    fake->task = nullptr;
    coordinator.queryCurrentDocument(QStringLiteral("alpha"));
    QVERIFY(waitFor([&fake]() { return !fake->task.isNull(); }));

    fake->task = nullptr;
    coordinator.retryQuery();
    QVERIFY(waitFor([&fake]() { return !fake->task.isNull(); }));
    QVERIFY(!fake->lastRequest.evidence.isEmpty());
}

QTEST_MAIN(TestRagCoordinator)
#include "test_rag_coordinator.moc"
