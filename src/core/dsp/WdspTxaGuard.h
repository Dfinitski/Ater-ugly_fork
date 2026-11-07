#pragma once

// ANAN-100D port (06.11.2026, crash on reconnect; crash dump 18:08:04):
// AnanPureSignal polls WDSP's PureSignal of the TXA channel from the GUI
// thread (GetPSInfo in its tick) and feeds it from the transport thread
// (pscc), while Hl2TxDsp::buildModulator() on the TX thread destroys and
// re-creates that same channel on a (re)connect -- with the same channel
// id, so nothing on the PureSignal side saw it. GetPSInfo then entered the
// deleted calcc critical section: access violation writing 0x24 in ntdll
// (RtlEnterCriticalSection with DebugInfo == NULL).
//
// The TX thread holds txaRebuildMutex() exclusively while it destroys and
// creates the channel; every PureSignal call into WDSP holds it shared, by
// TxaShared, and skips its work when the channel is being rebuilt (it only
// tries: a feed or a poll is dropped, never waited for). txaGeneration()
// counts finished rebuilds, so a channel re-created with the same id is
// still noticed and set up again.

#include <atomic>
#include <shared_mutex>

namespace AetherSDR::wdsp {

inline std::shared_mutex& txaRebuildMutex()
{
    static std::shared_mutex m;
    return m;
}

inline std::atomic<unsigned>& txaGeneration()
{
    static std::atomic<unsigned> g{0};
    return g;
}

// Shared hold for the PureSignal side; nests on one thread (only the
// outermost takes the lock). False when a rebuild is in progress.
class TxaShared {
public:
    TxaShared()
    {
        if (s_depth++ == 0)
            s_held = txaRebuildMutex().try_lock_shared();
    }
    ~TxaShared()
    {
        if (--s_depth == 0 && s_held) {
            txaRebuildMutex().unlock_shared();
            s_held = false;
        }
    }
    TxaShared(const TxaShared&) = delete;
    TxaShared& operator=(const TxaShared&) = delete;
    explicit operator bool() const noexcept { return s_held; }

private:
    static inline thread_local int s_depth = 0;
    static inline thread_local bool s_held = false;
};

}  // namespace AetherSDR::wdsp
