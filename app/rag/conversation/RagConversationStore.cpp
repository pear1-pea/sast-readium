#include "RagConversationStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <utility>

namespace Rag {
namespace {

constexpr int kMaxDocuments = 100;

QJsonObject citationToJson(const RagCitation& citation) {
    return QJsonObject{{QStringLiteral("chunkId"), citation.chunkId},
                       {QStringLiteral("pageStart"), citation.pageStart},
                       {QStringLiteral("pageEnd"), citation.pageEnd},
                       {QStringLiteral("sectionPath"), citation.sectionPath},
                       {QStringLiteral("snippet"), citation.snippet}};
}

RagCitation citationFromJson(const QJsonObject& object) {
    RagCitation citation;
    citation.chunkId = object.value(QStringLiteral("chunkId")).toString();
    citation.pageStart = object.value(QStringLiteral("pageStart")).toInt(-1);
    citation.pageEnd = object.value(QStringLiteral("pageEnd")).toInt(-1);
    citation.sectionPath =
        object.value(QStringLiteral("sectionPath")).toString();
    citation.snippet = object.value(QStringLiteral("snippet")).toString();
    return citation;
}

QJsonObject evidenceToJson(const RagEvidence& evidence) {
    QJsonArray locations;
    for (const ChunkPageLocation& location : evidence.locations) {
        QJsonArray rects;
        for (const QRectF& rect : location.boundingRects) {
            rects.append(
                QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()});
        }
        locations.append(
            QJsonObject{{QStringLiteral("pageIndex"), location.pageIndex},
                        {QStringLiteral("charStart"), location.charStart},
                        {QStringLiteral("charLength"), location.charLength},
                        {QStringLiteral("rects"), rects}});
    }
    return QJsonObject{{QStringLiteral("chunkId"), evidence.chunkId},
                       {QStringLiteral("documentHash"), evidence.documentHash},
                       {QStringLiteral("pageStart"), evidence.pageStart},
                       {QStringLiteral("pageEnd"), evidence.pageEnd},
                       {QStringLiteral("text"), evidence.text},
                       {QStringLiteral("sectionPath"), evidence.sectionPath},
                       {QStringLiteral("locations"), locations}};
}

RagEvidence evidenceFromJson(const QJsonObject& object) {
    RagEvidence evidence;
    evidence.chunkId = object.value(QStringLiteral("chunkId")).toString();
    evidence.documentHash =
        object.value(QStringLiteral("documentHash")).toString();
    evidence.pageStart = object.value(QStringLiteral("pageStart")).toInt(-1);
    evidence.pageEnd = object.value(QStringLiteral("pageEnd")).toInt(-1);
    evidence.text = object.value(QStringLiteral("text")).toString();
    evidence.sectionPath =
        object.value(QStringLiteral("sectionPath")).toString();
    for (const QJsonValue& locationValue :
         object.value(QStringLiteral("locations")).toArray()) {
        const QJsonObject locationObject = locationValue.toObject();
        ChunkPageLocation location;
        location.pageIndex =
            locationObject.value(QStringLiteral("pageIndex")).toInt(-1);
        location.charStart =
            locationObject.value(QStringLiteral("charStart")).toInt();
        location.charLength =
            locationObject.value(QStringLiteral("charLength")).toInt();
        for (const QJsonValue& rectValue :
             locationObject.value(QStringLiteral("rects")).toArray()) {
            const QJsonArray rect = rectValue.toArray();
            if (rect.size() == 4) {
                location.boundingRects.append(
                    QRectF(rect.at(0).toDouble(), rect.at(1).toDouble(),
                           rect.at(2).toDouble(), rect.at(3).toDouble()));
            }
        }
        evidence.locations.append(location);
    }
    return evidence;
}

QJsonObject turnToJson(const RagConversationTurn& turn) {
    QJsonArray citations;
    for (const RagCitation& citation : turn.citations) {
        citations.append(citationToJson(citation));
    }
    QJsonArray evidence;
    for (const RagEvidence& item : turn.evidence) {
        for (const RagCitation& citation : turn.citations) {
            if (citation.chunkId == item.chunkId) {
                evidence.append(evidenceToJson(item));
                break;
            }
        }
    }
    return QJsonObject{
        {QStringLiteral("id"), turn.id},
        {QStringLiteral("generation"), double(turn.generation)},
        {QStringLiteral("historyEpoch"), double(turn.historyEpoch)},
        {QStringLiteral("question"), turn.question},
        {QStringLiteral("answerText"), turn.answerText},
        {QStringLiteral("answerMarkdown"), turn.answerMarkdown},
        {QStringLiteral("citations"), citations},
        {QStringLiteral("evidence"), evidence}};
}

RagConversationTurn turnFromJson(const QJsonObject& object) {
    RagConversationTurn turn;
    turn.id = object.value(QStringLiteral("id")).toString();
    turn.generation =
        quint64(object.value(QStringLiteral("generation")).toDouble());
    turn.historyEpoch =
        quint64(object.value(QStringLiteral("historyEpoch")).toDouble());
    if (turn.historyEpoch == 0) {
        turn.historyEpoch = 1;
    }
    turn.question = object.value(QStringLiteral("question")).toString();
    turn.answerText = object.value(QStringLiteral("answerText")).toString();
    turn.answerMarkdown =
        object.value(QStringLiteral("answerMarkdown")).toString();
    for (const QJsonValue& citation :
         object.value(QStringLiteral("citations")).toArray()) {
        if (citation.isObject()) {
            turn.citations.append(citationFromJson(citation.toObject()));
        }
    }
    for (const QJsonValue& evidence :
         object.value(QStringLiteral("evidence")).toArray()) {
        if (evidence.isObject()) {
            turn.evidence.append(evidenceFromJson(evidence.toObject()));
        }
    }
    turn.state = RagTurnState::Ready;
    turn.failure = RagChatFailure::None;
    turn.evidenceComplete = false;
    return turn;
}

}  // namespace

