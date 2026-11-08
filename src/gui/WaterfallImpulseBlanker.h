#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// The waterfall "NB Blank" impulse test (#277), as a pure function, since
// SpectrumWidget links into no test. On a Flex tile (positive intensity) it is
// a ratio over the ring's mean; on an absolute dB row (dBFS under a dBm label,
// negative) it is a dB margin. The unit is the declared capability
// RadioCapabilities::panBinsAbsolute(), never the sign of the data.

namespace AetherSDR::WaterfallImpulseBlanker {

enum class RowKind {
    TileIntensity,  // native waterfall tile, int16(raw) / 128
    AbsoluteDb,     // host-computed pan frame reused as the row
};

// Row means the baseline ring holds; SpectrumWidget asserts its ring is this.
// The ring holds one unit at a time: absolute-dB residue read under the tile
// law holds a tile waterfall for seconds, so SpectrumWidget::setPanBinsAbsolute
// empties the ring when the row kind changes.
inline constexpr int kRingRows = 32;

// Rows of history the ring needs before any row may be called an impulse.
inline constexpr int kMinHistoryRows = 8;

// What a rejected row may add to the ring, as a threshold value: the lowest
// setting the operator control offers. It lets a level that is here to stay
// pull the baseline up until its rows pass again, without letting one burst
// move the baseline by its full height.
inline constexpr float kRejectedRowCapThreshold = 1.05f;

// dB above the baseline per unit of (threshold - 1) on an absolute row.
// ANAN port (build 201): 0.2 dB per step of the 5..95 control -- 1 dB lowest,
// 3 dB at the default 1.15, 19 dB highest. The absolute row is ONE unaveraged
// FFT frame (Hl2Backend: the waterfall takes the frame before FFT AVG), and its
// level is the row MEDIAN (the noise floor; signals are a minority of the
// bins), whose frame-to-frame spread is ~0.2 dB on 1024 bins. A broadband
// impulse lifts that floor by several dB. The former 1 dB per step (5..95 dB,
// 15 dB default, on the row mean) sat far above any real impulse: the blanker
// never fired.
inline constexpr float kDbPerThresholdUnit = 20.0f;

// ANAN port (build 201): consecutive rejected rows after which the new level
// is taken as real (a retune, a band opening, a gain change) and the ring
// restarts from it. An impulse burst is shorter; without this a lasting step
// would freeze the waterfall while the capped ring crept up to it.
inline constexpr int kMaxRejectedRun = 8;

inline float dbMargin(float threshold)
{
    return (threshold - 1.0f) * kDbPerThresholdUnit;
}

struct Decision {
    bool impulse;     // replace this row with the last good one
    float ringValue;  // what this row contributes to the baseline ring
};

// historyRows: rows already in the ring. baseline: their mean. rowMean: the
// incoming row's level -- the mean of its bins for a tile, the median for an
// absolute dB row (rowMedian()). threshold: the operator's setting, 1.05 to
// 2.0. baseline and rowMean are in the unit `kind` names.
inline Decision decide(RowKind kind, int historyRows, float baseline,
                       float rowMean, float threshold)
{
    if (kind == RowKind::AbsoluteDb) {
        // A baseline that is not finite fails OPEN. A -inf in the ring (one
        // empty bin in one frame) would otherwise make every later row an
        // impulse and write -inf back, and the waterfall would never move
        // again; this way the ring refills from accepted rows.
        if (historyRows >= kMinHistoryRows && std::isfinite(baseline)
                && rowMean - baseline > dbMargin(threshold)) {
            return {true,
                    std::min(rowMean,
                             baseline + dbMargin(kRejectedRowCapThreshold))};
        }
        return {false, rowMean};
    }

    // Tile intensity: the test and the cap exactly as #277 wrote them.
    if (historyRows >= kMinHistoryRows && baseline > 0.0f
            && rowMean > baseline * threshold) {
        return {true, std::min(rowMean, baseline * kRejectedRowCapThreshold)};
    }
    return {false, rowMean};
}

// ANAN port (build 203): how far to lower an absolute dB row whose floor is
// `riseDb` above the baseline, for a margin `marginDb` (dbMargin()). A soft
// knee: nothing at or below the baseline, rise^2 / margin up to the margin
// (at most margin / 4 left standing, at half the margin), the whole rise above
// it. A hard step at the margin (build 202: a row 0.1 dB below it drawn as is,
// one 0.1 dB above it pulled down by the whole margin) striped a jumpy band.
inline float floorShiftDb(float riseDb, float marginDb)
{
    if (!(riseDb > 0.0f) || !(marginDb > 0.0f))
        return 0.0f;
    return riseDb >= marginDb ? riseDb : riseDb * riseDb / marginDb;
}

// The median of a row (its noise floor): an absolute dB row's level for decide().
// `scratch` is reused across calls. 0 for an empty row.
template <typename Container>
inline float rowMedian(const Container& bins, std::vector<float>& scratch)
{
    scratch.assign(bins.begin(), bins.end());
    if (scratch.empty())
        return 0.0f;
    auto mid = scratch.begin() + static_cast<std::ptrdiff_t>(scratch.size() / 2);
    std::nth_element(scratch.begin(), mid, scratch.end());
    return *mid;
}

} // namespace AetherSDR::WaterfallImpulseBlanker
