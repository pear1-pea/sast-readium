#include "SmartPrerenderEngine.h"
#include "UnifiedRenderManager.h"
#include "../cache/UnifiedCacheSystem.h"
#include <QDebug>
#include <QMutexLocker>
#include <QApplication>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

// PredictionModel Implementation
void PredictionModel::updateModel(const QList<UserSession>& sessions)
{
    if (sessions.isEmpty()) return;
    
    // Analyze patterns across sessions
    QHash<ReadingPattern, int> patternCounts;
    QHash<ReadingPattern, QList<PageAccess>> patternAccesses;
    
    for (const UserSession& session : sessions) {
        patternCounts[session.dominantPattern]++;
        patternAccesses[session.dominantPattern].append(session.pageAccesses);
    }
    
    // Update pattern-specific weights
    for (auto it = patternCounts.begin(); it != patternCounts.end(); ++it) {
        ReadingPattern pattern = it.key();
        const QList<PageAccess>& accesses = patternAccesses[pattern];
        
        ModelWeights& weights = m_patternWeights[pattern];
        
        // Adjust weights based on pattern characteristics
        switch (pattern) {
            case ReadingPattern::Sequential:
                weights.sequentialWeight = 0.6;
                weights.frequencyWeight = 0.2;
                weights.recencyWeight = 0.15;
                weights.durationWeight = 0.05;
                break;
                
            case ReadingPattern::Skipping:
                weights.sequentialWeight = 0.2;
                weights.frequencyWeight = 0.4;
                weights.recencyWeight = 0.3;
                weights.durationWeight = 0.1;
                break;
                
            case ReadingPattern::Searching:
                weights.sequentialWeight = 0.1;
                weights.frequencyWeight = 0.5;
                weights.recencyWeight = 0.3;
                weights.durationWeight = 0.1;
                break;
                
            case ReadingPattern::Random:
                weights.sequentialWeight = 0.15;
                weights.frequencyWeight = 0.35;
                weights.recencyWeight = 0.35;
                weights.durationWeight = 0.15;
                break;
                
            case ReadingPattern::Reviewing:
                weights.sequentialWeight = 0.25;
                weights.frequencyWeight = 0.3;
                weights.recencyWeight = 0.25;
                weights.durationWeight = 0.2;
                break;
        }
    }
}

QList<NavigationPrediction> PredictionModel::predictNextPages(int currentPage, 
                                                             const UserSession& currentSession, int count)
{
    QList<NavigationPrediction> predictions;
    
    if (currentPage < 0) return predictions;
    
    ReadingPattern pattern = detectPattern(currentSession.pageAccesses);
    ModelWeights weights = m_patternWeights.value(pattern, m_weights);
    
    // Generate candidate pages based on different factors
    QSet<int> candidates;
    
    // Sequential candidates
    for (int i = 1; i <= count + 2; ++i) {
        candidates.insert(currentPage + i);
        if (currentPage - i >= 0) {
            candidates.insert(currentPage - i);
        }
    }
    
    // Frequency-based candidates
    auto freqIt = currentSession.navigationFrequency.begin();
    while (freqIt != currentSession.navigationFrequency.end() && candidates.size() < count * 3) {
        candidates.insert(freqIt.key());
        ++freqIt;
    }
    
    // Calculate prediction scores
    for (int pageNumber : candidates) {
        if (pageNumber == currentPage) continue;
        
        NavigationPrediction prediction;
        prediction.pageNumber = pageNumber;
        
        double sequentialScore = calculateSequentialScore(currentPage, pageNumber);
        double frequencyScore = calculateFrequencyScore(pageNumber, currentSession.navigationFrequency);
        double recencyScore = calculateRecencyScore(pageNumber, currentSession.pageAccesses);
        double durationScore = calculateDurationScore(pageNumber, currentSession.pageDurations);
        
        prediction.probability = 
            weights.sequentialWeight * sequentialScore +
            weights.frequencyWeight * frequencyScore +
            weights.recencyWeight * recencyScore +
            weights.durationWeight * durationScore;
        
        prediction.priority = static_cast<int>(prediction.probability * 10);
        prediction.estimatedAccessTime = QDateTime::currentMSecsSinceEpoch() + 
                                        static_cast<qint64>(5000 / prediction.probability);
        
        predictions.append(prediction);
    }
    
    // Sort by probability and take top candidates
    std::sort(predictions.begin(), predictions.end());
    
    if (predictions.size() > count) {
        predictions = predictions.mid(0, count);
    }
    
    return predictions;
}

