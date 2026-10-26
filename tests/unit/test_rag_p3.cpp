#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextBrowser>
#include <QToolButton>
#include <QtTest>
#include "rag/chat/ContextAssembler.h"
#include "rag/chat/OllamaChatProvider.h"
#include "rag/controller/RagConversationModel.h"
#include "rag/domain/RagConversation.h"
#include "rag/embedding/OllamaEmbeddingProvider.h"
#include "rag/ui/AskPdfConversationView.h"
#include "rag/ui/AskPdfProgress.h"
#include "rag/ui/AskPdfState.h"
#include "rag/ui/AskPdfTurnCard.h"
#include "rag/ui/AskPdfWidget.h"
using namespace Rag;
class TestRagP3 : public QObject {
    Q_OBJECT
private slots:
    void askPdfStateCopy();
    void askPdfStatePanelActions();
    void askPdfProgressCopy();
    void askPdfStatusTextCopy();
    void progressBarReflectsIndexing();
    void turnCardActions();
    void turnCardMarkdownAndCollapse();
    void conversationViewFollowsModel();
    void askPdfWidgetSubmitsAndClearsConversation();
    void statusAndSettingsButton();
    void citationLabels() {
        RagConversationTurn turn;
        turn.id = "turn_a";
        turn.state = RagTurnState::Ready;
        turn.answerText = "answer";

        RagCitation first;
        first.chunkId = "chunk_a";
        first.pageStart = 11;
        first.pageEnd = 11;
        first.sectionPath = "Methods";
        first.snippet = "Alpha evidence";
        turn.citations.append(first);

        RagCitation second;
        second.chunkId = "chunk_b";
        second.pageStart = 20;
        second.pageEnd = 21;
        second.snippet = "Beta evidence";
        turn.citations.append(second);

        AskPdfTurnCard card(turn);
        auto* list = card.findChild<QListWidget*>("turnCitationList");
        QVERIFY(list);
        QCOMPARE(list->count(), 2);
        QVERIFY(list->item(0)->text().contains("第 12 页"));
        QVERIFY(list->item(0)->text().contains("Methods"));
        QVERIFY(list->item(0)->text().contains("Alpha evidence"));
        QVERIFY(list->item(1)->text().contains("第 21–22 页"));
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),
                 QString("chunk_a"));
    }

    void contextAndCitations() {
        RagEvidence item;
        item.chunkId = "chunk_one";
        item.documentHash = "doc";
        item.pageStart = 3;
        item.pageEnd = 3;
        item.sectionPath = "section one";
        item.text = QString(200, 'a');
        auto request =
            ContextAssembler::assemble("question", {item, item}, {}, 300);
        QVERIFY(request.context.size() <= 300);
        QCOMPARE(request.evidence.size(), 1);
        auto answer = CitationValidator::validate(
            "Answer [[cite:chunk_one]] [[cite:fake]] [[cite:chunk_one]]",
            request.evidence, "doc");
        QCOMPARE(answer.citations.size(), 1);
        QCOMPARE(answer.citations.first().chunkId, QString("chunk_one"));
        QCOMPARE(answer.citations.first().pageStart, 3);
        QCOMPARE(answer.citations.first().pageEnd, 3);
        QCOMPARE(answer.citations.first().sectionPath, QString("section one"));
        QCOMPARE(answer.citations.first().snippet.size(), 160);
        QVERIFY(answer.citations.first().snippet.endsWith("..."));
        QVERIFY(!answer.text.contains("[[cite:"));
        QCOMPARE(
            CitationValidator::validate("[[cite:chunk_one]]", {item}, "other")
                .citations.size(),
            0);
        QCOMPARE(
            ContextAssembler::assemble("q", {item}, {"q1", "a1", "incomplete"})
                .conversation,
            (QStringList{"q1", "a1"}));
    }

    void contextKeepsWholeEvidenceAtBudgetBoundary() {
        RagEvidence item;
        item.chunkId = "approval";
        item.documentHash = "doc";
        item.pageStart = item.pageEnd = 2;
        item.text =
            QStringLiteral("填写表单并提交。仅限已经通过审核的申请。🌍");
        item.locations = {
            {2, 80, int(item.text.size()), {QRectF(1, 2, 30, 4)}}};
        const auto full = ContextAssembler::assemble("q", {item}, {});
        QCOMPARE(full.evidence.size(), 1);
        const int exactBudget = full.context.size();
        const auto exact =
            ContextAssembler::assemble("q", {item}, {}, exactBudget);
        QCOMPARE(exact.context, full.context);
        QCOMPARE(exact.evidence.first().text, item.text);
        QCOMPARE(exact.evidence.first().locations.first().charStart, 80);
        QCOMPARE(exact.evidence.first().locations.first().charLength,
                 item.text.size());
        QCOMPARE(exact.evidence.first().locations.first().boundingRects,
                 item.locations.first().boundingRects);

        for (int budget : {-1, 0, 1, exactBudget - 1}) {
            const auto shortRequest =
                ContextAssembler::assemble("q", {item}, {}, budget);
            QVERIFY(shortRequest.context.isEmpty());
            QVERIFY(shortRequest.evidence.isEmpty());
            QVERIFY(CitationValidator::validate("[[cite:approval]]",
                                                shortRequest.evidence, "doc")
                        .citations.isEmpty());
        }
    }

    void contextSkipsOversizedBlocksAndContinues() {
        RagEvidence oversized;
        oversized.chunkId = "long";
        oversized.documentHash = "doc";
        oversized.text = QString(500, 'x') + " Only after approval.";
        RagEvidence small;
        small.chunkId = "small";
        small.documentHash = "doc";
        small.pageStart = small.pageEnd = 0;
        small.text = "Approval is required.";
        RagEvidence longHeader = small;
        longHeader.chunkId = QString(300, 'h');
        RagEvidence empty = small;
        empty.chunkId = "empty";
        empty.text.clear();

        const auto expected = ContextAssembler::assemble("q", {small}, {});
        const auto request = ContextAssembler::assemble(
            "q", {oversized, longHeader, empty, small, small}, {},
            expected.context.size());
        QCOMPARE(request.context, expected.context);
        QCOMPARE(request.evidence.size(), 1);
        QCOMPARE(request.evidence.first().chunkId, small.chunkId);
        QCOMPARE(request.evidence.first().text, small.text);
        const auto answer = CitationValidator::validate(
            "[[cite:long]] [[cite:small]]", request.evidence, "doc");
        QCOMPARE(answer.citations.size(), 1);
        QCOMPARE(answer.citations.first().chunkId, small.chunkId);
        QCOMPARE(answer.citations.first().snippet, small.text);
    }
    void chatRequestCarriesBudgetAndRealPromptTokens() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray received;
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket,
                    [socket, &received]() {
                        received += socket->readAll();
                        if (socket->property("sent").toBool())
                            return;
                        const int headerEnd = received.indexOf("\r\n\r\n");
                        if (headerEnd < 0)
                            return;
                        int length = 0;
                        const auto lines = received.left(headerEnd).split('\n');
                        for (const auto& line : lines) {
                            if (line.toLower().startsWith("content-length:"))
                                length = line.mid(15).trimmed().toInt();
                        }
                        if (received.size() < headerEnd + 4 + length)
                            return;
                        socket->setProperty("sent", true);
                        const QByteArray body =
                            "{\"message\":{\"content\":\"ok\"},\"done\":false,"
                            "\"prompt_eval_count\":42}\n"
                            "{\"message\":{\"content\":\"\"},\"done\":true}\n";
                        socket->write(
                            "HTTP/1.1 200 OK\r\nContent-Type: "
                            "application/x-ndjson\r\nContent-Length: " +
                            QByteArray::number(body.size()) +
                            "\r\nConnection: close\r\n\r\n" + body);
                    });
        });

        ChatRequest request;
        request.question = QStringLiteral("question");
        request.context = QStringLiteral("evidence");
        request.systemPrompt = QStringLiteral("custom system prompt");
        request.contextWindowTokens = 2048;
        request.maxOutputTokens = 512;
        request.tokenSafetyMargin = 64;

        OllamaChatProvider provider("fixture",
                                    QUrl(QString("http://127.0.0.1:%1/api/chat")
                                             .arg(server.serverPort())));
        auto* task = provider.generate(request, this);
        QSignalSpy done(task, &ChatTask::completed);
        QSignalSpy tokens(task, &ChatTask::promptEvaluated);
        task->start();
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(tokens.count(), 1);
        QCOMPARE(tokens.first().first().toInt(), 42);

        const int headerEnd = received.indexOf("\r\n\r\n");
        QVERIFY(headerEnd > 0);
        int length = 0;
        const auto lines = received.left(headerEnd).split('\n');
        for (const auto& line : lines) {
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
        }
        const auto body =
            QJsonDocument::fromJson(received.mid(headerEnd + 4, length))
                .object();
        const auto options = body.value("options").toObject();
        QCOMPARE(options.value("num_ctx").toInt(), 2048);
        QCOMPARE(options.value("num_predict").toInt(), 512);
        const auto messages = body.value("messages").toArray();
        QCOMPARE(messages.first().toObject().value("content").toString(),
                 request.systemPrompt);
        QCOMPARE(messages.last().toObject().value("content").toString(),
                 request.question + "\nEvidence:\n" + request.context);
        delete task;
    }

    void chatRejectsRequestOverBudget() {
        OllamaChatProvider provider("fixture");
        ChatRequest request;
        request.maxOutputTokens = 4096;
        auto* task = provider.generate(request, this);
        QSignalSpy failed(task, &ChatTask::failed);
        QSignalSpy done(task, &ChatTask::completed);
        task->start();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(done.count(), 0);
        QVERIFY(!failed.first().at(1).toString().isEmpty());
        delete task;
    }

    void idleTimeoutAfterFirstLine() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                if (socket->property("sent").toBool())
                    return;
                socket->setProperty("sent", true);
                const QByteArray body =
                    "{\"message\":{\"content\":\"partial\"},\"done\":false}\n";
                socket->write(
                    "HTTP/1.1 200 OK\r\nContent-Type: "
                    "application/x-ndjson\r\nContent-Length: 100\r\n"
                    "Connection: keep-alive\r\n\r\n" +
                    body);
            });
        });

        OllamaChatProvider provider("fixture",
                                    QUrl(QString("http://127.0.0.1:%1/api/chat")
                                             .arg(server.serverPort())),
                                    500, 100);
        auto* task = provider.generate({}, this);
        QSignalSpy failed(task, &ChatTask::failed);
        QSignalSpy done(task, &ChatTask::completed);
        task->start();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QCOMPARE(done.count(), 0);
        delete task;
    }

    void slowStreamWithSmallIdleGapsCompletes() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                if (socket->property("sent").toBool())
                    return;
                socket->setProperty("sent", true);
                const QByteArray body =
                    "{\"message\":{\"content\":\"a\"},\"done\":false}\n"
                    "{\"message\":{\"content\":\"b\"},\"done\":false}\n"
                    "{\"message\":{\"content\":\"c\"},\"done\":false}\n"
                    "{\"message\":{\"content\":\"d\"},\"done\":true}\n";
                socket->write(
                    "HTTP/1.1 200 OK\r\nContent-Type: "
                    "application/x-ndjson\r\nContent-Length: " +
                    QByteArray::number(body.size()) +
                    "\r\nConnection: keep-alive\r\n\r\n" +
                    body.left(body.indexOf('\n') + 1));
                for (int i = 1; i < 4; ++i) {
                    QTimer::singleShot(100 * i, socket, [socket, body, i]() {
                        int start = 0;
                        for (int line = 0; line < i; ++line)
                            start = body.indexOf('\n', start) + 1;
                        const int end = body.indexOf('\n', start) + 1;
                        socket->write(body.mid(start, end - start));
                    });
                }
            });
        });

        OllamaChatProvider provider("fixture",
                                    QUrl(QString("http://127.0.0.1:%1/api/chat")
                                             .arg(server.serverPort())),
                                    500, 250);
        auto* task = provider.generate({}, this);
        QSignalSpy failed(task, &ChatTask::failed);
        QSignalSpy done(task, &ChatTask::completed);
        task->start();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 4000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(done.first().first().toString(), QString("abcd"));
        delete task;
    }

    void embeddingRequestRespectsConfiguredTimeout() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            auto* socket = server.nextPendingConnection();
            // Accept the request but never answer.
            connect(socket, &QTcpSocket::readyRead, socket,
                    [socket]() { socket->readAll(); });
        });

        OllamaEmbeddingProvider provider("fixture", "v1", 100);
        RagCancellationToken cancel;
        QString error;
        QElapsedTimer timer;
        timer.start();
        const auto vectors =
            provider.embed({QStringLiteral("text")}, cancel, &error);
        QVERIFY(vectors.isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY2(timer.elapsed() < 3000,
                 qPrintable(QString::number(timer.elapsed())));
    }

    void fragmentedStream() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                const auto data = socket->readAll();
                if (!data.contains("POST") || socket->property("sent").toBool())
                    return;
                socket->setProperty("sent", true);
                QByteArray body =
                    "{\"message\":{\"content\":\"Hello\"},\"done\":false}\n"
                    "{\"message\":{\"content\":\" world\"},\"done\":true}\n";
                socket->write(
                    "HTTP/1.1 200 OK\r\nContent-Type: "
                    "application/x-ndjson\r\nContent-Length: " +
                    QByteArray::number(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body.left(13));
                QTimer::singleShot(20, socket, [socket, body]() {
                    socket->write(body.mid(13));
                    socket->disconnectFromHost();
                });
            });
        });
        OllamaChatProvider provider("fixture",
                                    QUrl(QString("http://127.0.0.1:%1/api/chat")
                                             .arg(server.serverPort())));
        auto* task = provider.generate({}, this);
        QSignalSpy done(task, &ChatTask::completed);
        QSignalSpy failed(task, &ChatTask::failed);
        task->start();
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(done.first().first().toString(), QString("Hello world"));
        QCOMPARE(failed.count(), 0);
        task->deleteLater();
    }
    void cancelBeforeStart() {
        OllamaChatProvider provider("fixture");
        auto* task = provider.generate({}, this);
        QSignalSpy cancelled(task, &ChatTask::cancelled);
        task->cancel();
        task->start();
        QCOMPARE(cancelled.count(), 1);
        delete task;
    }
    void cancelRunningRequest() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        OllamaChatProvider provider("fixture",
                                    QUrl(QString("http://127.0.0.1:%1/api/chat")
                                             .arg(server.serverPort())));
        auto* task = provider.generate({}, this);
        QSignalSpy cancelled(task, &ChatTask::cancelled);
        QSignalSpy done(task, &ChatTask::completed);
        QSignalSpy failed(task, &ChatTask::failed);
        task->start();
        QTRY_VERIFY(server.hasPendingConnections());
        task->cancel();
        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(done.count(), 0);
        QCOMPARE(failed.count(), 0);
        delete task;
    }
    void streamErrors_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("invalid-json") << QByteArray("not-json\n");
        QTest::newRow("server-error")
            << QByteArray("{\"error\":\"model missing\"}\n");
        QTest::newRow("missing-done") << QByteArray(
            "{\"message\":{\"content\":\"partial\"},\"done\":false}\n");
    }
    void streamErrors() {
        QFETCH(QByteArray, body);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, body]() {
                socket->readAll();
                if (socket->property("sent").toBool())
                    return;
                socket->setProperty("sent", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                              QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        OllamaChatProvider provider("fixture",
                                    QUrl(QString("http://127.0.0.1:%1/api/chat")
                                             .arg(server.serverPort())));
        auto* task = provider.generate({}, this);
        QSignalSpy failed(task, &ChatTask::failed);
        QSignalSpy done(task, &ChatTask::completed);
        task->start();
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(done.count(), 0);
        delete task;
    }
};

