#pragma once

// ANAN-100D port: the PS tile -- PureSignal (adaptive predistortion) for the
// ANAN-100D, modelled on Thetis' PureSignal form:
//   PS on/off, Auto / Single, Calibrate, Reset,
//   2 TONE (WDSP two-tone test transmission; SSB only),
//   indicators: feedback level (Thetis colours: red < 90 < yellow < 128 <
//   green <= 181 < blue), correction applied, calibrations, engine state,
//   feedback attenuator (auto or by hand), feedback input, HW peak, MOX and
//   amplifier delays, and "Amp View..." -- the window with the curves and
//   the tests (PsAmpViewDialog).
// Talks to the backend's AnanPureSignal (Hl2Backend::pureSignal()).

#include <QElapsedTimer>
#include <QPointer>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

namespace AetherSDR {

class RadioModel;
class PsAmpViewDialog;
namespace anan { class AnanPureSignal; }

class PsApplet : public QWidget {
    Q_OBJECT
public:
    explicit PsApplet(QWidget* parent = nullptr);

    void setRadioModel(RadioModel* model);
    // How the tile keys the transmitter for 2 TONE (MainWindow: the TX tile's
    // MOX path, so every interlock applies).
    void setMoxFunction(std::function<void(bool)> fn) { m_mox = std::move(fn); }

    // The controller of the connected radio, or nullptr.
    anan::AnanPureSignal* pureSignal() const;
    // 2 TONE on/off (also from the Amp View window).
    void requestTwoTone(bool on);
    bool twoToneAllowed() const;
    void openAmpView();

private:
    void refresh();
    void loadControls();

    RadioModel* m_model = nullptr;
    std::function<void(bool)> m_mox;
    QPointer<PsAmpViewDialog> m_ampView;
    QTimer* m_timer = nullptr;
    bool m_loading = false;
    bool m_twoToneKeyed = false;
    bool m_sawTx = false;
    QElapsedTimer m_twoToneSince;
    anan::AnanPureSignal* m_lastPs = nullptr;

    QPushButton* m_psBtn = nullptr;
    QPushButton* m_autoBtn = nullptr;
    QPushButton* m_singleBtn = nullptr;
    QPushButton* m_calBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_twoToneBtn = nullptr;
    QPushButton* m_ampViewBtn = nullptr;
    QLabel* m_fbLamp = nullptr;
    QLabel* m_corrLamp = nullptr;
    QLabel* m_feedLamp = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_calLabel = nullptr;
    QCheckBox* m_autoAtt = nullptr;
    QSlider* m_att = nullptr;
    QLabel* m_attValue = nullptr;
    QComboBox* m_source = nullptr;
    QComboBox* m_fbAdc = nullptr;
};

} // namespace AetherSDR
