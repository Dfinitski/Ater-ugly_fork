#pragma once

// ANAN fork build 628 (operator, 22.11: "since we cut the latency, a retune
// with a fairly large step plays a different sound at once and then a short
// dropout, every time -- a critical error, find it"). Markers for a retune:
// Hl2Backend arms it when receiver 1's NCO register or shift changes; for
// 400 ms each stage records what it saw -- the DSP thread's longest stall and
// slowest setShift, the NCO-move dip, the WDSP output per block, the mixer's
// longest gap, the PC sink's underruns and least queued audio, the codec
// feed's underruns and least depth. Hl2Backend prints one line at 400 ms.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>

namespace AetherSDR::anan {

class AnanRetuneTrace {
public:
    static AnanRetuneTrace& instance()
    {
        static AnanRetuneTrace t;
        return t;
    }
    static std::int64_t nowUs()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    static constexpr std::int64_t kWindowUs = 400000;
    static constexpr int kProfLen = 40;

    void arm()
    {
        m_shiftMaxUs.store(0, std::memory_order_relaxed);
        m_shiftAtUs.store(-1, std::memory_order_relaxed);
        m_ncoAtUs.store(-1, std::memory_order_relaxed);
        m_dipStartUs.store(-1, std::memory_order_relaxed);
        m_dipEndUs.store(-1, std::memory_order_relaxed);
        m_dspGapMaxUs.store(0, std::memory_order_relaxed);
        m_dspCallMaxUs.store(0, std::memory_order_relaxed);
        m_mixGapMaxUs.store(0, std::memory_order_relaxed);
        m_pcUnderFirst.store(-1, std::memory_order_relaxed);
        m_pcUnderLast.store(-1, std::memory_order_relaxed);
        m_pcQueuedMin.store(INT_MAX, std::memory_order_relaxed);
        m_feedUnderFirst.store(-1, std::memory_order_relaxed);
        m_feedUnderLast.store(-1, std::memory_order_relaxed);
        m_feedDepthMin.store(INT_MAX, std::memory_order_relaxed);
        m_outN.store(0, std::memory_order_relaxed);
        m_t0.store(nowUs(), std::memory_order_release);
    }
    bool active() const
    {
        const std::int64_t t0 = m_t0.load(std::memory_order_acquire);
        return t0 > 0 && nowUs() - t0 < kWindowUs;
    }
    std::int64_t t0() const { return m_t0.load(std::memory_order_relaxed); }
    double relMs(std::int64_t at) const { return at < 0 ? -1.0 : (at - t0()) / 1000.0; }

    // DSP thread
    void shiftCall(int us)
    {
        if (!active()) return;
        if (m_shiftAtUs.load(std::memory_order_relaxed) < 0)
            m_shiftAtUs.store(nowUs(), std::memory_order_relaxed);
        maxStore(m_shiftMaxUs, us);
    }
    void ncoMoved() { if (active()) m_ncoAtUs.store(nowUs(), std::memory_order_relaxed); }
    void dipStart() { if (active()) m_dipStartUs.store(nowUs(), std::memory_order_relaxed); }
    void dipEnd() { if (active() && m_dipEndUs.load() < 0) m_dipEndUs.store(nowUs(), std::memory_order_relaxed); }
    void dspCall(int gapUs, int callUs)
    {
        if (!active()) return;
        maxStore(m_dspGapMaxUs, gapUs);
        maxStore(m_dspCallMaxUs, callUs);
    }
    void outBlock(float peak)
    {
        if (!active()) return;
        const int i = m_outN.load(std::memory_order_relaxed);
        if (i >= kProfLen) return;
        m_outPk[static_cast<std::size_t>(i)].store(peak, std::memory_order_relaxed);
        m_outN.store(i + 1, std::memory_order_release);
    }
    // Mixer
    void mixerGap(int us) { if (active()) maxStore(m_mixGapMaxUs, us); }
    // AudioEngine (its 10 ms tick)
    void pcSink(int underruns, int queuedMs)
    {
        if (!active()) return;
        if (m_pcUnderFirst.load(std::memory_order_relaxed) < 0)
            m_pcUnderFirst.store(underruns, std::memory_order_relaxed);
        m_pcUnderLast.store(underruns, std::memory_order_relaxed);
        minStore(m_pcQueuedMin, queuedMs);
    }
    // Codec feed (take)
    void feed(int underruns, int depthFrames)
    {
        if (!active()) return;
        if (m_feedUnderFirst.load(std::memory_order_relaxed) < 0)
            m_feedUnderFirst.store(underruns, std::memory_order_relaxed);
        m_feedUnderLast.store(underruns, std::memory_order_relaxed);
        minStore(m_feedDepthMin, depthFrames);
    }

    // Report
    int shiftMaxUs() const { return m_shiftMaxUs.load(); }
    std::int64_t shiftAtUs() const { return m_shiftAtUs.load(); }
    std::int64_t ncoAtUs() const { return m_ncoAtUs.load(); }
    std::int64_t dipStartUs() const { return m_dipStartUs.load(); }
    std::int64_t dipEndUs() const { return m_dipEndUs.load(); }
    int dspGapMaxUs() const { return m_dspGapMaxUs.load(); }
    int dspCallMaxUs() const { return m_dspCallMaxUs.load(); }
    int mixGapMaxUs() const { return m_mixGapMaxUs.load(); }
    int pcUnderDelta() const
    {
        const int a = m_pcUnderFirst.load(), b = m_pcUnderLast.load();
        return a < 0 ? -1 : b - a;
    }
    int pcQueuedMin() const { const int v = m_pcQueuedMin.load(); return v == INT_MAX ? -1 : v; }
    int feedUnderDelta() const
    {
        const int a = m_feedUnderFirst.load(), b = m_feedUnderLast.load();
        return a < 0 ? -1 : b - a;
    }
    int feedDepthMin() const { const int v = m_feedDepthMin.load(); return v == INT_MAX ? -1 : v; }
    int outCount() const { return m_outN.load(std::memory_order_acquire); }
    float outPeak(int i) const { return m_outPk[static_cast<std::size_t>(i)].load(); }

private:
    static void maxStore(std::atomic<int>& a, int v)
    {
        int cur = a.load(std::memory_order_relaxed);
        while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    static void minStore(std::atomic<int>& a, int v)
    {
        int cur = a.load(std::memory_order_relaxed);
        while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }

    std::atomic<std::int64_t> m_t0{0};
    std::atomic<int> m_shiftMaxUs{0};
    std::atomic<std::int64_t> m_shiftAtUs{-1}, m_ncoAtUs{-1}, m_dipStartUs{-1}, m_dipEndUs{-1};
    std::atomic<int> m_dspGapMaxUs{0}, m_dspCallMaxUs{0}, m_mixGapMaxUs{0};
    std::atomic<int> m_pcUnderFirst{-1}, m_pcUnderLast{-1}, m_pcQueuedMin{INT_MAX};
    std::atomic<int> m_feedUnderFirst{-1}, m_feedUnderLast{-1}, m_feedDepthMin{INT_MAX};
    std::array<std::atomic<float>, kProfLen> m_outPk{};
    std::atomic<int> m_outN{0};
};

} // namespace AetherSDR::anan
