#include "AskPdfState.h"

#include <utility>

namespace Rag {
namespace {

bool isIndexing(RagIndexState state) {
    return state == RagIndexState::CheckingCache ||
           state == RagIndexState::Extracting ||
           state == RagIndexState::Chunking || state == RagIndexState::Indexing;
}

bool embeddingUnavailable(RagEmbeddingState state) {
    return state == RagEmbeddingState::Disabled ||
           state == RagEmbeddingState::Failed ||
           state == RagEmbeddingState::Cancelled;
}

QString embeddingDetail(RagEmbeddingState state) {
    switch (state) {
        case RagEmbeddingState::Failed:
            return QStringLiteral(
                "Embedding 模型当前不可用，可以在本地 AI "
                "设置中检查。");
        case RagEmbeddingState::Cancelled:
            return QStringLiteral(
                "向量生成已取消，可以重新打开文档或重建索引。");
        case RagEmbeddingState::Disabled:
        default:
            return QStringLiteral(
                "尚未配置 Embedding 模型，可以在本地 AI "
                "设置中启用。");
    }
}

AskPdfStateView makeView(
    AskPdfViewState state, QString title, QString detail, QString primaryText,
    AskPdfStateAction primaryAction, QString secondaryText = {},
    AskPdfStateAction secondaryAction = AskPdfStateAction::None) {
    AskPdfStateView view;
    view.state = state;
    view.visible = true;
    view.title = std::move(title);
    view.detail = std::move(detail);
    view.primaryText = std::move(primaryText);
    view.primaryAction = primaryAction;
    view.secondaryText = std::move(secondaryText);
    view.secondaryAction = secondaryAction;
    return view;
}

}  // namespace

AskPdfViewState askPdfViewState(const AskPdfViewInput& input) {
    if (!input.hasDocument) {
        return AskPdfViewState::NoDocument;
    }
    if (isIndexing(input.indexState)) {
        return AskPdfViewState::Indexing;
    }
    if (input.indexState == RagIndexState::Cancelled) {
        return AskPdfViewState::IndexCancelled;
    }
    if (input.indexState == RagIndexState::Unsupported) {
        return AskPdfViewState::IndexUnsupported;
    }
    if (input.indexState == RagIndexState::Failed) {
        return AskPdfViewState::IndexFailed;
    }
    if (input.indexState != RagIndexState::Ready) {
        return AskPdfViewState::ReadyIdle;
    }
    if (input.busy) {
        return AskPdfViewState::Searching;
    }
    if (input.queryFailed) {
        return AskPdfViewState::QueryFailed;
    }
    if (input.hasSearched && !input.hasEvidence) {
        return AskPdfViewState::NoEvidence;
    }
    if (input.answerFailed) {
        return AskPdfViewState::AnswerFailed;
    }
    if (!input.answerEnabled) {
        return AskPdfViewState::EvidenceOnly;
    }
    if (embeddingUnavailable(input.embeddingState)) {
        return AskPdfViewState::SemanticUnavailable;
    }
    return AskPdfViewState::ReadyIdle;
}

QString askPdfStatusText(const AskPdfViewInput& input) {
    if (!input.hasDocument) {
        return QStringLiteral("未打开 PDF");
    }
    switch (input.indexState) {
        case RagIndexState::NotIndexed:
            return QStringLiteral("尚未建立索引");
        case RagIndexState::CheckingCache:
            return QStringLiteral("正在检查索引缓存...");
        case RagIndexState::Extracting:
            return QStringLiteral("正在提取 PDF 文本...");
        case RagIndexState::Chunking:
            return QStringLiteral("正在切分文本...");
        case RagIndexState::Indexing:
            return QStringLiteral("正在建立搜索索引...");
        case RagIndexState::Ready: {
            QString semantic;
            switch (input.embeddingState) {
                case RagEmbeddingState::Pending:
                case RagEmbeddingState::Running:
                    semantic = QStringLiteral("语义检索准备中 %1/%2")
                                   .arg(input.embeddedChunkCount)
                                   .arg(input.chunkCount);
                    break;
                case RagEmbeddingState::Ready:
                    semantic = QStringLiteral("语义检索就绪");
                    break;
                case RagEmbeddingState::Failed:
                    semantic = input.embeddingError.isEmpty()
                                   ? QStringLiteral("语义检索失败")
                                   : QStringLiteral("语义检索失败：%1")
                                         .arg(input.embeddingError);
                    break;
                case RagEmbeddingState::Cancelled:
                    semantic = QStringLiteral("语义检索已取消");
                    break;
                case RagEmbeddingState::Disabled:
                default:
                    semantic = QStringLiteral("语义检索关闭");
                    break;
            }
            QString text = QStringLiteral("索引就绪（%1 个文本块） · %2")
                               .arg(input.chunkCount)
                               .arg(semantic);
            if (!input.answerEnabled) {
                text += QStringLiteral(" · 回答已关闭");
            }
            return text;
        }
        case RagIndexState::Cancelled:
        case RagIndexState::Failed:
        case RagIndexState::Unsupported: {
            const AskPdfStateView view = askPdfStateView(input);
            if (view.visible) {
                return view.detail.isEmpty() ? view.title
                                             : QStringLiteral("%1\n%2")
                                                   .arg(view.title)
                                                   .arg(view.detail);
            }
            break;
        }
    }
    return QStringLiteral("索引就绪");
}

AskPdfStateView askPdfStateView(const AskPdfViewInput& input) {
    const AskPdfViewState state = askPdfViewState(input);
    switch (state) {
        case AskPdfViewState::NoDocument:
            return makeView(state, QStringLiteral("打开 PDF 后即可搜索证据。"),
                            {}, {}, AskPdfStateAction::None);
        case AskPdfViewState::Indexing:
        case AskPdfViewState::ReadyIdle:
        case AskPdfViewState::Searching:
            return {};
        case AskPdfViewState::IndexCancelled:
            return makeView(state, QStringLiteral("索引已取消。"),
                            QStringLiteral("重建索引后即可搜索证据。"),
                            QStringLiteral("重建索引"),
                            AskPdfStateAction::RebuildIndex);
        case AskPdfViewState::IndexUnsupported: {
            if (input.failureReason == RagIndexFailureReason::ScannedImage) {
                return makeView(
                    state,
                    QStringLiteral("当前 PDF 没有可提取文本，可能是扫描版。"),
                    QStringLiteral("需要 OCR 后才能使用 Ask PDF。"), {},
                    AskPdfStateAction::None);
            }
            if (input.failureReason == RagIndexFailureReason::NoPages) {
                return makeView(state, QStringLiteral("PDF 没有可读取的页面。"),
                                QStringLiteral("文件可能是空的或已损坏。"),
                                QStringLiteral("重试"),
                                AskPdfStateAction::RebuildIndex);
            }
            return makeView(state, QStringLiteral("当前 PDF 不支持 Ask PDF。"),
                            QStringLiteral("无法从该文件生成可搜索的文本。"),
                            QStringLiteral("重试"),
                            AskPdfStateAction::RebuildIndex);
        }
        case AskPdfViewState::IndexFailed: {
            switch (input.failureReason) {
                case RagIndexFailureReason::Encrypted:
                    return makeView(
                        state, QStringLiteral("PDF 已加密，无法提取文本。"),
                        QStringLiteral("请先解除密码保护，再重试。"),
                        QStringLiteral("重试"),
                        AskPdfStateAction::RebuildIndex);
                case RagIndexFailureReason::Corrupt:
                    return makeView(
                        state, QStringLiteral("PDF 无法解析，文件可能已损坏。"),
                        QStringLiteral("可以重试；如果仍然失败，请检查文件。"),
                        QStringLiteral("重试"),
                        AskPdfStateAction::RebuildIndex);
                case RagIndexFailureReason::FileMissing:
                    return makeView(state, QStringLiteral("找不到 PDF 文件。"),
                                    QStringLiteral("文件可能已被移动或删除。"),
                                    QStringLiteral("重试"),
                                    AskPdfStateAction::RebuildIndex);
                case RagIndexFailureReason::FileUnreadable:
                    return makeView(state,
                                    QStringLiteral("无法读取 PDF 文件。"),
                                    QStringLiteral("请检查文件权限后重试。"),
                                    QStringLiteral("重试"),
                                    AskPdfStateAction::RebuildIndex);
                case RagIndexFailureReason::NoPages:
                    return makeView(state,
                                    QStringLiteral("PDF 没有可读取的页面。"),
                                    QStringLiteral("文件可能是空的或已损坏。"),
                                    QStringLiteral("重试"),
                                    AskPdfStateAction::RebuildIndex);
                case RagIndexFailureReason::Database:
                    return makeView(
                        state, QStringLiteral("本地索引不可用或已损坏。"),
                        QStringLiteral(
                            "可以重建索引；如果仍然失败，请清除索引。"),
                        QStringLiteral("重建索引"),
                        AskPdfStateAction::RebuildIndex,
                        QStringLiteral("清除索引"),
                        AskPdfStateAction::ClearIndex);
                case RagIndexFailureReason::None:
                case RagIndexFailureReason::ScannedImage:
                case RagIndexFailureReason::Unknown:
                default:
                    return makeView(
                        state, QStringLiteral("索引失败。"),
                        input.indexError.isEmpty()
                            ? QStringLiteral(
                                  "请重试；如果仍然失败，可以重建索引。")
                            : input.indexError,
                        QStringLiteral("重试"),
                        AskPdfStateAction::RebuildIndex);
            }
        }
        case AskPdfViewState::QueryFailed:
            return makeView(
                state, QStringLiteral("检索失败。"),
                input.queryError.isEmpty() ? QStringLiteral("请稍后重试。")
                                           : input.queryError,
                QStringLiteral("重试"), AskPdfStateAction::RetryQuery);
        case AskPdfViewState::NoEvidence:
            return makeView(
                state,
                QStringLiteral(
                    "没有找到足够相关的证据。尝试换一种问法或重建索引。"),
                {}, QStringLiteral("换个问法"), AskPdfStateAction::FocusQuery,
                QStringLiteral("重建索引"), AskPdfStateAction::RebuildIndex);
        case AskPdfViewState::AnswerFailed:
            return makeView(
                state, QStringLiteral("回答生成失败。"),
                input.answerError.isEmpty() ? QStringLiteral("请稍后重试。")
                                            : input.answerError,
                QStringLiteral("重试"), AskPdfStateAction::RetryAnswer,
                QStringLiteral("检查本地模型"),
                AskPdfStateAction::OpenSettings);
        case AskPdfViewState::EvidenceOnly: {
            QString detail;
            if (embeddingUnavailable(input.embeddingState)) {
                detail = QStringLiteral("语义检索不可用，当前使用关键词检索。");
            }
            return makeView(
                state, QStringLiteral("未启用回答生成。当前仅显示本地证据。"),
                detail, QStringLiteral("配置本地模型"),
                AskPdfStateAction::OpenSettings);
        }
        case AskPdfViewState::SemanticUnavailable:
            return makeView(
                state, QStringLiteral("语义检索不可用，当前使用关键词检索。"),
                embeddingDetail(input.embeddingState),
                QStringLiteral("检查本地模型"),
                AskPdfStateAction::OpenSettings);
    }
    return {};
}

}  // namespace Rag
