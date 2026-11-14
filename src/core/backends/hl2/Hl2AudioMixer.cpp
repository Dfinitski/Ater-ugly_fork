#include "core/backends/hl2/Hl2AudioMixer.h"

#include "core/LogManager.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

namespace AetherSDR::hl2 {

Hl2AudioMixer::Hl2AudioMixer(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<AetherSDR::PcmFrame>("AetherSDR::PcmFrame");
    qRegisterMetaType<std::vector<float>>("std::vector<float>");
}

void Hl2AudioMixer::setParams(const Params& params)
{
    std::lock_guard<std::mutex> lock(m_paramsMutex);
    m_params = params;
}

void Hl2AudioMixer::startStream()
{
    // A new session each time: PcmProducer::start() retires the previous
    // epoch, so frames of the old connection still queued for the engine are
    // refused there, and the engine restarts its chain on the first new one.
    m_streaming = m_producer.start(PcmPurpose::Speaker);
    if (!m_streaming)
        qCWarning(lcHl2) << "ANAN audio thread: the speaker stream refused to start -- no receive audio";
    for (auto& q : m_pending)
        q.clear();
}

void Hl2AudioMixer::stopStream()
{
    m_streaming = false;
    m_producer.invalidate();
    for (auto& q : m_pending)
        q.clear();
}

void Hl2AudioMixer::diag(const std::vector<float>& pcm, bool dropped)
{
    ++m_diagBlocks;
    if (dropped)
        ++m_diagDropped;
    for (float s : pcm)
        m_diagPeak = std::max(m_diagPeak, std::abs(s));
    if (!m_diagClock.isValid()) {
        m_diagClock.start();
        return;
    }
    if (m_diagClock.elapsed() < 5000)
        return;
    qCInfo(lcHl2).nospace()
        << "RX audio diag (audio thread): " << m_diagBlocks << " blocks in, " << m_diagDropped
        << " dropped by mute, " << m_diagFrames << " frames to the engine, longest gap "
        << m_diagMaxGapMs << " ms, peak " << m_diagPeak
        << ", rxMuted=" << m_muted.load(std::memory_order_acquire);
    m_diagClock.restart();
    m_diagBlocks = m_diagDropped = m_diagFrames = 0;
    m_diagPeak = 0.0f;
    m_diagMaxGapMs = 0;
}

void Hl2AudioMixer::emitOut(const std::vector<float>& pcm, const Params& p)
{
    if (pcm.empty())
        return;
    if (p.live && m_streaming) {
        QVector<float> samples(static_cast<qsizetype>(pcm.size()));
        std::copy(pcm.begin(), pcm.end(), samples.begin());
        if (const auto frame = m_producer.produce(std::move(samples))) {
            if (m_outClock.isValid())
                m_diagMaxGapMs = std::max(m_diagMaxGapMs, m_outClock.restart());
            else
                m_outClock.start();
            ++m_diagFrames;
            emit speakerFrameReady(*frame);
        }
    }
    // The owner's copy, whether or not the stream is live: it applies its own
    // gate (publishLegacyAudio) for the consumers behind it.
    emit mixedBlockReady(pcm);
}

void Hl2AudioMixer::receiverAudio(const void* dsp, const std::vector<float>& pcm)
{
    Params p;
    {
        std::lock_guard<std::mutex> lock(m_paramsMutex);
        p = m_params;
    }
    int receiver = -1;
    for (int i = 0; i < kMaxReceivers; ++i) {
        if (dsp && p.dsp[static_cast<std::size_t>(i)] == dsp)
            receiver = i;
    }
    if (receiver < 0) {
        // Not in the mix: a muted receiver, or a chain the owner has not
        // announced yet or has already withdrawn.
        diag(pcm, true);
        return;
    }
    if (receiver == p.fdvReceiver) {
        // The modem signal: decoded by the owner whether or not the audio is
        // held (the decoder has to keep its sync), back through decodedAudio().
        emit fdvInput(receiver, pcm);
        return;
    }
    mixIn(receiver, pcm, p);
}

void Hl2AudioMixer::decodedAudio(int receiver, const std::vector<float>& pcm)
{
    Params p;
    {
        std::lock_guard<std::mutex> lock(m_paramsMutex);
        p = m_params;
    }
    if (receiver < 0 || receiver >= kMaxReceivers || !p.dsp[static_cast<std::size_t>(receiver)]) {
        diag(pcm, true);
        return;
    }
    mixIn(receiver, pcm, p);
}

void Hl2AudioMixer::passthrough(const std::vector<float>& pcm)
{
    Params p;
    {
        std::lock_guard<std::mutex> lock(m_paramsMutex);
        p = m_params;
    }
    // The owner publishes its own copy of this (it made the block): only the
    // loudspeaker frame from here.
    if (pcm.empty() || !p.live || !m_streaming)
        return;
    QVector<float> samples(static_cast<qsizetype>(pcm.size()));
    std::copy(pcm.begin(), pcm.end(), samples.begin());
    if (const auto frame = m_producer.produce(std::move(samples))) {
        ++m_diagFrames;
        emit speakerFrameReady(*frame);
    }
}

// The mixer of Hl2Backend::mixReceiverAudio(), moved here unchanged in what it
// does: one unmuted receiver at unity gain and centre balance passes straight
// through; otherwise the receivers are summed sample-aligned (as much as every
// contributor can supply), with a starvation guard so a stalled receiver
// cannot silence the others, whole stereo frames only, clamped not scaled.
void Hl2AudioMixer::mixIn(int receiver, const std::vector<float>& pcm, const Params& p)
{
    const unsigned flush = m_flushSeq.load(std::memory_order_acquire);
    if (flush != m_flushSeen) {
        m_flushSeen = flush;
        for (auto& q : m_pending)
            q.clear();
    }
    // Queues of receivers no longer in the mix must not grow, and must not
    // play out when the receiver comes back.
    for (int i = 0; i < kMaxReceivers; ++i) {
        if (!p.dsp[static_cast<std::size_t>(i)])
            m_pending[static_cast<std::size_t>(i)].clear();
    }

    const bool muted = m_muted.load(std::memory_order_acquire);
    diag(pcm, muted);
    // The hold is a GAP in the output, not zeros (the engine's capture and
    // the unkey timing rely on that), and blocks in flight at key-down are
    // dropped.
    if (muted)
        return;

    const auto ri = static_cast<std::size_t>(receiver);
    auto& q = m_pending[ri];
    q.insert(q.end(), pcm.begin(), pcm.end());

    int contributors = 0;
    for (int i = 0; i < kMaxReceivers; ++i) {
        if (p.dsp[static_cast<std::size_t>(i)])
            ++contributors;
    }
    // FAST PATH: one receiver, unity gain, centre balance -- not a mix.
    if (contributors <= 1 && p.gain[ri] == 1.0f && p.pan[ri] == kPanCentre) {
        if (q.size() == pcm.size()) {
            emitOut(pcm, p);
            q.clear();
            return;
        }
        // Residue left by the aligned drain below: flush the whole queue
        // rather than drop it (always whole stereo frames).
        m_accum.assign(q.cbegin(), q.cend());
        q.clear();
        emitOut(m_accum, p);
        return;
    }

    std::size_t n = std::numeric_limits<std::size_t>::max();
    std::size_t deepest = 0;
    for (int i = 0; i < kMaxReceivers; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        if (!p.dsp[ui])
            continue;
        n = std::min(n, m_pending[ui].size());
        deepest = std::max(deepest, m_pending[ui].size());
    }
    if (n == std::numeric_limits<std::size_t>::max())
        return;
    // STARVATION GUARD: past the cap a stalled receiver is mixed as silence
    // and the rest stay audible.
    if (n == 0) {
        if (deepest < kStarvationSamples)
            return;
        n = deepest - kStarvationSamples;
        if (n == 0)
            return;
    }
    // Whole STEREO frames: an odd count would swap the channels of everything
    // after it.
    n &= ~std::size_t{1};
    if (n == 0)
        return;

    m_accum.assign(n, 0.0f);
    for (int i = 0; i < kMaxReceivers; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        if (!p.dsp[ui])
            continue;
        auto& src = m_pending[ui];
        const std::size_t take = std::min(n, src.size()) & ~std::size_t{1};
        // A balance, not a constant-power pan: centre leaves both channels at
        // unity; off centre only attenuates the side moved away from.
        const float g = p.gain[ui];
        const int pan = p.pan[ui];
        const float lw = g * (pan <= kPanCentre ? 1.0f
                                                : static_cast<float>(100 - pan) / kPanCentre);
        const float rw = g * (pan >= kPanCentre ? 1.0f
                                                : static_cast<float>(pan) / kPanCentre);
        for (std::size_t k = 0; k + 1 < take; k += 2) {
            m_accum[k] += src[k] * lw;
            m_accum[k + 1] += src[k + 1] * rw;
        }
        src.erase(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(take));
    }
    // Clamp rather than scale by 1/N: a single slice keeps its level.
    for (float& s : m_accum)
        s = std::clamp(s, -kCeiling, kCeiling);
    emitOut(m_accum, p);
}

} // namespace AetherSDR::hl2
