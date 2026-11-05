#pragma once

// ANAN-100D port: spectrum of the transmit IQ exactly as it goes to the radio.
//
// P2Transport feeds every DUC IQ packet through push() right before it is
// packed for port 1029, after the same [-1, 1] clamp buildDucIq() applies, so
// whatever reaches the DUC -- TUNE tone, host CW carrier, voice, FreeDV modem
// audio -- is what this measures. Nothing upstream (mic gain, ALC, PROC, the
// RF power slider) moves the scale: 0 dBFS is the full scale of the wire
// samples (24-bit on Protocol 2), i.e. a complex tone of |x| = 1.
//
// 4096-point FFT at 192 kHz (46.9 Hz bins), 4-term Blackman-Harris window
// normalised by its sum so a full-scale tone on a bin reads 0 dBFS. The DC bin
// is kept: the CW carrier and the suppressed SSB carrier sit there. Output is
// the centre +-12 kHz (513 bins, lowest RF first -- the wire's HPSDR
// handedness is mirrored back to RF), one frame per four
// FFTs at 50 % overlap (~23 frames per second), power-averaged over those four
// and lightly smoothed across frames. A wideband signal spreads its power over
// many bins, so voice reads below 0 dBFS per bin even when it peaks at full
// scale; a tone does not.
//
// Not thread-safe; P2Transport uses it from its own thread only.

#include <complex>
#include <vector>

namespace AetherSDR::anan {

class TxScope {
public:
    static constexpr int kFftSize = 4096;
    static constexpr int kHop = kFftSize / 2;
    static constexpr double kSampleRateHz = 192000.0;
    static constexpr int kHalfBins = 256;
    static constexpr int kBins = 2 * kHalfBins + 1;
    static constexpr double kHalfSpanHz = kHalfBins * kSampleRateHz / kFftSize;
    static constexpr int kFftsPerFrame = 4;
    static constexpr float kFloorDbfs = -200.0f;
    static_assert(kHalfSpanHz == 12000.0, "TX scope must span exactly +-12 kHz");

    TxScope();

    // Forget the history, e.g. at the start of a transmission.
    void reset() noexcept;

    // Feed n samples. Returns true when a new frame was written to out
    // (resized to kBins); at most one frame per call is reported -- a burst
    // long enough to complete two keeps the newer.
    bool push(const std::complex<float>* iq, int n, std::vector<float>& out);

private:
    void runFft();
    void finishFrame(std::vector<float>& out);

    std::vector<std::complex<float>> m_ring;   // last kFftSize samples
    int m_pos = 0;
    int m_filled = 0;
    int m_sinceFft = 0;
    int m_fftsInFrame = 0;

    std::vector<double> m_window;
    double m_windowSumSq = 1.0;
    std::vector<std::complex<double>> m_buf;
    std::vector<std::complex<double>> m_twiddle;
    std::vector<int> m_bitrev;

    std::vector<double> m_framePower;   // kBins, summed over this frame's FFTs
    std::vector<double> m_smoothed;     // kBins, across frames
    bool m_haveSmoothed = false;
};

} // namespace AetherSDR::anan