ReadingPattern PredictionModel::detectPattern(const QList<PageAccess>& recentAccesses)
{
    if (recentAccesses.size() < 3) {
        return ReadingPattern::Sequential;
    }
    
    int sequentialCount = 0;
    int backwardCount = 0;
    int jumpCount = 0;
    
    for (int i = 1; i < recentAccesses.size(); ++i) {
        int diff = recentAccesses[i].pageNumber - recentAccesses[i-1].pageNumber;
        
        if (diff == 1) {
            sequentialCount++;
        } else if (diff == -1) {
            backwardCount++;
        } else if (qAbs(diff) > 5) {
            jumpCount++;
        }
    }
    
    double total = recentAccesses.size() - 1;
    double sequentialRatio = sequentialCount / total;
    double backwardRatio = backwardCount / total;
    double jumpRatio = jumpCount / total;
    
    if (sequentialRatio > 0.7) {
        return ReadingPattern::Sequential;
    } else if (jumpRatio > 0.5) {
        return ReadingPattern::Skipping;
    } else if (backwardRatio > 0.3) {
        return ReadingPattern::Reviewing;
    } else if (jumpRatio > 0.3) {
        return ReadingPattern::Random;
    }
    
    return ReadingPattern::Sequential;
}

double PredictionModel::calculateSequentialScore(int currentPage, int targetPage)
{
    int distance = qAbs(targetPage - currentPage);
    if (distance == 0) return 0.0;
    
    // Higher score for closer pages, with preference for forward direction
    double score = 1.0 / (1.0 + distance);
    
    if (targetPage > currentPage) {
        score *= 1.2; // Slight preference for forward navigation
    }
    
    return qMin(1.0, score);
}

double PredictionModel::calculateFrequencyScore(int targetPage, const QHash<int, int>& frequency)
{
    if (frequency.isEmpty()) return 0.0;
    
    int pageFreq = frequency.value(targetPage, 0);
    int maxFreq = 1;
    
    for (auto it = frequency.begin(); it != frequency.end(); ++it) {
        maxFreq = qMax(maxFreq, it.value());
    }
    
    return static_cast<double>(pageFreq) / maxFreq;
}

double PredictionModel::calculateRecencyScore(int targetPage, const QList<PageAccess>& accesses)
{
    if (accesses.isEmpty()) return 0.0;
    
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 mostRecentAccess = 0;
    
    for (const PageAccess& access : accesses) {
        if (access.pageNumber == targetPage) {
            mostRecentAccess = qMax(mostRecentAccess, access.timestamp);
        }
    }
    
    if (mostRecentAccess == 0) return 0.0;
    
    qint64 timeDiff = now - mostRecentAccess;
    return 1.0 / (1.0 + timeDiff / 3600000.0); // Time difference in hours
}

double PredictionModel::calculateDurationScore(int targetPage, const QHash<int, qint64>& durations)
{
    if (durations.isEmpty()) return 0.0;
    
    qint64 pageDuration = durations.value(targetPage, 0);
    qint64 maxDuration = 1;
    
    for (auto it = durations.begin(); it != durations.end(); ++it) {
        maxDuration = qMax(maxDuration, it.value());
    }
    
    return static_cast<double>(pageDuration) / maxDuration;
}