void TestRagP3::askPdfStateCopy() {
    AskPdfViewInput input;
    input.hasDocument = true;
    input.indexState = RagIndexState::Ready;
    input.embeddingState = RagEmbeddingState::Ready;
    input.answerEnabled = true;

    QCOMPARE(askPdfViewState(input), AskPdfViewState::ReadyIdle);

    input.answerEnabled = false;
    auto view = askPdfStateView(input);
    QCOMPARE(view.state, AskPdfViewState::EvidenceOnly);
    QVERIFY(view.title.contains(QStringLiteral("未启用回答生成")));
    QCOMPARE(view.primaryText, QStringLiteral("配置本地模型"));
    QCOMPARE(view.primaryAction, AskPdfStateAction::OpenSettings);

    input.answerEnabled = true;
    input.hasSearched = true;
    view = askPdfStateView(input);
    QCOMPARE(view.state, AskPdfViewState::NoEvidence);
    QVERIFY(view.title.contains(QStringLiteral("没有找到足够相关的证据")));
    QCOMPARE(view.primaryAction, AskPdfStateAction::FocusQuery);

    input.hasSearched = false;
    input.embeddingState = RagEmbeddingState::Disabled;
    view = askPdfStateView(input);
    QCOMPARE(view.state, AskPdfViewState::SemanticUnavailable);
    QVERIFY(view.title.contains(QStringLiteral("语义检索不可用")));

    input.embeddingState = RagEmbeddingState::Ready;
    input.queryFailed = true;
    input.queryError = QStringLiteral("boom");
    view = askPdfStateView(input);
    QCOMPARE(view.state, AskPdfViewState::QueryFailed);
    QCOMPARE(view.detail, QStringLiteral("boom"));
    QCOMPARE(view.primaryAction, AskPdfStateAction::RetryQuery);

    input.queryFailed = false;
    input.answerFailed = true;
    input.answerError = QStringLiteral("chat failed");
    view = askPdfStateView(input);
    QCOMPARE(view.state, AskPdfViewState::AnswerFailed);
    QCOMPARE(view.primaryAction, AskPdfStateAction::RetryAnswer);
    QCOMPARE(view.secondaryAction, AskPdfStateAction::OpenSettings);

    input.answerFailed = false;
    input.hasDocument = false;
    QCOMPARE(askPdfViewState(input), AskPdfViewState::NoDocument);

    input.hasDocument = true;
    input.indexState = RagIndexState::Failed;
    input.failureReason = RagIndexFailureReason::Encrypted;
    view = askPdfStateView(input);
    QCOMPARE(view.state, AskPdfViewState::IndexFailed);
    QVERIFY(view.title.contains(QStringLiteral("加密")));
    QCOMPARE(view.primaryAction, AskPdfStateAction::RebuildIndex);

    input.failureReason = RagIndexFailureReason::Database;
    view = askPdfStateView(input);
    QVERIFY(view.title.contains(QStringLiteral("本地索引")));
    QCOMPARE(view.secondaryAction, AskPdfStateAction::ClearIndex);

    input.indexState = RagIndexState::Unsupported;
    input.failureReason = RagIndexFailureReason::ScannedImage;
    view = askPdfStateView(input);
    QCOMPARE(view.state, AskPdfViewState::IndexUnsupported);
    QVERIFY(view.title.contains(QStringLiteral("扫描版")));
    QVERIFY(view.primaryText.isEmpty());

    input.indexState = RagIndexState::Ready;
    input.busy = true;
    QCOMPARE(askPdfViewState(input), AskPdfViewState::Searching);
}

