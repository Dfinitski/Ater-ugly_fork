#pragma once

// ANAN fork build 717 (operator, 25.11: "a simple equaliser on WDSP, at the
// sliders' frequencies, with the right Q for the number of sliders; receive
// and transmit independently"). The applet's graphic EQ as the control points
// of WDSP's equaliser (eq.c, SetRXAEQProfile / SetTXAEQProfile).
//
// WDSP interpolates its profile linearly in dB over LINEAR frequency between
// the points it is given. Handed only the slider centres, a slider would reach
// out to its neighbours' centres -- on 63 Hz .. 8 kHz far wider at the top than
// at the bottom, nothing like a band. So the curve is built here: each slider
// is a peaking band (the analogue prototype of the RBJ peaking EQ) at its
// centre, with the Q of the spacing between the sliders -- the bandwidth in
// octaves between neighbouring centres, Q = sqrt(2^N) / (2^N - 1), 1.414 for
// the 8 one-octave sliders -- and the sum of the bands in dB is sampled at
// kPoints log-spaced frequencies, dense enough that WDSP's linear
// interpolation between them follows the curve. G[0] (WDSP's preamp) is 0.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace AetherSDR {

struct GraphicEqCurve {
    std::vector<double> freqsHz;   // [0] unused (0), [1..n] rising
    std::vector<double> gainsDb;   // [0] preamp (0), [1..n]
    [[nodiscard]] int points() const noexcept
    {
        return static_cast<int>(freqsHz.size()) - 1;
    }
};

template <std::size_t N>
[[nodiscard]] inline GraphicEqCurve buildGraphicEqCurve(const std::array<double, N>& centresHz,
                                                        const std::array<double, N>& bandGainsDb,
                                                        double sampleRateHz)
{
    static_assert(N >= 2, "a graphic EQ has at least two sliders");
    constexpr int kPoints = 160;   // WDSP takes up to 256
    // The bandwidth of one slider in octaves: the spacing of the centres.
    const double octaves = std::log2(centresHz[N - 1] / centresHz[0]) / static_cast<double>(N - 1);
    const double r = std::exp2(octaves);
    const double q = std::sqrt(r) / (r - 1.0);

    // The response of one band in dB at f.
    const auto bandDb = [q](double f0, double g, double f) {
        if (g == 0.0)
            return 0.0;
        // |H(j x)|^2 of H(s) = (s^2 + s A/Q + 1) / (s^2 + s / (A Q) + 1),
        // x = f / f0, A = 10^(g/40): g dB at the centre, 0 dB far away.
        const double a = std::pow(10.0, g / 40.0);
        const double x = f / f0;
        const double re = 1.0 - x * x;
        const double num = re * re + (x * a / q) * (x * a / q);
        const double den = re * re + (x / (a * q)) * (x / (a * q));
        return 10.0 * std::log10(num / den);
    };
    // Neighbouring bands overlap (at one octave a band is still ~-5 dB of its
    // gain at the next centre), so the sliders' values are not what the
    // summed curve shows at the centres: all at +10 dB would read +15. The
    // bands' own gains are corrected until the curve AT EACH CENTRE equals its
    // slider (a few fixed-point passes; the interaction is mild).
    std::array<double, N> g = bandGainsDb;
    for (int pass = 0; pass < 8; ++pass) {
        for (std::size_t i = 0; i < N; ++i) {
            double at = 0.0;
            for (std::size_t b = 0; b < N; ++b)
                at += bandDb(centresHz[b], g[b], centresHz[i]);
            g[i] = std::clamp(g[i] + (bandGainsDb[i] - at), -30.0, 30.0);
        }
    }

    const double lo = 10.0;
    const double hi = std::min(20000.0, 0.45 * sampleRateHz);

    GraphicEqCurve c;
    c.freqsHz.assign(kPoints + 1, 0.0);
    c.gainsDb.assign(kPoints + 1, 0.0);
    for (int k = 1; k <= kPoints; ++k) {
        const double f = lo * std::pow(hi / lo, static_cast<double>(k - 1) / (kPoints - 1));
        double db = 0.0;
        for (std::size_t b = 0; b < N; ++b)
            db += bandDb(centresHz[b], g[b], f);
        c.freqsHz[static_cast<std::size_t>(k)] = f;
        c.gainsDb[static_cast<std::size_t>(k)] = db;
    }
    return c;
}

}  // namespace AetherSDR
