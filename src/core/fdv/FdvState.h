#pragma once

// ANAN-100D port: FreeDV (mode "FDV") state shared between the GUI (FDV
// applet, mode combos, VFO badge) and Hl2Backend, which runs the libfdv engine
// (FdvEngine) in the receive and transmit audio paths. Ported from MicronSDR
// (FdvUnit / libfdv.dll). GUI thread only -- the backend's timers live there
// too -- so a plain singleton; each side polls, as AnanUiState does.

#include "core/AppSettings.h"

#include <QString>

#include <algorithm>

namespace AetherSDR::fdv {

// Operator-facing sub-modes, in the order the FDV applet lists them.
enum class FdvSubMode : int { M1600 = 0, M700D = 1, M700E = 2, RADE = 3 };
inline constexpr int kFdvSubModeCount = 4;

[[nodiscard]] inline const char* fdvSubModeName(int m)
{
    static const char* n[kFdvSubModeCount] = {"1600", "700D", "700E", "RADE"};
    return (m >= 0 && m < kFdvSubModeCount) ? n[m] : "?";
}

// libfdv's FDV_MODE_* for a sub-mode (fdv_api.h: RADE 0, 1600 1, 700D 2, 700E 3).
[[nodiscard]] inline int fdvLibMode(int subMode)
{
    switch (subMode) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 3;
    default: return 0;
    }
}

// ANAN fork build 337: the receive band of a sub-mode in the demodulated audio,
// as MicronSDR's fdvBand(): centred at 1500 Hz, the modem's RF bandwidth plus
// 100 Hz on each side for the tuning.
inline constexpr int kFdvCenterHz = 1500;
inline void fdvRxBandHz(int subMode, int* lo, int* hi)
{
    static const int bw[kFdvSubModeCount] = {1250, 1000, 1500, 1500};   // 1600, 700D, 700E, RADE
    const int half = bw[std::clamp(subMode, 0, kFdvSubModeCount - 1)] / 2 + 100;
    *lo = kFdvCenterHz - half;
    *hi = kFdvCenterHz + half;
}

