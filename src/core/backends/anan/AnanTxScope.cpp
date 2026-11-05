#include "core/backends/anan/AnanTxScope.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace AetherSDR::anan {

namespace {
// Across-frame smoothing: half of the new frame each time (~40 ms memory at
// ~23 frames per second). A steady signal reads the same with or without it.
constexpr double kFrameAlpha = 0.5;
}

TxScope::TxScope()
    : m_ring(kFftSize),
      m_window(kFftSize),
      m_buf(kFftSize),
      m_twiddle(kFftSize / 2),
      m_bitrev(kFftSize),
      m_framePower(kBins, 0.0),
      m_smoothed(kBins, 0.0)
{
    // Periodic 4-term Blackman-Harris (-92 dB sidelobes).
    constexpr double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    constexpr double twoPi = 2.0 * std::numbers::pi;
    double sum = 0.0;
    for (int i = 0; i < kFftSize; ++i) {
        const double t = twoPi * i / kFftSize;
        m_window[i] = a0 - a1 * std::cos(t) + a2 * std::cos(2.0 * t) - a3 * std::cos(3.0 * t);
        sum += m_window[i];
    }
    // |X[k]| of a tone A*exp(j w0 n) on bin k is A * sum(w); dividing the
    // power by sum(w)^2 makes a full-scale tone 0 dBFS.
    m_windowSumSq = sum * sum;

    for (int k = 0; k < kFftSize / 2; ++k)
        m_twiddle[k] = std::polar(1.0, -twoPi * k / kFftSize);

    int bits = 0;
    while ((1 << bits) < kFftSize)
        ++bits;
    for (int i = 0; i < kFftSize; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        m_bitrev[i] = r;
    }
}

void TxScope::reset() noexcept
{
    std::fill(m_ring.begin(), m_ring.end(), std::complex<float>{});
    m_pos = 0;
    m_filled = 0;
    m_sinceFft = 0;
    m_fftsInFrame = 0;
    std::fill(m_framePower.begin(), m_framePower.end(), 0.0);
    std::fill(m_smoothed.begin(), m_smoothed.end(), 0.0);
    m_haveSmoothed = false;
}

bool TxScope::push(const std::complex<float>* iq, int n, std::vector<float>& out)
{
    bool produced = false;
    for (int i = 0; i < n; ++i) {
        // The clamp buildDucIq() applies, so this is the wire value.
        const float re = std::clamp(iq[i].real(), -1.0f, 1.0f);
        const float im = std::clamp(iq[i].imag(), -1.0f, 1.0f);
        m_ring[m_pos] = {re, im};
        m_pos = (m_pos + 1) % kFftSize;
        if (m_filled < kFftSize)
            ++m_filled;
        if (++m_sinceFft < kHop || m_filled < kFftSize)
            continue;
        m_sinceFft = 0;
        runFft();
        if (++m_fftsInFrame == kFftsPerFrame) {
            finishFrame(out);
            produced = true;
        }
    }
    return produced;
}

void TxScope::runFft()
{
    // Oldest sample first: m_pos is where the next one would go.
    for (int i = 0; i < kFftSize; ++i) {
        const std::complex<float> s = m_ring[(m_pos + i) % kFftSize];
        m_buf[m_bitrev[i]] = {static_cast<double>(s.real()) * m_window[i],
                              static_cast<double>(s.imag()) * m_window[i]};
    }
    // Iterative radix-2 decimation in time.
    for (int len = 2; len <= kFftSize; len <<= 1) {
        const int half = len >> 1;
        const int stride = kFftSize / len;
        for (int start = 0; start < kFftSize; start += len) {
            for (int k = 0; k < half; ++k) {
                const std::complex<double> t = m_twiddle[k * stride] * m_buf[start + k + half];
                const std::complex<double> u = m_buf[start + k];
                m_buf[start + k] = u + t;
                m_buf[start + k + half] = u - t;
            }
        }
    }
    // Centre +-kHalfBins, lowest RF first. The wire has the HPSDR handedness,
    // the mirror of RF (Hl2TxDsp conjugates for it; RX conjugates back), so
    // output j -- RF offset (j - kHalfBins) bins -- is wire bin kHalfBins - j.
    for (int j = 0; j < kBins; ++j) {
        const int k = (kHalfBins - j + kFftSize) % kFftSize;
        m_framePower[j] += std::norm(m_buf[k]) / m_windowSumSq;
    }
}

void TxScope::finishFrame(std::vector<float>& out)
{
    out.resize(kBins);
    for (int j = 0; j < kBins; ++j) {
        const double p = m_framePower[j] / kFftsPerFrame;
        m_smoothed[j] = m_haveSmoothed ? m_smoothed[j] + kFrameAlpha * (p - m_smoothed[j]) : p;
        const double db = 10.0 * std::log10(m_smoothed[j] + 1e-20);
        out[j] = static_cast<float>(std::max(db, static_cast<double>(kFloorDbfs)));
    }
    m_haveSmoothed = true;
    std::fill(m_framePower.begin(), m_framePower.end(), 0.0);
    m_fftsInFrame = 0;
}

} // namespace AetherSDR::anan
