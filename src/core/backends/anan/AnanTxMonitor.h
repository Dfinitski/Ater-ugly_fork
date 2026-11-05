#pragma once

// ANAN-100D port: MON -- the transmitted signal as audio.
//
// Fed with exactly the IQ the host hands to the radio's DUC (192 kHz, after
// the whole TXA chain: microphone or PC audio, PROC, EQ, ALC, 2-TONE / TUNE
// generator, PureSignal correction), it demodulates that IQ the way a
// receiver would and decimates to 24 kHz, so MON plays what goes to the
// transmitter whatever produced it -- no per-mode or per-source switching.
//
//   SSB / DIGI / DSB / CW   Re(iq): the baseband is the analytic signal of
//                           the audio (USB: +f, LSB: -f), its real part is
//                           the audio itself;
//   AM / SAM                envelope |iq| minus its mean (carrier);
//   FM                      frequency discriminator, +-5 kHz -> +-1.
//
// Then a 127-tap Blackman windowed-sinc low-pass (8 kHz) evaluated only at
// every 8th sample (192 -> 24 kHz). Single thread (the radio I/O thread).

#include "core/dsp/WdspChannel.h"

#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace AetherSDR::anan {

class AnanTxMonitor {
public:
    static constexpr int kDecim = 8;
    static constexpr int kTaps = 127;

    AnanTxMonitor()
    {
        constexpr double kPi = 3.14159265358979323846;
        const double fc = 8000.0 / 192000.0;   // cycles per sample
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            const double m = i - (kTaps - 1) / 2.0;
            const double sinc = m == 0.0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * m) / (kPi * m);
            const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1))
                             + 0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
            m_h[std::size_t(i)] = sinc * w;
            sum += m_h[std::size_t(i)];
        }
        for (double& v : m_h)
            v /= sum;
        reset();
    }

    void reset()
    {
        m_hist.fill(0.0);
        m_pos = 0;
        m_phase = 0;
        m_amDc = 0.0;
        m_prev = {1.0f, 0.0f};
    }

    void process(const std::complex<float>* iq, std::size_t n, WdspChannel::Mode mode,
                 std::vector<float>& out)
    {
        constexpr double kPi = 3.14159265358979323846;
        for (std::size_t k = 0; k < n; ++k) {
            const std::complex<float> z = iq[k];
            double a;
            switch (mode) {
            case WdspChannel::Mode::Am:
            case WdspChannel::Mode::Sam: {
                const double e = std::abs(z);
                m_amDc += (e - m_amDc) * (1.0 / 19200.0);   // ~0.1 s
                a = e - m_amDc;
                break;
            }
            case WdspChannel::Mode::Fm: {
                const std::complex<float> d = z * std::conj(m_prev);
                m_prev = z;
                a = (std::abs(d) > 0.0f ? std::arg(d) : 0.0f) * 192000.0 / (2.0 * kPi * 5000.0);
                break;
            }
            default:
                a = z.real();
                break;
            }
            m_hist[std::size_t(m_pos)] = a;
            m_hist[std::size_t(m_pos + kTaps)] = a;   // doubled ring, contiguous window
            m_pos = (m_pos + 1) % kTaps;
            if (++m_phase == kDecim) {
                m_phase = 0;
                double y = 0.0;
                const double* x = &m_hist[std::size_t(m_pos)];   // oldest first
                for (int i = 0; i < kTaps; ++i)
                    y += m_h[std::size_t(i)] * x[i];
                out.push_back(static_cast<float>(y));
            }
        }
    }

private:
    std::array<double, kTaps> m_h{};
    std::array<double, 2 * kTaps> m_hist{};
    int m_pos = 0;
    int m_phase = 0;
    double m_amDc = 0.0;
    std::complex<float> m_prev{1.0f, 0.0f};
};

} // namespace AetherSDR::anan
