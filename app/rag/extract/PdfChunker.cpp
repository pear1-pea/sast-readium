#include "PdfChunker.h"

#include <QCryptographicHash>

namespace Rag {
namespace {

struct TextRange {
    int start = 0;
    int end = 0;
};

TextRange trimmedRange(const QString& text, int start, int end) {
    while (start < end && text.at(start).isSpace()) {
        ++start;
    }
    while (end > start && text.at(end - 1).isSpace()) {
        --end;
    }
    return {start, end};
}

QVector<TextRange> paragraphRanges(const QString& text) {
    QVector<TextRange> ranges;
    int start = 0;
    for (int i = 0; i + 1 < text.size(); ++i) {
        if (text.at(i) == QChar('\n') && text.at(i + 1) == QChar('\n')) {
            const TextRange range = trimmedRange(text, start, i);
            if (range.start < range.end) {
                ranges.append(range);
            }
            i += 1;
            start = i + 1;
        }
    }

    const TextRange last = trimmedRange(text, start, text.size());
    if (last.start < last.end) {
        ranges.append(last);
    }
    return ranges;
}

int splitBoundary(const QString& text, int start, int desiredEnd) {
    const int minimumEnd = qMin(desiredEnd, start + 1);
    for (int i = desiredEnd - 1; i >= minimumEnd; --i) {
        const QChar character = text.at(i);
        if (character == QChar('.') || character == QChar('!') ||
            character == QChar('?') || character == QChar(0x3002) ||
            character == QChar(0xFF01) || character == QChar(0xFF1F) ||
            character == QChar(';') || character == QChar(0xFF1B) ||
            character.isSpace()) {
            return i + 1;
        }
    }
    return desiredEnd;
}

QVector<TextRange> splitRange(const QString& text, const TextRange& range,
                              const PdfChunkerOptions& options) {
    QVector<TextRange> result;
    const int maxCharacters = qMax(1, options.maxCharacters);
    const int overlap =
        qBound(0, options.overlapCharacters, qMax(0, maxCharacters - 1));
    int start = range.start;

    while (start < range.end) {
        const int desiredEnd = qMin(range.end, start + maxCharacters);
        int end = desiredEnd;
        if (desiredEnd < range.end) {
            end = splitBoundary(text, start, desiredEnd);
        }
        end = qBound(start + 1, end, range.end);
        result.append({start, end});
        if (end == range.end) {
            break;
        }
        start = qMax(start + 1, end - overlap);
    }
    return result;
}

QVector<TextRange> rangesForPage(const QString& text,
                                 const PdfChunkerOptions& options) {
    QVector<TextRange> paragraphs = paragraphRanges(text);
    QVector<TextRange> grouped;
    if (paragraphs.isEmpty()) {
        return grouped;
    }

    const int targetCharacters = qMax(1, options.targetCharacters);
    TextRange current = paragraphs.first();
    for (int i = 1; i < paragraphs.size(); ++i) {
        const TextRange candidate = paragraphs.at(i);
        if (candidate.end - current.start <= targetCharacters) {
            current.end = candidate.end;
            continue;
        }
        grouped.append(current);
        current = candidate;
    }
    grouped.append(current);

    QVector<TextRange> result;
    for (const TextRange& range : grouped) {
        const QVector<TextRange> pieces = splitRange(text, range, options);
        result += pieces;
    }
    return result;
}

QString hashText(const QString& text) {
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
}

QVector<ChunkPageLocation> locationsForRange(const PdfPageText& page,
                                             const TextRange& range) {
    ChunkPageLocation location;
    location.pageIndex = page.pageIndex;
    location.charStart = range.start;
    location.charLength = range.end - range.start;

    for (const PdfTextBox& textBox : page.textBoxes) {
        const int boxEnd = textBox.charStart + textBox.charLength;
        if (textBox.charStart < range.end && boxEnd > range.start) {
            location.boundingRects.append(textBox.boundingRect);
        }
    }

    return {location};
}

}  // namespace

QVector<PdfChunk> PdfChunker::chunk(const QVector<PdfPageText>& pages,
                                    const DocumentKey& document,
                                    const RagJobContext& context,
                                    const PdfChunkerOptions& options) {
    QVector<PdfChunk> chunks;
    const PdfChunkerOptions normalizedOptions{
        qMax(1, options.targetCharacters),
        qMax(qMax(1, options.targetCharacters), options.maxCharacters),
        qMax(0, options.overlapCharacters)};

    for (const PdfPageText& page : pages) {
        if (context.isCancelled()) {
            return {};
        }
        if (!page.extractionSucceeded || !page.hasText()) {
            continue;
        }

        const QVector<TextRange> ranges =
            rangesForPage(page.plainText, normalizedOptions);
        for (const TextRange& range : ranges) {
            if (context.isCancelled()) {
                return {};
            }

            const TextRange trimmed =
                trimmedRange(page.plainText, range.start, range.end);
            if (trimmed.start >= trimmed.end) {
                continue;
            }

            const QString text =
                page.plainText.mid(trimmed.start, trimmed.end - trimmed.start);
            if (text.isEmpty()) {
                continue;
            }

            PdfChunk chunk;
            chunk.document = document;
            chunk.pageStart = page.pageIndex;
            chunk.pageEnd = page.pageIndex;
            chunk.text = text;
            chunk.charStart = trimmed.start;
            chunk.charLength = trimmed.end - trimmed.start;
            chunk.chunkHash = hashText(text);
            chunk.id = QStringLiteral("chunk_%1")
                           .arg(hashText(QStringLiteral("%1|%2|%3|%4")
                                             .arg(document.contentHash)
                                             .arg(page.pageIndex)
                                             .arg(trimmed.start)
                                             .arg(chunk.chunkHash)));
            chunk.locations = locationsForRange(page, trimmed);
            chunks.append(std::move(chunk));
        }
    }
    return chunks;
}

}  // namespace Rag
