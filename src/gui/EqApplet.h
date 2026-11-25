#pragma once

#include <QWidget>
#include <array>
#include "models/EqualizerModel.h"

class QPushButton;
class QLabel;
class QSlider;

namespace AetherSDR {

// EQ applet — 8-band graphic equalizer for TX and RX.
//
// Layout:
//  - Title bar: "EQ"
//  - ON button + RX/TX selector
//  - Band labels: 63 | 125 | 250 | 500 | 1k | 2k | 4k | 8k
//  - 8 vertical sliders (-10 to +10 dB)
//  - dB scale labels on sides
class EqApplet : public QWidget {
    Q_OBJECT

public:
    explicit EqApplet(QWidget* parent = nullptr);

    void setEqualizerModel(EqualizerModel* model);
    // ANAN fork build 686 (operator, 24.11): while the Aether RX / TX EQ is
    // on for a side, this EQ is locked for that side -- the ON button, the
    // reset and the sliders of that view are disabled (MainWindow decides).
    void setSideLocks(bool rxLocked, bool txLocked);

private:
    void buildUI();
    void syncFromModel();
    void updateActiveHighlight();
    void persistViewSelection();
    void applySideLock();   // build 686
    bool m_rxLocked{false};
    bool m_txLocked{false};

    EqualizerModel* m_model{nullptr};
    bool m_updatingFromModel{false};
    bool m_showTx{true};  // true = TX view, false = RX view

    QPushButton* m_onBtn{nullptr};
    QPushButton* m_rxBtn{nullptr};
    QPushButton* m_txBtn{nullptr};

    QWidget* m_resetBtn{nullptr};

    std::array<QSlider*, EqualizerModel::BandCount> m_sliders{};
    std::array<QLabel*,  EqualizerModel::BandCount> m_valueLabels{};
};

} // namespace AetherSDR
