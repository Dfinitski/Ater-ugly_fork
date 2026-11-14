#include "core/backends/hl2/Hl2Spectrum.h"

#include <fftw3.h>

#include "core/dsp/WdspChannel.h"

#include <algorithm>
#include <cmath>

namespace AetherSDR::hl2 {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

Hl2Spectrum::Hl2Spectrum(int fftSize, double sampleRateHz)
    : m_fftSize(fftSize < 2 ? 2 : fftSize),
      m_sampleRateHz(sampleRateHz > 0.0 ? sampleRateHz : 0.0)
{
    m_acc.assign(static_cast<std::size_t>(m_fftSize), {});   // build 308: the ring
    m_window.resize(static_cast<std::size_t>(m_fftSize));
    double sum = 0.0;
    for (int n = 0; n < m_fftSize; ++n) {
        m_window[static_cast<std::size_t>(n)] =
            0.5 * (1.0 - std::cos(2.0 * kPi * n / (m_fftSize - 1)));   // Hanning
        sum += m_window[static_cast<std::size_t>(n)];
    }
    m_coherentGain = sum / 2.0;
    // Guard the per-bin normalization divisor (used in process() below). A
    // degenerate window sums to 0 — e.g. a length-2 Hanning, whose two endpoints
    // are both 0 — which would make the magnitude divide produce inf. Unreachable
    // at the production fftSize (1024) but cheap to make safe; with an all-zero
    // window the input is zeroed anyway, so the bins come out 0 rather than inf.
    if (m_coherentGain < 1e-9)
        m_coherentGain = 1.0;
    // Squared once here because computeFrame() works in power: dividing
    // (re^2 + im^2) by this is the same normalisation as dividing the
    // magnitude by m_coherentGain, without the square root in between.
    m_coherentGainSq = m_coherentGain * m_coherentGain;
    // Sized now, never resized later: process() is documented allocation-free
    // and setAverageFrames() must not allocate either, since the alternative
    // is a first-frame malloc on whichever thread happens to change the depth.
    m_avgPower.assign(static_cast<std::size_t>(m_fftSize), 0.0);

    {
        // FFTW's planner is process-global and not thread-safe, and this runs on
        // the beginDspSetup() worker while WdspChannel::open() plans and
        // allocates through FFTW on another thread (#5275). The lock covers the
        // allocations too: TSan names fftw_malloc_plain vs free (firmin.c), not
        // only the planner. execute() stays unguarded, as in processIq().
        auto lock = WdspChannel::fftwSetupLock();
        m_in = fftw_malloc(sizeof(fftw_complex) * static_cast<std::size_t>(m_fftSize));
        m_out = fftw_malloc(sizeof(fftw_complex) * static_cast<std::size_t>(m_fftSize));
        m_plan = fftw_plan_dft_1d(m_fftSize, static_cast<fftw_complex*>(m_in),
                                  static_cast<fftw_complex*>(m_out), FFTW_FORWARD, FFTW_ESTIMATE);
    }
}

Hl2Spectrum::~Hl2Spectrum()
{
    // Same lock as the constructor, and over the frees for the same reason: the
    // race TSan reported was a free on one thread against an allocation on
    // another, so guarding only destroy_plan would leave the teardown half of
    // that edge open.
    auto lock = WdspChannel::fftwSetupLock();
    if (m_plan) fftw_destroy_plan(static_cast<fftw_plan>(m_plan));
    if (m_in) fftw_free(m_in);
    if (m_out) fftw_free(m_out);
}

int Hl2Spectrum::process(std::span<const std::complex<float>> iq, std::vector<float>& binsDbfs)
{
    int frames = 0;
    for (std::size_t i = 0; i < iq.size(); ++i) {
        accPush(iq[i]);
        ++m_samplesSinceFrame;
        ++m_clockSamples;
        if (static_cast<int>(m_accCount) == m_fftSize) {
            computeFrame(binsDbfs);
            ++frames;
            if (m_sliding) {
                // Build 225: keep the window minus its oldest sample; the rest
                // of this block only slides it (accumulate()).
                accDropOldest();
                accumulate(iq.subspan(i + 1));
                return frames;
            }
            accClear();
        }
    }
    return frames;
}

void Hl2Spectrum::setAverageFrames(int frames) noexcept
{
    if (frames < 1) {
        frames = 1;
    }
    // A re-applied identical depth (settings replay) must not clear the
    // accumulator. The operator's averaging survives a zoom through
    // setAverageTimeMs(), which Hl2RxDsp re-applies in installChannel(), not
    // through this.
    if (frames == m_averageFrames && m_averageTimeMs == 0.0) {
        return;
    }
    m_averageFrames = frames;
    m_averageTimeMs = 0.0;
    // An exponential state built at one alpha is not a state at the next one.
    dropAverage();
}

void Hl2Spectrum::setAverageTimeMs(double tauMs) noexcept
{
    if (!(tauMs > 0.0)) {   // also catches NaN
        tauMs = 0.0;
    }
    // Same no-op rule as setAverageFrames(): a replay of the value already in
    // force must not throw the average away.
    if (tauMs == m_averageTimeMs && m_averageFrames == 1) {
        return;
    }
    m_averageTimeMs = tauMs;
    m_averageFrames = 1;
    dropAverage();
}

void Hl2Spectrum::setLogAverage(bool on) noexcept
{
    if (on == m_logAverage) {
        return;
    }
    m_logAverage = on;
    // The state is in the OTHER domain now; blending into it would mix dB and
    // power in one number.
    dropAverage();
}

void Hl2Spectrum::shiftBins(int k) noexcept
{
    clearSnapshots();   // another axis
    if (k == 0 || !m_haveAverage)
        return;
    const int n = m_fftSize;
    if (k >= n || k <= -n) {
        dropAverage();
        return;
    }
    // Build 335: the unsettled range moves with the bins and grows by the
    // edge filled now; it settles after 3 time constants (at least 0.3 s).
    {
        const bool active = m_freshHi > m_freshLo && m_clockSamples < m_freshUntil;
        int lo = active ? std::clamp(m_freshLo - k, 0, n) : n;
        int hi = active ? std::clamp(m_freshHi - k, 0, n) : 0;
        const int eLo = k > 0 ? n - k : 0;
        const int eHi = k > 0 ? n : -k;
        m_freshLo = hi > lo ? std::min(lo, eLo) : eLo;
        m_freshHi = hi > lo ? std::max(hi, eHi) : eHi;
        const double rate = m_sampleRateHz > 0.0 ? m_sampleRateHz : 192000.0;
        double settleS = 0.3;
        if (m_averageTimeMs > 0.0)
            settleS = std::max(settleS, 3.0 * m_averageTimeMs / 1000.0);
        else if (m_averageFrames > 1)
            settleS = std::max(settleS, 3.0 * m_averageFrames * m_fftSize / rate);
        m_freshUntil = m_clockSamples + static_cast<std::uint64_t>(rate * settleS);
        // ANAN fork build 362 (operator, 13.11: "on a fast pan drag the floor
        // and the spectrum jump up ~15 dB and stay there"; the build 359 log
        // showed the frames: after a move the 20th percentile of the view fell
        // 9 dB while a third of it was newly revealed). The revealed bins were
        // filled with the band-edge bin -- deep in the DDC filter's stopband --
        // and then BLENDED up from there: a hole in the trace for several
        // time constants, in the log average longest of all. The panadapter's
        // Auto Floor took the hole for a lower noise floor and moved its scale
        // down at once (everything on screen jumps up), and moves back only
        // slowly. They hold no average, so they are treated as bins without
        // one: the next frame is taken whole there (as after dropAverage()),
        // and averaging goes on from real data.
        const bool seeded = m_seedHi > m_seedLo;
        const int sLo = seeded ? std::clamp(m_seedLo - k, 0, n) : n;
        const int sHi = seeded ? std::clamp(m_seedHi - k, 0, n) : 0;
        m_seedLo = sHi > sLo ? std::min(sLo, eLo) : eLo;
        m_seedHi = sHi > sLo ? std::max(sHi, eHi) : eHi;
    }
    // A signal at a fixed RF frequency moves DOWN k bins when the NCO goes up
    // k bins: new[i] = old[i + k].
    if (k > 0) {
        for (int i = 0; i < n - k; ++i)
            m_avgPower[static_cast<std::size_t>(i)] = m_avgPower[static_cast<std::size_t>(i + k)];
        const double edge = m_avgPower[static_cast<std::size_t>(n - k - 1)];
        for (int i = n - k; i < n; ++i)
            m_avgPower[static_cast<std::size_t>(i)] = edge;
    } else {
        const int m = -k;
        for (int i = n - 1; i >= m; --i)
            m_avgPower[static_cast<std::size_t>(i)] = m_avgPower[static_cast<std::size_t>(i - m)];
        const double edge = m_avgPower[static_cast<std::size_t>(m)];
        for (int i = 0; i < m; ++i)
            m_avgPower[static_cast<std::size_t>(i)] = edge;
    }
}

void Hl2Spectrum::seedFrom(const Hl2Spectrum& other)
{
    const int on = other.m_fftSize;
    const int nn = m_fftSize;
    if (other.m_haveAverage && on > 0 && other.m_logAverage == m_logAverage) {
        const double scale = static_cast<double>(on) / static_cast<double>(nn);
        const double scaleDb = 10.0 * std::log10(scale);
        for (int k = 0; k < nn; ++k) {
            // Both are fftshifted (DC at n/2): map by frequency offset.
            const double f = (static_cast<double>(k) - nn / 2) * scale;
            int src = static_cast<int>(std::lround(f + on / 2));
            src = std::clamp(src, 0, on - 1);
            const double v = other.m_avgPower[static_cast<std::size_t>(src)];
            m_avgPower[static_cast<std::size_t>(k)] = m_logAverage ? v + scaleDb : v * scale;
        }
        m_haveAverage = true;
    }
    // The newest IQ the old analyser had not yet framed.
    const std::size_t keep = std::min(other.m_accCount, static_cast<std::size_t>(nn - 1));
    accClear();
    for (std::size_t i = other.m_accCount - keep; i < other.m_accCount; ++i)
        accPush(other.accAt(i));
    m_samplesSinceFrame = other.m_samplesSinceFrame;
}

bool Hl2Spectrum::unsettledBins(int& lo, int& hi) const noexcept
{
    if (m_freshHi <= m_freshLo || m_clockSamples >= m_freshUntil)
        return false;
    lo = m_freshLo;
    hi = m_freshHi;
    return true;
}

void Hl2Spectrum::dropAverage() noexcept
{
    m_freshLo = m_freshHi = 0;   // build 335: nothing averaged, nothing unsettled
    m_seedLo = m_seedHi = 0;     // build 362
    // Assign rather than resize — the vector was sized at construction, so
    // this touches no allocator.
    m_avgPower.assign(static_cast<std::size_t>(m_fftSize), 0.0);
    m_haveAverage = false;
    clearSnapshots();
}

void Hl2Spectrum::clearSnapshots() noexcept
{
    for (auto& s : m_snaps)
        s.valid = false;
}

void Hl2Spectrum::markOverStart() noexcept
{
    m_overActive = true;
    m_preOver.valid = false;
    const double rate = m_sampleRateHz > 0.0 ? m_sampleRateHz : 192000.0;
    const std::uint64_t minAge = static_cast<std::uint64_t>(rate * 0.15);
    const AvgSnapshot* best = nullptr;
    for (const auto& s : m_snaps) {
        if (!s.valid || m_clockSamples < s.atSample + minAge)
            continue;
        if (!best || s.atSample > best->atSample)
            best = &s;
    }
    if (best)
        m_preOver = *best;
}

void Hl2Spectrum::restorePreOver() noexcept
{
    if (!m_overActive)
        return;
    m_overActive = false;
    accClear();   // the partial window may still hold the over
    if (m_preOver.valid && m_preOver.log == m_logAverage
        && m_preOver.power.size() == m_avgPower.size()) {
        m_avgPower = m_preOver.power;
        m_haveAverage = true;
    } else {
        // Nothing usable from before the over (an average just switched
        // on, a retune): start from the next frame instead.
        m_avgPower.assign(static_cast<std::size_t>(m_fftSize), 0.0);
        m_haveAverage = false;
    }
    m_preOver.valid = false;
    clearSnapshots();   // they hold the over now
    m_lastSnapAt = m_clockSamples;
}

double Hl2Spectrum::blendAlpha(double dtSeconds, double tauSeconds) noexcept
{
    if (!(tauSeconds > 0.0)) {
        return 1.0;
    }
    if (!(dtSeconds > 0.0)) {
        return 0.0;
    }
    // -expm1(-x) is 1 - exp(-x) without the cancellation at small x, which is
    // exactly the regime of a long average at a high frame rate.
    return -std::expm1(-dtSeconds / tauSeconds);
}

void Hl2Spectrum::accumulate(std::span<const std::complex<float>> iq)
{
    // Time passes whether or not these samples survive the cap below.
    m_samplesSinceFrame += iq.size();
    m_clockSamples += iq.size();
    // Hold at most fftSize - 1: see the header for why exactly-full would wedge
    // process()'s boundary check. Build 308: in the ring, each sample past
    // that replaces the oldest.
    const std::size_t keep = static_cast<std::size_t>(m_fftSize) - 1;
    const std::size_t skip = iq.size() > keep ? iq.size() - keep : 0;
    for (std::size_t i = skip; i < iq.size(); ++i) {
        if (m_accCount >= keep)
            accDropOldest();
        accPush(iq[i]);
    }
}

void Hl2Spectrum::computeFrame(std::vector<float>& binsDbfs)
{
    // Remove the frame's DC offset (the ADC offset lives on I in direct
    // sampling), then apply the window.
    double meanRe = 0.0, meanIm = 0.0;
    for (std::size_t i = 0; i < m_accCount; ++i) {
        const auto s = accAt(i);
        meanRe += s.real();
        meanIm += s.imag();
    }
    meanRe /= m_fftSize;
    meanIm /= m_fftSize;

    auto* in = static_cast<fftw_complex*>(m_in);
    for (int n = 0; n < m_fftSize; ++n) {
        const double w = m_window[static_cast<std::size_t>(n)];
        const auto s = accAt(static_cast<std::size_t>(n));
        in[n][0] = (static_cast<double>(s.real()) - meanRe) * w;
        in[n][1] = (static_cast<double>(s.imag()) - meanIm) * w;
    }

    fftw_execute(static_cast<fftw_plan>(m_plan));

    const auto* out = static_cast<fftw_complex*>(m_out);
    binsDbfs.resize(static_cast<std::size_t>(m_fftSize));
    m_rawBins.resize(static_cast<std::size_t>(m_fftSize));
    const int half = m_fftSize / 2;
    // Two ways to be averaging: a time constant (the operator's control,
    // alpha from the elapsed IQ time) or a fixed frame depth (alpha = 1/N,
    // fixtures). Only one is ever set; see the setters.
    const bool timed = m_averageTimeMs > 0.0 && m_sampleRateHz > 0.0;
    const bool blending = timed || m_averageFrames > 1;
    double alpha = 1.0;
    if (timed) {
        const double dt = static_cast<double>(m_samplesSinceFrame) / m_sampleRateHz;
        alpha = blendAlpha(dt, m_averageTimeMs / 1000.0);
    } else if (blending) {
        alpha = 1.0 / static_cast<double>(m_averageFrames);
    }
    m_samplesSinceFrame = 0;
    const bool logDomain = blending && m_logAverage;
    // ANAN fork build 308 (CPU): this loop runs on the I/O thread with the
    // receivers' DSP, up to 64 k bins a frame per receiver. The logarithms
    // are float (a dB value needs no more), the raw frame's is reused when
    // nothing is averaged, and the fftshift index needs no modulo.
    const auto toDb = [](double power) {
        return 10.0f * std::log10(static_cast<float>(power) + 1e-30f);
    };
    for (int k = 0; k < m_fftSize; ++k) {
        const int src = k < half ? k + half : k - half;                // fftshift: DC -> centre
        const double re = out[src][0];
        const double im = out[src][1];
        // POWER, not magnitude. The square root this replaces was only ever
        // undone by the logarithm below — 20*log10(mag) IS 10*log10(mag^2) —
        // so for a single frame this is the same number by the same route.
        // What it buys is that anything accumulated across frames is
        // accumulated in the domain where an average means what the operator
        // reads it to mean; see setAverageFrames() in the header for what an
        // average of logarithms actually computes.
        double power = (re * re + im * im) / m_coherentGainSq;
        const float rawDb = toDb(power);
        m_rawBins[static_cast<std::size_t>(k)] = rawDb;
        if (logDomain) {
            // Log-recursive, by the operator's choice (setLogAverage): the
            // state is dBFS and the blend is of logarithms. Emitted as-is.
            const double db = rawDb;
            double& state = m_avgPower[static_cast<std::size_t>(k)];
            const bool held = m_haveAverage && !(k >= m_seedLo && k < m_seedHi);   // build 362
            state = held ? state + alpha * (db - state) : db;
            binsDbfs[static_cast<std::size_t>(k)] = static_cast<float>(state);
            continue;
        }
        if (!blending) {
            binsDbfs[static_cast<std::size_t>(k)] = rawDb;   // the same number
            continue;
        }
        {
            double& state = m_avgPower[static_cast<std::size_t>(k)];
            // Take the first frame whole rather than blending it into a zero
            // state, which would open every average with a ramp from the
            // floor. After that it is an ordinary exponential blend.
            const bool held = m_haveAverage && !(k >= m_seedLo && k < m_seedHi);   // build 362
            state = held ? state + alpha * (power - state) : power;
            power = state;
        }
        // IQ is normalized to full scale 1.0, so this is dBFS directly. The
        // power floor keeps log10 finite (float: 1e-30, an exactly-zero bin
        // reads -300 dBFS).
        binsDbfs[static_cast<std::size_t>(k)] = toDb(power);
    }
    // Set after the loop and only while blending, so a pass with averaging off
    // cannot leave a stale "we have a state" behind for the next one to blend
    // into. setAverageFrames() clears it on every depth change.
    m_seedLo = m_seedHi = 0;   // build 362: taken (or nothing is averaged)
    if (blending) {
        m_haveAverage = true;
        const double rate = m_sampleRateHz > 0.0 ? m_sampleRateHz : 192000.0;
        if (m_clockSamples >= m_lastSnapAt + static_cast<std::uint64_t>(rate * 0.1)) {
            AvgSnapshot& s = m_snaps[m_snapNext];
            s.power = m_avgPower;
            s.atSample = m_clockSamples;
            s.log = m_logAverage;
            s.valid = true;
            m_snapNext = (m_snapNext + 1) % 3;
            m_lastSnapAt = m_clockSamples;
        }
    }
}

}  // namespace AetherSDR::hl2
