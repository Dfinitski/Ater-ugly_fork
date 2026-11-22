#pragma once

// ANAN fork build 565 (operator, 21.11: "NB and noise reduction buttons on the
// receiver applet's first row; what they are set to is kept for each receiver
// separately, the sub receivers take their main receiver's").
//
// What is kept per receiver (slice id of the OWNER: 0..3; a sub receiver 6 / 7
// maps to 0 / 1):
//   - which blanker the NB button means, NB or NB2 -- also while it is off
//     (whether it is on travels with the receiver's session snapshot);
//   - whether the noise reduction is on and which method.
// Build 591: the methods kept here -- NR2, NNR, DFNR -- run in each receiver's
// own audio (NR2 in its WDSP chain, NNR / DFNR in Hl2AudioMixer), every
// receiver with its own; the engine's switches stand for the SELECTED
// receiver's (MainWindow::ananRxNrApply). NR4, RN2, BNR and MNR are the
// post-processing of the mixed audio and are not kept per receiver.

#include "core/AppSettings.h"
#include "core/backends/NoiseBlankerKind.h"

#include <QString>

namespace AetherSDR::anan_rxdsp {

inline int ownerOf(int sliceId) { return sliceId >= 6 ? sliceId - 6 : sliceId; }

inline QString key(const char* what, int owner)
{
    return QStringLiteral("AnanRx%1_%2").arg(QString::fromLatin1(what)).arg(owner);
}

inline NoiseBlankerKind nbKind(int owner)
{
    return AppSettings::instance().value(key("NbKind", owner), QStringLiteral("NB")).toString()
               == QLatin1String("NB2")
        ? NoiseBlankerKind::Advanced : NoiseBlankerKind::Impulse;
}

inline void setNbKind(int owner, NoiseBlankerKind kind)
{
    auto& st = AppSettings::instance();
    st.setValue(key("NbKind", owner),
                kind == NoiseBlankerKind::Advanced ? QStringLiteral("NB2") : QStringLiteral("NB"));
    st.save();
}

// Empty: never set for this receiver.
inline QString nrOnRaw(int owner)
{
    return AppSettings::instance().value(key("NrOn", owner), QString()).toString();
}

inline bool nrOn(int owner) { return nrOnRaw(owner) == QLatin1String("True"); }

// Build 591: a receiver's own method; anything else (an older choice) is NR2.
inline bool isRxOwnMethod(const QString& m)
{
#ifdef HAVE_DFNR
    if (m == QLatin1String("DFNR"))
        return true;
#endif
    return m == QLatin1String("NR2") || m == QLatin1String("NNR");
}

inline QString nrMethod(int owner)
{
    const QString m = AppSettings::instance()
        .value(key("NrMethod", owner), QStringLiteral("NR2")).toString();
    return isRxOwnMethod(m) ? m : QStringLiteral("NR2");
}

inline void setNr(int owner, bool on, const QString& method)
{
    auto& st = AppSettings::instance();
    st.setValue(key("NrOn", owner), on ? QStringLiteral("True") : QStringLiteral("False"));
    if (!method.isEmpty())
        st.setValue(key("NrMethod", owner), method);
    st.save();
}

} // namespace AetherSDR::anan_rxdsp
