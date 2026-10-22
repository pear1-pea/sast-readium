#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QtTest/QtTest>

#include "rag/chat/ContextAssembler.h"
#include "rag/extract/PdfChunker.h"
#include "rag/retrieve/EvidenceExpander.h"
#include "rag/retrieve/HybridRetriever.h"

using namespace Rag;

namespace {

struct SourceSpan {
    QString documentHash;
    int page = 0;
    int start = 0;
    int length = 0;
};

struct Question {
    QString category;
    QString query;
    QVector<SourceSpan> required;
};

QJsonArray locationsJson(const QVector<ChunkPageLocation>& locations) {
    QJsonArray result;
    for (const auto& location : locations)
        result.append(QJsonObject{{"page", location.pageIndex},
                                  {"start", location.charStart},
                                  {"length", location.charLength}});
    return result;
}

QJsonArray evidenceJson(const QVector<RagEvidence>& evidence) {
    QJsonArray result;
    for (const auto& item : evidence)
        result.append(
            QJsonObject{{"chunk_id", item.chunkId},
                        {"document_hash", item.documentHash},
                        {"text", item.text},
                        {"locations", locationsJson(item.locations)}});
    return result;
}

QSet<int> coveredPositions(const QVector<RagEvidence>& evidence,
                           const QString& documentHash, int page) {
    QSet<int> positions;
    for (const auto& item : evidence) {
        if (item.documentHash != documentHash)
            continue;
        for (const auto& location : item.locations) {
            if (location.pageIndex != page)
                continue;
            for (int i = 0; i < location.charLength; ++i)
                positions.insert(location.charStart + i);
        }
    }
    return positions;
}

double recall(const QVector<SourceSpan>& required,
              const QVector<RagEvidence>& evidence,
              const QVector<PdfPageText>& pages) {
    int complete = 0;
    for (const auto& span : required) {
        const auto covered =
            coveredPositions(evidence, span.documentHash, span.page);
        bool found = true;
        for (int i = 0; i < span.length; ++i) {
            const int offset = span.start + i;
            if (!pages[span.page].plainText.at(offset).isSpace())
                found = found && covered.contains(offset);
        }
        complete += found;
    }
    return required.isEmpty() ? 0.0 : double(complete) / required.size();
}

int duplicateCharacters(const QVector<RagEvidence>& evidence,
                        const DocumentKey& document) {
    int total = 0;
    int unique = 0;
    for (const auto& item : evidence) {
        if (item.documentHash == document.contentHash) {
            for (const auto& location : item.locations)
                total += location.charLength;
        }
    }
    for (int page = 0; page < document.pageCount; ++page)
        unique += coveredPositions(evidence, document.contentHash, page).size();
    return total - unique;
}

QVector<PdfPageText> fixturePages() {
    const QStringList text{
        "A ledger is a chronological record of financial transactions.",
        "Enrollment requires a verified identity. Enrollment steps: create an "
        "account, verify the email address, then send the form.",
        "Standard claims require an invoice. Emergency claims additionally "
        "require supervisor approval before payment.",
        "The following procedure applies only after approval has been granted.",
        "Submission portal: upload the completed form and press Send.",
        QStringLiteral("Duplicate policy introduction. ") +
            QStringLiteral("Duplicate evidence preserves the same rule. ")
                .repeated(65) +
            QStringLiteral(
                "Duplicate policy final exception requires review.")};
    QVector<PdfPageText> pages;
    for (int i = 0; i < text.size(); ++i) {
        PdfPageText page;
        page.pageIndex = i;
        page.plainText = text[i];
        page.extractionSucceeded = true;
        pages.append(page);
    }
    return pages;
}

}  // namespace

class TestRagEvidenceBaseline : public QObject {
    Q_OBJECT
private slots:
    void sourceEvidenceSurvivesRechunking();
};

