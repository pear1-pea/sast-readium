#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

#include "rag/config/RagSettings.h"
#include "rag/probe/OllamaProbe.h"

using namespace Rag;

namespace {

class FakeOllama : public QObject {
public:
    FakeOllama() {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket,
                        [this, socket]() {
                            QByteArray buffer =
                                socket->property("buffer").toByteArray();
                            buffer += socket->readAll();
                            socket->setProperty("buffer", buffer);
                            if (!buffer.contains("\r\n\r\n") ||
                                socket->property("handled").toBool()) {
                                return;
                            }
                            socket->setProperty("handled", true);
                            handle(socket, requestPath(buffer));
                        });
                connect(socket, &QTcpSocket::disconnected, socket,
                        &QObject::deleteLater);
            }
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost); }
    QString endpoint() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    bool hang = false;
    bool versionMissing = false;
    bool embeddingMissing = false;
    bool embeddingPathError = false;
    bool chatMissing = false;
    int delayMs = 0;
    QStringList models{QStringLiteral("nomic-embed-text:latest"),
                       QStringLiteral("llama3:latest")};

private:
    void handle(QTcpSocket* socket, const QByteArray& path) {
        if (hang) {
            return;
        }
        if (delayMs > 0) {
            QTimer::singleShot(delayMs, socket, [this, socket, path]() {
                respond(socket, path);
            });
            return;
        }
        respond(socket, path);
    }

    void respond(QTcpSocket* socket, const QByteArray& path) {
        if (path == "/api/version") {
            if (versionMissing) {
                respondJson(socket, 404, R"({"error":"404 page not found"})");
                return;
            }
            respondJson(socket, 200, R"({"version":"0.5.0"})");
            return;
        }
        if (path == "/api/tags") {
            QJsonArray array;
            for (const QString& model : models) {
                array.append(QJsonObject{{"name", model}});
            }
            const QJsonObject body{{"models", array}};
            respondJson(socket, 200,
                        QJsonDocument(body).toJson(QJsonDocument::Compact));
            return;
        }
        if (path == "/api/embed") {
            if (embeddingPathError) {
                respondJson(socket, 404, R"({"error":"404 page not found"})");
                return;
            }
            if (embeddingMissing) {
                respondJson(
                    socket, 404,
                    R"({"error":"model 'nomic' not found, try pulling it first"})");
                return;
            }
            respondJson(socket, 200, R"({"embeddings":[[0.1,0.2]]})");
            return;
        }
        if (path == "/api/chat") {
            if (chatMissing) {
                respondJson(socket, 404,
                            R"({"error":"model 'llama' not found"})");
                return;
            }
            respondJson(
                socket, 200,
                R"({"model":"llama","message":{"role":"assistant","content":"p"},"done":true})");
            return;
        }
        respondJson(socket, 404, R"({"error":"unknown"})");
    }

    static void respondJson(QTcpSocket* socket, int status,
                            const QByteArray& body) {
        const QByteArray header =
            "HTTP/1.1 " + QByteArray::number(status) +
            (status == 200 ? " OK" : " Error") +
            "\r\nContent-Type: application/json\r\nContent-Length: " +
            QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
        socket->write(header + body);
        socket->disconnectFromHost();
    }

    static QByteArray requestPath(const QByteArray& request) {
        const int firstSpace = request.indexOf(' ');
        if (firstSpace < 0) {
            return {};
        }
        const int secondSpace = request.indexOf(' ', firstSpace + 1);
        if (secondSpace < 0) {
            return {};
        }
        return request.mid(firstSpace + 1, secondSpace - firstSpace - 1);
    }

    QTcpServer m_server;
};

}  // namespace

class TestOllamaProbe : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void serviceAndModels();
    void emptyModelListIsReported();
    void servicePathErrorIsNotModelMissing();
    void unreachableService();
    void remoteEndpointIsRejected();
    void embeddingReady();
    void embeddingMissingModel();
    void embeddingPathErrorIsNotModelMissing();
    void chatReady();
    void chatMissingModel();
    void timeoutIsDistinct();
    void cancelDropsResult();
    void staleResultIsDropped();
};

