#pragma once

// ANAN fork build 578 (operator, 21.11): the rated output power of the ANAN
// model in use, for the power gauges (needle S-meter, SmartMTR). The model is
// the backend's capabilities().model, which is the model's key ("ANAN-100D",
// "Odyssey-3"); a model without its own rating, or another radio family,
// gives 0 -- the program's usual scale.

#include "core/backends/anan/P2Protocol.h"
#include "models/RadioModel.h"

#include <QByteArray>

namespace AetherSDR {

inline int ananRatedWatts(const RadioModel& radio)
{
    const QByteArray key = radio.backendCapabilities().model.toLatin1();
    if (key.isEmpty())
        return 0;
    const auto m = anan::modelFromKey(key.constData(), std::size_t(key.size()));
    return m ? anan::modelTraits(*m).ratedWatts : 0;
}

} // namespace AetherSDR