void TestRagEvidenceBaseline::sourceEvidenceSurvivesRechunking() {
    const auto pages = fixturePages();
    QJsonArray sourcePages;
    QByteArray source;
    for (const auto& page : pages) {
        sourcePages.append(
            QJsonObject{{"page", page.pageIndex}, {"text", page.plainText}});
        source += page.plainText.toUtf8() + '\0';
    }
    DocumentKey document;
    document.canonicalPath = QStringLiteral("synthetic-policy-v1.pdf");
    document.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
    document.pageCount = pages.size();
    auto span = [&](int page, const QString& text) {
        return SourceSpan{document.contentHash, page,
                          int(pages[page].plainText.indexOf(text)),
                          int(text.size())};
    };
    const QVector<Question> questions{
        {"definition",
         "ledger",
         {span(
             0,
             "A ledger is a chronological record of financial transactions.")}},
        {"process",
         "Enrollment steps",
         {span(1, "Enrollment requires a verified identity."),
          span(1,
               "create an account, verify the email address, then send the "
               "form.")}},
        {"condition",
         "Enrollment requires",
         {span(1, "Enrollment requires a verified identity.")}},
        {"exception",
         "claims",
         {span(2, "Standard claims require an invoice."),
          span(2,
               "Emergency claims additionally require supervisor approval "
               "before payment.")}},
        {"cross-page",
         "Submission portal",
         {span(3,
               "The following procedure applies only after approval has been "
               "granted."),
          span(
              4,
              "Submission portal: upload the completed form and press Send.")}},
        {"duplicate",
         "Duplicate",
         {span(5, "Duplicate policy introduction."),
          span(5, "Duplicate policy final exception requires review.")}}};
    QJsonArray gold;
    for (const auto& question : questions) {
        QJsonArray required;
        for (const auto& item : question.required) {
            QVERIFY(item.start >= 0);
            QVERIFY(item.length > 0);
            required.append(QJsonObject{{"document_hash", item.documentHash},
                                        {"page", item.page},
                                        {"start", item.start},
                                        {"length", item.length}});
        }
        gold.append(QJsonObject{{"category", question.category},
                                {"query", question.query},
                                {"required_source_spans", required}});
    }
    const QVector<QPair<QString, PdfChunkerOptions>> configurations{
        {"default", {800, 1200, 120, false}},
        {"no-overlap", {800, 1200, 0, false}},
        {"sentence-overlap", {800, 1200, 120, true}},
        {"smaller", {180, 260, 120, false}}};
    QJsonArray configurationsJson;
    for (const auto& configuration : configurations) {
        RagIndexStore store;
        QString error;
        QVERIFY2(store.open({}, &error), qPrintable(error));
        QElapsedTimer indexTimer;
        indexTimer.start();
        const auto chunks =
            PdfChunker::chunk(pages, document, {}, configuration.second);
        QVERIFY2(store.replaceDocument(document, chunks, &error),
                 qPrintable(error));
        const qint64 indexNs = indexTimer.nsecsElapsed();
        QStringList ids;
        int indexedCharacters = 0;
        for (const auto& chunk : chunks) {
            ids.append(chunk.id);
            indexedCharacters += chunk.text.size();
            QCOMPARE(chunk.text, pages[chunk.pageStart].plainText.mid(
                                     chunk.charStart, chunk.charLength));
        }
        const auto allEvidence = store.evidenceByIds(document, ids, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(allEvidence.size(), chunks.size());
        for (const auto& question : questions)
            QCOMPARE(recall(question.required, allEvidence, pages), 1.0);
        auto wrongDocument = questions.first().required;
        wrongDocument.first().documentHash = "different-document";
        QCOMPARE(recall(wrongDocument, allEvidence, pages), 0.0);
        auto partial = allEvidence;
        for (auto& item : partial) {
            for (auto& location : item.locations) {
                if (location.pageIndex == 0)
                    location.charLength = 10;
            }
        }
        QCOMPARE(recall(questions.first().required, partial, pages), 0.0);
        if (configuration.first == "no-overlap")
            QCOMPARE(duplicateCharacters(allEvidence, document), 0);
        else
            QVERIFY(duplicateCharacters(allEvidence, document) > 0);

        HybridRetriever retriever(QString{});
        QJsonArray runs;
        for (const int budget : {12000, 700}) {
            double retrievalTotal = 0.0;
            double contextTotal = 0.0;
            for (const auto& question : questions) {
                QElapsedTimer queryTimer;
                queryTimer.start();
                const auto retrieved = retriever.retrieve(
                    store, document, question.query, 3, nullptr, {}, &error);
                QVERIFY2(error.isEmpty(), qPrintable(error));
                const qint64 retrievalNs = queryTimer.nsecsElapsed();
                const auto request = ContextAssembler::assemble(
                    question.query, retrieved, {}, budget);
                const double retrievalRecall =
                    recall(question.required, retrieved, pages);
                const double contextRecall =
                    recall(question.required, request.evidence, pages);
                QVERIFY(contextRecall <= retrievalRecall);
                QVERIFY(request.context.size() <= budget);
                for (const auto& evidence : request.evidence)
                    QVERIFY(request.context.contains(evidence.text));
                if (question.category == "cross-page") {
                    QCOMPARE(retrievalRecall, 0.5);
                    QCOMPARE(contextRecall, 0.5);
                } else if (question.category != "duplicate") {
                    QCOMPARE(retrievalRecall, 1.0);
                    QCOMPARE(contextRecall, 1.0);
                }
                retrievalTotal += retrievalRecall;
                contextTotal += contextRecall;
                runs.append(QJsonObject{
                    {"category", question.category},
                    {"query", question.query},
                    {"comparison",
                     budget == 12000 ? "fixed-k" : "fixed-character-budget"},
                    {"top_k", 3},
                    {"context_character_budget", budget},
                    {"retrieval_evidence_recall", retrievalRecall},
                    {"context_evidence_recall", contextRecall},
                    {"retrieved", evidenceJson(retrieved)},
                    {"sent_evidence", evidenceJson(request.evidence)},
                    {"actual_context", request.context},
                    {"context_characters", int(request.context.size())},
                    {"retrieval_duplicate_characters",
                     duplicateCharacters(retrieved, document)},
                    {"context_duplicate_characters",
                     duplicateCharacters(request.evidence, document)},
                    {"retrieval_nanoseconds", double(retrievalNs)}});
            }
            qInfo().noquote()
                << configuration.first << "budget=" << budget
                << "recall@3=" << retrievalTotal / questions.size()
                << "context evidence recall="
                << contextTotal / questions.size();
        }
        // Token budgets are compared at equal estimated input size, not at
        // equal character counts: the three chunk configurations produce
        // different characters-per-token ratios, so a fixed character budget
        // silently gives each configuration a different real input size.
        for (const int window : {16384, 8192}) {
            ChatBudget budget;
            budget.contextWindowTokens = window;
            double retrievalTotal = 0.0;
            double contextTotal = 0.0;
            for (const auto& question : questions) {
                QElapsedTimer queryTimer;
                queryTimer.start();
                const auto retrieved = retriever.retrieve(
                    store, document, question.query, 3, nullptr, {}, &error);
                QVERIFY2(error.isEmpty(), qPrintable(error));
                const qint64 retrievalNs = queryTimer.nsecsElapsed();
                // Production expands neighbours before budgeting; the token
                // runs measure that full pipeline.
                const auto expanded = EvidenceExpander::expand(
                    retrieved, store, document, {}, &error);
                QVERIFY2(error.isEmpty(), qPrintable(error));
                const auto request = ContextAssembler::assembleBudgeted(
                    question.query, expanded, {}, budget);
                QVERIFY2(ContextAssembler::budgetError(request).isEmpty(),
                         qPrintable(ContextAssembler::budgetError(request)));
                const int outputReservation =
                    request.maxOutputTokens + request.tokenSafetyMargin;
                QVERIFY(request.estimatedInputTokens + outputReservation <=
                        window);
                const double retrievalRecall =
                    recall(question.required, retrieved, pages);
                const double expandedRecall =
                    recall(question.required, expanded, pages);
                const double contextRecall =
                    recall(question.required, request.evidence, pages);
                QVERIFY(expandedRecall >= retrievalRecall);
                QVERIFY(contextRecall <= expandedRecall);
                for (const auto& evidence : request.evidence)
                    QVERIFY(request.context.contains(evidence.text));
                retrievalTotal += retrievalRecall;
                contextTotal += contextRecall;
                runs.append(QJsonObject{
                    {"category", question.category},
                    {"query", question.query},
                    {"comparison", "fixed-token-budget"},
                    {"top_k", 3},
                    {"context_window_tokens", window},
                    {"output_reservation_tokens", outputReservation},
                    {"estimated_input_tokens",
                     double(request.estimatedInputTokens)},
                    {"retrieval_evidence_recall", retrievalRecall},
                    {"expanded_evidence_recall", expandedRecall},
                    {"context_evidence_recall", contextRecall},
                    {"retrieved", evidenceJson(retrieved)},
                    {"expanded", evidenceJson(expanded)},
                    {"sent_evidence", evidenceJson(request.evidence)},
                    {"actual_context", request.context},
                    {"context_characters", int(request.context.size())},
                    {"skipped_evidence_ids",
                     QJsonArray::fromStringList(request.skippedEvidenceIds)},
                    {"dropped_history_turns", request.droppedHistoryTurns},
                    {"retrieval_duplicate_characters",
                     duplicateCharacters(retrieved, document)},
                    {"context_duplicate_characters",
                     duplicateCharacters(request.evidence, document)},
                    {"retrieval_nanoseconds", double(retrievalNs)}});
            }
            qInfo().noquote()
                << configuration.first << "token_window=" << window
                << "recall@3=" << retrievalTotal / questions.size()
                << "context evidence recall="
                << contextTotal / questions.size();
        }
        configurationsJson.append(QJsonObject{
            {"name", configuration.first},
            {"target_characters", configuration.second.targetCharacters},
            {"max_characters", configuration.second.maxCharacters},
            {"overlap_characters", configuration.second.overlapCharacters},
            {"index_chunks", int(chunks.size())},
            {"indexed_characters", indexedCharacters},
            {"index_nanoseconds", double(indexNs)},
            {"chunks", evidenceJson(allEvidence)},
            {"runs", runs}});
    }
    const QJsonObject report{
        {"schema_version", 2},
        {"fixture", "synthetic-policy-v1"},
        {"document_hash", document.contentHash},
        {"scope",
         "Synthetic parsed page text; lexical retrieval only. No PDF "
         "extraction, embedding or answer evaluation."},
        {"length_unit",
         "QString UTF-16 code units for character budgets; UTF-8 bytes plus "
         "message overhead for token budgets (conservative, not a tokenizer)"},
        {"recall_definition",
         "Fraction of required source spans whose non-whitespace positions are "
         "fully covered by the union of returned ranges."},
        {"source_pages", sourcePages},
        {"gold", gold},
        {"configurations", configurationsJson}};
    const QString reportPath = qEnvironmentVariable("READIUM_RAG_EVAL_REPORT");
    if (!reportPath.isEmpty()) {
        QVERIFY2(QFileInfo(reportPath).isAbsolute(),
                 "Report path must be absolute");
        QSaveFile output(reportPath);
        QVERIFY2(output.open(QIODevice::WriteOnly),
                 qPrintable(output.errorString()));
        const auto bytes =
            QJsonDocument(report).toJson(QJsonDocument::Indented);
        QCOMPARE(output.write(bytes), qint64(bytes.size()));
        QVERIFY2(output.commit(), qPrintable(output.errorString()));
    }
}

QTEST_MAIN(TestRagEvidenceBaseline)
#include "test_rag_evidence_baseline.moc"
