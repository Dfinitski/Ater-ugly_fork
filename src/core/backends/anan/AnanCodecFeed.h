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
// Since 01.11 this path is optional (AppSettings "AnanCodecAetherRx", default
// off): by default the codec plays the I/O-thread mix on the radio's clock.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>

namespace AetherSDR::anan {

class AnanCodecFeed {
public:
    static AnanCodecFeed& instance()
    {
        static AnanCodecFeed feed;
        return feed;
    }

    static constexpr std::size_t kRate = 48000;
    static constexpr std::size_t kTargetFrames = 4800;   // 100 ms prime
    static constexpr std::size_t kHighFrames = 12000;    // 250 ms: trim back to the target
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
        }
        if (have > kHighFrames + frames) {   // a backlog after a stall: back to the target
            const std::size_t drop = have - kTargetFrames;
            m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<std::ptrdiff_t>(2 * drop));
            have -= drop;
        }
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
};

} // namespace AetherSDR::anan
