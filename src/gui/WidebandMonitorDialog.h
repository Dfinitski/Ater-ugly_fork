#pragma once

// ANAN-100D port: Wideband Monitor -- the raw ADC spectrum, DC .. 61.44 MHz,
// from Protocol 2 wideband data (port 1027 = ADC1, 1028 = ADC2; General
// packet bytes 23..28). Each frame is 16384 contiguous 16-bit samples at
// 122.88 Msps (32 packets x 512); one Blackman-Harris FFT per frame, 7.5 kHz
// bins, averaged in power, shown as a trace and a waterfall. Unlike Thetis,
// which pushes every 512-sample packet into a streaming analyzer as if the
// frames were continuous, a frame here is transformed only when all of its
// packets arrived in order.

#include "gui/PersistentDialog.h"

#include <QImage>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QSpinBox;

namespace AetherSDR {

class RadioModel;

class WidebandView : public QWidget {
    Q_OBJECT
public:
    explicit WidebandView(QWidget* parent = nullptr);
    // dB per bin, DC .. Nyquist; binHz = Nyquist / (bins - 1).
    void setFrame(const std::vector<float>& db, double binHz);
    void setLevels(float topDb, float bottomDb);
    void setMarkerHz(double hz);
    void clearWaterfall();

signals:
    void hoverInfo(const QString& text);
    void tuneRequested(double hz);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect plotRect() const;
    QRect waterfallRect() const;
    double xToHz(int x) const;
    int hzToX(double hz) const;
    void addWaterfallRow();
    void clampView();
    float levelAt(double hz) const;

    std::vector<float> m_db;
    double m_binHz = 0.0;
    double m_viewLo = 0.0;
    double m_viewHi = 61.44e6;
    float m_top = -20.0f;
    float m_bottom = -130.0f;
    double m_markerHz = 0.0;
    QImage m_wf;
    bool m_dragging = false;
    int m_dragX = 0;
    double m_dragLo = 0.0;
};

class WidebandMonitorDialog : public PersistentDialog {
    Q_OBJECT
public:
    explicit WidebandMonitorDialog(RadioModel* model, QWidget* parent = nullptr);
    ~WidebandMonitorDialog() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void attachTransport();
    void pushEnable(bool on);
    void onFrame(int adc, const QVector<float>& samples);
    void saveSettings();

    RadioModel* m_model = nullptr;
    QPointer<QObject> m_transport;
    QMetaObject::Connection m_frameConn;
    WidebandView* m_view = nullptr;
    QComboBox* m_adcCombo = nullptr;
    QComboBox* m_rateCombo = nullptr;
    QSpinBox* m_avgSpin = nullptr;
    QSpinBox* m_topSpin = nullptr;
    QSpinBox* m_bottomSpin = nullptr;
    QLabel* m_info = nullptr;
    QLabel* m_status = nullptr;

    std::vector<float> m_window;
    float m_windowSum = 1.0f;
    std::vector<float> m_re, m_im;
    std::vector<int> m_bitrev;
    std::vector<float> m_avgPower;
    std::vector<float> m_db;
    int m_frames = 0;
    bool m_enabled = false;
    int m_framesLogged = 0;
};

} // namespace AetherSDR
