#pragma once

// ANAN-100D port: spectrum of the transmit IQ exactly as it goes to the radio.
//
// P2Transport feeds every DUC IQ packet through push() right before it is
// packed for port 1029, after the same [-1, 1] clamp buildDucIq() applies, so
// whatever reaches the DUC -- TUNE tone, host CW carrier, voice, FreeDV modem
// audio -- is what this measures. Nothing upstream (mic gain, ALC, PROC, the
// RF power slider) moves the scale: 0 dBFS is the full scale of the wire
// samples (24-bit on Protocol 2), i.e. a complex tone of |x| = 1.
// Both protocols feed this from the same 192 ksps transmit path (Protocol 1
// decimates to its 48 ksps wire rate after it), so the span shown and the
// resolution are the same on both.
//
// ANAN fork build 346 (operator, 13.11: "the same resolution a receiver would
// have in such a view, and the receiver's FFT settings applied to transmit
// too; and a tool that shows how far the transmitter is driven"):
//
// * Resolution: the receive rule (Hl2Backend::spectrumFftSizeFor: about 4096
//   bins across the view) for the +-12 kHz shown gives 5.86 Hz bins. Got here
//   by decimating 192 -> 48 ksps (96-tap low-pass, flat to +-12 kHz, aliases
//   below -90 dB) and an 8192-point FFT -- the same bins as a 32768-point FFT
//   of the 192 ksps stream at a fraction of the work, which matters because
//   this runs on the transmit pacer's thread. 4097 bins, lowest RF first (the
//   wire's HPSDR handedness is mirrored back to RF). 4-term Blackman-Harris
//   window normalised by its sum: a full-scale tone on a bin reads 0 dBFS.
//   The DC bin is kept: the CW carrier and the suppressed SSB carrier sit
//   there. One frame per FFT at 75 % overlap, ~23 frames per second.
// * Averaging: the receiver's FFT AVG time constant (setAverageMs), power
//   domain, alpha = 1 - exp(-dt / tau) as Hl2Spectrum; 0 = none.
// * Envelope peak: the largest |x| sent since the previous frame, in dBFS,
//   appended to the frame as its LAST value (after the kBins of spectrum).
//   0 dBFS is the envelope of a full-scale tone -- what TUNE sends and the
//   most the ALC lets through; a wideband signal (voice, FreeDV) spreads its
//   power over many bins and reads far below 0 per bin, so the spectrum
//   cannot show how far the transmitter is driven -- this does.
//
// Not thread-safe; P2Transport uses it from its own thread only.

#include <complex>
#include <vector>

namespace AetherSDR::anan {

class TxScope {
public:
    static constexpr double kSampleRateHz = 192000.0;   // what push() is fed
    static constexpr int kDecimation = 4;
    static constexpr double kFftRateHz = kSampleRateHz / kDecimation;   // 48 ksps
    static constexpr int kFftSize = 8192;
    static constexpr int kHop = kFftSize / 4;           // decimated samples per frame
    static constexpr int kHalfBins = 2048;
    static constexpr int kBins = 2 * kHalfBins + 1;
    static constexpr double kHalfSpanHz = kHalfBins * kFftRateHz / kFftSize;
    static constexpr int kFirTaps = 96;
    static constexpr float kFloorDbfs = -200.0f;
    static_assert(kHalfSpanHz == 12000.0, "TX scope must span exactly +-12 kHz");

    TxScope();

    // Forget the history, e.g. at the start of a transmission.
    void reset() noexcept;

    // The receiver's FFT AVG time constant, ms; 0 = no averaging.
    void setAverageMs(double tauMs) noexcept { m_tauMs = tauMs > 0.0 ? tauMs : 0.0; }

    // Feed n samples. Returns true when a new frame was written to out:
    // kBins dBFS values and then the envelope peak in dBFS (kBins + 1 in
    // all). At most one frame per call is reported -- a burst long enough to
    // complete two keeps the newer.
    bool push(const std::complex<float>* iq, int n, std::vector<float>& out);

private:
    void runFft();
    void finishFrame(std::vector<float>& out);

    std::vector<float> m_fir;                  // kFirTaps, DC gain 1
    // The last kFirTaps input samples, each written twice (at p and
    // p + kFirTaps) so the window [p, p + kFirTaps) is always contiguous,
    // oldest first -- a plain loop the compiler vectorises.
    std::vector<float> m_firRe, m_firIm;       // 2 * kFirTaps each
    int m_firPos = 0;
    int m_firPhase = 0;

    std::vector<std::complex<float>> m_ring;   // last kFftSize decimated samples
    int m_pos = 0;
    int m_filled = 0;
    int m_sinceFft = 0;

    std::vector<double> m_window;
    double m_windowSumSq = 1.0;
    std::vector<std::complex<double>> m_buf;
    std::vector<std::complex<double>> m_twiddle;
    std::vector<int> m_bitrev;

    std::vector<double> m_framePower;   // kBins, this frame
    std::vector<double> m_smoothed;     // kBins, across frames
    bool m_haveSmoothed = false;
    double m_tauMs = 0.0;
    float m_peak = 0.0f;                // largest |x| since the last frame
};

} // namespace AetherSDR::anan