void TestOllamaProbe::initTestCase() {
    qRegisterMetaType<Rag::OllamaProbe::Result>("Rag::OllamaProbe::Result");
}

void TestOllamaProbe::serviceAndModels() {
    FakeOllama server;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::modelsFetched);
    probe.fetchModels(server.endpoint());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Ready);
    QVERIFY(result.models.contains(QStringLiteral("llama3:latest")));
}

void TestOllamaProbe::emptyModelListIsReported() {
    FakeOllama server;
    server.models.clear();
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::modelsFetched);
    probe.fetchModels(server.endpoint());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Ready);
    QVERIFY(result.models.isEmpty());
    QVERIFY(result.message.contains(QStringLiteral("尚未安装")));
}

void TestOllamaProbe::servicePathErrorIsNotModelMissing() {
    FakeOllama server;
    server.versionMissing = true;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::modelsFetched);
    probe.fetchModels(server.endpoint());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Failed);
    QVERIFY(result.message.contains(QStringLiteral("404")));
}

void TestOllamaProbe::unreachableService() {
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::modelsFetched);
    probe.fetchModels(QStringLiteral("http://127.0.0.1:1"), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Unreachable);
    QVERIFY(!result.message.isEmpty());
}

void TestOllamaProbe::remoteEndpointIsRejected() {
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::modelsFetched);
    probe.fetchModels(QStringLiteral("http://192.168.1.4:11434"), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Failed);
    QVERIFY(result.message.contains(QStringLiteral("本机")));
}

void TestOllamaProbe::embeddingReady() {
    FakeOllama server;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::embeddingTested);
    probe.testEmbedding(server.endpoint(), QStringLiteral("nomic-embed-text"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Ready);
}

void TestOllamaProbe::embeddingMissingModel() {
    FakeOllama server;
    server.embeddingMissing = true;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::embeddingTested);
    probe.testEmbedding(server.endpoint(), QStringLiteral("nomic"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::ModelMissing);
    QVERIFY(result.message.contains(QStringLiteral("不存在")));
}

void TestOllamaProbe::embeddingPathErrorIsNotModelMissing() {
    FakeOllama server;
    server.embeddingPathError = true;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::embeddingTested);
    probe.testEmbedding(server.endpoint(), QStringLiteral("nomic"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Failed);
    QVERIFY(result.message.contains(QStringLiteral("404")));
}

void TestOllamaProbe::chatReady() {
    FakeOllama server;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::chatTested);
    probe.testChat(server.endpoint(), QStringLiteral("llama3"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Ready);
}

void TestOllamaProbe::chatMissingModel() {
    FakeOllama server;
    server.chatMissing = true;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::chatTested);
    probe.testChat(server.endpoint(), QStringLiteral("llama3"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::ModelMissing);
}

void TestOllamaProbe::timeoutIsDistinct() {
    FakeOllama server;
    server.hang = true;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::chatTested);
    probe.testChat(server.endpoint(), QStringLiteral("llama3"), 100);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.status, OllamaProbe::Status::Timeout);
    QVERIFY(result.message.contains(QStringLiteral("超时")));
}

void TestOllamaProbe::cancelDropsResult() {
    FakeOllama server;
    server.hang = true;
    QVERIFY(server.listen());
    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::chatTested);
    probe.testChat(server.endpoint(), QStringLiteral("llama3"), 2000);
    probe.cancelAll();
    QTest::qWait(300);
    QCOMPARE(spy.count(), 0);
}

void TestOllamaProbe::staleResultIsDropped() {
    FakeOllama slow;
    slow.delayMs = 400;
    QVERIFY(slow.listen());
    FakeOllama fast;
    QVERIFY(fast.listen());

    OllamaProbe probe;
    QSignalSpy spy(&probe, &OllamaProbe::embeddingTested);
    probe.testEmbedding(slow.endpoint(), QStringLiteral("nomic"));
    probe.testEmbedding(fast.endpoint(), QStringLiteral("nomic"));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QTest::qWait(600);
    QCOMPARE(spy.count(), 1);
    const auto result = spy.first().first().value<OllamaProbe::Result>();
    QCOMPARE(result.endpoint, fast.endpoint());
}

QTEST_GUILESS_MAIN(TestOllamaProbe)
#include "test_ollama_probe.moc"
