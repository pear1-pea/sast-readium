#pragma once

#include <QColor>
#include <QRectF>
#include <QVector>

namespace Rag {

struct EvidenceHighlightRequest {
    int pageIndex = -1;
    QVector<QRectF> originalTopLeftRects;
    QColor color = QColor(64, 128, 255, 100);
    bool focus = false;
};

}  // namespace Rag