void TestRagP3::askPdfStatePanelActions() {
    AskPdfWidget widget;
    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/example.pdf");
    document.contentHash = QStringLiteral("hash");
    widget.setDocumentContext(document);
    widget.setIndexState(RagIndexState::Ready, 3, {}, RagEmbeddingState::Ready,
                         3, {});

    auto* panel =
        widget.findChild<QWidget*>(QStringLiteral("askPdfStatePanel"));
    auto* title = widget.findChild<QLabel*>(QStringLiteral("askPdfStateTitle"));
    auto* primary =
        widget.findChild<QPushButton*>(QStringLiteral("askPdfStatePrimary"));
    QVERIFY(panel);
    QVERIFY(title);
    QVERIFY(primary);
    QVERIFY(!panel->isVisibleTo(&widget));

    QSignalSpy settingsSpy(&widget, &AskPdfWidget::settingsRequested);
    widget.setAnswerEnabled(false);
    QVERIFY(panel->isVisibleTo(&widget));
    QVERIFY(title->text().contains(QStringLiteral("未启用回答生成")));
    QCOMPARE(primary->text(), QStringLiteral("配置本地模型"));
    primary->click();
    QCOMPARE(settingsSpy.count(), 1);
}

void TestRagP3::askPdfProgressCopy() {
    AskPdfProgressInput input;
    input.indexState = RagIndexState::Extracting;
    input.totalPages = 12;
    input.processedPages = 3;
    auto view = askPdfProgressView(input);
    QVERIFY(view.visible);
    QVERIFY(!view.indeterminate);
    QCOMPARE(view.percent, 15);
    QVERIFY(view.text.contains(QStringLiteral("3/12")));

    input.indexState = RagIndexState::Chunking;
    input.processedPages = 6;
    view = askPdfProgressView(input);
    QCOMPARE(view.percent, 65);

    input.indexState = RagIndexState::Indexing;
    view = askPdfProgressView(input);
    QVERIFY(view.visible);
    QVERIFY(view.indeterminate);
    QCOMPARE(view.percent, 70);

    input.indexState = RagIndexState::Ready;
    input.embeddingsEnabled = true;
    input.embeddingState = RagEmbeddingState::Running;
    input.chunkCount = 100;
    input.embeddedChunkCount = 40;
    view = askPdfProgressView(input);
    QVERIFY(view.visible);
    QCOMPARE(view.percent, 88);

    input.embeddingState = RagEmbeddingState::Ready;
    view = askPdfProgressView(input);
    QVERIFY(!view.visible);

    input.embeddingsEnabled = false;
    input.embeddingState = RagEmbeddingState::Running;
    view = askPdfProgressView(input);
    QVERIFY(!view.visible);

    input.indexState = RagIndexState::Failed;
    view = askPdfProgressView(input);
    QVERIFY(!view.visible);
}

