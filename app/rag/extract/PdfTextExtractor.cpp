#include "PdfTextExtractor.h"

#include <poppler/qt6/poppler-qt6.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMap>

#include <algorithm>
#include <functional>
#include <memory>

namespace Rag {
namespace {

const char* const kSectionSeparator = " › ";

/**
 * Map each page to the deepest outline entry that starts at or before it.
 * Outlines are optional; a document without one keeps empty section paths.
 */
QMap<int, QString> outlineSections(Poppler::Document* document) {
    struct Entry {
        int page = 0;
        QString path;
    };
    QVector<Entry> entries;
    std::function<void(const QList<Poppler::OutlineItem>&, const QString&)>
        walk = [&entries, &walk](const QList<Poppler::OutlineItem>& items,
                                 const QString& parent) {
            for (const auto& item : items) {
                const QString name = item.name().trimmed();
                QString path = parent;
                if (!name.isEmpty()) {
                    path = path.isEmpty()
                               ? name
                               : path + QLatin1String(kSectionSeparator) + name;
                }
                const auto destination = item.destination();
                if (destination && destination->pageNumber() > 0) {
                    entries.append({destination->pageNumber() - 1, path});
                }
                if (!item.children().isEmpty()) {
                    walk(item.children(), path);
                }
            }
        };
    walk(document->outline(), {});
    std::stable_sort(
        entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) { return a.page < b.page; });
    QMap<int, QString> sections;
    QString current;
    int next = 0;
    for (int page = 0; page < document->numPages(); ++page) {
        while (next < entries.size() && entries.at(next).page <= page) {
            current = entries.at(next).path;
            ++next;
        }
        if (!current.isEmpty()) {
            sections.insert(page, current);
        }
    }
    return sections;
}

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
        result.reason = RagIndexFailureReason::FileMissing;
        result.error = QStringLiteral("PDF path is empty");
        return result;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.reason = RagIndexFailureReason::FileMissing;
        result.error = QStringLiteral("PDF file does not exist");
        return result;
    }

    // Keep the file identity even when parsing fails so the UI can offer a
    // retry instead of treating the document as missing.
    result.document.canonicalPath = canonicalPathFor(filePath);
    result.document.fileSize = fileInfo.size();
    result.document.modifiedTime = fileInfo.lastModified();

    if (context.isCancelled()) {
        result.error = QStringLiteral("Extraction cancelled");
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.reason = RagIndexFailureReason::FileUnreadable;
        result.error = QStringLiteral("Cannot open PDF file");
        return result;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!updateFileHash(file, hash, context)) {
        if (!context.isCancelled()) {
            result.reason = RagIndexFailureReason::FileUnreadable;
        }
        result.error = context.isCancelled()
                           ? QStringLiteral("Extraction cancelled")
                           : QStringLiteral("Cannot hash PDF file");
        return result;
    }

    result.document.contentHash = QString::fromLatin1(hash.result().toHex());

    std::unique_ptr<Poppler::Document> document(
        Poppler::Document::load(filePath));
    if (!document) {
        result.reason = RagIndexFailureReason::Corrupt;
        result.error = QStringLiteral("Cannot load PDF document");
        return result;
    }
    if (document->isLocked()) {
        result.reason = RagIndexFailureReason::Encrypted;
        result.error = QStringLiteral("PDF document is locked");
        return result;
    }

    result.document.pageCount = document->numPages();

    if (result.document.pageCount <= 0) {
        result.reason = RagIndexFailureReason::NoPages;
        result.error = QStringLiteral("PDF document has no pages");
        return result;
    }

    result.pages.reserve(result.document.pageCount);
    const QMap<int, QString> sections = outlineSections(document.get());
    for (int pageIndex = 0; pageIndex < result.document.pageCount;
         ++pageIndex) {
        if (context.isCancelled()) {
            result.error = QStringLiteral("Extraction cancelled");
            result.pages.clear();
            return result;
        }

        std::unique_ptr<Poppler::Page> page(document->page(pageIndex));
        if (!page) {
            result.reason = RagIndexFailureReason::Corrupt;
            result.error =
                QStringLiteral("Cannot access PDF page %1").arg(pageIndex + 1);
            return result;
        }

        PdfPageText pageText;
        pageText.pageIndex = pageIndex;
        pageText.pageSize = page->pageSizeF();
        pageText.sectionPath = sections.value(pageIndex);
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
