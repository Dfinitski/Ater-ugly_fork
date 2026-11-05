#pragma once

// ANAN-100D port: two-tone IMD of a complex baseband record -- the same
// figure for every point of the transmit chain (host DUC IQ, DAC data =
// PureSignal reference, coupler = PureSignal feedback), so a distortion can
// be placed: before the radio, inside the FPGA / DAC, or in the amplifier
// and the feedback path.
//
// Hann-windowed single-bin DFTs (Goertzel) at the two tones, their 3rd and
// 5th order products. The tones' sign (USB +f, LSB -f) is taken from the
// data. Products are relative to the mean tone power, in dBc.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>

namespace AetherSDR::anan {

struct TwoToneImd {
    bool valid = false;
    double toneDbfs = -200.0;   // mean tone level
    double imd3 = 0.0;          // dBc, worse of the two products
    double imd5 = 0.0;
    double balance = 0.0;       // tone2 - tone1, dB
    int side = 0;               // +1: tones at +f1/+f2, -1: at -f1/-f2
    double sideDb = 0.0;        // tone power on that side over the other, dB
};

// iq: interleaved I/Q doubles, n samples; fs Hz; f1/f2 audio Hz (positive).
inline TwoToneImd twoToneImd(const double* iq, std::size_t n, double fs, double f1, double f2)
{
    TwoToneImd r;
    if (!iq || n < 1024 || f1 <= 0.0 || f2 <= 0.0)
        return r;
    constexpr double kPi = 3.14159265358979323846;
    auto bin = [&](double f) {
        // Hann window (coherent gain 0.5 -> scale by 2/n), rotating phasor.
        const std::complex<double> step = std::polar(1.0, -2.0 * kPi * f / fs);
        std::complex<double> ph = 1.0, acc = 0.0;
        const double dw = 2.0 * kPi / double(n - 1);
        for (std::size_t i = 0; i < n; ++i) {
            const double win = 0.5 - 0.5 * std::cos(dw * double(i));
            acc += std::complex<double>(iq[2 * i], iq[2 * i + 1]) * (win * ph);
            ph *= step;
            if ((i & 1023) == 1023)
                ph /= std::abs(ph);   // keep the phasor on the unit circle
        }
        return std::norm(acc) * 4.0 / (double(n) * double(n));
    };
    const double pPos = bin(f1) + bin(f2);
    const double pNeg = bin(-f1) + bin(-f2);
    const double sgn = pPos >= pNeg ? 1.0 : -1.0;
    const double t1 = bin(sgn * f1), t2 = bin(sgn * f2);
    const double tone = 0.5 * (t1 + t2);
    if (tone <= 1e-20)
        return r;
    const double i3 = std::max(bin(sgn * (2.0 * f1 - f2)), bin(sgn * (2.0 * f2 - f1)));
    const double i5 = std::max(bin(sgn * (3.0 * f1 - 2.0 * f2)), bin(sgn * (3.0 * f2 - 2.0 * f1)));
    r.valid = true;
    r.side = sgn > 0 ? 1 : -1;
    r.sideDb = 10.0 * std::log10(std::max(std::max(pPos, pNeg), 1e-30) / std::max(std::min(pPos, pNeg), 1e-30));
    r.toneDbfs = 10.0 * std::log10(tone);
    r.imd3 = 10.0 * std::log10(std::max(i3, 1e-30) / tone);
    r.imd5 = 10.0 * std::log10(std::max(i5, 1e-30) / tone);
    r.balance = 10.0 * std::log10(std::max(t2, 1e-30) / std::max(t1, 1e-30));
    return r;
}

// The carrier (02.11: a line at the carrier in the Amp View spectrum, about
// -50 dBc, although the view removes the mean). dcDbc: the constant part
// (the complex mean); peakDbc / peakHz: the strongest line within +-150 Hz
// once the mean is removed (a carrier that is offset or moves). Both
// relative to one tone (tonePower = 10^(toneDbfs/10) from twoToneImd).
struct CarrierLine {
    bool valid = false;
    double dcDbc = -200.0;
    double peakDbc = -200.0;
    double peakHz = 0.0;
};

inline CarrierLine carrierLine(const double* iq, std::size_t n, double fs, double tonePower)
{
    CarrierLine r;
    if (!iq || n < 1024 || !(tonePower > 0.0))
        return r;
    constexpr double kPi = 3.14159265358979323846;
    // The mean weighted by the same Hann window: the plain mean of tones that
    // do not fit whole cycles into the snapshot is their leakage (~-50 dBc).
    std::complex<double> mean = 0.0;
    double wsum = 0.0;
    {
        const double dw = 2.0 * kPi / double(n - 1);
        for (std::size_t i = 0; i < n; ++i) {
            const double win = 0.5 - 0.5 * std::cos(dw * double(i));
            mean += std::complex<double>(iq[2 * i], iq[2 * i + 1]) * win;
            wsum += win;
        }
    }
    mean /= wsum;
    auto bin = [&](double f) {
        const std::complex<double> step = std::polar(1.0, -2.0 * kPi * f / fs);
        std::complex<double> ph = 1.0, acc = 0.0;
        const double dw = 2.0 * kPi / double(n - 1);
        for (std::size_t i = 0; i < n; ++i) {
            const double win = 0.5 - 0.5 * std::cos(dw * double(i));
            acc += (std::complex<double>(iq[2 * i], iq[2 * i + 1]) - mean) * (win * ph);
            ph *= step;
            if ((i & 1023) == 1023)
                ph /= std::abs(ph);
        }
        return std::norm(acc) * 4.0 / (double(n) * double(n));
    };
    r.valid = true;
    r.dcDbc = 10.0 * std::log10(std::max(std::norm(mean), 1e-30) / tonePower);
    const double stepHz = 0.5 * fs / double(n);   // half a bin
    double best = -1.0;
    for (double f = -150.0; f <= 150.0 + 1e-9; f += stepHz) {
        const double p = bin(f);
        if (p > best) {
            best = p;
            r.peakHz = f;
        }
    }
    r.peakDbc = 10.0 * std::log10(std::max(best, 1e-30) / tonePower);
    return r;
}

} // namespace AetherSDR::anan
