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
    static const int band[kFdvSubModeCount][2] = {
        {700, 2300}, {600, 2400}, {400, 2600}, {100, 3000}};   // 1600, 700D, 700E, RADE
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
