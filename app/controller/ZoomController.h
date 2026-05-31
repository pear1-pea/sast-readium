#pragma once

#include <QObject>
#include <QTimer>
#include "../ui/viewer/ZoomTypes.h"

class ZoomController : public QObject {
    Q_OBJECT

public:
    explicit ZoomController(QObject* parent = nullptr);

    // Zoom value management
    void setZoom(double factor, ZoomType type = ZoomType::FixedValue);
    double currentZoom() const { return m_factor; }
    ZoomType zoomType() const { return m_type; }

    // Convenience
    void zoomIn();
    void zoomOut();
    void setFromPercentage(int percentage);

    // Persistence
    void saveSettings() const;
    void loadSettings();

signals:
    void zoomChanged(double factor);
    void zoomTypeChanged(ZoomType type);

private slots:
    void onDebounceTimeout();

private:
    void apply(double factor);

    double m_factor = 1.0;
    ZoomType m_type = ZoomType::FixedValue;

    // Debounce
    QTimer* m_timer = nullptr;
    double m_pendingFactor = 1.0;
    bool m_isPending = false;

    static constexpr double MIN_ZOOM = 0.1;
    static constexpr double MAX_ZOOM = 5.0;
    static constexpr double DEFAULT_ZOOM = 1.0;
    static constexpr double ZOOM_STEP = 0.1;
};
