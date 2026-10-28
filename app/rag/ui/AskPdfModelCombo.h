#pragma once

#include <QComboBox>
#include <QSet>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>

namespace Rag::ModelCombo {

/**
 * Raw model tag for the current selection.
 *
 * The combo is editable, so typing replaces the line edit text without moving
 * currentIndex. Item data is therefore only trusted while the visible text is
 * still that item's text; otherwise the typed tag wins. The annotated display
 * text of an unknown-capability model must never reach settings or a request.
 */
inline QString selectedTag(const QComboBox* combo) {
    if (!combo) {
        return {};
    }
    const int index = combo->currentIndex();
    if (index >= 0) {
        const QString raw = combo->itemData(index, Qt::UserRole).toString();
        if (!raw.isEmpty() && combo->currentText() == combo->itemText(index)) {
            return raw.trimmed();
        }
    }
    return combo->currentText().trimmed();
}

/**
 * Replace the items with `models`, keeping the current tag selected.
 *
 * Models in `unknownModels` are annotated in the display text and tooltip, but
 * their raw tag stays in Qt::UserRole. A tag that the refreshed list does not
 * contain is kept in the line edit, and an empty selection stays empty.
 */
inline void populate(QComboBox* combo, const QStringList& models,
                     const QStringList& unknownModels) {
    if (!combo) {
        return;
    }
    const QString current = selectedTag(combo);
    const QSet<QString> unknown(unknownModels.begin(), unknownModels.end());
    {
        const QSignalBlocker blocker(combo);
        combo->clear();
        for (const QString& model : models) {
            const bool isUnknown = unknown.contains(model);
            combo->addItem(isUnknown
                               ? QStringLiteral("%1（能力未知）").arg(model)
                               : model);
            const int index = combo->count() - 1;
            combo->setItemData(index, model, Qt::UserRole);
            if (isUnknown) {
                combo->setItemData(
                    index,
                    QStringLiteral(
                        "服务未报告该模型的能力，可能不适用于此用途"),
                    Qt::ToolTipRole);
            }
        }
        const int restored = combo->findData(current, Qt::UserRole);
        if (restored >= 0) {
            combo->setCurrentIndex(restored);
        } else if (current.isEmpty()) {
            // QComboBox selects the first inserted item by default.
            combo->setCurrentIndex(-1);
            combo->setEditText(QString());
        } else {
            // Keep a tag that is not in the refreshed list, including an empty
            // list where the user typed the value by hand.
            combo->setCurrentIndex(-1);
            combo->setEditText(current);
        }
    }
}

}  // namespace Rag::ModelCombo
