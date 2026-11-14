#include "core/backends/anan/AnanTxScope.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace AetherSDR::anan {

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
// Periodic 4-term Blackman-Harris (-92 dB sidelobes).
double blackmanHarris(int i, int n)
{
    constexpr double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    const double t = kTwoPi * i / n;
    return a0 - a1 * std::cos(t) + a2 * std::cos(2.0 * t) - a3 * std::cos(3.0 * t);
}
}

TxScope::TxScope()
    : m_fir(kFirTaps),
      m_firRe(2 * kFirTaps, 0.0f),
      m_firIm(2 * kFirTaps, 0.0f),
      m_ring(kFftSize),
      m_window(kFftSize),
      m_buf(kFftSize),
      m_twiddle(kFftSize / 2),
      m_bitrev(kFftSize),
      m_framePower(kBins, 0.0),
      m_smoothed(kBins, 0.0)
{
    // Decimating low-pass: windowed sinc, -6 dB at 24 kHz. With 96 taps the
    // Blackman-Harris transition is +-8 kHz around that: flat to 16 kHz (the
    // +-12 kHz shown is untouched), down > 90 dB from 32 kHz -- and what
    // folds onto +-12 kHz at 48 ksps comes from 36 kHz and above.
    {
        const double fc = 24000.0 / kSampleRateHz;   // cycles per sample
        const double mid = 0.5 * (kFirTaps - 1);
        double sum = 0.0;
        std::vector<double> h(kFirTaps);
        for (int i = 0; i < kFirTaps; ++i) {
            const double x = i - mid;
            const double s = std::abs(x) < 1e-12 ? 2.0 * fc
                                                 : std::sin(kTwoPi * fc * x) / (std::numbers::pi * x);
            // Symmetric window over the taps.
            h[i] = s * blackmanHarris(i, kFirTaps - 1);
            sum += h[i];
        }
        for (int i = 0; i < kFirTaps; ++i)
            m_fir[i] = static_cast<float>(h[i] / sum);
    }

    double sum = 0.0;
    for (int i = 0; i < kFftSize; ++i) {
        m_window[i] = blackmanHarris(i, kFftSize);
        sum += m_window[i];
    }
    // |X[k]| of a tone A*exp(j w0 n) on bin k is A * sum(w); dividing the
    // power by sum(w)^2 makes a full-scale tone 0 dBFS.
    m_windowSumSq = sum * sum;

    for (int k = 0; k < kFftSize / 2; ++k)
        m_twiddle[k] = std::polar(1.0, -kTwoPi * k / kFftSize);

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
    std::fill(m_firRe.begin(), m_firRe.end(), 0.0f);
    std::fill(m_firIm.begin(), m_firIm.end(), 0.0f);
    m_firPos = 0;
    m_firPhase = 0;
    std::fill(m_ring.begin(), m_ring.end(), std::complex<float>{});
    m_pos = 0;
    m_filled = 0;
    m_sinceFft = 0;
    std::fill(m_framePower.begin(), m_framePower.end(), 0.0);
    std::fill(m_smoothed.begin(), m_smoothed.end(), 0.0);
    m_haveSmoothed = false;
    m_peak = 0.0f;
}

bool TxScope::push(const std::complex<float>* iq, int n, std::vector<float>& out)
{
    bool produced = false;
    for (int i = 0; i < n; ++i) {
        // The clamp buildDucIq() applies, so this is the wire value.
        const float re = std::clamp(iq[i].real(), -1.0f, 1.0f);
        const float im = std::clamp(iq[i].imag(), -1.0f, 1.0f);
        m_peak = std::max(m_peak, re * re + im * im);   // squared; the root at the frame
        m_firRe[m_firPos] = m_firRe[m_firPos + kFirTaps] = re;
        m_firIm[m_firPos] = m_firIm[m_firPos + kFirTaps] = im;
        m_firPos = (m_firPos + 1) % kFirTaps;
        if (++m_firPhase < kDecimation)
            continue;
        m_firPhase = 0;
        // One decimated sample: the FIR over the last kFirTaps inputs, which
        // lie at [m_firPos, m_firPos + kFirTaps), oldest first.
        const float* xr = m_firRe.data() + m_firPos;
        const float* xi = m_firIm.data() + m_firPos;
        const float* h = m_fir.data();
        float ar = 0.0f, ai = 0.0f;
        for (int t = 0; t < kFirTaps; ++t) {
            ar += h[t] * xr[t];
            ai += h[t] * xi[t];
        }
        m_ring[m_pos] = {ar, ai};
        m_pos = (m_pos + 1) % kFftSize;
        if (m_filled < kFftSize)
            ++m_filled;
        if (++m_sinceFft < kHop || m_filled < kFftSize)
            continue;
        m_sinceFft = 0;
        runFft();
        finishFrame(out);
        produced = true;
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
        m_framePower[j] = std::norm(m_buf[k]) / m_windowSumSq;
    }
}

void TxScope::finishFrame(std::vector<float>& out)
{
    // The receiver's law (Hl2Spectrum::blendAlpha): alpha = 1 - exp(-dt/tau).
    const double dtMs = 1000.0 * kHop / kFftRateHz;
    const double alpha = m_tauMs > 0.0 ? 1.0 - std::exp(-dtMs / m_tauMs) : 1.0;
    out.resize(kBins + 1);
    for (int j = 0; j < kBins; ++j) {
        const double p = m_framePower[j];
        m_smoothed[j] = m_haveSmoothed ? m_smoothed[j] + alpha * (p - m_smoothed[j]) : p;
        const double db = 10.0 * std::log10(m_smoothed[j] + 1e-20);
        out[j] = static_cast<float>(std::max(db, static_cast<double>(kFloorDbfs)));
    }
    m_haveSmoothed = true;
    out[kBins] = static_cast<float>(
        std::max(10.0 * std::log10(static_cast<double>(m_peak) + 1e-20),
                 static_cast<double>(kFloorDbfs)));
    m_peak = 0.0f;
}

} // namespace AetherSDR::anan
