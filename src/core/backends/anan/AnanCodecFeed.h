#pragma once

// ANAN-100D port (operator, 31.10: "AetherRX must reach the codec too; no
// point in separate parameters for different audio outputs").
//
// The radio's own speaker (codec, port 1028) plays the SAME audio as the PC
// speaker: AudioEngine's receive output AFTER the whole client RX chain
// (AetherRX: NR, gate, EQ, compressor, tube, exciter, boost, trim) and BEFORE
// the PC volume, which is a device attenuation. AudioEngine (audio thread)
// pushes it here at its output rate; Hl2Backend's codec mixer (radio I/O
// thread, radio clock) takes it at 48 kHz.
//
// Only ONE clock meets here: AudioEngine processes the receive audio as it
// arrives (pushed, not pulled by the sound card) and resamples it by a fixed
// ratio, so what it pushes runs at the radio's rate. The former drift
// correction (drop / repeat a single frame whenever the depth left 25..90 ms)
// therefore only reacted to delivery jitter through the GUI thread -- each
// correction a tiny click, together a rustle (operator, 01.11). Now: no
// per-frame correction; the depth is trimmed only if it grows past 250 ms,
// and an underrun (the GUI thread stalled) fades to silence and primes again.
//
// The path is optional (AppSettings "AnanCodecAetherRx"): off, the codec plays
// the I/O-thread mix on the radio's clock. Default on since build 567.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>

#include "core/backends/anan/AnanLatencyMarks.h"
#include "core/backends/anan/AnanRetuneTrace.h"

namespace AetherSDR::anan {

class AnanCodecFeed {
public:
    static AnanCodecFeed& instance()
    {
        static AnanCodecFeed feed;
        return feed;
    }

    static constexpr std::size_t kRate = 48000;
    // ANAN fork build 600 (operator, 21.11: "on the device speaker the delay
    // is large too and too noticeable"). The 100 ms prime / 250 ms trim were
    // sized for the former GUI-thread delivery; AudioEngine now pushes from
    // its 10 ms precise drain timer, and the mixer's longest gap measured is
    // 11 ms (log 21.11 22:12, "longest gap 11 ms"). 40 ms is four pushes of
    // cushion; a backlog past 100 ms (a stall that came back in a burst) is
    // trimmed back to it. The radio's own 2048-frame FIFO (~42 ms) comes after
    // this one and is the firmware's. depth/underruns go to the ANAN diag.
    // ANAN fork build 614 (operator, 22.11, after the 613 unkey timeline:
    // the feed held 2000..3000 frames, 40..60 ms, on receive): the prime is
    // 15 ms -- the longest push gap measured is 11 ms. Underruns are counted
    // in the ANAN diag; if they appear, this is the number to raise.
    static constexpr std::size_t kTargetFrames = 720;    // 15 ms prime
    static constexpr std::size_t kHighFrames = 4800;     // 100 ms: trim back to the target
    static constexpr std::size_t kMaxFrames = 24000;     // 500 ms cap

    // Hl2Backend: the codec mixer is running (else AudioEngine skips the copy).
    void setWanted(bool on) { m_wanted.store(on, std::memory_order_relaxed); }
    bool wanted() const { return m_wanted.load(std::memory_order_relaxed); }

    // AudioEngine, audio thread: interleaved stereo float at `rate`.
    void push(const float* stereo, std::size_t frames, int rate)
    {
        if (!stereo || frames == 0 || rate <= 0)
            return;
        const double step = static_cast<double>(rate) / static_cast<double>(kRate);
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool loudIn = AnanLatencyMarks::above(stereo, 2 * frames, AnanLatencyMarks::kSilent);
        // Build 613: unkey timeline -- the first real audio in.
        auto& marks = AnanLatencyMarks::instance();
        if (marks.waiting(AnanLatencyMarks::FeedPush) && AnanLatencyMarks::loud(stereo, 2 * frames))
            marks.mark(AnanLatencyMarks::FeedPush, static_cast<int>(m_fifo.size() / 2));
        // ANAN fork build 614 (613 timeline: 2100..2500 frames, 45..55 ms of
        // silence queued ahead of the first sound after an over). OUR ERROR:
        // the mixer's resume cushion (build 597) is meant for the PC sound
        // device, and AudioEngine drops what it has queued of it (build 599),
        // but here it stayed and the radio speaker played it first. Now the
        // first real audio after a silence drops the queued silence down to
        // the prime -- only when ALL of it is silent, so no audio is lost.
        if (loudIn && !m_lastPushLoud && m_fifo.size() > 2 * kTargetFrames) {
            bool silent = true;
            for (float v : m_fifo) {
                if (std::abs(v) > AnanLatencyMarks::kSilent) {
                    silent = false;
                    break;
                }
            }
            if (silent) {
                const std::size_t drop = m_fifo.size() - 2 * kTargetFrames;
                m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<std::ptrdiff_t>(drop));
                m_silenceDrops.fetch_add(1, std::memory_order_relaxed);
                m_silenceDroppedFrames.store(static_cast<unsigned>(drop / 2), std::memory_order_relaxed);
            }
        }
        m_lastPushLoud = loudIn;
        if (rate != m_lastRate) {
            m_lastRate = rate;
            m_pos = 1.0;
        }
        for (std::size_t i = 0; i < frames; ++i) {
            const float l = stereo[2 * i];
            const float r = stereo[2 * i + 1];
            // Linear resampler: one output per `step` of input position.
            while (m_pos <= 1.0) {
                const float t = static_cast<float>(m_pos);
                float ol = m_prevL + (l - m_prevL) * t;
                float orr = m_prevR + (r - m_prevR) * t;
                if (m_blankIn > 0) {
                    ol = orr = 0.0f;
                    --m_blankIn;
                }
                m_fifo.push_back(ol);
                m_fifo.push_back(orr);
                m_pos += step;
            }
            m_pos -= 1.0;
            m_prevL = l;
            m_prevR = r;
        }
        while (m_fifo.size() > 2 * kMaxFrames) {
            m_fifo.pop_front();
            m_fifo.pop_front();
        }
        m_lastPush.store(nowMs(), std::memory_order_relaxed);
    }

