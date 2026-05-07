#pragma once

#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QToolButton>
#include "../../controller/tool.hpp"

class StatusBar : public QStatusBar {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);

    // Status information update interface
    void setDocumentInfo(const QString& fileName, int currentPage,
                         int totalPages, int zoomPercent);
    void setPageInfo(int current, int total);
    void setZoomLevel(int percent);
    void setZoomLevel(double factor);
    void setFileName(const QString& fileName);
    void setMessage(const QString& message);

    // Clear status information
    void clearDocumentInfo();

    // Loading progress methods
    void showLoadingProgress(const QString& message = "正在加载...");
    void updateLoadingProgress(int progress);
    void setLoadingMessage(const QString& message);
    void hideLoadingProgress();

signals:
    void pageJumpRequested(int pageNumber);
    void zoomChanged(int percentage);
    void zoomInClicked();
    void zoomOutClicked();
    void actionTriggered(ActionMap action);

private slots:
    void onPageSpinBoxChanged(int pageNumber);
    void onZoomSliderChanged(int value);

private:
    QLabel* fileNameLabel;
    QLabel* separatorLabel1;
    QLabel* separatorLabel2;
    QLabel* separatorLabel3;

    // Page navigation controls
    QToolButton* firstPageBtn;
    QToolButton* prevPageBtn;
    QSpinBox* pageSpinBox;
    QLabel* pageCountLabel;
    QToolButton* nextPageBtn;
    QToolButton* lastPageBtn;

    // Zoom controls
    QWidget* zoomWidget;
    QToolButton* zoomOutBtn;
    QSlider* zoomSlider;
    QSpinBox* zoomPercentSpinBox;
    QToolButton* zoomInBtn;

    // Loading progress controls
    QProgressBar* loadingProgressBar;
    QLabel* loadingMessageLabel;
    QPropertyAnimation* progressAnimation;

    int currentTotalPages;

    void setupUI();
    void setupSeparators();
    void setupPageNavigation();
    void setupZoomControls();
    void setupLoadingProgress();
    QString formatFileName(const QString& fullPath) const;
};
