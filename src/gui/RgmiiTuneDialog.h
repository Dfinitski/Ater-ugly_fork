#pragma once

// ANAN-100D port, test firmware (02.11): "RGMII: receive tuning".
// The radio's KSZ9031 RGMII receive delays (General bytes 40/41) by hand or
// found automatically; what the radio runs (status bytes 28/29, layout 4)
// and its link error counters (status 16..27). The radio dropped DUC
// packets with a bad FCS the PHY did not flag (log 20:33/20:48: about one a
// second) -- the receive timing in the FPGA.
//
// Auto-tune: one scale S (60 ps per step): S = 0 is RX_CLK 15 / RXD 7;
// -15..+16 moves RX_CLK only; beyond, RX_CLK stays at 0 / 31 and RXD moves
// (down to S = -23 / up to S = +23). Walked one step at a time from 0 up,
// then from 0 down (the radio must keep receiving our commands, which it
// only accepts with a good FCS), each direction until the errors pass a
// limit; every step: wait for the radio to report the value, settle, count
// FCS + RXER errors over the dwell against the DUC packets sent. The middle
// of the widest error-free run wins.

#include "gui/PersistentDialog.h"

#include <QPointer>

#include <array>
#include <deque>
#include <vector>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace AetherSDR {

namespace anan {
class P2Transport;
}

class RgmiiTuneDialog : public PersistentDialog {
    Q_OBJECT
public:
    RgmiiTuneDialog(anan::P2Transport* transport, QWidget* parent = nullptr);

    // S -> (RX_CLK, RXD) as the firmware note defines it.
    static void scaleToDelays(int s, int& clk, int& data);
    static constexpr int kSMin = -23, kSMax = 23;

private:
    void sendNow();
    void tick();
    void resetCounters();
    void startAuto();
    void stopAuto(const QString& why);
    void autoTick();
    void applyS(int s);
    void logLine(const QString& t);

    QPointer<anan::P2Transport> m_t;
    QCheckBox* m_manual = nullptr;
    QSpinBox* m_clk = nullptr;
    QSpinBox* m_data = nullptr;
    QLabel* m_clkPs = nullptr;
    QLabel* m_dataPs = nullptr;
    QSpinBox* m_dwell = nullptr;
    QPushButton* m_reset = nullptr;
    QPushButton* m_auto = nullptr;
    // 02.11: "errors per ~4000 packets at zero power vs at normal power --
    // if almost none without RF, the transmitter couples into the link".
    QPushButton* m_count = nullptr;
    bool m_counting = false;
    std::array<unsigned long long, 7> m_countBase{};
    unsigned long long m_countBaseDuc = 0;
    int m_countDrive = -1;
    unsigned long long m_zeroDuc = 0;
    unsigned long long m_ducNow = 0;
    static constexpr unsigned long long kCountPackets = 4000;
    QLabel* m_now = nullptr;
    QLabel* m_counters = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QTimer* m_timer = nullptr;

    // Counters, unwrapped from the radio's 16-bit values.
    bool m_haveRaw = false;
    std::array<unsigned, 7> m_lastRaw{};   // [5] PHY idle errors, [6] KSZ9031 receive errors (v6)
    std::array<unsigned long long, 7> m_total{}, m_zero{};
    std::deque<std::pair<long long, unsigned long long>> m_fcsHist;   // (ms, FCS total)
    long long m_lastResendMs = 0;

    // Auto-tune.
    enum class Phase { Idle, Apply, Settle, Measure };
    Phase m_phase = Phase::Idle;
    int m_dir = +1;              // +1 first, then -1
    int m_s = 0;
    long long m_phaseMs = 0;
    long long m_applyFromMs = 0;
    std::array<unsigned long long, 7> m_base{};
    unsigned long long m_baseDuc = 0;
    struct Step {
        int s = 0;
        unsigned long long errs = 0, iq = 0, pkts = 0;
        bool applied = true;
    };
    std::vector<Step> m_steps;
    bool m_prevManual = false;
    int m_prevClk = 15, m_prevData = 7;
};

}  // namespace AetherSDR