// SmartPrerenderEngine Implementation
SmartPrerenderEngine::SmartPrerenderEngine(QObject* parent)
    : QObject(parent)
    , m_renderManager(nullptr)
    , m_cacheSystem(nullptr)
    , m_predictionModel(new PredictionModel())
    , m_strategy(PrerenderStrategy::Balanced)
    , m_maxPrerenderPages(5)
    , m_learningEnabled(true)
    , m_prerenderingPaused(false)
    , m_currentPage(-1)
    , m_currentZoomLevel(1.0)
    , m_prerenderTimer(nullptr)
    , m_learningTimer(nullptr)
    , m_analysisTimer(nullptr)
    , m_successfulPrerenders(0)
    , m_totalPrerenders(0)
    , m_settings(nullptr)
{
    // Initialize settings
    m_settings = new QSettings("SAST", "Readium-SmartPrerender", this);
    
    // Load configuration
    loadSettings();
    
    // Initialize timers
    initializeTimers();
    
    qDebug() << "SmartPrerenderEngine: Initialized with strategy:" << static_cast<int>(m_strategy);
}

SmartPrerenderEngine::~SmartPrerenderEngine()
{
    saveSettings();
    saveModel();
    delete m_predictionModel;
}

void SmartPrerenderEngine::initializeTimers()
{
    // Prerender timer - triggers prerendering decisions
    m_prerenderTimer = new QTimer(this);
    m_prerenderTimer->setInterval(1000); // 1 second
    connect(m_prerenderTimer, &QTimer::timeout, this, &SmartPrerenderEngine::onPrerenderTimer);
    m_prerenderTimer->start();
    
    // Learning timer - updates the prediction model
    m_learningTimer = new QTimer(this);
    m_learningTimer->setInterval(30000); // 30 seconds
    connect(m_learningTimer, &QTimer::timeout, this, &SmartPrerenderEngine::onLearningTimer);
    m_learningTimer->start();
    
    // Analysis timer - analyzes user behavior patterns
    m_analysisTimer = new QTimer(this);
    m_analysisTimer->setInterval(10000); // 10 seconds
    connect(m_analysisTimer, &QTimer::timeout, this, &SmartPrerenderEngine::onAnalysisTimer);
    m_analysisTimer->start();
}

void SmartPrerenderEngine::recordPageView(int pageNumber, qint64 duration, double zoomLevel)
{
    if (pageNumber < 0) return;
    
    PageAccess access;
    access.pageNumber = pageNumber;
    access.timestamp = QDateTime::currentMSecsSinceEpoch();
    access.duration = duration;
    access.zoomLevel = zoomLevel;
    access.pattern = m_predictionModel->detectPattern(m_recentAccesses);
    
    m_currentSession.pageAccesses.append(access);
    m_currentSession.pageDurations[pageNumber] += duration;
    
    m_recentAccesses.append(access);
    if (m_recentAccesses.size() > 20) {
        m_recentAccesses.removeFirst();
    }
    
    // Update current state
    m_currentPage = pageNumber;
    m_currentZoomLevel = zoomLevel;
    
    // Trigger prerendering
    if (!m_prerenderingPaused) {
        triggerPrerendering(pageNumber);
    }
}

void SmartPrerenderEngine::triggerPrerendering(int currentPage)
{
    if (!m_renderManager || currentPage < 0) return;
    
    QList<NavigationPrediction> predictions;
    
    switch (m_strategy) {
        case PrerenderStrategy::Conservative:
            predictions = generateConservativePredictions(currentPage);
            break;
        case PrerenderStrategy::Balanced:
            predictions = generateBalancedPredictions(currentPage);
            break;
        case PrerenderStrategy::Aggressive:
            predictions = generateAggressivePredictions(currentPage);
            break;
        case PrerenderStrategy::Adaptive:
            predictions = generateAdaptivePredictions(currentPage);
            break;
    }
    
    m_lastPredictions = predictions;
    executePrerenderPlan(predictions);
    
    emit predictionUpdated(predictions);
}

QList<NavigationPrediction> SmartPrerenderEngine::generateConservativePredictions(int currentPage)
{
    QList<NavigationPrediction> predictions;

    // Only prerender next 1-2 pages
    for (int i = 1; i <= 2; ++i) {
        NavigationPrediction pred;
        pred.pageNumber = currentPage + i;
        pred.probability = 0.8 - (i - 1) * 0.3;
        pred.priority = 10 - i;
        predictions.append(pred);
    }

    return predictions;
}