void TestRagP3::askPdfStatusTextCopy() {
    AskPdfViewInput input;
    input.hasDocument = true;
    input.indexState = RagIndexState::NotIndexed;
    QCOMPARE(askPdfStatusText(input), QStringLiteral("尚未建立索引"));

    input.indexState = RagIndexState::Indexing;
    QCOMPARE(askPdfStatusText(input), QStringLiteral("正在建立搜索索引..."));

    input.indexState = RagIndexState::Ready;
    input.embeddingState = RagEmbeddingState::Ready;
    QVERIFY(askPdfStatusText(input).contains(QStringLiteral("索引就绪")));

    input.indexState = RagIndexState::Failed;
    input.failureReason = RagIndexFailureReason::Encrypted;
    QVERIFY(askPdfStatusText(input).contains(QStringLiteral("加密")));

    input.indexState = RagIndexState::Cancelled;
    QVERIFY(askPdfStatusText(input).contains(QStringLiteral("索引已取消")));

    input.hasDocument = false;
    input.indexState = RagIndexState::NotIndexed;
    QCOMPARE(askPdfStatusText(input), QStringLiteral("未打开 PDF"));
}

void TestRagP3::progressBarReflectsIndexing() {
    AskPdfWidget widget;
    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/progress.pdf");
    document.contentHash = QStringLiteral("progress-hash");
    widget.setDocumentContext(document);
    widget.setEmbeddingEnabled(true);

    auto* bar =
        widget.findChild<QProgressBar*>(QStringLiteral("askPdfProgress"));
    QVERIFY(bar);

    widget.setIndexState(RagIndexState::Extracting, 0, {},
                         RagEmbeddingState::Disabled, 0, {},
                         RagIndexFailureReason::None, 3, 12);
    QVERIFY(bar->isVisibleTo(&widget));
    QCOMPARE(bar->value(), 15);
    QVERIFY(bar->format().contains(QStringLiteral("3/12")));

    widget.setIndexState(RagIndexState::Ready, 100, {},
                         RagEmbeddingState::Running, 40, {},
                         RagIndexFailureReason::None, 12, 12);
    QVERIFY(bar->isVisibleTo(&widget));
    QCOMPARE(bar->value(), 88);

    widget.setIndexState(RagIndexState::Ready, 100, {},
                         RagEmbeddingState::Ready, 100, {},
                         RagIndexFailureReason::None, 12, 12);
    QVERIFY(!bar->isVisibleTo(&widget));
}

