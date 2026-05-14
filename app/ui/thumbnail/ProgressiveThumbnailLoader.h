#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>

class ThumbnailGenerator;

/**
 * Coordinates dwell-based thumbnail loading:
 * - High-res thumbnail is dispatched after the viewport has been stationary
 *   for DWELL_DELAY_MS (fast scrolling keeps resetting the timer).
 * - Only the final resting position gets full-resolution rendering.
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

signals:
    // Emitted after dwell timer fires (viewport has been stationary)
    void requestHighRes(int pageNumber);

private slots:
    void onDwellTimer();

private:
    void scheduleHighResForRange(int start, int end);

    ThumbnailGenerator* m_generator;

    // Scroll state
    double m_scrollVelocity;
    int m_scrollDirection;

    // Dwell detection
    QTimer* m_dwellTimer;

    // Per-page state tracking
    QSet<int> m_highResRequested;  // pages with high-res already dispatched

    // Last known visible range (to compute delta)
    int m_lastFirstVisible;
    int m_lastLastVisible;

    static constexpr int DWELL_DELAY_MS = 150;
    static constexpr double VELOCITY_HIGH_RES_CUTOFF = 0.5;  // px/ms
};
