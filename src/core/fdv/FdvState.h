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