void TestRagP3::turnCardActions() {
    RagConversationTurn turn;
    turn.id = QStringLiteral("turn-a");
    turn.state = RagTurnState::Ready;
    turn.question = QStringLiteral("question");
    turn.answerText = QStringLiteral("answer");

    RagConversationTurn loading;
    loading.id = QStringLiteral("loading");
    loading.state = RagTurnState::Retrieving;
    AskPdfTurnCard loadingCard(loading);
    auto* skeleton =
        loadingCard.findChild<QLabel*>(QStringLiteral("turnSkeleton"));
    QVERIFY(skeleton);
    QVERIFY(skeleton->isVisibleTo(&loadingCard));

    AskPdfTurnCard card(turn);
    auto* copy = card.findChild<QPushButton*>(QStringLiteral("turnCopyButton"));
    auto* regenerate =
        card.findChild<QPushButton*>(QStringLiteral("turnRegenerateButton"));
    auto* stop = card.findChild<QPushButton*>(QStringLiteral("turnStopButton"));
    auto* retry =
        card.findChild<QPushButton*>(QStringLiteral("turnRetryButton"));
    QVERIFY(copy);
    QVERIFY(regenerate);
    QVERIFY(stop);
    QVERIFY(retry);

    QSignalSpy copySpy(&card, &AskPdfTurnCard::copyRequested);
    QSignalSpy regenerateSpy(&card, &AskPdfTurnCard::regenerateRequested);
    QSignalSpy stopSpy(&card, &AskPdfTurnCard::stopRequested);

    QVERIFY(copy->isVisibleTo(&card));
    copy->click();
    QCOMPARE(copySpy.count(), 1);
    QCOMPARE(copySpy.first().first().toString(), QStringLiteral("answer"));

    QVERIFY(regenerate->isVisibleTo(&card));
    regenerate->click();
    QCOMPARE(regenerateSpy.count(), 1);
    QCOMPARE(regenerateSpy.first().first().toString(),
             QStringLiteral("turn-a"));

    turn.state = RagTurnState::Generating;
    card.setTurn(turn);
    QVERIFY(stop->isVisibleTo(&card));
    stop->click();
    QCOMPARE(stopSpy.count(), 1);

    turn.state = RagTurnState::Failed;
    turn.error = QStringLiteral("boom");
    card.setTurn(turn);
    QVERIFY(retry->isVisibleTo(&card));
    QVERIFY(!stop->isVisibleTo(&card));
    retry->click();
    QCOMPARE(regenerateSpy.count(), 2);
}

