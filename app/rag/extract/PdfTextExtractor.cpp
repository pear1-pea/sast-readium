#include "PdfTextExtractor.h"

#include <poppler/qt6/poppler-qt6.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <memory>

namespace Rag {
namespace {

QString canonicalPathFor(const QString& filePath) {
    const QFileInfo info(filePath);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool updateFileHash(QFile& file, QCryptographicHash& hash,
                    const RagJobContext& context) {
    constexpr qint64 kReadBlockSize = 1024 * 1024;
    while (!file.atEnd()) {
        if (context.isCancelled()) {
            return false;
        }
        const QByteArray block = file.read(kReadBlockSize);
        if (block.isEmpty() && !file.atEnd()) {
            return false;
        }
        hash.addData(block);
    }
    return !context.isCancelled();
}

void appendTextBox(PdfPageText& pageText, const Poppler::TextBox& box) {
    const QString text = box.text();
    if (text.isEmpty()) {
        return;
    }

    if (!pageText.textBoxes.isEmpty()) {
        const PdfTextBox& previous = pageText.textBoxes.constLast();
        const qreal lineThreshold =
            qMax(previous.boundingRect.height(), box.boundingBox().height());
        const bool newLine =
            qAbs(box.boundingBox().top() - previous.boundingRect.top()) >
            lineThreshold * 0.8;

        if (newLine) {
            pageText.plainText += QChar('\n');
        } else if (!previous.hasSpaceAfter &&
                   !pageText.plainText.endsWith(QChar('\n')) &&
                   !pageText.plainText.endsWith(QChar(' '))) {
            pageText.plainText += QChar(' ');
        }
    }

    PdfTextBox textBox;
    textBox.text = text;
    textBox.boundingRect = box.boundingBox();
    textBox.charStart = pageText.plainText.size();
    textBox.charLength = text.size();
    textBox.hasSpaceAfter = box.hasSpaceAfter();
    pageText.textBoxes.append(textBox);
    pageText.plainText += text;
    if (textBox.hasSpaceAfter) {
        pageText.plainText += QChar(' ');
    }
}

}  // namespace

PdfTextExtractionResult PdfTextExtractor::extractFile(
    const QString& filePath, const RagJobContext& context) {
    PdfTextExtractionResult result;
    if (filePath.isEmpty()) {
        result.error = QStringLiteral("PDF path is empty");
        return result;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.error = QStringLiteral("PDF file does not exist");
        return result;
    }
    if (context.isCancelled()) {
        result.error = QStringLiteral("Extraction cancelled");
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Cannot open PDF file");
        return result;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!updateFileHash(file, hash, context)) {
        result.error = context.isCancelled()
                           ? QStringLiteral("Extraction cancelled")
                           : QStringLiteral("Cannot hash PDF file");
        return result;
    }

    std::unique_ptr<Poppler::Document> document(
        Poppler::Document::load(filePath));
    if (!document) {
        result.error = QStringLiteral("Cannot load PDF document");
        return result;
    }
    if (document->isLocked()) {
        result.error = QStringLiteral("PDF document is locked");
        return result;
    }

    result.document.canonicalPath = canonicalPathFor(filePath);
    result.document.fileSize = fileInfo.size();
    result.document.modifiedTime = fileInfo.lastModified();
    result.document.contentHash = QString::fromLatin1(hash.result().toHex());
    result.document.pageCount = document->numPages();

    if (result.document.pageCount <= 0) {
        result.error = QStringLiteral("PDF document has no pages");
        return result;
    }

    result.pages.reserve(result.document.pageCount);
    for (int pageIndex = 0; pageIndex < result.document.pageCount;
         ++pageIndex) {
        if (context.isCancelled()) {
            result.error = QStringLiteral("Extraction cancelled");
            result.pages.clear();
            return result;
        }

        std::unique_ptr<Poppler::Page> page(document->page(pageIndex));
        if (!page) {
            result.error =
                QStringLiteral("Cannot access PDF page %1").arg(pageIndex + 1);
            return result;
        }

        PdfPageText pageText;
        pageText.pageIndex = pageIndex;
        pageText.pageSize = page->pageSizeF();
        const auto textBoxes = page->textList();
        pageText.textBoxes.reserve(textBoxes.size());
        for (const std::unique_ptr<Poppler::TextBox>& textBox : textBoxes) {
            if (context.isCancelled()) {
                result.error = QStringLiteral("Extraction cancelled");
                result.pages.clear();
                return result;
            }
            if (textBox) {
                appendTextBox(pageText, *textBox);
            }
        }
        pageText.extractionSucceeded = true;
        result.hasText = result.hasText || pageText.hasText();
        result.pages.append(std::move(pageText));
    }

    result.success = true;
    return result;
}

}  // namespace Rag
