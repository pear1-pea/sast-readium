#pragma once

#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QTimer>

class ThumbnailGenerator;

/**
 * Coordinates two-stage thumbnail loading:
 * 1. Instant low-res preview (quality=0.15, ~40x60px)
 * 2. Full-res thumbnail after viewport dwell (150ms stationary)
 *
 * Fast scrolling repeatedly restarts the dwell timer, skipping intermediate
 * high-res frames. Only the final resting position gets full resolution.
 */
class ProgressiveThumbnailLoader : public QObject {
    Q_OBJECT

public:
    explicit ProgressiveThumbnailLoader(ThumbnailGenerator* generator,
                                        QObject* parent = nullptr);
    ~ProgressiveThumbnailLoader() override;

    // Called by ThumbnailListView when visible range or scroll state changes
    void onVisibleRangeChanged(int firstVisible, int lastVisible);
    void onScrollStateChanged(double velocity, int direction);

    // Configuration
    void setLowResTargetSize(const QSize& size);
    QSize lowResTargetSize() const { return m_lowResTargetSize; }

signals:
    // Emitted when a low-res preview pixmap is ready
    void lowResPreviewReady(int pageNumber, const QPixmap& preview);
    // Emitted after dwell timer fires (viewport has been stationary)
    void requestHighRes(int pageNumber);

private slots:
    void onDwellTimer();
    void onLowResGenerated(int pageNumber, const QPixmap& preview);

private:
    void requestLowResForRange(int start, int end);
    void scheduleHighResForRange(int start, int end);

    ThumbnailGenerator* m_generator;
    QSize m_lowResTargetSize;

    // Scroll state
    double m_scrollVelocity;
    int m_scrollDirection;

    // Dwell detection
    QTimer* m_dwellTimer;

    // Per-page state tracking
    QSet<int> m_lowResComplete;    // pages with low-res preview rendered
    QSet<int> m_highResRequested;  // pages with high-res already dispatched

    // Last known visible range (to compute delta)
    int m_lastFirstVisible;
    int m_lastLastVisible;

    static constexpr QSize DEFAULT_LOW_RES_SIZE{40, 60};
    static constexpr int DWELL_DELAY_MS = 150;
    static constexpr double VELOCITY_HIGH_RES_CUTOFF = 0.5;  // px/ms
};