QList<NavigationPrediction> SmartPrerenderEngine::generateBalancedPredictions(int currentPage)
{
    QList<NavigationPrediction> predictions;

    // Prerender next 3-4 pages and previous 1 page
    for (int i = 1; i <= 4; ++i) {
        NavigationPrediction pred;
        pred.pageNumber = currentPage + i;
        pred.probability = 0.7 - (i - 1) * 0.15;
        pred.priority = 8 - i;
        predictions.append(pred);
    }

    // Previous page
    if (currentPage > 0) {
        NavigationPrediction pred;
        pred.pageNumber = currentPage - 1;
        pred.probability = 0.3;
        pred.priority = 4;
        predictions.append(pred);
    }

    return predictions;
}

QList<NavigationPrediction> SmartPrerenderEngine::generateAggressivePredictions(int currentPage)
{
    QList<NavigationPrediction> predictions;

    // Prerender next 5-7 pages and previous 2-3 pages
    for (int i = 1; i <= 7; ++i) {
        NavigationPrediction pred;
        pred.pageNumber = currentPage + i;
        pred.probability = 0.6 - (i - 1) * 0.08;
        pred.priority = 7 - i;
        predictions.append(pred);
    }

    for (int i = 1; i <= 3; ++i) {
        if (currentPage - i >= 0) {
            NavigationPrediction pred;
            pred.pageNumber = currentPage - i;
            pred.probability = 0.4 - (i - 1) * 0.1;
            pred.priority = 4 - i;
            predictions.append(pred);
        }
    }

    return predictions;
}

QList<NavigationPrediction> SmartPrerenderEngine::generateAdaptivePredictions(int currentPage)
{
    // Use machine learning model for predictions
    return m_predictionModel->predictNextPages(currentPage, m_currentSession, m_maxPrerenderPages);
}

void SmartPrerenderEngine::executePrerenderPlan(const QList<NavigationPrediction>& predictions)
{
    if (!m_renderManager) return;

    for (const NavigationPrediction& pred : predictions) {
        if (shouldPrerender(pred.pageNumber)) {
            int priority = calculatePrerenderPriority(pred);

            emit prerenderRequested(pred.pageNumber, m_currentZoomLevel, priority);
            m_totalPrerenders++;

            qDebug() << "SmartPrerenderEngine: Prerendering page" << pred.pageNumber
                     << "probability:" << pred.probability << "priority:" << priority;
        }
    }
}

bool SmartPrerenderEngine::shouldPrerender(int pageNumber) const
{
    if (!m_cacheSystem || pageNumber < 0) return false;

    // Don't prerender if already cached
    QString key = QString("type_0_page_%1_%2_0").arg(pageNumber).arg(m_currentZoomLevel);
    return !m_cacheSystem->contains(key);
}

int SmartPrerenderEngine::calculatePrerenderPriority(const NavigationPrediction& prediction) const
{
    // Convert probability to priority (0-10 scale)
    return qBound(0, static_cast<int>(prediction.probability * 10), 10);
}

void SmartPrerenderEngine::onPrerenderTimer()
{
    if (m_prerenderingPaused || m_currentPage < 0) return;

    // Validate previous predictions
    validatePredictions();

    // Trigger new prerendering if needed
    triggerPrerendering(m_currentPage);
}

void SmartPrerenderEngine::onLearningTimer()
{
    if (!m_learningEnabled) return;

    updateLearningModel();
    adaptToUserBehavior();
}

void SmartPrerenderEngine::onAnalysisTimer()
{
    analyzeUserBehavior();
    updateCurrentSession();
}

void SmartPrerenderEngine::validatePredictions()
{
    // Check if our predictions were accurate
    for (const NavigationPrediction& pred : m_lastPredictions) {
        if (m_currentPage == pred.pageNumber) {
            m_successfulPrerenders++;
            break;
        }
    }
}

void SmartPrerenderEngine::updateLearningModel()
{
    if (m_sessionHistory.size() < 2) return;

    m_predictionModel->updateModel(m_sessionHistory);

    double accuracy = calculatePredictionAccuracy();
    emit modelTrained(accuracy);

    qDebug() << "SmartPrerenderEngine: Model updated, accuracy:" << accuracy;
}

