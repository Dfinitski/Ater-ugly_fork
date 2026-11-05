#pragma once

// ANAN-100D port (operator, 03.11): Tools > HW Diag -- everything the Odyssey
// firmware reports about itself in the high priority status packet (port
// 1025, status layout v8, "Odyssey-3 2.1 ANP: status additions in the high
// priority status packet"), in one window instead of scattered over the PureSignal view:
//   Ethernet: frames with a bad FCS, frames with PHY RXER, command / audio /
//             I/Q packets the radio threw away (16..27);
//   PHY KSZ9031: 1000BASE-T idle errors, receive errors, the PC's EEE, the
//             1000BASE-T status, the PHY crystal against the radio (8..13, 53/54);
//   buffers:  TX I/Q and speaker FIFO levels, underflows, overflows (32..46);
//   codec:    PLL lock losses, SPI writes to the TLV320 (51, 52).
// Counters are the radio's (16 / 8 bit, wrapping); shown since "Reset
// counters" (and the raw value since the radio's power-on where it has one).

#include "gui/PersistentDialog.h"

#include <QPointer>

#include <array>
#include <deque>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace AetherSDR {

class RadioModel;

class HwDiagDialog : public PersistentDialog {
    Q_OBJECT
public:
    explicit HwDiagDialog(RadioModel* model, QWidget* parent = nullptr);

private:
    void tick();
    void resetCounters();
    void logLine(const QString& t);

    // Counters followed here: index -> status byte offset and width.
    enum Counter {
        kCmdNotApplied, kAudioDropped, kIqDropped, kBadFcs, kRxer,   // 16..27 (v3)
        kPhyIdle, kPhyRx,                                            // 8..11 (v6)
        kTxEmpty, kTxOverflow, kSpkFades, kSpkEmptyReads, kSpkOverflow,   // 34..45 (v1)
        kPllLoss, kCodecWrites,                                      // 51, 52 (v2, 8 bit)
        kCounters
    };

    RadioModel* m_model = nullptr;
    QLabel* m_head = nullptr;
    QLabel* m_eth = nullptr;
    QLabel* m_phy = nullptr;
    QLabel* m_buf = nullptr;
    QLabel* m_codec = nullptr;
    QPushButton* m_reset = nullptr;
    QPushButton* m_count = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QTimer* m_timer = nullptr;

    bool m_have = false;
    unsigned long long m_lastCount = 0;   // status packets (a new session restarts it)
    std::array<unsigned, kCounters> m_last{};
    std::array<unsigned long long, kCounters> m_total{}, m_zero{};
    unsigned long long m_zeroDuc = 0, m_ducNow = 0;
    std::deque<std::pair<long long, unsigned long long>> m_fcsHist;   // (ms, FCS total)
    // Level ranges over the last ~5 s.
    std::deque<std::pair<long long, int>> m_txLevels, m_spkLevels;

    // "Count 4000 packets".
    bool m_counting = false;
    std::array<unsigned long long, kCounters> m_countBase{};
    unsigned long long m_countBaseDuc = 0;
    int m_countDrive = -1;
    static constexpr unsigned long long kCountPackets = 4000;
};

}  // namespace AetherSDR
