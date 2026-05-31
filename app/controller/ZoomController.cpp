#include "ZoomController.h"
#include <qdebug.h>
#include <QSettings>
#include <QtGlobal>

ZoomController::ZoomController(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(150);
    connect(m_timer, &QTimer::timeout, this,
            &ZoomController::onDebounceTimeout);
}

void ZoomController::setZoom(double factor, ZoomType type) {
    m_type = type;
    factor = qBound(MIN_ZOOM, factor, MAX_ZOOM);

    if (qAbs(factor - m_factor) < 0.001) {
        return;
    }

    // Debounce: small changes while timer is active get coalesced
    bool shouldDebounce = false;
    if (type == ZoomType::FixedValue) {
        double changeMagnitude = qAbs(factor - m_factor);
        shouldDebounce = (changeMagnitude < 0.15 && m_timer->isActive());
    }

    if (shouldDebounce) {
        m_pendingFactor = factor;
        m_isPending = true;
        m_timer->start();
    } else {
        if (m_timer->isActive()) {
            m_timer->stop();
        }
        m_isPending = false;
        apply(factor);
    }
}

void ZoomController::zoomIn() {
    setZoom(m_factor + ZOOM_STEP, ZoomType::FixedValue);
}

void ZoomController::zoomOut() {
    setZoom(m_factor - ZOOM_STEP, ZoomType::FixedValue);
}

void ZoomController::setFromPercentage(int percentage) {
    setZoom(percentage / 100.0, ZoomType::FixedValue);
}

void ZoomController::apply(double factor) {
    factor = qBound(MIN_ZOOM, factor, MAX_ZOOM);
    if (factor != m_factor) {
        m_factor = factor;
        emit zoomChanged(m_factor);
    }
}

void ZoomController::onDebounceTimeout() {
    if (m_isPending) {
        double factor = m_pendingFactor;
        m_isPending = false;
        apply(factor);
    }
}

void ZoomController::saveSettings() const {
    QSettings settings;
    settings.beginGroup("PDFViewer");
    settings.setValue("defaultZoom", m_factor);
    settings.setValue("zoomType", static_cast<int>(m_type));
    settings.endGroup();
}

void ZoomController::loadSettings() {
    QSettings settings;
    settings.beginGroup("PDFViewer");

    double savedZoom = settings.value("defaultZoom", DEFAULT_ZOOM).toDouble();
    int savedZoomType =
        settings.value("zoomType", static_cast<int>(ZoomType::FixedValue))
            .toInt();

    settings.endGroup();

    m_factor = qBound(MIN_ZOOM, savedZoom, MAX_ZOOM);
    m_type = static_cast<ZoomType>(savedZoomType);
}
