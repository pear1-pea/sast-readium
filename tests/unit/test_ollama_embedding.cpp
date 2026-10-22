#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

#include "rag/embedding/OllamaEmbeddingProvider.h"

namespace {

class EmbeddingServer : public QTcpServer {
public:
    explicit EmbeddingServer(QObject* parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            auto* socket = nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket,
                    &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                QByteArray bytes =
                    socket->property("requestBytes").toByteArray();
                bytes += socket->readAll();
                socket->setProperty("requestBytes", bytes);
                if (socket->property("received").toBool())
                    return;
                const int headerEnd = bytes.indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                int contentLength = -1;
                for (const auto& line : bytes.left(headerEnd).split('\n')) {
                    if (line.toLower().startsWith("content-length:"))
                        contentLength =
                            line.mid(line.indexOf(':') + 1).trimmed().toInt();
                }
                if (contentLength < 0 ||
                    bytes.size() - headerEnd - 4 < contentLength)
                    return;
                socket->setProperty("received", true);
                ++requestCount;
                request = QJsonDocument::fromJson(
                              bytes.mid(headerEnd + 4, contentLength))
                              .object();
                if (holdResponse)
                    return;
                socket->write("HTTP/1.1 " + QByteArray::number(status) +
                              " Response\r\nContent-Type: application/json\r\n"
                              "Content-Length: " +
                              QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }

    QUrl endpoint() const {
        return QUrl(
            QStringLiteral("http://127.0.0.1:%1/api/embed").arg(serverPort()));
    }

    int status = 200;
    QByteArray body = R"({"embeddings":[[0.1,0.2],[0.3,0.4]]})";
    bool holdResponse = false;
    int requestCount = 0;
    QJsonObject request;
};

}  // namespace

class TestOllamaEmbedding : public QObject {
    Q_OBJECT

private slots:
    void disablesTruncationAndPreservesBatch();
    void serverErrors_data();
    void serverErrors();
    void invalidVectors_data();
    void invalidVectors();
    void timeoutUsesConfiguredEndpoint();
    void cancellationStopsRequest();
    void cancelledBeforeRequest();
    void unavailableService();
};

void TestOllamaEmbedding::disablesTruncationAndPreservesBatch() {
    EmbeddingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    Rag::OllamaEmbeddingProvider provider("fixture", "v1", server.endpoint());
    const QStringList texts{"Full text including the tail exception.",
                            QStringLiteral("中文前提和操作步骤。")};
    QString error = "stale error";
    const auto vectors = provider.embed(texts, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(server.requestCount, 1);
    QCOMPARE(server.request.value("model").toString(), QString("fixture"));
    QCOMPARE(server.request.value("input").toArray(),
             QJsonArray::fromStringList(texts));
    QVERIFY(server.request.value("truncate").isBool());
    QVERIFY(!server.request.value("truncate").toBool());
    QCOMPARE(vectors.size(), 2);
    QCOMPARE(vectors.at(0), (QVector<float>{0.1f, 0.2f}));
    QCOMPARE(vectors.at(1), (QVector<float>{0.3f, 0.4f}));
}

void TestOllamaEmbedding::serverErrors_data() {
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("diagnostic");
    QTest::newRow("over-context")
        << 400
        << QByteArray(
               R"({"error":"input length exceeds maximum context length"})")
        << QString("input length exceeds maximum context length");
    QTest::newRow("missing-model")
        << 404 << QByteArray(R"({"error":"model 'fixture' not found"})")
        << QString("model 'fixture' not found");
    QTest::newRow("non-json-service-error")
        << 503 << QByteArray("service temporarily unavailable")
        << QString("HTTP 503");
    QTest::newRow("error-with-success-status")
        << 200
        << QByteArray(R"({"error":"runner failed","embeddings":[[1,2]]})")
        << QString("runner failed");
}

void TestOllamaEmbedding::serverErrors() {
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    QFETCH(QString, diagnostic);
    EmbeddingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.status = status;
    server.body = body;
    Rag::OllamaEmbeddingProvider provider("fixture", "v1", server.endpoint());
    QString error;
    QVERIFY(provider.embed({"evidence"}, {}, &error).isEmpty());
    QCOMPARE(server.requestCount, 1);
    QVERIFY2(error.contains(diagnostic), qPrintable(error));
    if (status >= 400)
        QVERIFY(error.contains(QString("HTTP %1").arg(status)));
}

void TestOllamaEmbedding::invalidVectors_data() {
    QTest::addColumn<QByteArray>("body");
    QTest::newRow("malformed-json") << QByteArray("not json");
    QTest::newRow("missing-batch-entry")
        << QByteArray(R"({"embeddings":[[1,2]]})");
    QTest::newRow("inconsistent-dimensions")
        << QByteArray(R"({"embeddings":[[1,2],[3]]})");
    QTest::newRow("non-numeric-value")
        << QByteArray(R"({"embeddings":[[1,2],[3,"four"]]})");
}

void TestOllamaEmbedding::invalidVectors() {
    QFETCH(QByteArray, body);
    EmbeddingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.body = body;
    Rag::OllamaEmbeddingProvider provider("fixture", "v1", server.endpoint());
    QString error;
    QVERIFY(provider.embed({"first", "second"}, {}, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestOllamaEmbedding::timeoutUsesConfiguredEndpoint() {
    EmbeddingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.holdResponse = true;
    Rag::OllamaEmbeddingProvider provider("fixture", "v1", server.endpoint(),
                                          100);
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(provider.embed({"evidence"}, {}, &error).isEmpty());
    QCOMPARE(server.requestCount, 1);
    QCOMPARE(error, QString("Embedding request timed out"));
    QVERIFY(timer.elapsed() < 3000);
}

void TestOllamaEmbedding::cancellationStopsRequest() {
    EmbeddingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.holdResponse = true;
    Rag::OllamaEmbeddingProvider provider("fixture", "v1", server.endpoint(),
                                          5000);
    Rag::RagCancellationToken cancellation;
    QTimer::singleShot(50, &server,
                       [cancellation]() { cancellation.cancel(); });
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(provider.embed({"evidence"}, cancellation, &error).isEmpty());
    QCOMPARE(server.requestCount, 1);
    QCOMPARE(error, QString("Embedding cancelled"));
    QVERIFY(timer.elapsed() < 3000);
}

void TestOllamaEmbedding::cancelledBeforeRequest() {
    EmbeddingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    Rag::OllamaEmbeddingProvider provider("fixture", "v1", server.endpoint());
    Rag::RagCancellationToken cancellation;
    cancellation.cancel();
    QString error;
    QVERIFY(provider.embed({"evidence"}, cancellation, &error).isEmpty());
    QCOMPARE(server.requestCount, 0);
    QCOMPARE(error, QString("Embedding cancelled"));
}

void TestOllamaEmbedding::unavailableService() {
    EmbeddingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const QUrl endpoint = server.endpoint();
    server.close();
    Rag::OllamaEmbeddingProvider provider("fixture", "v1", endpoint, 1000);
    QString error;
    QVERIFY(provider.embed({"evidence"}, {}, &error).isEmpty());
    QVERIFY2(error.contains("Embedding network error"), qPrintable(error));
}

QTEST_GUILESS_MAIN(TestOllamaEmbedding)
#include "test_ollama_embedding.moc"
