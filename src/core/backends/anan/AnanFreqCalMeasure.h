#pragma once

// ANAN fork build 633 (operator, 22.11: "frequency calibration: I just want to
// pick a frequency from the list and press Calibrate; see how MicronSDR does
// it"). The measurement of MicronSDR::calAnalyse, on the raw DDC IQ: a
// Blackman-Harris windowed FFT, the strongest bin within a window around the
// expected carrier, its level against the median of the window (the carrier
// takes a few bins only), a parabola through the log magnitudes of the three
// bins for the fraction of a bin.
//
// From the offset to the clock error (no retune needed): the NCO register is
// written in nominal Hz (reg), the radio's clock is nominal * (1 + e). The LO
// lands on reg * (1 + e) and the DDC output runs at fs * (1 + e), so a carrier
// of true frequency F reads, on the nominal fs scale, at
//     m = F / (1 + e) - reg      ->      1 + e = F / (m + reg).
// The current correction is inside reg, so the result is the absolute error.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

namespace AetherSDR::anan {

struct FreqCalPeak {
    bool found = false;      // a carrier, not at the window's edge
    double offsetHz = 0.0;   // above the DDC centre (upright spectrum)
    double levelDb = -200.0; // dBFS (a full-scale tone = 0 dB)
    double snrDb = 0.0;      // above the median of the window
};

namespace detail {
inline void fftRadix2(std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace detail

// x: upright IQ (signals above the NCO at positive frequency), size a power
// of two. The carrier is searched within expectHz +- windowHz.
inline FreqCalPeak freqCalFindCarrier(const std::vector<std::complex<float>>& x, double fs,
                                      double expectHz, double windowHz)
{
    FreqCalPeak r;
    const std::size_t n = x.size();
    if (n < 1024 || (n & (n - 1)) != 0 || !(fs > 0.0))
        return r;
    std::vector<std::complex<double>> a(n);
    double winSum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t)
            - 0.01168 * std::cos(3.0 * t);
        winSum += w;
        a[i] = std::complex<double>(x[i].real(), x[i].imag()) * w;
    }
    detail::fftRadix2(a);
    const double df = fs / static_cast<double>(n);
    const long long nn = static_cast<long long>(n);
    auto pw = [&](long long k) {
        const long long b = ((k % nn) + nn) % nn;
        return std::norm(a[static_cast<std::size_t>(b)]);
    };
    const long long kLo = static_cast<long long>(std::floor((expectHz - windowHz) / df));
    const long long kHi = static_cast<long long>(std::ceil((expectHz + windowHz) / df));
    if (kHi - kLo < 8 || kHi - kLo >= nn)
        return r;
    long long kMax = kLo;
    double pMax = -1.0;
    std::vector<double> all;
    all.reserve(static_cast<std::size_t>(kHi - kLo + 1));
    for (long long k = kLo; k <= kHi; ++k) {
        const double p = pw(k);
        all.push_back(p);
        if (p > pMax) {
            pMax = p;
            kMax = k;
        }
    }
    std::nth_element(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(all.size() / 2), all.end());
    const double noise = all[all.size() / 2] + 1e-30;
    r.levelDb = 10.0 * std::log10(pMax / (winSum * winSum) + 1e-30);
    r.snrDb = 10.0 * std::log10(pMax / noise);
    const double pa = 10.0 * std::log10(pw(kMax - 1) + 1e-30);
    const double pb = 10.0 * std::log10(pMax + 1e-30);
    const double pc = 10.0 * std::log10(pw(kMax + 1) + 1e-30);
    const double den = pa - 2.0 * pb + pc;
    const double frac = std::clamp(std::abs(den) > 1e-12 ? 0.5 * (pa - pc) / den : 0.0, -0.5, 0.5);
    r.offsetHz = (static_cast<double>(kMax) + frac) * df;
    r.found = kMax != kLo && kMax != kHi;
    return r;
}

// The clock error (ppb, > 0 = the radio's clock fast) from a carrier of true
// frequency refHz found at offsetHz above an NCO written as regHz.
inline double freqCalPpbFrom(double refHz, double offsetHz, double regHz)
{
    const double d = offsetHz + regHz;
    return d > 0.0 ? (refHz / d - 1.0) * 1e9 : 0.0;
}

} // namespace AetherSDR::anan
