/* AetherSDR: portable CPacer for Dream's receiver core (replaces Dream's
 * src/linux/Pacer.cpp and src/windows/Pacer.cpp). Only Dream's file inputs
 * (audiofilein, PacketSourceFile) pace with it; AetherSDR feeds the receiver
 * itself. GPL v2 or later, like the rest of third_party/dream. */

#include "../src/util/Pacer.h"

#include <chrono>
#include <thread>

namespace {
uint64_t nowNs()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}
} // namespace

CPacer::CPacer(uint64_t ns)
    : timekeeper(nowNs() + ns), interval(ns)
{
}

CPacer::~CPacer() = default;

uint64_t CPacer::nstogo()
{
    const uint64_t now = nowNs();
    return timekeeper > now ? timekeeper - now : 0;
}

void CPacer::wait()
{
    const uint64_t delay = nstogo();
    if (delay > 0)
        std::this_thread::sleep_for(std::chrono::nanoseconds(delay));
    timekeeper += interval;
}
