#pragma once

// ANAN fork build 380: the FTX mode (FT8 / FT4 / JS8 / WSPR), being ported
// from MicronSDR (FtUnit / FtPanel / Js8Panel / FtWaterfall, libftx.dll and
// libjs8.dll). This is the state shared between the GUI and Hl2Backend, as
// FdvState is for FreeDV: GUI thread only, a plain singleton, each side polls.
//
// While FTX is on, its receiver is TAKEN OVER by the mode: its audio does not
// go to the loudspeakers (the decoders take it), the microphone does not go
// to the transmitter (the mode's own waveform will), and the panadapter of
// that receiver is replaced by the FTX view.

#include "core/AppSettings.h"

#include <QString>

namespace AetherSDR::ftx {

// The values of MicronSDR's FtUnit (the libraries take them).
enum : int { kFt8 = 8, kFt4 = 4, kJs8 = 100, kWspr = 120 };

[[nodiscard]] inline const char* subModeName(int m)
{
    switch (m) {
    case kFt8: return "FT8";
    case kFt4: return "FT4";
    case kJs8: return "JS8";
    case kWspr: return "WSPR";
    default: return "?";
    }
}

// The T/R period, ms, and the width of one signal, Hz (the waterfall's marks).
[[nodiscard]] inline int periodMs(int m)
{
    return m == kFt4 ? 7500 : m == kWspr ? 120000 : 15000;
}
[[nodiscard]] inline double signalWidthHz(int m)
{
    return m == kFt4 ? 4.0 * 12000.0 / 576.0 : m == kWspr ? 6.0 : 8.0 * 6.25;
}

class FtxState {
public:
    static FtxState& instance()
    {
        static FtxState s;
        return s;
    }
    FtxState()
    {
        const int m = AppSettings::instance().value(QStringLiteral("FtxSubMode"), kFt8).toInt();
        subMode = (m == kFt4 || m == kJs8 || m == kWspr) ? m : kFt8;
    }
    void setSubMode(int m)
    {
        subMode = m;
        auto& st = AppSettings::instance();
        st.setValue(QStringLiteral("FtxSubMode"), m);
        st.save();
    }

    int sliceId = -1;      // the receiver FTX has taken over, -1 = FTX is off
    int subMode = kFt8;
    // Build 383: the mode's own transmitter is keying (set by its window for
    // the time of its transmission). Every other key -- MOX, PTT, TUNE in the
    // program, the PTT of the radio -- is refused while FTX has the
    // transmitting receiver (Hl2Backend::applyKeying).
    bool ownTx = false;
};

}  // namespace AetherSDR::ftx
