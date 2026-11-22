#pragma once

#include "DbmRangePlausibility.h"
#include <QMetaType>
#include <QtGlobal>

namespace AetherSDR {

// The scale used to decode THIS observation, captured with the samples.
// An empty scale denotes absolute-level samples with no pixel-range encoding.
struct SpectrumDecodeScale {
    float minDbm{0};
    float maxDbm{0};
    quint64 generation{0};
    // ANAN fork build 618: the frequency frame the samples were computed in
    // (the backend's pan centre / span when the FFT frame was made). 0 = not
    // given: the samples are taken to be in the view's frame, as before.
    double frameCenterMhz{0.0};
    double frameBandwidthMhz{0.0};
    bool hasFrame() const { return frameBandwidthMhz > 0.0 && frameCenterMhz > 0.0; }

    bool valid() const
    {
        return generation != 0 && dbmRangeLooksPlausible(minDbm, maxDbm);
    }
};

} // namespace AetherSDR

Q_DECLARE_METATYPE(AetherSDR::SpectrumDecodeScale)
