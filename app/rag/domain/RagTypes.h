#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

namespace Rag {

struct DocumentFingerprint {
    QString canonicalPath;
    qint64 fileSize = 0;
    QDateTime modifiedTime;

    bool operator==(const DocumentFingerprint& other) const {
        return canonicalPath == other.canonicalPath &&
               fileSize == other.fileSize && modifiedTime == other.modifiedTime;
    }
};

struct DocumentKey {
    QString canonicalPath;
    qint64 fileSize = 0;
    QDateTime modifiedTime;
    QString contentHash;
    int pageCount = 0;

    bool hasContentHash() const { return !contentHash.isEmpty(); }

    DocumentFingerprint fingerprint() const {
        return {canonicalPath, fileSize, modifiedTime};
    }
};

struct RagCancellationToken {
    std::shared_ptr<std::atomic_bool> flag =
        std::make_shared<std::atomic_bool>(false);

    void cancel() const {
        if (flag) {
            flag->store(true, std::memory_order_relaxed);
        }
    }

    bool isCancelled() const {
        return flag && flag->load(std::memory_order_relaxed);
    }
};

struct RagJobContext {
    QString jobId;
    DocumentKey document;
    quint64 generation = 0;
    RagCancellationToken cancellation;
    /** Optional worker-thread progress callback (marshal to the UI thread). */
    std::function<void(int processed, int total)> onProgress;

    bool isCancelled() const { return cancellation.isCancelled(); }
};

struct PdfTextBox {
    QString text;
    QRectF boundingRect;
    int charStart = 0;
    int charLength = 0;
    bool hasSpaceAfter = false;
};

struct PdfPageText {
    int pageIndex = -1;
    QString plainText;
    QString sectionPath;
    QSizeF pageSize;
    QVector<PdfTextBox> textBoxes;
    bool extractionSucceeded = false;

    bool hasText() const { return !plainText.trimmed().isEmpty(); }
};

struct ChunkPageLocation {
    int pageIndex = -1;
    int charStart = 0;
    int charLength = 0;
    QVector<QRectF> boundingRects;
};

struct PdfChunk {
    QString id;
    DocumentKey document;
    int pageStart = -1;
    int pageEnd = -1;
    QString text;
    QString sectionPath;
    int charStart = 0;
    int charLength = 0;
    QString chunkHash;
    QVector<ChunkPageLocation> locations;
};

struct RagEvidence {
    QString chunkId;
    QString documentHash;
    int pageStart = -1;
    int pageEnd = -1;
    QString text;
    QString sectionPath;
    QVector<ChunkPageLocation> locations;
    double lexicalScore = 0.0;
    double vectorScore = 0.0;
    double rerankScore = 0.0;
    double finalScore = 0.0;
    int rank = -1;
};

struct RagCitation {
    QString chunkId;
    int pageStart = -1;
    int pageEnd = -1;
    QString sectionPath;
    QString snippet;
};

struct RagAnswer {
    /** Markdown with numbered inline citation links. */
    QString text;
    /** Clean text without citation markers, used for copy/history. */
    QString plainText;
    QVector<RagCitation> citations;
};

/** Terminal reason for a chat generation attempt. */
enum class RagChatFailure {
    None,
    Timeout,
    Unreachable,
    ModelMissing,
    BudgetEvidence,
    ContextTooSmall,
    Rejected,
    Cancelled,
    Unknown
};

/** Lifecycle state of one conversation turn. */
enum class RagTurnState { Retrieving, Generating, Ready, Failed, Cancelled };

enum class RagIndexState {
    NotIndexed,
    CheckingCache,
    Extracting,
    Chunking,
    Indexing,
    Ready,
    Cancelled,
    Failed,
    Unsupported
};

enum class RagEmbeddingState {
    Disabled,
    Pending,
    Running,
    Ready,
    Failed,
    Cancelled
};

enum class RagIndexFailureReason {
    None,
    FileMissing,
    FileUnreadable,
    Encrypted,
    Corrupt,
    NoPages,
    ScannedImage,
    Database,
    Unknown
};

struct RagEmbeddingIdentity {
    QString model;
    QString version;

    bool isEmpty() const { return model.isEmpty() || version.isEmpty(); }

    bool operator==(const RagEmbeddingIdentity& other) const {
        return model == other.model && version == other.version;
    }
    bool operator!=(const RagEmbeddingIdentity& other) const {
        return !(*this == other);
    }
};

struct RagDocumentSnapshot {
    DocumentKey key;
    QString filePath;
    RagIndexState state = RagIndexState::NotIndexed;
    RagIndexFailureReason failureReason = RagIndexFailureReason::None;
    quint64 generation = 0;
    int stageSequence = 0;
    int processedPages = 0;
    int totalPages = 0;
    int chunkCount = 0;
    QString error;
    RagEmbeddingState embeddingState = RagEmbeddingState::Disabled;
    RagEmbeddingIdentity embeddingIdentity;
    int embeddedChunkCount = 0;
    QString embeddingError;

    bool isReady() const { return state == RagIndexState::Ready; }
};

struct RagDocumentIndexStats {
    bool present = false;
    int chunkCount = 0;
    int vectorCount = 0;
    qint64 textBytes = 0;
    qint64 vectorBytes = 0;
};

struct RagIndexStats {
    qint64 databaseBytes = 0;
    int documentCount = 0;
    RagDocumentIndexStats current;
};

}  // namespace Rag

Q_DECLARE_METATYPE(Rag::RagIndexStats)
Q_DECLARE_METATYPE(Rag::RagChatFailure)
Q_DECLARE_METATYPE(Rag::RagTurnState)
Q_DECLARE_METATYPE(Rag::RagEvidence)
Q_DECLARE_METATYPE(Rag::RagCitation)
Q_DECLARE_METATYPE(Rag::RagAnswer)
Q_DECLARE_METATYPE(QVector<Rag::RagEvidence>)
Q_DECLARE_METATYPE(QVector<Rag::RagCitation>)
