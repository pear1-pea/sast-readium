#pragma once

#include <poppler/qt6/poppler-qt6.h>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QQueue>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

class PDFRenderCache;

/**
 * Intelligent PDF page prerendering system with predictive loading
 * Uses background workers to prerender likely-to-be-viewed pages
 */
class PDFPrerenderer : public QObject {
    Q_OBJECT

public:
    struct RenderRequest {
        int pageNumber;
        double scaleFactor;
        int rotation;
        int priority;  // Lower number = higher priority
        qint64 timestamp;

        bool operator<(const RenderRequest& other) const {
            if (priority != other.priority) {
                return priority < other.priority;
            }
            return timestamp < other.timestamp;
        }
    };

    enum class PrerenderStrategy {
        Conservative,  // Only prerender adjacent pages
        Balanced,      // Prerender based on reading patterns
        Aggressive     // Prerender extensively for smooth experience
    };

    explicit PDFPrerenderer(QObject* parent = nullptr);
    ~PDFPrerenderer();

    // Configuration
    void setDocument(Poppler::Document* document);
    void setStrategy(PrerenderStrategy strategy);
    void setMaxWorkerThreads(int threads);
    void setRenderCache(PDFRenderCache* cache);

    // Prerendering control
    void requestPrerender(int pageNumber, double scaleFactor, int rotation,
                          int priority = 5);
    void prioritizePages(const QList<int>& pageNumbers);
    void cancelPrerenderingForPage(int pageNumber);
    void clearPrerenderQueue();

    // Statistics and monitoring
    int queueSize() const;

    // Adaptive learning
    void recordPageView(int pageNumber, qint64 viewDuration);
    void recordNavigationPattern(int fromPage, int toPage);

public slots:
    void startPrerendering();
    void stopPrerendering();
    void pausePrerendering();
    void resumePrerendering();

private slots:
    void onRenderCompleted(int pageNumber, const QPixmap& pixmap,
                           double scaleFactor, int rotation);
    void onAdaptiveAnalysis();

private:
    void setupWorkerThreads();
    void cleanupWorkerThreads();
    void scheduleAdaptivePrerendering(int currentPage);
    void analyzeReadingPatterns();
    QList<int> predictNextPages(int currentPage);
    int calculatePriority(int pageNumber, int currentPage);

    // Core components
    Poppler::Document* m_document;
    QList<QThread*> m_workerThreads;
    QList<class PDFRenderWorker*> m_workers;

    // Configuration
    PrerenderStrategy m_strategy;
    int m_maxWorkerThreads;

    // Request management
    QQueue<RenderRequest> m_renderQueue;
    QMutex m_queueMutex;
    QWaitCondition m_queueCondition;
    bool m_isRunning;
    bool m_isPaused;

    // Adaptive learning
    QHash<int, QList<qint64>> m_pageViewTimes;
    QHash<int, QHash<int, int>> m_navigationPatterns;  // from -> to -> count
    QTimer* m_adaptiveTimer;

    // Reading pattern analysis
    QList<int> m_accessHistory;
    int m_prerenderRange;

    // Helper methods
    PDFRenderCache* m_renderCache = nullptr;

signals:
    void pagePrerendered(int pageNumber, double scaleFactor, int rotation);
    void prerenderingStarted();
    void prerenderingStopped();
};

/**
 * Background worker thread for PDF page rendering
 */
class PDFRenderWorker : public QObject {
    Q_OBJECT

public:
    explicit PDFRenderWorker(QObject* parent = nullptr);

    void setDocument(Poppler::Document* document);
    void addRenderRequest(const PDFPrerenderer::RenderRequest& request);
    void clearQueue();
    void stop();

public slots:
    void processRenderQueue();

private:
    QPixmap renderPage(const PDFPrerenderer::RenderRequest& request);
    double calculateOptimalDPI(double scaleFactor);

    Poppler::Document* m_document;
    QQueue<PDFPrerenderer::RenderRequest> m_localQueue;
    QMutex m_queueMutex;
    QWaitCondition m_queueCondition;
    bool m_shouldStop;

signals:
    void pageRendered(int pageNumber, const QPixmap& pixmap, double scaleFactor,
                      int rotation);
    void renderError(int pageNumber, const QString& error);
};