    // I/O thread: AudioEngine delivered within the last 300 ms.
    bool live() const
    {
        const long long t = m_lastPush.load(std::memory_order_relaxed);
        return t > 0 && nowMs() - t < 300;
    }

    // I/O thread: `frames` stereo frames at 48 kHz into out (2*frames floats).
    void take(float* out, std::size_t frames)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::size_t have = m_fifo.size() / 2;
        if (!m_primed) {
            if (have < kTargetFrames) {
                std::fill(out, out + 2 * frames, 0.0f);
                return;
            }
            m_primed = true;
            AnanLatencyMarks::instance().mark(AnanLatencyMarks::FeedPrimed, static_cast<int>(have));
        }
        if (have > kHighFrames + frames) {   // a backlog after a stall: back to the target
            const std::size_t drop = have - kTargetFrames;
            m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<std::ptrdiff_t>(2 * drop));
            have -= drop;
            m_trims.fetch_add(1, std::memory_order_relaxed);
        }
        m_depthMin = std::min(m_depthMin, have);
        m_depthMax = std::max(m_depthMax, have);
        AnanRetuneTrace::instance().feed(static_cast<int>(m_underruns.load(std::memory_order_relaxed)),
                                         static_cast<int>(have));   // build 628
        std::size_t f = 0;
        for (; f < frames; ++f) {
            if (m_fifo.size() < 2)
                break;
            m_outL = m_fifo.front();
            m_fifo.pop_front();
            m_outR = m_fifo.front();
            m_fifo.pop_front();
            out[2 * f] = m_outL;
            out[2 * f + 1] = m_outR;
        }
        if (f < frames) {   // underrun: fade the last sample out (no step), prime again
            float l = m_outL, r = m_outR;
            for (std::size_t k = f; k < frames; ++k) {
                l *= 0.97f;
                r *= 0.97f;
                out[2 * k] = l;
                out[2 * k + 1] = r;
            }
            m_outL = m_outR = 0.0f;
            m_primed = false;
            m_underruns.fetch_add(1, std::memory_order_relaxed);
        }
        // Build 613: unkey timeline -- the first real audio out.
        auto& marks = AnanLatencyMarks::instance();
        if (marks.waiting(AnanLatencyMarks::FeedTake) && AnanLatencyMarks::loud(out, 2 * f))
            marks.mark(AnanLatencyMarks::FeedTake, static_cast<int>(have));
    }

    // I/O thread, radio PTT press (Hl2Backend's press hook): what is queued
    // and the next `framesIn` frames to arrive are silenced -- the PTT
    // contact's disturbance precedes the status that reports it.
    void blank(std::size_t framesIn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::fill(m_fifo.begin(), m_fifo.end(), 0.0f);
        m_blankIn = framesIn;
    }

    std::size_t depthFrames() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_fifo.size() / 2;
    }
    unsigned underruns() const { return m_underruns.load(std::memory_order_relaxed); }

    // Diagnostics (build 600): depth range seen by take() since the last
    // call, in frames; false when take() did not run since then.
    bool takeDepthRange(std::size_t& minFrames, std::size_t& maxFrames)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_depthMin > m_depthMax)
            return false;
        minFrames = m_depthMin;
        maxFrames = m_depthMax;
        m_depthMin = static_cast<std::size_t>(-1);
        m_depthMax = 0;
        return true;
    }
    unsigned trims() const { return m_trims.load(std::memory_order_relaxed); }
    // Build 614: silence dropped ahead of the first sound (count, last frames).
    unsigned silenceDrops() const { return m_silenceDrops.load(std::memory_order_relaxed); }
    unsigned lastSilenceDropFrames() const { return m_silenceDroppedFrames.load(std::memory_order_relaxed); }

private:
    static long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    mutable std::mutex m_mutex;
    std::deque<float> m_fifo;      // 48 kHz interleaved stereo
    double m_pos = 1.0;
    int m_lastRate = 0;
    float m_prevL = 0.0f, m_prevR = 0.0f;
    float m_outL = 0.0f, m_outR = 0.0f;
    std::size_t m_blankIn = 0;
    bool m_primed = false;
    std::atomic<bool> m_wanted{false};
    std::atomic<long long> m_lastPush{0};
    std::atomic<unsigned> m_underruns{0};
    std::atomic<unsigned> m_trims{0};
    std::atomic<unsigned> m_silenceDrops{0};
    std::atomic<unsigned> m_silenceDroppedFrames{0};
    bool m_lastPushLoud = false;
    std::size_t m_depthMin = static_cast<std::size_t>(-1);
    std::size_t m_depthMax = 0;
};

} // namespace AetherSDR::anan
