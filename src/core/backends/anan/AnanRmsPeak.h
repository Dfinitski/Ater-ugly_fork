#pragma once

// ANAN fork build 562 (operator, 21.11: "the SmartMTR numbers on the flag --
// a peak detector of 0.5 s, so they update every 0.5 s; and see that the
// values are RMS").
//
// The S-meter stream that reaches a flag is the NEEDLE's: the detector chosen
// in Setup > Receive (Peak by default -- the block's highest |IQ|^2, not an
// RMS quantity) after the needle's ballistics. The numbers need the RMS
// reading whatever the needle uses: WDSP's average signal meter (RXA_S_AV, a
// 100 ms power average), referred to dBm exactly as the needle's value is.
// The backend feeds every such reading here; the highest of each 0.5 s is
// held for the flag to read. Per slice id (0..3 the receivers, 6..7 their sub
// receivers).
//
// feed() is called from the backend's thread only; held() from the GUI thread.

#include <QElapsedTimer>

#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace AetherSDR::anan {

class RmsPeakBoard {
public:
    static constexpr int kSlices = 8;
    static constexpr qint64 kWindowMs = 500;
    // A held value older than this is nobody's (the receiver was closed, the
    // radio is keyed or gone): the flag falls back to its own stream.
    static constexpr qint64 kStaleMs = 1500;

    static RmsPeakBoard& instance()
    {
        static RmsPeakBoard board;
        return board;
    }

    // One RMS reading of a slice, dBm.
    void feed(int slice, double dbm)
    {
        if (slice < 0 || slice >= kSlices || !std::isfinite(dbm))
            return;
        Window& w = m_windows[static_cast<std::size_t>(slice)];
        const qint64 now = m_clock.elapsed();
        if (w.startMs < 0 || now - w.startMs > 2 * kWindowMs) {   // first, or after a gap
            w.startMs = now;
            w.maxDbm = dbm;
            return;
        }
        if (dbm > w.maxDbm)
            w.maxDbm = dbm;
        if (now - w.startMs >= kWindowMs) {
            m_held[static_cast<std::size_t>(slice)].store(static_cast<float>(w.maxDbm),
                                                          std::memory_order_relaxed);
            m_heldAtMs[static_cast<std::size_t>(slice)].store(now, std::memory_order_relaxed);
            w.startMs = now;
            w.maxDbm = -std::numeric_limits<double>::infinity();
        }
    }

    // The peak of the last whole 0.5 s, dBm; NaN when there is none that is
    // fresh.
    [[nodiscard]] float held(int slice) const
    {
        if (slice < 0 || slice >= kSlices)
            return std::numeric_limits<float>::quiet_NaN();
        const qint64 at = m_heldAtMs[static_cast<std::size_t>(slice)].load(std::memory_order_relaxed);
        if (at < 0 || m_clock.elapsed() - at > kStaleMs)
            return std::numeric_limits<float>::quiet_NaN();
        return m_held[static_cast<std::size_t>(slice)].load(std::memory_order_relaxed);
    }

private:
    RmsPeakBoard()
    {
        m_clock.start();
        for (auto& v : m_held)
            v.store(std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
        for (auto& t : m_heldAtMs)
            t.store(-1, std::memory_order_relaxed);
    }

    struct Window {
        qint64 startMs = -1;
        double maxDbm = 0.0;
    };

    QElapsedTimer m_clock;   // started once; elapsed() is read from both threads
    std::array<Window, kSlices> m_windows{};
    std::array<std::atomic<float>, kSlices> m_held;
    std::array<std::atomic<qint64>, kSlices> m_heldAtMs;
};

} // namespace AetherSDR::anan