// ANAN fork build 339: the transmit filter of a sub-mode, audio Hz, as
// MicronSDR's fdvTxBand(): the whole modem signal with its skirts. Measured
// there, not taken from the documentation (the modem output is down 25 dB at
// these edges; RADE: 99 % of the power in 100 .. 3070 Hz).
inline void fdvTxBandHz(int subMode, int* lo, int* hi)
{
    // Build 405 (operator, 14.11: "the transmit filter in FDV by the
    // specifications, cutting off what is not the signal -- and I must see
    // the result in the transmit spectrum"). It was the whole modem output
    // with its skirts (339: RADE 100 .. 3000, 99 % of the power). Now the
    // RF bandwidth of the mode around 1500 Hz plus one carrier spacing
    // (50 Hz) on each side:
    //   1600  1125 Hz (codec2 README_freedv)          ->  900 .. 2100
    //   700D  1000 Hz                                  ->  950 .. 2050
    //   700E  1500 Hz                                  ->  700 .. 2300
    //   RADE  1500 Hz at -6 dB, 30 carriers x 50 Hz    ->  700 .. 2300
    // RADE V1's own document (FreeDV-036): "spectral 'grass' at the low and
    // high frequency edges is high at around -12 dB from the peak ... the
    // bandwidth of the signal is effectively set by the SSB radio Tx
    // filter" -- this filter is that. The transmit panadapter shows the DUC
    // I/Q as sent (AnanTxScope), i.e. after it.
    //
    // Build 412 -- OUR ERROR in 405 for RADE: 700 .. 2300 is narrower than
    // the modem's own transmit filter. RADE's carriers are 750 .. 2200 Hz
    // (rade_ofdm.c: 30 x 50 Hz from 1500 - 750), and its own Tx BPF
    // (rade_tx.c) is 1.2 x that span about its centre: 1740 Hz at 1475 Hz,
    // 605 .. 2345 Hz. With 700 the edge stood 50 Hz under the first carrier.
    // Measured on the operator's radio, 14.11 20:37, 700 .. 2300: WDSP's ALC
    // at work in 97 % of the blocks, mean -2.2 dB, dips to -5 dB (the peaks
    // behind the filter 5 dB over full scale; in: envelope crest about 2 dB).
    // Now the modem's own figures: 600 .. 2350.
    //
    // Build 421 -- the codec2 modes checked against codec2's source
    // (github.com/drowe67/codec2, src/):
    //   1600 (fdmdv.c, freedv_1600.c): Nc 16 + a pilot at 1500 Hz, 75 Hz
    //        apart -> carriers 900 .. 2100 Hz, root raised cosine alpha 0.5 at
    //        50 baud (gt_alpha5_root): each +-37.5 Hz -> 862.5 .. 2137.5 Hz.
    //        OUR ERROR: 900 .. 2100 put the filter's -6 dB point on the two
    //        outer carriers and cut their outer halves by 38 dB (the WDSP
    //        filter, modelled). Now 800 .. 2200: flat over the signal.
    //   700D (ofdm_mode.c): Nc 17 + edge pilots, Ts 18 ms (55.6 Hz) about
    //        1500 -> 1000 .. 2000 Hz; codec2's own Tx BPF filtP650S900
    //        (pass +-650 Hz at 8 kHz, ofdm.c allocate_tx_bpf) -> 850 .. 2150.
    //   700E: Nc 21, no edge pilots, Ts 14 ms (71.4 Hz) -> 786 .. 2214 Hz;
    //        its Tx BPF filtP900S1100 -> 600 .. 2400.
    // 700D / 700E take codec2's own passbands, as RADE takes radae's: the
    // modem's authors' filter after their clipper ("a little extra bandwidth
    // helps reduce PAPR", filter_coef.h). 950 .. 2050 took 0.9 dB off 700D's
    // carrier edges.
    static const int band[kFdvSubModeCount][2] = {
        {800, 2200}, {850, 2150}, {600, 2400}, {600, 2350}};   // 1600, 700D, 700E, RADE
    const int m = std::clamp(subMode, 0, kFdvSubModeCount - 1);
    *lo = band[m][0];
    *hi = band[m][1];
}

class FdvState {
public:
    static FdvState& instance()
    {
        static FdvState s;
        return s;
    }

    FdvState()
    {
        auto& st = AppSettings::instance();
        subMode = std::clamp(st.value(QStringLiteral("FdvMode"), 0).toInt(), 0,
                             kFdvSubModeCount - 1);
        callsign = sanitizeCallsign(st.value(QStringLiteral("FdvCallsign"), QString()).toString());
    }

    // Callsign rule (operator): up to 8 characters, A-Z and 0-9 only.
    [[nodiscard]] static QString sanitizeCallsign(const QString& in)
    {
        QString out;
        for (QChar c : in.toUpper()) {
            if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
                || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
                out += c;
            if (out.size() == 8)
                break;
        }
        return out;
    }

    void setSubMode(int m)
    {
        subMode = std::clamp(m, 0, kFdvSubModeCount - 1);
        AppSettings::instance().setValue(QStringLiteral("FdvMode"), subMode);
        AppSettings::instance().save();
    }
    void setCallsign(const QString& cs)
    {
        callsign = sanitizeCallsign(cs);
        AppSettings::instance().setValue(QStringLiteral("FdvCallsign"), callsign);
        AppSettings::instance().save();
    }
    [[nodiscard]] QString label() const
    {
        return QStringLiteral("FDV %1").arg(QLatin1String(fdvSubModeName(subMode)));
    }

    // ---- written by the GUI ----
    int sliceId = -1;          // slice running FDV, -1 = off
    bool eooRequested = false; // PTT released: finish the over, then unkey
    int subMode = 0;           // FdvSubMode
    QString callsign;

    // ---- written by the backend ----
    bool engineLoaded = false;
    QString engineError;       // why libfdv could not be used ("" = fine)
    bool sync = false;
    float snrDb = 0.0f;
    float freqOffsetHz = 0.0f;
    bool txActive = false;
    QString rxCallsign;        // last callsign received
    int rxCallsignSerial = 0;  // increments with each callsign received
};

}  // namespace AetherSDR::fdv
