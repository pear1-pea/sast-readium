#include <QJsonObject>
#include <QtTest>

#include "rag/chat/ContextAssembler.h"

using namespace Rag;

namespace {
RagEvidence evidence(const QString& id, const QString& text) {
    RagEvidence item;
    item.chunkId = id;
    item.documentHash = "document";
    item.pageStart = item.pageEnd = 1;
    item.text = text;
    item.locations = {{1, 90, int(text.size()), {QRectF(1, 2, 30, 4)}}};
    return item;
}

ChatBudget exactBudget(const ChatRequest& request) {
    ChatBudget budget;
    budget.contextWindowTokens =
        int(ContextAssembler::estimateInputTokens(request)) +
        budget.maxOutputTokens + budget.tokenSafetyMargin;
    return budget;
}
}  // namespace

class TestChatBudget : public QObject {
    Q_OBJECT
private slots:
    void countsAllSerializedMessages() {
        ChatRequest request;
        request.question = QStringLiteral("中文问题🌍");
        request.context = "Evidence including a limiting condition.";
        request.conversation = {"previous question", "previous answer"};
        const auto messages = ContextAssembler::messages(request);
        QCOMPARE(messages.size(), 4);
        QCOMPARE(messages[0].toObject().value("content").toString(),
                 request.systemPrompt);
        QCOMPARE(messages[3].toObject().value("content").toString(),
                 request.question + "\nEvidence:\n" + request.context);
        const auto total = ContextAssembler::estimateInputTokens(request);
        qint64 contentBytes = 0;
        for (const auto& message : messages)
            contentBytes +=
                message.toObject().value("content").toString().toUtf8().size();
        QVERIFY(total > contentBytes);
        request.systemPrompt += QStringLiteral("额外限制");
        QCOMPARE(ContextAssembler::estimateInputTokens(request) - total,
                 QStringLiteral("额外限制").toUtf8().size());
    }

    void preservesQuestionAndWholeEvidence() {
        const auto item = evidence(
            "approval", QStringLiteral("提交表单。仅限审核通过者。🌍"));
        const auto full = ContextAssembler::assembleBudgeted("q", {item}, {});
        QVERIFY2(full.assemblyError.isEmpty(), qPrintable(full.assemblyError));
        QCOMPARE(full.evidence.size(), 1);
        auto budget = exactBudget(full);
        const auto exact =
            ContextAssembler::assembleBudgeted("q", {item}, {}, budget);
        QCOMPARE(exact.context, full.context);
        QCOMPARE(exact.estimatedInputTokens + exact.maxOutputTokens +
                     exact.tokenSafetyMargin,
                 budget.contextWindowTokens);
        QCOMPARE(exact.evidence.first().locations.first().charStart, 90);
        QCOMPARE(exact.evidence.first().locations.first().charLength,
                 item.text.size());
        --budget.contextWindowTokens;
        const auto shortRequest =
            ContextAssembler::assembleBudgeted("q", {item}, {}, budget);
        QVERIFY(shortRequest.evidence.isEmpty());
        QVERIFY(shortRequest.context.isEmpty());
        QCOMPARE(shortRequest.skippedEvidenceIds, QStringList{"approval"});
        QVERIFY(!shortRequest.assemblyError.isEmpty());
    }

    void skipsLargeEvidenceAndKeepsLaterBlock() {
        const auto large = evidence("large", QString(6000, 'a'));
        const auto small = evidence("small", "Only after approval.");
        const auto expected =
            ContextAssembler::assembleBudgeted("q", {small}, {});
        const auto result = ContextAssembler::assembleBudgeted(
            "q", {large, small, small}, {}, exactBudget(expected));
        QCOMPARE(result.context, expected.context);
        QCOMPARE(result.evidence.size(), 1);
        QCOMPARE(result.skippedEvidenceIds, QStringList{"large"});
        QVERIFY(result.assemblyError.isEmpty());
    }

