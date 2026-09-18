#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

#include "rag/retrieve/HybridRetriever.h"

using namespace Rag;

namespace {

class FixedEmbedding : public EmbeddingProvider {
public:
    QString modelId() const override { return QStringLiteral("fixture-model"); }
    QString modelVersion() const override { return QStringLiteral("v1"); }
    QVector<QVector<float>> embed(const QStringList& texts,
                                  const RagCancellationToken&,
                                  QString* error) override {
        if (error)
            error->clear();
        QVector<QVector<float>> vectors;
        for (const QString& text : texts) {
            vectors.append(text.contains(QStringLiteral("semantic"))
                               ? QVector<float>{1, 0}
                               : QVector<float>{0, 1});
        }
        return vectors;
    }
};

PdfChunk chunk(const DocumentKey& document, const QString& id,
               const QString& text, int page) {
    PdfChunk item;
    item.document = document;
    item.id = id;
    item.chunkHash = id;
    item.text = text;
    item.pageStart = item.pageEnd = page;
    item.locations = {{page, 0, int(text.size()), {QRectF(10, 20, 30, 10)}}};
    return item;
}

}  // namespace

class TestRagP2 : public QObject {
    Q_OBJECT
private slots:
    void testVectorCacheAndHybridRetrieval();
};

void TestRagP2::testVectorCacheAndHybridRetrieval() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("index.sqlite"));
    RagIndexStore store;
    QString error;
    QVERIFY2(store.open(path, &error), qPrintable(error));
    DocumentKey doc;
    doc.canonicalPath = dir.filePath(QStringLiteral("source.pdf"));
    doc.contentHash = QStringLiteral("hash1");
    const auto lexical = chunk(doc, QStringLiteral("lexical"),
                               QStringLiteral("keyword appears"), 0);
    const auto semantic =
        chunk(doc, QStringLiteral("semantic"),
              QStringLiteral("related idea without exact words"), 1);
    QVERIFY2(store.replaceDocument(doc, {lexical, semantic}, &error),
             qPrintable(error));
    FixedEmbedding provider;
    QCOMPARE(store
                 .chunksMissingVectors(doc, provider.modelId(),
                                       provider.modelVersion(), 0, &error)
                 .size(),
             2);
    RagCancellationToken cancel;
    QVERIFY2(
        store.storeVectors(doc, provider.modelId(), provider.modelVersion(),
                           {{lexical.id, lexical.chunkHash, {}, {0, 1}},
                            {semantic.id, semantic.chunkHash, {}, {1, 0}}},
                           cancel, &error),
        qPrintable(error));
    QCOMPARE(store
                 .chunksMissingVectors(doc, provider.modelId(),
                                       provider.modelVersion(), 0, &error)
                 .size(),
             0);
    QCOMPARE(store
                 .chunksMissingVectors(doc, provider.modelId(),
                                       QStringLiteral("v2"), 0, &error)
                 .size(),
             2);
    QVERIFY(store.storeVectors(doc, provider.modelId(), provider.modelVersion(),
                               {{lexical.id, lexical.chunkHash, {}, {0, 1, 0}}},
                               cancel, &error));
    QCOMPARE(store
                 .loadVectors(doc, provider.modelId(), provider.modelVersion(),
                              &error)
                 .size(),
             1);
    QVERIFY(store.storeVectors(doc, provider.modelId(), provider.modelVersion(),
                               {{lexical.id, lexical.chunkHash, {}, {0, 1}},
                                {semantic.id, semantic.chunkHash, {}, {1, 0}}},
                               cancel, &error));
    HybridRetriever retriever(path);
    auto found = retriever.retrieve(doc, QStringLiteral("semantic"), 5,
                                    &provider, cancel, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(found.size(), 2);
    QVERIFY(found.first().vectorScore > 0);
    QCOMPARE(found.first().documentHash, doc.contentHash);
    QCOMPARE(found.first().locations.first().pageIndex, 1);
    // Recall@5 = 1, reciprocal rank = 1 for this fixed fixture.
    QCOMPARE(found.first().chunkId, semantic.id);
    auto fallback = retriever.retrieve(doc, QStringLiteral("keyword"), 5,
                                       nullptr, cancel, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(fallback.first().chunkId, lexical.id);
    QElapsedTimer timer;
    timer.start();
    int hits = 0;
    double reciprocalRank = 0;
    double discountedGain = 0;
    for (const auto& fixture : QVector<QPair<QString, QString>>{
             {QStringLiteral("semantic"), semantic.id},
             {QStringLiteral("keyword"), lexical.id}}) {
        const auto candidates = retriever.retrieve(doc, fixture.first, 5,
                                                   &provider, cancel, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        for (int rank = 0; rank < qMin(5, int(candidates.size())); ++rank) {
            if (candidates[rank].chunkId == fixture.second) {
                ++hits;
                reciprocalRank += 1.0 / (rank + 1);
                discountedGain += 1.0 / std::log2(rank + 2.0);
                break;
            }
        }
    }
    QCOMPARE(hits, 2);                    // Recall@5 = 1.
    QCOMPARE(reciprocalRank / 2.0, 1.0);  // MRR = 1.
    QCOMPARE(discountedGain / 2.0, 1.0);  // Binary nDCG@5 = 1.
    qInfo() << "Hybrid fixture latency (2 queries, ms):" << timer.elapsed();
    DocumentKey next = doc;
    next.contentHash = QStringLiteral("hash2");
    QVERIFY(store.replaceDocument(
        next, {chunk(next, QStringLiteral("new"), QStringLiteral("fresh"), 0)},
        &error));
    QCOMPARE(store
                 .loadVectors(doc, provider.modelId(), provider.modelVersion(),
                              &error)
                 .size(),
             0);
}

QTEST_MAIN(TestRagP2)
#include "test_rag_p2.moc"
