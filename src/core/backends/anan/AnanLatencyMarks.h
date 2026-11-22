#pragma once

// ANAN fork build 613 (operator, 22.11: "the user measured 200 ms from the
// unkey to sound on the codec -- far too much, and it does not match our
// estimates, so we are missing something. Put markers in, rather than
// estimating the delays module by module").
//
// One timeline per over. Hl2Backend arms it at the host unkey (MOX off); each
// stage of the receive audio path stamps the FIRST time it passes real
// (non-silent) audio after that, with up to two numbers of its own (a depth,
// a queued time). The stamps are steady_clock microseconds, written with one
// compare-exchange, so any thread may mark (DSP, mixer, AudioEngine, I/O,
// speaker sender) and a stage that fires again is ignored. Hl2Backend prints
// one line two seconds after the unkey and disarms.
//
// "note" stages (MOX off sent, the radio's speaker FIFO status) keep their
// LAST time instead, and may lie before the unkey (negative in the report).

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace AetherSDR::anan {

class AnanLatencyMarks {
public:
    enum Stage : int {
        MoxOffSent = 0,  // note: P2 high-priority packet with MOX cleared sent
        SpkStatus,       // note: last radio status with the speaker FIFO level (a = frames)
        RxDspFlushed,    // RX DSP: over tail blanked, WDSP filters flushed
        RxDspSound,      // RX DSP (main receiver): first non-silent output block
        MixerLive,       // Hl2AudioMixer: first block to the PC path after the hold (any)
        MixerSound,      // Hl2AudioMixer: first non-silent block to the PC path
        CodecMixSound,   // I/O thread: first non-silent raw receiver mix (codec mixer)
        EngineSound,     // AudioEngine: first non-silent post-chain block (a = queued ms ahead, b = device ms)
        FeedPush,        // AnanCodecFeed: first non-silent push (a = depth frames before)
        FeedPrimed,      // AnanCodecFeed: primed again after the over (a = depth frames)
        FeedTake,        // AnanCodecFeed: first non-silent frame taken (a = depth frames)
        SpkSent,         // P2 speaker sender: first non-silent packet sent (a = host queue frames)
        kCount
    };

    static AnanLatencyMarks& instance()
    {
        static AnanLatencyMarks marks;
        return marks;
    }

    static std::int64_t nowUs()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // Host unkey: a new timeline (the note stages keep their last values).
    void arm()
    {
        for (int i = 0; i < kCount; ++i) {
            if (isNote(i))
                continue;
            m_t[static_cast<std::size_t>(i)].store(kUnset, std::memory_order_relaxed);
            m_a[static_cast<std::size_t>(i)].store(0, std::memory_order_relaxed);
            m_b[static_cast<std::size_t>(i)].store(0, std::memory_order_relaxed);
        }
        m_t0.store(nowUs(), std::memory_order_relaxed);
        m_armed.store(true, std::memory_order_release);
    }
    void disarm() { m_armed.store(false, std::memory_order_release); }
    bool armed() const { return m_armed.load(std::memory_order_acquire); }
    std::int64_t t0() const { return m_t0.load(std::memory_order_relaxed); }

    // First time after arm(); later calls are ignored.
    void mark(Stage s, int a = 0, int b = 0)
    {
        if (!armed())
            return;
        auto& t = m_t[static_cast<std::size_t>(s)];
        std::int64_t expect = kUnset;
        if (t.compare_exchange_strong(expect, nowUs(), std::memory_order_acq_rel)) {
            m_a[static_cast<std::size_t>(s)].store(a, std::memory_order_relaxed);
            m_b[static_cast<std::size_t>(s)].store(b, std::memory_order_relaxed);
        }
    }
    // Last time, armed or not.
    void note(Stage s, int a = 0, int b = 0)
    {
        m_a[static_cast<std::size_t>(s)].store(a, std::memory_order_relaxed);
        m_b[static_cast<std::size_t>(s)].store(b, std::memory_order_relaxed);
        m_t[static_cast<std::size_t>(s)].store(nowUs(), std::memory_order_release);
    }
    // Cheap test before scanning a block: is this stage still waiting?
    bool waiting(Stage s) const
    {
        return armed() && m_t[static_cast<std::size_t>(s)].load(std::memory_order_relaxed) == kUnset;
    }

    // ms from the unkey, or NaN when the stage did not fire.
    double ms(Stage s) const
    {
        const std::int64_t t = m_t[static_cast<std::size_t>(s)].load(std::memory_order_acquire);
        if (t == kUnset)
            return std::nan("");
        return static_cast<double>(t - t0()) / 1000.0;
    }
    int a(Stage s) const { return m_a[static_cast<std::size_t>(s)].load(std::memory_order_relaxed); }
    int b(Stage s) const { return m_b[static_cast<std::size_t>(s)].load(std::memory_order_relaxed); }

    // Real audio: anything above -80 dBFS.
    static constexpr float kLoud = 1e-4f;
    static bool loud(const float* p, std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
            if (std::abs(p[i]) > kLoud)
                return true;
        return false;
    }
    static bool loud(const std::int16_t* p, std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
            if (p[i] > 3 || p[i] < -3)
                return true;
        return false;
    }

private:
    static constexpr std::int64_t kUnset = INT64_MIN;
    static bool isNote(int i) { return i == MoxOffSent || i == SpkStatus; }

    AnanLatencyMarks()
    {
        for (auto& t : m_t)
            t.store(kUnset, std::memory_order_relaxed);
    }

    std::atomic<bool> m_armed{false};
    std::atomic<std::int64_t> m_t0{0};
    std::array<std::atomic<std::int64_t>, kCount> m_t{};
    std::array<std::atomic<int>, kCount> m_a{};
    std::array<std::atomic<int>, kCount> m_b{};
};

} // namespace AetherSDR::anan
