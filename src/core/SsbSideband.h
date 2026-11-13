#pragma once

// ANAN fork build 330: the analog SSB sideband convention in one place --
// LSB below 10 MHz, USB from 10 MHz up, and USB on 60 m by regulation (the
// whole 5.25-5.45 MHz span, which holds every country's 60 m allocation).
// Used by FreeDV (build 310, freedv.org: "On bands below 10 MHz, LSB is used
// for FreeDV. On 10MHz and above, USB is used.") and by the VFO flag's SSB
// button when it enters SSB from another mode. Not for the digital data
// modes: FT8/FT4/PSK31/JS8 are USB (DIGU) on every band.

#include <QString>

namespace AetherSDR {

inline QString ssbSidebandFor(double mhz)
{
    if (mhz >= 5.25 && mhz <= 5.45)
        return QStringLiteral("USB");
    return mhz < 10.0 ? QStringLiteral("LSB") : QStringLiteral("USB");
}

} // namespace AetherSDR