    void retainsRecentHistoryPairsAfterEvidence() {
        const auto item = evidence("rule", "Rule with necessary condition.");
        ChatRequest target =
            ContextAssembler::assembleBudgeted("q", {item}, {});
        target.conversation = {"recent question", "recent answer"};
        const auto result = ContextAssembler::assembleBudgeted(
            "q", {item},
            {QString(2000, 'x'), "old answer", "recent question",
             "recent answer", "unpaired question"},
            exactBudget(target));
        QCOMPARE(result.conversation, target.conversation);
        QCOMPARE(result.droppedHistoryTurns, 1);
        QCOMPARE(result.evidence.size(), 1);
        QVERIFY(ContextAssembler::budgetError(result).isEmpty());
        QCOMPARE(result.estimatedInputTokens,
                 ContextAssembler::estimateInputTokens(result));
    }

    void historyRemainsContiguousAndUncut() {
        const auto item = evidence("rule", "Rule.");
        ChatBudget tight;
        tight.contextWindowTokens = 4096;
        const auto result = ContextAssembler::assembleBudgeted(
            "q", {item}, {"old", "answer", QString(8000, 'x'), "recent answer"},
            tight);
        QVERIFY(result.conversation.isEmpty());
        QCOMPARE(result.droppedHistoryTurns, 2);
        const QString question(2100, 'q');
        ChatBudget budget;
        budget.contextWindowTokens = 8192;
        const auto longQuestion =
            ContextAssembler::assembleBudgeted(question, {item}, {}, budget);
        QCOMPARE(longQuestion.question, question);
        QVERIFY(longQuestion.assemblyError.isEmpty());
    }

    void maximalChunksFitDefaultWindow() {
        // A 1200-character Chinese chunk is the largest unit the chunker emits.
        // The default window must fit several of them or every real question
        // would fail with a budget error.
        const QString text(1200, QChar(0x4E2D));
        const QVector<RagEvidence> chunks{
            evidence("a", text), evidence("b", text), evidence("c", text)};
        const auto result =
            ContextAssembler::assembleBudgeted("问题", chunks, {});
        QVERIFY2(result.assemblyError.isEmpty(),
                 qPrintable(result.assemblyError));
        QCOMPARE(result.evidence.size(), 3);
        QVERIFY(result.skippedEvidenceIds.isEmpty());
        QVERIFY(ContextAssembler::budgetError(result).isEmpty());
    }

    void rejectsImpossibleBudgetAndQuestion() {
        const auto item = evidence("rule", "Rule.");
        ChatBudget budget;
        budget.maxOutputTokens = budget.contextWindowTokens;
        auto result =
            ContextAssembler::assembleBudgeted("q", {item}, {}, budget);
        QVERIFY(!result.assemblyError.isEmpty());
        QVERIFY(result.evidence.isEmpty());
        budget = {};
        budget.tokenSafetyMargin = -1;
        QVERIFY(!ContextAssembler::assembleBudgeted("q", {item}, {}, budget)
                     .assemblyError.isEmpty());
        const QString question(20000, 'q');
        result = ContextAssembler::assembleBudgeted(question, {item}, {});
        QCOMPARE(result.question, question);
        QVERIFY(!result.assemblyError.isEmpty());
        QVERIFY(result.evidence.isEmpty());
    }

    void outputReservationReducesInputCapacity() {
        const auto item = evidence("rule", QString(500, 'a'));
        const auto full = ContextAssembler::assembleBudgeted("q", {item}, {});
        auto budget = exactBudget(full);
        ++budget.maxOutputTokens;
        QVERIFY(ContextAssembler::assembleBudgeted("q", {item}, {}, budget)
                    .evidence.isEmpty());
        budget = exactBudget(full);
        budget.maxEvidenceCharacters = 0;
        QVERIFY(ContextAssembler::assembleBudgeted("q", {item}, {}, budget)
                    .evidence.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestChatBudget)
#include "test_chat_budget.moc"
