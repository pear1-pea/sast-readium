#include "AskPdfProgress.h"

#include <QtGlobal>

namespace Rag {
namespace {

int clampPercent(double value) {
    return qBound(0, static_cast<int>(value), 100);
}

/**
 * The bar is stage-local: each phase starts at 0% and reports its own work.
 * A cumulative bar would have to invent weights for phases whose cost is not
 * known in advance (embedding speed depends on the model and the machine).
 */
QString stagePrefix(int stage, int stages) {
    return QStringLiteral("阶段 %1/%2 · ").arg(stage).arg(stages);
}

}  // namespace

AskPdfProgressView askPdfProgressView(const AskPdfProgressInput& input) {
    AskPdfProgressView view;
    const int stages = input.embeddingsEnabled ? 4 : 3;

    switch (input.indexState) {
        case RagIndexState::CheckingCache:
            view.visible = true;
            view.indeterminate = true;
            view.text = QStringLiteral("正在检查索引缓存...");
            return view;
        case RagIndexState::Extracting: {
            view.visible = true;
            if (input.totalPages > 0) {
                const double ratio =
                    double(input.processedPages) / double(input.totalPages);
                view.percent = clampPercent(ratio * 100.0);
                view.text =
                    stagePrefix(1, stages) + QStringLiteral("提取 %1/%2 页")
                                                 .arg(input.processedPages)
                                                 .arg(input.totalPages);
            } else {
                view.indeterminate = true;
                view.text = stagePrefix(1, stages) +
                            QStringLiteral("正在提取 PDF 文本...");
            }
            return view;
        }
        case RagIndexState::Chunking:
            view.visible = true;
            // Chunking has no reliable per-page progress: the page counter is
            // already complete and the work happens per paragraph group.
            view.indeterminate = true;
            view.text =
                stagePrefix(2, stages) + QStringLiteral("正在切分文本...");
            return view;
        case RagIndexState::Indexing:
            view.visible = true;
            view.indeterminate = true;
            view.text =
                stagePrefix(3, stages) + QStringLiteral("正在写入索引...");
            return view;
        case RagIndexState::Ready: {
            const bool embeddingRunning =
                input.embeddingsEnabled &&
                (input.embeddingState == RagEmbeddingState::Pending ||
                 input.embeddingState == RagEmbeddingState::Running);
            if (!embeddingRunning) {
                return view;
            }
            view.visible = true;
            if (input.embeddingState == RagEmbeddingState::Pending) {
                view.indeterminate = true;
                view.text =
                    stagePrefix(4, stages) + QStringLiteral("正在准备向量...");
                return view;
            }
            if (input.chunkCount > 0) {
                const double ratio =
                    double(input.embeddedChunkCount) / double(input.chunkCount);
                view.percent = clampPercent(ratio * 100.0);
                view.text = stagePrefix(4, stages) +
                            QStringLiteral("向量 %1/%2 个文本块")
                                .arg(input.embeddedChunkCount)
                                .arg(input.chunkCount);
            } else {
                view.indeterminate = true;
                view.text =
                    stagePrefix(4, stages) + QStringLiteral("正在准备向量...");
            }
            return view;
        }
        default:
            return view;
    }
}

}  // namespace Rag
