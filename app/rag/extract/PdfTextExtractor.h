#pragma once

#include "rag/domain/RagTypes.h"

namespace Rag {

struct PdfTextExtractionResult {
    bool success = false;
    bool hasText = false;
    RagIndexFailureReason reason = RagIndexFailureReason::None;
    QString error;
    DocumentKey document;
    QVector<PdfPageText> pages;
};

class PdfTextExtractor {
public:
    static PdfTextExtractionResult extractFile(const QString& filePath,
                                               const RagJobContext& context);
};

}  // namespace Rag