void TestRagP3::turnCardMarkdownAndCollapse() {
    RagConversationTurn turn;
    turn.id = QStringLiteral("turn-long");
    turn.state = RagTurnState::Ready;
    turn.question = QStringLiteral("question");
    turn.answerText = QString(700, QLatin1Char('a'));
    turn.answerMarkdown = QString(700, QLatin1Char('a')) +
                          QStringLiteral(" [1](readium-cite:chunk_a)");
    RagCitation citation;
    citation.chunkId = QStringLiteral("chunk_a");
    citation.pageStart = 3;
    citation.pageEnd = 3;
    citation.sectionPath = QStringLiteral("Methods");
    citation.snippet = QStringLiteral("snippet");
    turn.citations.append(citation);

    AskPdfTurnCard card(turn);
    auto* answer = card.findChild<QTextBrowser*>(QStringLiteral("turnAnswer"));
    auto* expand =
        card.findChild<QPushButton*>(QStringLiteral("turnExpandButton"));
    QVERIFY(answer);
    QVERIFY(expand);
    QVERIFY(expand->isVisibleTo(&card));
    QVERIFY(answer->toPlainText().size() < turn.answerText.size());

    QSignalSpy citationSpy(&card, &AskPdfTurnCard::citationActivated);
    answer->anchorClicked(QUrl(QStringLiteral("readium-cite:chunk_a")));
    QCOMPARE(citationSpy.count(), 1);
    QCOMPARE(citationSpy.first().first().toString(), QStringLiteral("chunk_a"));

    expand->click();
    QVERIFY(answer->toPlainText().size() >= turn.answerText.size());
    QCOMPARE(expand->text(), QStringLiteral("收起"));
}

