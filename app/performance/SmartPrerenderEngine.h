#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QHash>
#include <QList>
#include <QDateTime>
#include <QSettings>
#include <QVariant>
#include <QtGlobal>

// Forward declarations
class UnifiedRenderManager;
class UnifiedCacheSystem;

/**
 * User behavior patterns
 */
enum class ReadingPattern {
    Sequential,         // Reading page by page
    Skipping,          // Jumping between pages
    Searching,         // Search-based navigation
    Random,            // Random access
    Reviewing          // Going back and forth
};

/**
 * Prerender strategy
 */
enum class PrerenderStrategy {
    Conservative,      // Minimal prerendering
    Balanced,          // Moderate prerendering
    Aggressive,        // Maximum prerendering
    Adaptive          // AI-driven adaptive strategy
};

/**
 * Page access record
 */
struct PageAccess {
    int pageNumber;
    qint64 timestamp;
    qint64 duration;
    double zoomLevel;
    ReadingPattern pattern;
    
    PageAccess() 
        : pageNumber(-1), timestamp(0), duration(0)
        , zoomLevel(1.0), pattern(ReadingPattern::Sequential) {}
};

/**
 * Navigation prediction
 */
struct NavigationPrediction {
    int pageNumber;
    double probability;
    int priority;
    qint64 estimatedAccessTime;
    
    NavigationPrediction()
        : pageNumber(-1), probability(0.0), priority(0), estimatedAccessTime(0) {}
    
    bool operator<(const NavigationPrediction& other) const {
        return probability > other.probability; // Higher probability first
    }
};

/**
 * User session data
 */
struct UserSession {
    QDateTime startTime;
    QDateTime endTime;
    QList<PageAccess> pageAccesses;
    QHash<int, qint64> pageDurations;
    QHash<int, int> navigationFrequency;
    ReadingPattern dominantPattern;
    double averageZoomLevel;
    
    UserSession() 
        : dominantPattern(ReadingPattern::Sequential), averageZoomLevel(1.0) {}
};

/**
 * Learning model for prediction
 */
class PredictionModel
{
public:
    struct ModelWeights {
        double sequentialWeight;
        double frequencyWeight;
        double recencyWeight;
        double durationWeight;
        double zoomWeight;
        
        ModelWeights() 
            : sequentialWeight(0.4), frequencyWeight(0.3)
            , recencyWeight(0.2), durationWeight(0.1), zoomWeight(0.1) {}
    };
    
    void updateModel(const QList<UserSession>& sessions);
    QList<NavigationPrediction> predictNextPages(int currentPage, const UserSession& currentSession, int count = 5);
    ReadingPattern detectPattern(const QList<PageAccess>& recentAccesses);
    void adjustWeights(const QList<PageAccess>& actualAccesses, const QList<NavigationPrediction>& predictions);
    
    ModelWeights getWeights() const { return m_weights; }
    void setWeights(const ModelWeights& weights) { m_weights = weights; }

private:
    ModelWeights m_weights;
    QHash<ReadingPattern, ModelWeights> m_patternWeights;
    
    double calculateSequentialScore(int currentPage, int targetPage);
    double calculateFrequencyScore(int targetPage, const QHash<int, int>& frequency);
    double calculateRecencyScore(int targetPage, const QList<PageAccess>& accesses);
    double calculateDurationScore(int targetPage, const QHash<int, qint64>& durations);
};

/**
 * Smart prerender engine with machine learning capabilities
 */
class SmartPrerenderEngine : public QObject
{
    Q_OBJECT

public:
    explicit SmartPrerenderEngine(QObject* parent = nullptr);
    ~SmartPrerenderEngine();

    // Configuration
    void setRenderManager(UnifiedRenderManager* manager);
    void setCacheSystem(UnifiedCacheSystem* cache);
    void setStrategy(PrerenderStrategy strategy);
    void setMaxPrerenderPages(int maxPages);
    void setLearningEnabled(bool enabled);
    
    // User behavior tracking
    void recordPageView(int pageNumber, qint64 duration, double zoomLevel = 1.0);
    void recordNavigation(int fromPage, int toPage);
    void startSession();
    void endSession();
    
    // Prerendering control
    void triggerPrerendering(int currentPage);
    void pausePrerendering();
    void resumePrerendering();
    void clearPredictions();
    
    // Analytics and insights
    ReadingPattern getCurrentPattern() const;
    QList<NavigationPrediction> getLastPredictions() const;
    UserSession getCurrentSession() const;
    QList<UserSession> getSessionHistory() const;
    
    // Model management
    void trainModel();
    void saveModel();
    void loadModel();
    void resetModel();
    
    // Performance metrics
    double getPredictionAccuracy() const;
    int getSuccessfulPrerenders() const;
    int getTotalPrerenders() const;
    
    // Settings
    void loadSettings();
    void saveSettings();

public slots:
    void onPageChanged(int pageNumber);
    void onZoomChanged(double zoomLevel);
    void onDocumentChanged();

signals:
    void prerenderRequested(int pageNumber, double scaleFactor, int priority);
    void patternDetected(ReadingPattern pattern);
    void predictionUpdated(const QList<NavigationPrediction>& predictions);
    void sessionStarted();
    void sessionEnded(const UserSession& session);
    void modelTrained(double accuracy);

private slots:
    void onPrerenderTimer();
    void onLearningTimer();
    void onAnalysisTimer();

private:
    // Core components
    UnifiedRenderManager* m_renderManager;
    UnifiedCacheSystem* m_cacheSystem;
    PredictionModel* m_predictionModel;
    
    // Configuration
    PrerenderStrategy m_strategy;
    int m_maxPrerenderPages;
    bool m_learningEnabled;
    bool m_prerenderingPaused;
    
    // Current state
    int m_currentPage;
    double m_currentZoomLevel;
    UserSession m_currentSession;
    QList<UserSession> m_sessionHistory;
    QList<NavigationPrediction> m_lastPredictions;
    
    // Timers
    QTimer* m_prerenderTimer;
    QTimer* m_learningTimer;
    QTimer* m_analysisTimer;
    
    // Performance tracking
    int m_successfulPrerenders;
    int m_totalPrerenders;
    QList<PageAccess> m_recentAccesses;
    
    // Settings
    QSettings* m_settings;
    
    // Helper methods
    void initializeTimers();
    void updateCurrentSession();
    void analyzeUserBehavior();
    void executePrerenderPlan(const QList<NavigationPrediction>& predictions);
    void validatePredictions();
    void adjustStrategy();
    
    // Strategy implementations
    QList<NavigationPrediction> generateConservativePredictions(int currentPage);
    QList<NavigationPrediction> generateBalancedPredictions(int currentPage);
    QList<NavigationPrediction> generateAggressivePredictions(int currentPage);
    QList<NavigationPrediction> generateAdaptivePredictions(int currentPage);
    
    // Learning and adaptation
    void updateLearningModel();
    void adaptToUserBehavior();
    double calculatePredictionAccuracy();
    
    // Utility methods
    bool shouldPrerender(int pageNumber) const;
    int calculatePrerenderPriority(const NavigationPrediction& prediction) const;
    void pruneOldSessions();
    void optimizeMemoryUsage();
};
