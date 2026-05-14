#include "ProgressiveThumbnailLoader.h"
#include "ThumbnailGenerator.h"

ProgressiveThumbnailLoader::~ProgressiveThumbnailLoader() = default;

ProgressiveThumbnailLoader::ProgressiveThumbnailLoader(
    ThumbnailGenerator* generator, QObject* parent)
    : QObject(parent),
      m_generator(generator),
      m_scrollVelocity(0.0),
      m_scrollDirection(0),
      m_dwellTimer(new QTimer(this)),
      m_lastFirstVisible(-1),
      m_lastLastVisible(-1) {
    m_dwellTimer->setSingleShot(true);
    m_dwellTimer->setInterval(DWELL_DELAY_MS);
    connect(m_dwellTimer, &QTimer::timeout, this,
            &ProgressiveThumbnailLoader::onDwellTimer);
}

void ProgressiveThumbnailLoader::onVisibleRangeChanged(int firstVisible,
                                                       int lastVisible) {
    if (firstVisible < 0 || lastVisible < 0)
        return;
    if (firstVisible == m_lastFirstVisible && lastVisible == m_lastLastVisible)
        return;

    // Schedule high-res for the full visible range
    scheduleHighResForRange(firstVisible, lastVisible);

    // Restart dwell timer (fast scrolling keeps resetting this)
    m_dwellTimer->start();

    m_lastFirstVisible = firstVisible;
    m_lastLastVisible = lastVisible;
}

void ProgressiveThumbnailLoader::onScrollStateChanged(double velocity,
                                                      int direction) {
    m_scrollVelocity = velocity;
    m_scrollDirection = direction;

    // Fast scrolling: keep restarting dwell timer to delay high-res
    if (velocity >= VELOCITY_HIGH_RES_CUTOFF && m_dwellTimer->isActive()) {
        m_dwellTimer->start();  // re-arm with full delay
    }
}

void ProgressiveThumbnailLoader::onDwellTimer() {
    // Viewport has been stationary long enough; dispatch high-res requests
    // Only dispatch if velocity is below cutoff (truly stopped/slow)
    if (m_scrollVelocity >= VELOCITY_HIGH_RES_CUTOFF) {
        // Velocity just dropped - give it another dwell cycle
        m_dwellTimer->start();
        return;
    }

    // Emit high-res requests for visible pages not yet requested
    for (int i = m_lastFirstVisible; i <= m_lastLastVisible; ++i) {
        if (!m_highResRequested.contains(i)) {
            m_highResRequested.insert(i);
            emit requestHighRes(i);
        }
    }
}

void ProgressiveThumbnailLoader::scheduleHighResForRange(int start, int end) {
    // No-op: high-res is dispatched only from onDwellTimer when the viewport
    // has been stationary long enough.  Early insertion into m_highResRequested
    // would suppress the actual emission.
    Q_UNUSED(start)
    Q_UNUSED(end)
}
