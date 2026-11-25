#pragma once

// ANAN fork build 578 (operator, 21.11): the rated output power of the ANAN
// model in use, for the power gauges (needle S-meter, SmartMTR). The model is
// the backend's capabilities().model, which is the model's key ("ANAN-100D",
// "Odyssey-3"); a model without its own rating, or another radio family,
// gives 0 -- the program's usual scale.

#include "core/AppSettings.h"
#include "core/backends/anan/P2Protocol.h"
#include "models/RadioModel.h"

#include <QByteArray>
#include <algorithm>
#include <QString>

namespace AetherSDR {

inline int ananRatedWatts(const RadioModel& radio)
{
    const QByteArray key = radio.backendCapabilities().model.toLatin1();
    if (key.isEmpty())
        return 0;
    const auto m = anan::modelFromKey(key.constData(), std::size_t(key.size()));
    return m ? anan::modelTraits(*m).ratedWatts : 0;
}

// ANAN fork build 711 (operator, 25.11: "in Power Meter Calibration add a
// Max Level field above the sliders: 120 W for every radio, 60 W for the
// Odyssey-3 -- the top of the power meters while transmitting"; one value,
// the red zone from Max Level / 1.2, as the 1.2 x rated scales had it). Per
// ANAN model (its key), so each radio keeps its own; unset = the default.
inline bool ananPowerModelKnown(const RadioModel& radio)
{
    const QByteArray key = radio.backendCapabilities().model.toLatin1();
    return !key.isEmpty()
        && anan::modelFromKey(key.constData(), std::size_t(key.size())).has_value();
}

inline QString ananPowerMaxLevelKey(const RadioModel& radio)
{
    return QStringLiteral("AnanPwrMaxLevel_") + radio.backendCapabilities().model;
}

inline int ananDefaultPowerMaxLevel(const RadioModel& radio)
{
    const int rated = ananRatedWatts(radio);
    return rated > 0 ? (rated * 6 + 2) / 5 : 120;   // 1.2 x rated (Odyssey-3: 60)
}

// The meters' top in watts for the ANAN model in use; 0 for another radio.
inline int ananPowerMaxLevel(const RadioModel& radio)
{
    if (!ananPowerModelKnown(radio))
        return 0;
    const int v = AppSettings::instance().value(ananPowerMaxLevelKey(radio), 0).toInt();
    // Build 713 (operator: "round the entered number to tens").
    return v > 0 ? std::max(10, (v + 5) / 10 * 10) : ananDefaultPowerMaxLevel(radio);
}

} // namespace AetherSDR
