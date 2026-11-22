#pragma once

// ANAN-100D port: the CW applet -- the radio's own (FPGA) CW keyer, the same
// settings as MicronSDR's Options -> Keyer page:
//   Keyer:        mode (straight / iambic A / iambic B), speed 10..63 WPM,
//                 weight 33..66 (50 = normal), paddle swap, auto letter spacing
//   Transmit:     hang time 10..1000 ms, CW demo (CW without RF)
//   Side tone:    level 0..99 %, frequency 400..1000 Hz
//
// The keyer values are stored in AppSettings (Anan* keys); Hl2Backend reads
// them every 300 ms and sends them to the radio in the Transmitter Specific
// packet (P2Transport::setCwKeyer). (MicronSDR's External Key group is left
// out on purpose: serial PTT/CW stays in Radio Setup -> Serial.)

#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QSlider;

namespace AetherSDR {

class CwApplet : public QWidget {
    Q_OBJECT
public:
    explicit CwApplet(QWidget* parent = nullptr);

    // From the rest of the app (TransmitModel's CW speed): no signal back.
    void setKeyerSpeed(int wpm);
    int keyerSpeed() const;

signals:
    void keyerSpeedChanged(int wpm);

private:
    QComboBox* m_mode{nullptr};
    QSlider*   m_speed{nullptr};
    QLabel*    m_speedValue{nullptr};
    QSlider*   m_weight{nullptr};
    QLabel*    m_weightValue{nullptr};
    QCheckBox* m_swap{nullptr};
    QCheckBox* m_autoSpace{nullptr};
    QSlider*   m_hang{nullptr};
    QLabel*    m_hangValue{nullptr};
    QCheckBox* m_demo{nullptr};
    QSlider*   m_stLevel{nullptr};
    QLabel*    m_stLevelValue{nullptr};
    QSlider*   m_stFreq{nullptr};
    QLabel*    m_stFreqValue{nullptr};
    bool m_loading{false};
};

} // namespace AetherSDR
