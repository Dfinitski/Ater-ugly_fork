#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace AetherSDR::hl2 {

// The HL2 panadapter path: accumulate raw IQ (normalized [-1, 1)) into fixed
// FFT frames and produce a DC-centered magnitude spectrum in dBFS. Ported from
// the live-validated tools/hl2/spectrum.py — Hanning-windowed, per-frame DC
// removal (the direct-sampling ADC offset sits on I), coherent-gain normalized,
// fftshifted so DC lands at the centre bin.
//
// Owns an FFTW plan; construction/destruction allocate, process() does not
// (fftw_execute is allocation-free). FFTW's global planner is not thread-safe,
// so construct instances off the real-time path (single-channel today).
class Hl2Spectrum {
public:
    // `sampleRateHz` is the IQ rate this object is fed at: it turns a count of
    // samples into time for setAverageTimeMs(). 0 (the default, for fixtures
    // that look at one frame) leaves time-based averaging inert.
    explicit Hl2Spectrum(int fftSize = 1024, double sampleRateHz = 0.0);
    ~Hl2Spectrum();
    Hl2Spectrum(const Hl2Spectrum&) = delete;
    Hl2Spectrum& operator=(const Hl2Spectrum&) = delete;

    [[nodiscard]] int fftSize() const noexcept { return m_fftSize; }

    // ANAN-100D port: the most recent frame BEFORE averaging (dBFS, the same
    // bins and order as process()'s output). The waterfall is drawn from this,
    // so FFT AVG changes the panadapter only.
    [[nodiscard]] const std::vector<float>& rawBins() const noexcept { return m_rawBins; }

    // A bin-centred steady tone of magnitude A reads 20*log10(A) plus this:
    // 20*log10(2), because the normalisation divides by half the window sum.
    static constexpr double kToneGainDb = 6.020599913279624;

    // A fixed depth in display frames (1 = none): EMA with alpha = 1/frames, for
    // fixtures. The operator's FFT AVG is a time (setAverageTimeMs()); setting
    // this clears it. Drops the state. Call on the hl2-io thread (unsynchronised).
    void setAverageFrames(int frames) noexcept;
    [[nodiscard]] int averageFrames() const noexcept { return m_averageFrames; }

    // The operator's FFT AVG as a time constant in ms (0 = none). Each frame blends
    // with alpha = 1 - exp(-dt/tau), dt = IQ samples since the last frame / rate
    // (WDSP's analyzer law): response time is fps-invariant, noise reduction is not.
    // A transport gap does not advance this clock, so the pre-gap estimate holds.
    // A change drops the state. Inert without a sample rate.
    void setAverageTimeMs(double tauMs) noexcept;
    [[nodiscard]] double averageTimeMs() const noexcept { return m_averageTimeMs; }

    // The blend weight one frame gets after dtSeconds, at time constant
    // tauSeconds. Exposed so the mapping is testable without an FFT; tau <= 0
    // means no averaging and returns 1 (the new frame replaces the state).
    [[nodiscard]] static double blendAlpha(double dtSeconds, double tauSeconds) noexcept;

    // Averaging domain. false (default) integrates power and takes the log at
    // emit; true blends dBFS (log-recursive, WDSP average mode 3), ~2.5 dB lower
    // on noise. The operator's weighted-average toggle selects it, as on the
    // ANAN. A change drops the state.
    void setLogAverage(bool on) noexcept;
    [[nodiscard]] bool logAverage() const noexcept { return m_logAverage; }

    // Forget the running average and take the next frame whole. Keeps the
    // partial frame, which a setting change leaves valid; a retune (NCO move)
    // also calls reset() to discard it.
    void dropAverage() noexcept;
    // ANAN-100D port (operator, 31.10: "after the over my own signal fades
    // slowly into the receiver with a long FFT average"): at the start of an
    // over remember the average from >= 150 ms before (the radio keys itself,
    // and the host hears of it ~100 ms late); after the over put it back, so
    // the trace continues from the band as it was -- no restart from one
    // frame (which is what a drop does), no fade of our own transmission.
    void markOverStart() noexcept;
    void restorePreOver() noexcept;
    // ANAN-100D port: the NCO moved by k bins (positive = up): move the running
    // average with it instead of dropping it, so a pan drag keeps a steady
    // trace. The window held for the next frame is kept too (one frame mixes
    // the two axes). Edges are filled from the nearest kept bin.
    void shiftBins(int k) noexcept;
    // ANAN fork build 335 (operator, 12.11: during a pan drag the noise floor
    // took the newly revealed edge, whose average had not built up yet). The
    // bins shiftBins() filled from the edge hold no averaged data until the
    // average has had time to build there (3 time constants, at least
    // 0.3 s): [lo, hi) in fftshifted bins, false when every bin is settled.
    [[nodiscard]] bool unsettledBins(int& lo, int& hi) const noexcept;
    // ANAN-100D port: carry another analyser's average (and its pending IQ)
    // into this one after an FFT-size change, so the trace and waterfall
    // continue instead of starting over. Same sample rate assumed; bins are
    // mapped by frequency, and power is rescaled by old/new size (per-bin
    // noise power goes as 1/N with this normalisation).
    void seedFrom(const Hl2Spectrum& other);

    // Append IQ samples; each time a full frame accumulates, compute one
    // spectrum. `binsDbfs` is resized to fftSize (DC at index fftSize/2) and
    // holds the most recent frame. Returns the number of frames produced (a
    // partial frame is carried to the next call).
    int process(std::span<const std::complex<float>> iq, std::vector<float>& binsDbfs);
    // Build 225 (a user: the panadapter's frequency resolution is too coarse):
    // sliding window. process() then produces at most one frame per call and
    // keeps the newest fftSize - 1 samples after it, so the next due frame
    // needs one new sample, not a whole new window: a large FFT (fine bins)
    // no longer lowers the frame rate to rate / fftSize. Off by default (the
    // fixtures count frames per window).
    void setSlidingWindow(bool on) noexcept { m_sliding = on; }

