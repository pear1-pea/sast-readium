#pragma once

#include <QMetaType>
#include <QString>

#include "rag/domain/RagTypes.h"

class QSettings;

namespace Rag {

/**
 * Local Ollama configuration for Ask PDF.
 *
 * Values are stored below the `rag/ollama/` keys. Environment variables are
 * only used once, when the settings have never been migrated: the migrated
 * values are written back (including empty ones) so later environment changes
 * cannot silently re-enable a capability the user turned off.
 */
struct RagSettings {
    QString endpoint = QStringLiteral("http://127.0.0.1:11434");
    QString embedModel;
    QString embedVersion;
    QString chatModel;
    bool answerEnabled = false;

    static const QString& defaultEndpoint();

    bool embeddingsEnabled() const;
    bool chatEnabled() const;
    RagEmbeddingIdentity embeddingIdentity() const;

    QString embedUrl() const;
    QString chatUrl() const;

    /** Load from the application settings (`SAST` / `Readium`). */
    static RagSettings load();
    /** Load with an injected backend, used by tests. */
    static RagSettings load(QSettings& settings);

    bool save(QString* error = nullptr) const;
    bool save(QSettings& settings, QString* error = nullptr) const;

    /** Set a non-empty model and fill the hidden version with "1". */
    RagSettings withEmbeddingModel(const QString& model) const;

    /** Trimmed settings with a validated endpoint. */
    RagSettings normalized() const;

    /** Returns an empty string when the endpoint is not a valid http(s) URL. */
    static QString normalizedEndpoint(const QString& endpoint,
                                      bool* ok = nullptr);

    bool operator==(const RagSettings& other) const;
    bool operator!=(const RagSettings& other) const {
        return !(*this == other);
    }
};

}  // namespace Rag

Q_DECLARE_METATYPE(Rag::RagSettings)