void TestRagP3::conversationViewFollowsModel() {
    RagConversationModel model;
    AskPdfConversationView view;
    view.setModel(&model);
    QVERIFY(view.findChildren<AskPdfTurnCard*>().isEmpty());

    RagConversationTurn turn;
    turn.id = QStringLiteral("turn-a");
    turn.question = QStringLiteral("question");
    turn.state = RagTurnState::Retrieving;
    model.appendTurn(turn);
    const auto cards = view.findChildren<AskPdfTurnCard*>();
    QCOMPARE(cards.size(), 1);
    auto* status =
        cards.first()->findChild<QLabel*>(QStringLiteral("turnStatus"));
    QVERIFY(status);
    QVERIFY(status->text().contains(QStringLiteral("检索")));

    model.updateTurn(QStringLiteral("turn-a"), [](RagConversationTurn& value) {
        value.state = RagTurnState::Ready;
        value.answerText = QStringLiteral("answer");
    });
    auto* answer =
        cards.first()->findChild<QTextBrowser*>(QStringLiteral("turnAnswer"));
    QVERIFY(answer);
    QCOMPARE(answer->toPlainText(), QStringLiteral("answer"));

    model.clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(view.findChildren<AskPdfTurnCard*>().isEmpty());
}

void TestRagP3::askPdfWidgetSubmitsAndClearsConversation() {
    AskPdfWidget widget;
    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/ask-pdf.pdf");
    document.contentHash = QStringLiteral("ask-pdf-hash");
    widget.setDocumentContext(document);
    widget.setIndexState(RagIndexState::Ready, 3, {}, RagEmbeddingState::Ready,
                         3, {});

    auto* query =
        widget.findChild<QPlainTextEdit*>(QStringLiteral("askPdfQuery"));
    auto* send = widget.findChild<QPushButton*>(QStringLiteral("askPdfSend"));
    auto* stop = widget.findChild<QToolButton*>(QStringLiteral("askPdfStop"));
    auto* clear = widget.findChild<QToolButton*>(
        QStringLiteral("askPdfClearConversation"));
    QVERIFY(query);
    QVERIFY(send);
    QVERIFY(stop);
    QVERIFY(clear);

    QSignalSpy questionSpy(&widget, &AskPdfWidget::questionSubmitted);
    QSignalSpy clearSpy(&widget, &AskPdfWidget::clearConversationRequested);

    query->setPlainText(QStringLiteral("what is the conclusion?"));
    send->click();
    QCOMPARE(questionSpy.count(), 1);
    QCOMPARE(questionSpy.first().first().toString(),
             QStringLiteral("what is the conclusion?"));
    QVERIFY(query->toPlainText().isEmpty());

    widget.setConversationBusy(true);
    QVERIFY(query->isEnabled());
    QVERIFY(!send->isEnabled());
    QVERIFY(stop->isVisibleTo(&widget));

    widget.setConversationBusy(false);
    QVERIFY(send->isEnabled());
    QVERIFY(!stop->isVisibleTo(&widget));

    clear->click();
    QCOMPARE(clearSpy.count(), 1);
}