    // Append IQ WITHOUT transforming, keeping only the newest samples. Used
    // while the display-rate cap is between frames: the window keeps filling, so
    // when the next frame comes due it completes from recent contiguous samples
    // instead of refilling from empty.
    //
    // (An empty accumulator would cost ~9 EP6 blocks per frame, making frame
    // rate track the span.) Caps at fftSize - 1: process() fires on == fftSize
    // after a push_back, so a full buffer would step past it and never emit.
    void accumulate(std::span<const std::complex<float>> iq);

    // Drop the partial frame on a transport sequence gap, returning samples
    // discarded (0 = none): discard, not zero-fill, which renders as a transient.
    // Averaging state is kept (same spectrum). A retune wants the opposite
    // answer and calls this AND dropAverage(); buildChannel() rebuilds this
    // object on FFT size/rate change.
    std::size_t reset() noexcept
    {
        const std::size_t discarded = m_accCount;
        accClear();
        return discarded;
    }

private:
    void computeFrame(std::vector<float>& binsDbfs);
    bool m_sliding = false;   // build 225

    int m_fftSize;
    // ANAN fork build 308 (CPU): the accumulation window is a ring of
    // m_fftSize samples (oldest at m_accStart, m_accCount held). It was a
    // vector whose front was erased: with the sliding window that moved the
    // whole window (512 KB at 64 k points) on every IQ packet, ~1600 times a
    // second at 384 ksps, per receiver, on the I/O thread.
    std::vector<std::complex<float>> m_acc;   // ring storage, size m_fftSize
    std::size_t m_accStart = 0;
    std::size_t m_accCount = 0;
    void accClear() noexcept { m_accStart = 0; m_accCount = 0; }
    void accPush(std::complex<float> v) noexcept
    {
        const std::size_t cap = m_acc.size();
        if (m_accCount < cap) {
            m_acc[(m_accStart + m_accCount) % cap] = v;
            ++m_accCount;
        } else {   // full: the oldest goes
            m_acc[m_accStart] = v;
            m_accStart = (m_accStart + 1) % cap;
        }
    }
    void accDropOldest() noexcept
    {
        if (m_accCount == 0)
            return;
        m_accStart = (m_accStart + 1) % m_acc.size();
        --m_accCount;
    }
    [[nodiscard]] std::complex<float> accAt(std::size_t i) const noexcept
    {
        return m_acc[(m_accStart + i) % m_acc.size()];
    }
    std::vector<double> m_window;             // Hanning window
    double m_coherentGain = 1.0;              // sum(window) / 2
    // The square of it, precomputed. computeFrame() divides POWER by this
    // where it used to divide magnitude by m_coherentGain, and the two are the
    // same normalisation: (mag/g)^2 == (re^2 + im^2)/g^2.
    double m_coherentGainSq = 1.0;
    int m_averageFrames = 1;                  // 1 = no averaging (the default)
    // Time-constant averaging; see setAverageTimeMs(). 0 = off. Takes
    // precedence over m_averageFrames, and each setter clears the other.
    double m_sampleRateHz = 0.0;
    double m_averageTimeMs = 0.0;
    bool m_logAverage = false;                // see setLogAverage()
    // IQ samples received since the last frame was computed — the dt of the
    // time-constant blend. Counts accumulate()'s discards too: they are time
    // that passed, whether or not they reach a transform.
    std::uint64_t m_samplesSinceFrame = 0;
    // Per-bin EMA state, fftshifted like the emitted bins; power, or dBFS while
    // m_logAverage is set. Sized once at construction so nothing here allocates.
    // HL2-private on purpose (RFC #5782 q3): it moves to src/core/dsp/ when a
    // second consumer (RTL) needs it.
    std::vector<double> m_avgPower;
    // Whether m_avgPower holds a frame yet. An EMA seeded at zero would show
    // the operator roughly `frames` frames of an artificially low floor every
    // time averaging is switched on or the geometry changes, so the first
    // frame after a drop is TAKEN rather than blended into silence.
    bool m_haveAverage = false;
    // markOverStart()/restorePreOver(): averages taken every ~100 ms of IQ.
    struct AvgSnapshot {
        std::uint64_t atSample = 0;
        bool valid = false;
        bool log = false;
        std::vector<double> power;
    };
    std::uint64_t m_clockSamples = 0;
    std::uint64_t m_lastSnapAt = 0;
    AvgSnapshot m_snaps[3];
    int m_snapNext = 0;
    AvgSnapshot m_preOver;
    bool m_overActive = false;
    void clearSnapshots() noexcept;
    std::vector<float> m_rawBins;   // see rawBins()
    // Opaque FFTW handles (kept as void* so fftw3.h stays out of the header).
    void* m_in = nullptr;                     // fftw_complex[m_fftSize]
    void* m_out = nullptr;                     // fftw_complex[m_fftSize]
    void* m_plan = nullptr;                    // fftw_plan
    // Build 335: see unsettledBins().
    int m_freshLo = 0;
    int m_freshHi = 0;
    std::uint64_t m_freshUntil = 0;   // m_clockSamples at which they are settled
    // Build 362: the bins shiftBins() could only fill from the edge take the
    // next frame whole, [m_seedLo, m_seedHi) -- see shiftBins().
    int m_seedLo = 0;
    int m_seedHi = 0;
};

}  // namespace AetherSDR::hl2
