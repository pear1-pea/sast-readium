#include "AskPdfProgress.h"

#include <QtGlobal>

namespace Rag {
namespace {

int clampPercent(double value) {
    return qBound(0, static_cast<int>(value), 100);
}

}  // namespace

AskPdfProgressView askPdfProgressView(const AskPdfProgressInput& input) {
    AskPdfProgressView view;

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
                view.percent = clampPercent(ratio * 60.0);
                view.text = QStringLiteral("提取 %1/%2 页")
                                .arg(input.processedPages)
                                .arg(input.totalPages);
            } else {
                view.indeterminate = true;
                view.text = QStringLiteral("正在提取 PDF 文本...");
            }
            return view;
        }
        case RagIndexState::Chunking: {
            view.visible = true;
            if (input.totalPages > 0) {
                const double ratio =
                    double(input.processedPages) / double(input.totalPages);
                view.percent = clampPercent(60.0 + ratio * 10.0);
                view.text = QStringLiteral("切分文本 %1/%2 页")
                                .arg(input.processedPages)
                                .arg(input.totalPages);
            } else {
                view.indeterminate = true;
                view.text = QStringLiteral("正在切分文本...");
            }
            return view;
        }
        case RagIndexState::Indexing:
            view.visible = true;
            view.indeterminate = true;
            view.percent = 70;
            view.text = QStringLiteral("正在写入索引...");
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
            if (input.chunkCount > 0) {
                const double ratio =
                    double(input.embeddedChunkCount) / double(input.chunkCount);
                view.percent = clampPercent(80.0 + ratio * 20.0);
                view.text = QStringLiteral("向量 %1/%2")
                                .arg(input.embeddedChunkCount)
                                .arg(input.chunkCount);
            } else {
                view.indeterminate = true;
                view.text = QStringLiteral("正在准备向量...");
            }
            return view;
        }
        default:
            return view;
    }
}

}  // namespace Rag