JsonConversationStore::JsonConversationStore(QString filePath)
    : m_path(std::move(filePath)) {}

QString JsonConversationStore::defaultPath() {
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString directory = QDir(appData).filePath(QStringLiteral("rag"));
    QDir().mkpath(directory);
    QFile::setPermissions(directory, QFileDevice::ReadOwner |
                                         QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner);
    return QDir(directory).filePath(QStringLiteral("conversations.json"));
}

void JsonConversationStore::ensureLoaded() {
    if (m_loaded) {
        return;
    }
    m_loaded = true;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return;
    }
    const QJsonObject documents =
        document.object().value(QStringLiteral("documents")).toObject();
    for (auto it = documents.constBegin(); it != documents.constEnd(); ++it) {
        RagConversationState state;
        QJsonArray turns;
        if (it.value().isArray()) {
            // Legacy format: the value was the turn array without metadata.
            turns = it.value().toArray();
        } else {
            const QJsonObject object = it.value().toObject();
            turns = object.value(QStringLiteral("turns")).toArray();
            state.historyEpoch = quint64(
                object.value(QStringLiteral("historyEpoch")).toDouble());
            state.chatModel =
                object.value(QStringLiteral("chatModel")).toString();
        }
        if (state.historyEpoch == 0) {
            state.historyEpoch = 1;
        }
        for (const QJsonValue& value : turns) {
            if (!value.isObject()) {
                continue;
            }
            RagConversationTurn turn = turnFromJson(value.toObject());
            if (!turn.id.isEmpty() && !turn.answerText.isEmpty()) {
                state.turns.append(std::move(turn));
            }
        }
        if (!state.turns.isEmpty()) {
            m_documents.insert(it.key(), state);
        }
    }
    for (const QJsonValue& key :
         document.object().value(QStringLiteral("order")).toArray()) {
        if (key.isString() && m_documents.contains(key.toString()) &&
            !m_order.contains(key.toString())) {
            m_order.append(key.toString());
        }
    }
    for (auto it = m_documents.constBegin(); it != m_documents.constEnd();
         ++it) {
        if (!m_order.contains(it.key())) {
            m_order.append(it.key());
        }
    }
}

RagConversationState JsonConversationStore::load(const QString& key) {
    ensureLoaded();
    return m_documents.value(key);
}

void JsonConversationStore::save(const QString& key,
                                 const RagConversationState& state) {
    ensureLoaded();
    RagConversationState persistable;
    persistable.historyEpoch = state.historyEpoch;
    persistable.chatModel = state.chatModel;
    for (const RagConversationTurn& turn : state.turns) {
        if (turn.state == RagTurnState::Ready && !turn.answerText.isEmpty()) {
            persistable.turns.append(turn);
        }
    }
    if (persistable.turns.isEmpty()) {
        m_documents.remove(key);
        m_order.removeAll(key);
    } else {
        m_documents.insert(key, persistable);
        m_order.removeAll(key);
        m_order.append(key);
    }
    write();
}

void JsonConversationStore::remove(const QString& key) {
    ensureLoaded();
    const bool removedTurns = m_documents.remove(key) > 0;
    const bool removedOrder = m_order.removeAll(key) > 0;
    if (removedTurns || removedOrder) {
        write();
    }
}

void JsonConversationStore::write() {
    while (m_order.size() > kMaxDocuments) {
        m_documents.remove(m_order.takeFirst());
    }
    QJsonObject documents;
    for (auto it = m_documents.constBegin(); it != m_documents.constEnd();
         ++it) {
        QJsonArray turns;
        for (const RagConversationTurn& turn : it.value().turns) {
            turns.append(turnToJson(turn));
        }
        documents.insert(
            it.key(),
            QJsonObject{{QStringLiteral("historyEpoch"),
                         double(it.value().historyEpoch)},
                        {QStringLiteral("chatModel"), it.value().chatModel},
                        {QStringLiteral("turns"), turns}});
    }
    QJsonArray order;
    for (const QString& key : m_order) {
        order.append(key);
    }
    QJsonObject root{{QStringLiteral("version"), 2},
                     {QStringLiteral("documents"), documents},
                     {QStringLiteral("order"), order}};

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        return;
    }
    QFile::setPermissions(m_path,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

RagConversationState InMemoryConversationStore::load(const QString& key) {
    return m_documents.value(key);
}

void InMemoryConversationStore::save(const QString& key,
                                     const RagConversationState& state) {
    m_documents.insert(key, state);
}

void InMemoryConversationStore::remove(const QString& key) {
    m_documents.remove(key);
}

}  // namespace Rag