void TestRagP3::statusAndSettingsButton() {
    AskPdfWidget widget;
    DocumentKey document;
    document.canonicalPath = QStringLiteral("/tmp/status.pdf");
    document.contentHash = QStringLiteral("status-hash");
    widget.setDocumentContext(document);
    auto* status = widget.findChild<QLabel*>(QStringLiteral("askPdfStatus"));
    auto* settings =
        widget.findChild<QToolButton*>(QStringLiteral("askPdfSettings"));
    QVERIFY(status);
    QVERIFY(settings);

    QSignalSpy settingsSpy(&widget, &AskPdfWidget::settingsRequested);
    settings->click();
    QCOMPARE(settingsSpy.count(), 1);

    widget.setAnswerEnabled(true);
    widget.setIndexState(RagIndexState::Ready, 5, {},
                         RagEmbeddingState::Disabled, 0, {});
    QVERIFY(status->text().contains(QStringLiteral("语义检索关闭")));
    QVERIFY(status->text().contains(QStringLiteral("5 个文本块")));
    QVERIFY(!status->text().contains(QStringLiteral("回答已关闭")));

    widget.setIndexState(RagIndexState::Ready, 5, {},
                         RagEmbeddingState::Running, 2, {});
    QVERIFY(status->text().contains(QStringLiteral("2/5")));

    widget.setAnswerEnabled(false);
    QVERIFY(status->text().contains(QStringLiteral("回答已关闭")));
}

QTEST_MAIN(TestRagP3)
#include "test_rag_p3.moc"
