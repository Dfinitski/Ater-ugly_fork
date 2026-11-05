#pragma once

// ANAN-100D port: PureSignal "Amp View" -- Thetis' AmpView and the test part
// of its PureSignal form, opened from the PS tile:
//   curves of the last calibration (WDSP GetPSDisp): the amplifier AM/AM
//   (output vs input amplitude) and AM/PM (phase vs input), the correction
//   (predistortion gain and phase) and the ideal line;
//   status: every calcc info word, engine state, feedback level, attenuator,
//   peak of the transmit signal, HW peak, pairs fed;
//   tests: 2 TONE (frequencies and level), Calibrate, Auto, Reset, save /
//   load a correction file.

#include "gui/PersistentDialog.h"

#include <QPointF>
#include <QPointer>
#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

namespace AetherSDR {

class PsApplet;

class PsCurveView : public QWidget {
    Q_OBJECT
public:
    explicit PsCurveView(QWidget* parent = nullptr);
    struct Curves {
        std::vector<QPointF> amAm, amPm;          // amplifier: (input, output) / (input, deg)
        std::vector<QPointF> corrMag, corrPhs;    // correction: (input, output) / (input, deg)
    };
    void setCurves(const Curves& c);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Curves m_c;
    double m_phsSpan = 30.0;   // +- degrees on the right axis
    int m_phsShrink = 0;
};

// The transmit signal as the coupler sees it (the PureSignal feedback, DDC0)
// around the two tones, in dB relative to one tone, with the IMD products
// marked, and IMD3 over the last 30 s underneath -- what the correction
// actually achieves, over time.
class PsSpectrumView : public QWidget {
    Q_OBJECT
public:
    explicit PsSpectrumView(QWidget* parent = nullptr);
    struct Frame {
        std::vector<float> dbc;   // per bin, relative to the mean tone
        double f0 = 0.0;          // Hz of dbc[0]
        double binHz = 1.0;
        double f1 = 0.0, f2 = 0.0;   // the tones
        bool valid = false;
    };
    void setFrame(const Frame& f);
    void addHistory(double imd3, double imd5);
    void clearHistory();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Frame m_f;
    std::vector<QPointF> m_hist;   // (imd3, imd5), oldest first
};

class PsAmpViewDialog : public PersistentDialog {
    Q_OBJECT
public:
    PsAmpViewDialog(PsApplet* applet, QWidget* parent = nullptr);
    void syncTwoTone(bool on);

private:
    void refresh();
    void measure();

    QPointer<PsApplet> m_applet;
    PsCurveView* m_view = nullptr;
    PsSpectrumView* m_spec = nullptr;
    QLabel* m_imd = nullptr;
    std::vector<float> m_win;
    float m_winSum = 1.0f;
    std::vector<int> m_rev;
    std::vector<double> m_re, m_im, m_snap;
    QTimer* m_timer = nullptr;
    QCheckBox* m_hold = nullptr;
    QPushButton* m_measDelay = nullptr;
    QPushButton* m_twoTone = nullptr;
    QPushButton* m_cal = nullptr;
    QPushButton* m_auto = nullptr;
    QPushButton* m_single = nullptr;
    QPushButton* m_reset = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_load = nullptr;
    QLabel* m_status = nullptr;
    int m_lastCal = -1;
};

} // namespace AetherSDR
