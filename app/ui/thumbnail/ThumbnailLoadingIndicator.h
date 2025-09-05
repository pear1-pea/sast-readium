#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPixmap>

/**
 * @brief Chrome风格的缩略图加载指示器
 * 
 * 特性：
 * - 旋转的加载动画
 * - 进度条显示
 * - 错误状态指示
 * - 淡入淡出效果
 * - 可自定义样式
 */
class ThumbnailLoadingIndicator : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int rotationAngle READ rotationAngle WRITE setRotationAngle)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    enum State {
        Hidden,
        Loading,
        Error,
        Success
    };

    explicit ThumbnailLoadingIndicator(QWidget* parent = nullptr);
    ~ThumbnailLoadingIndicator() override;

    // 状态管理
    void setState(State state);
    State state() const { return m_state; }
    
    void setLoading(bool loading);
    bool isLoading() const { return m_state == Loading; }
    
    void setError(const QString& errorMessage);
    void clearError();
    bool hasError() const { return m_state == Error; }
    
    // 进度管理
    void setProgress(int value);
    void setProgressRange(int minimum, int maximum);
    void setProgressVisible(bool visible);
    
    // 文本设置
    void setText(const QString& text);
    void setErrorText(const QString& text);
    
    // 动画设置
    void setAnimationEnabled(bool enabled);
    bool animationEnabled() const { return m_animationEnabled; }
    
    void setFadeEnabled(bool enabled);
    bool fadeEnabled() const { return m_fadeEnabled; }
    
    // 样式设置
    void setIndicatorSize(const QSize& size);
    QSize indicatorSize() const { return m_indicatorSize; }
    
    void setColors(const QColor& primary, const QColor& secondary, const QColor& error);
    
    // 显示控制
    void showIndicator();
    void hideIndicator();
    void showWithFade();
    void hideWithFade();

    // 属性访问器
    int rotationAngle() const { return m_rotationAngle; }
    void setRotationAngle(int angle);
    
    qreal opacity() const;
    void setOpacity(qreal opacity);

signals:
    void stateChanged(State newState, State oldState);
    void clicked();
    void errorClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void onRotationTimer();
    void onFadeInFinished();
    void onFadeOutFinished();

private:
    void setupUI();
    void setupAnimations();
    void updateLayout();
    void updateVisibility();
    
    void drawLoadingSpinner(QPainter& painter, const QRect& rect);
    void drawErrorIcon(QPainter& painter, const QRect& rect);
    void drawSuccessIcon(QPainter& painter, const QRect& rect);
    
    QRect getSpinnerRect() const;
    QRect getTextRect() const;
    QRect getProgressRect() const;

private:
    // 状态
    State m_state;
    QString m_text;
    QString m_errorText;
    
    // UI组件
    QLabel* m_textLabel;
    QProgressBar* m_progressBar;
    QVBoxLayout* m_layout;
    
    // 动画
    bool m_animationEnabled;
    bool m_fadeEnabled;
    QTimer* m_rotationTimer;
    QPropertyAnimation* m_rotationAnimation;
    QPropertyAnimation* m_fadeAnimation;
    QGraphicsOpacityEffect* m_opacityEffect;
    
    // 动画属性
    int m_rotationAngle;
    
    // 样式设置
    QSize m_indicatorSize;
    QColor m_primaryColor;
    QColor m_secondaryColor;
    QColor m_errorColor;
    
    // 常量
    static constexpr int DEFAULT_SIZE = 32;
    static constexpr int ROTATION_INTERVAL = 50; // ms
    static constexpr int ROTATION_STEP = 15; // degrees
    static constexpr int FADE_DURATION = 200; // ms
    static constexpr int SPINNER_WIDTH = 3; // pixels
    static constexpr int ERROR_ICON_SIZE = 24; // pixels
    static constexpr int SUCCESS_ICON_SIZE = 20; // pixels
};
