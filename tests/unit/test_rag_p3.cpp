#include <QListWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include "rag/chat/ContextAssembler.h"
#include "rag/chat/OllamaChatProvider.h"
#include "rag/ui/AskPdfWidget.h"
using namespace Rag;
class TestRagP3 : public QObject {
    Q_OBJECT
private slots:
    void citationLabels() {
        AskPdfWidget widget;
        RagAnswer answer;
        answer.text = "answer";

        RagCitation first;
        first.chunkId = "chunk_a";
        first.pageStart = 11;
        first.pageEnd = 11;
        first.sectionPath = "Methods";
        first.snippet = "Alpha evidence";
        answer.citations.append(first);

        RagCitation second;
        second.chunkId = "chunk_b";
        second.pageStart = 20;
        second.pageEnd = 21;
        second.snippet = "Beta evidence";
        answer.citations.append(second);

        widget.setAnswer(answer);
        auto* list = widget.findChild<QListWidget*>("citationList");
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
            ContextAssembler::assemble("question", {item, item}, {}, 100);
        QVERIFY(request.context.size() <= 100);
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
QTEST_MAIN(TestRagP3)
#include "test_rag_p3.moc"