void SmartPrerenderEngine::adaptToUserBehavior()
{
    ReadingPattern currentPattern = m_predictionModel->detectPattern(m_recentAccesses);

    if (currentPattern != m_currentSession.dominantPattern) {
        m_currentSession.dominantPattern = currentPattern;
        emit patternDetected(currentPattern);

        // Adjust strategy based on detected pattern
        adjustStrategy();
    }
}

void SmartPrerenderEngine::adjustStrategy()
{
    if (m_strategy != PrerenderStrategy::Adaptive) return;

    ReadingPattern pattern = m_currentSession.dominantPattern;

    switch (pattern) {
        case ReadingPattern::Sequential:
            m_maxPrerenderPages = 5;
            break;
        case ReadingPattern::Skipping:
            m_maxPrerenderPages = 3;
            break;
        case ReadingPattern::Searching:
            m_maxPrerenderPages = 2;
            break;
        case ReadingPattern::Random:
            m_maxPrerenderPages = 4;
            break;
        case ReadingPattern::Reviewing:
            m_maxPrerenderPages = 6;
            break;
    }
}

void SmartPrerenderEngine::analyzeUserBehavior()
{
    if (m_recentAccesses.size() < 3) return;

    // Calculate average viewing time
    qint64 totalDuration = 0;
    for (const PageAccess& access : m_recentAccesses) {
        totalDuration += access.duration;
    }

    double avgDuration = static_cast<double>(totalDuration) / m_recentAccesses.size();

    // Adjust prerendering based on reading speed
    if (avgDuration > 30000) { // Slow reader (>30 seconds per page)
        m_maxPrerenderPages = qMin(m_maxPrerenderPages + 1, 8);
    } else if (avgDuration < 5000) { // Fast reader (<5 seconds per page)
        m_maxPrerenderPages = qMax(m_maxPrerenderPages - 1, 2);
    }
}

double SmartPrerenderEngine::calculatePredictionAccuracy()
{
    if (m_totalPrerenders == 0) return 0.0;
    return static_cast<double>(m_successfulPrerenders) / m_totalPrerenders;
}

void SmartPrerenderEngine::startSession()
{
    m_currentSession = UserSession();
    m_currentSession.startTime = QDateTime::currentDateTime();

    emit sessionStarted();
    qDebug() << "SmartPrerenderEngine: Session started";
}

void SmartPrerenderEngine::endSession()
{
    m_currentSession.endTime = QDateTime::currentDateTime();
    m_currentSession.dominantPattern = m_predictionModel->detectPattern(m_currentSession.pageAccesses);

    // Calculate average zoom level
    double totalZoom = 0.0;
    for (const PageAccess& access : m_currentSession.pageAccesses) {
        totalZoom += access.zoomLevel;
    }
    if (!m_currentSession.pageAccesses.isEmpty()) {
        m_currentSession.averageZoomLevel = totalZoom / m_currentSession.pageAccesses.size();
    }

    m_sessionHistory.append(m_currentSession);

    // Keep only recent sessions
    if (m_sessionHistory.size() > 50) {
        m_sessionHistory.removeFirst();
    }

    emit sessionEnded(m_currentSession);
    qDebug() << "SmartPrerenderEngine: Session ended, pattern:" << static_cast<int>(m_currentSession.dominantPattern);
}

void SmartPrerenderEngine::loadSettings()
{
    if (!m_settings) return;

    m_strategy = static_cast<PrerenderStrategy>(
        m_settings->value("prerender/strategy", static_cast<int>(PrerenderStrategy::Balanced)).toInt());
    m_maxPrerenderPages = m_settings->value("prerender/maxPages", 5).toInt();
    m_learningEnabled = m_settings->value("prerender/learningEnabled", true).toBool();
}

void SmartPrerenderEngine::saveSettings()
{
    if (!m_settings) return;

    m_settings->setValue("prerender/strategy", static_cast<int>(m_strategy));
    m_settings->setValue("prerender/maxPages", m_maxPrerenderPages);
    m_settings->setValue("prerender/learningEnabled", m_learningEnabled);

    m_settings->sync();
}
