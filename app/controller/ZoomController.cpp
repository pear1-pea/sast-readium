#include "ZoomController.h"
#include <qdebug.h>
#include <QSettings>
#include <QtGlobal>
#include <cmath>

ZoomController::ZoomController(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(150);
    connect(m_timer, &QTimer::timeout, this,
            &ZoomController::onDebounceTimeout);
}

void ZoomController::setZoom(double factor, ZoomType type) {
    if (!std::isfinite(factor)) {
        return;
    }
    m_type = type;
    factor = qBound(MIN_ZOOM, factor, MAX_ZOOM);

    if (qAbs(factor - m_factor) < 0.001) {
        return;
    }

    // Apply immediately so the view reflects the input, and only debounce the
    // settled notification used for expensive final rendering.
    apply(factor);
    m_pendingFactor = factor;
    m_isPending = true;
    m_timer->start();
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
        m_isPending = false;
        emit zoomSettled(m_pendingFactor);
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
