#include "core/backends/hl2/Hl2AudioMixer.h"

#include "core/LogManager.h"
#include "core/backends/anan/AnanLatencyMarks.h"
#include "core/backends/anan/AnanRetuneTrace.h"
#include "core/fdv/FdvEngine.h"
#include "core/NnrFilter.h"
#ifdef HAVE_DFNR
#include "core/DeepFilterFilter.h"
#else
namespace AetherSDR {
// Build 591: a build without DeepFilterNet -- the stage never makes one.
class DeepFilterFilter {
public:
    QByteArray process(const QByteArray& pcm) { return pcm; }
    void setAttenLimit(float) {}
    void setPostFilterBeta(float) {}
};
}
#endif

#include <QByteArray>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

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

// ── Build 591: per-receiver NNR / DFNR ─────────────────────────────────────

// A filter built on a thread of its own and collected by the mixer thread on
// a later block. Shared by the two, so neither outlives the other's data.
struct Hl2AudioMixer::NrBuild {
    std::mutex mutex;
    bool done = false;
    std::unique_ptr<NnrFilter> nnr;
    std::unique_ptr<DeepFilterFilter> dfnr;
};

Hl2AudioMixer::~Hl2AudioMixer() = default;

void Hl2AudioMixer::setRxNr(int ddc, const RxNrConfig& config)
{
    if (ddc < 0 || ddc >= kMaxReceivers)
        return;
    std::lock_guard<std::mutex> lock(m_nrMutex);
    m_nrCfg[static_cast<std::size_t>(ddc)] = config;
}

void Hl2AudioMixer::receiverHold(const void* dsp, bool held)
{
    auto it = std::find(m_heldDsps.begin(), m_heldDsps.end(), dsp);
    if (held && it == m_heldDsps.end())
        m_heldDsps.push_back(dsp);
    else if (!held && it != m_heldDsps.end())
        m_heldDsps.erase(it);
}

bool Hl2AudioMixer::isHeld(const void* dsp) const
{
    return dsp && std::find(m_heldDsps.begin(), m_heldDsps.end(), dsp) != m_heldDsps.end();
}

void Hl2AudioMixer::runRxNr(int slot, int ddc, std::vector<float>& pcm, bool held)
{
    if (slot < 0 || slot >= kSlots || ddc < 0 || ddc >= kMaxReceivers || pcm.size() < 2)
        return;
    RxNrConfig cfg;
    {
        std::lock_guard<std::mutex> lock(m_nrMutex);
        cfg = m_nrCfg[static_cast<std::size_t>(ddc)];
    }
#ifndef HAVE_DFNR
    if (cfg.method == RxNrConfig::Dfnr)
        cfg.method = RxNrConfig::None;
#endif
    NrStage& st = m_nr[static_cast<std::size_t>(slot)];

    // A finished build: take it if it is still what is wanted.
    if (st.build) {
        std::unique_ptr<NnrFilter> nnr;
        std::unique_ptr<DeepFilterFilter> dfnr;
        bool done = false;
        {
            std::lock_guard<std::mutex> lock(st.build->mutex);
            done = st.build->done;
            if (done) {
                nnr = std::move(st.build->nnr);
                dfnr = std::move(st.build->dfnr);
            }
        }
        if (done) {
            if (st.buildMethod == cfg.method && st.buildStereo == cfg.stereo) {
                st.nnr = std::move(nnr);
                st.dfnr = std::move(dfnr);
                st.method = (st.nnr || st.dfnr) ? st.buildMethod : RxNrConfig::None;
                st.stereo = st.buildStereo;
                st.applied = RxNrConfig{};
                st.applied.method = -1;   // the parameters go to it below
                st.fifo.clear();
                st.primed = false;
                if (!st.nnr && !st.dfnr) {
                    st.failedMethod = st.buildMethod;
                    st.failedStereo = st.buildStereo;
                }
                if (!st.nnr && !st.dfnr)
                    qCWarning(lcHl2) << "ANAN: the receiver's"
                                     << (st.buildMethod == RxNrConfig::Nnr ? "NNR" : "DFNR")
                                     << "could not be made (slot" << slot << ")";
                else
                    qCInfo(lcHl2) << "ANAN: receiver audio slot" << slot << "runs"
                                  << (st.method == RxNrConfig::Nnr ? "NNR" : "DFNR")
                                  << (st.stereo ? "(stereo)" : "(mono)");
            }
            st.build.reset();   // a stale one is dropped (and freed) here
        }
    }

    // A change: the old filter goes now, a new one is built off this thread.
    if (cfg.method != st.method || (cfg.method != RxNrConfig::None && cfg.stereo != st.stereo)) {
        if (st.nnr || st.dfnr) {
            st.nnr.reset();
            st.dfnr.reset();
            st.method = RxNrConfig::None;
            st.fifo.clear();
            st.primed = false;
        }
        const bool building = st.build && st.buildMethod == cfg.method
                              && st.buildStereo == cfg.stereo;
        const bool failed = st.failedMethod == cfg.method && st.failedStereo == cfg.stereo;
        if (!failed)
            st.failedMethod = -1;
        if (cfg.method != RxNrConfig::None && !building && !failed) {
            auto build = std::make_shared<NrBuild>();
            st.build = build;
            st.buildMethod = cfg.method;
            st.buildStereo = cfg.stereo;
            const int method = cfg.method;
            const int channels = cfg.stereo ? 2 : 1;
            // ANAN fork build 596 (operator's log 21:35, the unkey trace of
            // 595: 190 ms of silence after every over with DFNR). OUR ERROR in
            // 591: the filters took the default 24 <-> 48 kHz converters, 2 %
            // transition band = 70.6 ms of delay EACH way (r8brain
            // getInLenBeforeOutPos, measured: 1694 / 3388 samples) -- with
            // DFNR's 40 ms the receiver played ~190 ms late, and after an over
            // that much was the filter's pre-over content, dropped as
            // silence. Receiver audio needs no more than ~8 kHz: 30 % (8.4 kHz
            // passband) costs 4.3 + 4.4 ms.
            constexpr double kRxNrTransBandPct = 30.0;
            std::thread([build, method, channels, kRxNrTransBandPct] {
                std::unique_ptr<NnrFilter> nnr;
                std::unique_ptr<DeepFilterFilter> dfnr;
                if (method == RxNrConfig::Nnr) {
                    nnr = std::make_unique<NnrFilter>(24000, channels, kRxNrTransBandPct);
                    if (!nnr->isValid())
                        nnr.reset();
                }
#ifdef HAVE_DFNR
                if (method == RxNrConfig::Dfnr) {
                    dfnr = std::make_unique<DeepFilterFilter>(24000, channels, kRxNrTransBandPct);
                    if (!dfnr->isValid())
                        dfnr.reset();
                }
#endif
                std::lock_guard<std::mutex> lock(build->mutex);
                build->nnr = std::move(nnr);
                build->dfnr = std::move(dfnr);
                build->done = true;
            }).detach();
        } else if (cfg.method == RxNrConfig::None) {
            st.build.reset();
            st.buildMethod = RxNrConfig::None;
        }
    }
    if (!st.nnr && !st.dfnr)
        return;   // plays unfiltered (nothing wanted, or still being built)

    // Build 592 (operator, 21.11: "during transmit the processing must be
    // frozen in every noise reduction and resume after the guard interval,
    // so the transmission does not get into it and spoil its statistics"):
    // while the receiver's output is held the filter takes nothing -- its
    // state stays as it was before the over. On the first block after,
    // what it still holds from before the over comes out first: that much
    // is silence, then the audio after the over.
    if (held) {
        st.frozen = true;
        std::fill(pcm.begin(), pcm.end(), 0.0f);
        return;
    }
    if (st.frozen) {
        st.frozen = false;
        if (st.nnr) {
            st.fifo.clear();
            st.primed = false;
            st.dropSamples = 2 * static_cast<std::size_t>(std::max(0, st.nnr->delaySamples()));
        } else if (st.dfnr) {
#ifdef HAVE_DFNR
            st.dropSamples = 2 * static_cast<std::size_t>(std::max(0, st.dfnr->delaySamples()));
#endif
        }
        st.sumIn = st.sumOut = 0.0;
        st.gainSamples = 0;
    }

    // The operator's settings (atomics in the filters, applied on their next block).
    if (!(cfg == st.applied)) {
        if (st.nnr) {
            st.nnr->setStrength(cfg.nnrStrength);
            st.nnr->setModel(cfg.nnrModel);
            st.nnr->setAlpha(cfg.nnrAlpha);
            st.nnr->setAlphaKnee(cfg.nnrKnee);
            st.nnr->setTau(cfg.nnrTau);
            st.nnr->setMaxGain(cfg.nnrMaxGain);
            st.nnr->setSmoothing(cfg.nnrAttackMs, cfg.nnrReleaseMs);
        }
        if (st.dfnr) {
            st.dfnr->setAttenLimit(cfg.dfnrAttenDb);
            st.dfnr->setPostFilterBeta(cfg.dfnrBeta);
        }
        st.applied = cfg;
    }

    const std::size_t n = pcm.size() & ~std::size_t{1};
    double eIn = 0.0;
    for (std::size_t k = 0; k < n; ++k)
        eIn += double(pcm[k]) * double(pcm[k]);
    const QByteArray in(reinterpret_cast<const char*>(pcm.data()),
                        static_cast<qsizetype>(n * sizeof(float)));
    if (st.dfnr) {
        // DeepFilterFilter returns as much as it was given (silence while it fills).
        const QByteArray out = st.dfnr->process(in);
        if (out.size() == in.size())
            std::memcpy(pcm.data(), out.constData(), static_cast<std::size_t>(out.size()));
    } else {
        // NnrFilter returns whole NNR blocks: queued here and played block
        // for block, with one NNR block (128 frames at 24 kHz) and a margin
        // in hand so the queue never runs dry between them.
        constexpr std::size_t kMarginSamples = 2 * 256;   // stereo floats
        const QByteArray out = st.nnr->process(in);
        const auto* f = reinterpret_cast<const float*>(out.constData());
        st.fifo.insert(st.fifo.end(), f, f + out.size() / static_cast<qsizetype>(sizeof(float)));
        if (!st.primed && st.fifo.size() >= n + kMarginSamples)
            st.primed = true;
        if (st.primed && st.fifo.size() >= n) {
            std::copy(st.fifo.begin(), st.fifo.begin() + static_cast<std::ptrdiff_t>(n), pcm.begin());
            st.fifo.erase(st.fifo.begin(), st.fifo.begin() + static_cast<std::ptrdiff_t>(n));
        } else {
            std::fill(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
            st.primed = false;   // filled again before it plays on
        }
        constexpr std::size_t kFifoCap = 24000;   // 0.5 s: a stalled reader
        if (st.fifo.size() > kFifoCap)
            st.fifo.erase(st.fifo.begin(), st.fifo.begin()
                          + static_cast<std::ptrdiff_t>((st.fifo.size() - kFifoCap) & ~std::size_t{1}));
    }
    // Build 592: the filter's pre-over content, as silence.
    if (st.dropSamples > 0) {
        const std::size_t z = std::min(st.dropSamples, n);
        std::fill(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(z), 0.0f);
        st.dropSamples -= z;
        return;   // no NR GAIN reading from it
    }

    // The NR GAIN reading of a main receiver: energy out over energy in,
    // over about 50 ms.
    if (slot < kMaxReceivers) {
        double eOut = 0.0;
        for (std::size_t k = 0; k < n; ++k)
            eOut += double(pcm[k]) * double(pcm[k]);
        st.sumIn += eIn;
        st.sumOut += eOut;
        st.gainSamples += n / 2;
        if (st.gainSamples >= 1200) {
            if (st.sumIn > 1.0e-12)
                emit rxNrGain(ddc, static_cast<float>(std::min(1.0, std::sqrt(st.sumOut / st.sumIn))));
            st.sumIn = st.sumOut = 0.0;
            st.gainSamples = 0;
        }
    }
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
    // ANAN fork build 597 (operator, 21.11: with DFNR the return to receive
    // is good now, without noise reduction the pause is clearer; heard in
    // the computer's headphones). The 596 traces: with DFNR the engine gets
    // ~50 ms of silence after the hold, then sound -- good; without, sound
    // from the very first block. Through the over the engine gets nothing
    // (the hold is a gap), its sound device runs dry, and it restarts on
    // the first block with nothing in hand: a moment of sound, a dropout
    // while the next blocks catch up, then sound. The first sound after the
    // hold now always has 50 ms of silence before it (less what the chain
    // already gave, e.g. DFNR's) -- the case heard as good.
    // Build 599: the first sound after a silence-filled hold -- the engine
    // drops the silence it still has queued ahead of it.
    if (m_resumeFlushPending && p.live && m_streaming && !m_muted.load(std::memory_order_acquire)) {
        bool silent = true;
        for (float v : pcm) {
            if (v != 0.0f) {
                silent = false;
                break;
            }
        }
        if (!silent) {
            m_resumeFlushPending = false;
            emit rxResumeFromHold();
        }
    }
    if (m_resumePending && p.live && m_streaming && !m_muted.load(std::memory_order_acquire)) {
        bool silent = true;
        for (float v : pcm) {
            if (v != 0.0f) {
                silent = false;
                break;
            }
        }
        if (silent) {
            m_resumeSilentFrames += pcm.size() / 2;
        } else {
            m_resumePending = false;
            if (m_resumeSilentFrames < kResumeCushionFrames) {
                QVector<float> cushion(
                    static_cast<qsizetype>(2 * (kResumeCushionFrames - m_resumeSilentFrames)), 0.0f);
                if (const auto frame = m_producer.produce(std::move(cushion)))
                    emit speakerFrameReady(*frame);
            }
        }
    }
    if (p.live && m_streaming) {
        // Build 613: unkey timeline -- the PC path after the hold.
        auto& marks = anan::AnanLatencyMarks::instance();
        marks.mark(anan::AnanLatencyMarks::MixerLive);
        if (marks.waiting(anan::AnanLatencyMarks::MixerSound)
            && anan::AnanLatencyMarks::loud(pcm.data(), pcm.size()))
            marks.mark(anan::AnanLatencyMarks::MixerSound);
        QVector<float> samples(static_cast<qsizetype>(pcm.size()));
        std::copy(pcm.begin(), pcm.end(), samples.begin());
        if (const auto frame = m_producer.produce(std::move(samples))) {
            if (m_outClock.isValid()) {
                const qint64 gapMs = m_outClock.restart();
                m_diagMaxGapMs = std::max(m_diagMaxGapMs, gapMs);
                anan::AnanRetuneTrace::instance().mixerGap(static_cast<int>(gapMs * 1000));   // build 628
            } else {
                m_outClock.start();
            }
            ++m_diagFrames;
            emit speakerFrameReady(*frame);
        }
    }
    if (m_codecSink)
        m_codecSink(pcm);   // the radio's codec, when the I/O-thread mixer is not feeding it
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
        // Build 393: with nothing in the mix at all, this chain's blocks
        // become silence of the same length -- the output keeps running. Not
        // during the transmit hold (a GAP there, as in mixIn).
        if (dsp && dsp == p.silenceClock && !m_muted.load(std::memory_order_acquire)) {
            m_accum.assign(pcm.size(), 0.0f);
            diag(m_accum, false);
            emitOut(m_accum, p);
            return;
        }
        diag(pcm, true);
        return;
    }
    if (receiver == p.fdvReceiver) {
        // The modem signal: decoded whether or not the audio is held (the
        // decoder has to keep its sync). libfdv's speech replaces it; without
        // libfdv the receiver is silent rather than hissing.
        if (!m_fdv) {
            emit fdvInput(receiver, pcm);   // the owner decodes: decodedAudio()
            return;
        }
        m_speech = pcm;
        if (m_fdv->loaded())
            m_fdv->processRx(m_speech);
        else
            std::fill(m_speech.begin(), m_speech.end(), 0.0f);
        mixIn(receiver, m_speech, p);
        return;
    }
    // Build 591: the receiver's own NNR / DFNR, before the mix -- also while
    // the audio is held (the block is then dropped by mixIn), so what the
    // filter holds from before an over is not played after it.
    m_nrBuf = pcm;
    const bool held = isHeld(dsp);
    runRxNr(receiver, receiver, m_nrBuf, held);
    unkeyTrace(receiver, pcm, m_nrBuf, held);   // build 595, diagnostics
    mixIn(receiver, m_nrBuf, p);
}

void Hl2AudioMixer::unkeyTrace(int receiver, const std::vector<float>& in,
                               const std::vector<float>& out, bool held)
{
    // ANAN fork build 595 (operator, 21.11: after an over "sound for a
    // moment, a pause, then sound", most of all with DFNR): what this thread
    // gets and sends on for the first receiver, from the end of the hold.
    int first = -1;
    {
        std::lock_guard<std::mutex> lock(m_paramsMutex);
        for (int i = 0; i < kMaxReceivers && first < 0; ++i)
            if (m_params.dsp[static_cast<std::size_t>(i)])
                first = i;
    }
    if (receiver != first)
        return;
    auto& t = m_ukTr;
    const bool muted = m_muted.load(std::memory_order_acquire);
    if (t.prevMuted && !muted) {
        t.frames = 0;
        t.inPk.fill(-1.0f);
        t.outPk.fill(-1.0f);
        t.held.fill(' ');
        t.drop.fill(' ');
        t.method.fill(' ');
    }
    t.prevMuted = muted;
    if (t.frames < 0)
        return;
    const int row = t.frames / 240;   // 10 ms at 24 kHz
    if (row >= UnkeyTrace::kRows) {
        auto line = [](const std::array<float, UnkeyTrace::kRows>& a) {
            QString s;
            for (float v : a)
                s += (v < 0.0f ? QStringLiteral("_") : v > 0.0f
                          ? QString::number(20.0 * std::log10(v), 'f', 0) : QStringLiteral("z"))
                     + QLatin1Char(' ');
            return s;
        };
        auto flags = [](const std::array<char, UnkeyTrace::kRows>& a) {
            QString s;
            for (char c : a)
                s += QLatin1Char(c);
            return s;
        };
        qCInfo(lcHl2).noquote() << "ANAN unkey test, audio thread, receiver" << receiver
                                << "10 ms rows from the end of the hold (z = digital silence):";
        qCInfo(lcHl2).noquote() << "  in pk dBFS:  " << line(t.inPk);
        qCInfo(lcHl2).noquote() << "  out pk dBFS: " << line(t.outPk);
        qCInfo(lcHl2).noquote() << "  held:        " << flags(t.held);
        qCInfo(lcHl2).noquote() << "  NR drop:     " << flags(t.drop);
        qCInfo(lcHl2).noquote() << "  NR method:   " << flags(t.method);
        t.frames = -1;
        return;
    }
    float pi = 0.0f, po = 0.0f;
    for (float v : in)
        pi = std::max(pi, std::abs(v));
    for (float v : out)
        po = std::max(po, std::abs(v));
    const auto r = static_cast<std::size_t>(row);
    t.inPk[r] = std::max(t.inPk[r], pi);
    t.outPk[r] = std::max(t.outPk[r], po);
    if (held)
        t.held[r] = 'H';
    const NrStage& st = m_nr[static_cast<std::size_t>(receiver)];
    if (st.dropSamples > 0)
        t.drop[r] = 'D';
    t.method[r] = st.method == RxNrConfig::Dfnr ? 'F' : st.method == RxNrConfig::Nnr ? 'N'
                : st.build ? 'b' : '-';
    t.frames += static_cast<int>(in.size() / 2);
}

void Hl2AudioMixer::applyFlush()
{
    const unsigned flush = m_flushSeq.load(std::memory_order_acquire);
    if (flush != m_flushSeen) {
        m_flushSeen = flush;
        for (auto& q : m_pending)
            q.clear();
    }
}

void Hl2AudioMixer::subAudio(const void* ownerDsp, const std::vector<float>& pcm)
{
    Params p;
    {
        std::lock_guard<std::mutex> lock(m_paramsMutex);
        p = m_params;
    }
    int slot = -1;
    for (int i = kMaxReceivers; i < kSlots; ++i) {
        if (ownerDsp && p.dsp[static_cast<std::size_t>(i)] == ownerDsp)
            slot = i;
    }
    if (slot < 0)
        return;   // not in the mix: switched off, or its receiver is muted
    // Build 591: the sub receiver takes its receiver's NNR / DFNR (own filter).
    std::vector<float> filtered;
    {
        int ownerDdc = -1;
        for (int i = 0; i < kMaxReceivers; ++i) {
            if (p.dsp[static_cast<std::size_t>(i)] == ownerDsp)
                ownerDdc = i;
        }
        if (ownerDdc >= 0) {
            filtered = pcm;
            runRxNr(slot, ownerDdc, filtered, isHeld(ownerDsp));
        }
    }
    const std::vector<float>& subPcm = filtered.empty() ? pcm : filtered;
    applyFlush();
    // The hold is a gap for the sub receiver as for its receiver (mixIn).
    if (m_muted.load(std::memory_order_acquire))
        return;
    auto& q = m_pending[static_cast<std::size_t>(slot)];
    q.insert(q.end(), subPcm.begin(), subPcm.end() - static_cast<std::ptrdiff_t>(subPcm.size() & 1));
    // Only queued: mixIn() takes it when the receiver's own block arrives.
    // It cannot get further ahead than that receiver's queue does; the cap
    // is for a receiver whose blocks have stopped.
    constexpr std::size_t kCap = 8 * kStarvationSamples;
    if (q.size() > kCap)
        q.erase(q.begin(), q.begin() + static_cast<std::ptrdiff_t>((q.size() - kCap + 1) & ~std::size_t{1}));
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
    if (pcm.empty())
        return;
    // Build 598: the monitor plays to the loudspeaker.
    if (m_passClock.isValid())
        m_passClock.restart();
    else
        m_passClock.start();
    if (m_codecSink)
        m_codecSink(pcm);
    if (!p.live || !m_streaming)
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
    applyFlush();
    // Queues of receivers no longer in the mix must not grow, and must not
    // play out when the receiver comes back.
    for (int i = 0; i < kSlots; ++i) {
        if (!p.dsp[static_cast<std::size_t>(i)])
            m_pending[static_cast<std::size_t>(i)].clear();
    }

    const bool muted = m_muted.load(std::memory_order_acquire);
    diag(pcm, muted);
    // The hold is a GAP in the output, not zeros (the engine's capture and
    // the unkey timing rely on that), and blocks in flight at key-down are
    // dropped.
    if (muted) {
        // ANAN fork build 598 (operator, 21.11: "let's send the card zeros
        // without breaks, then it will not run dry"). Through the hold the
        // engine -- the computer's sound device -- gets silence of the same
        // length, once per period (the first receiver in the mix clocks it),
        // so it never runs dry and the first sound after the over comes on a
        // running device. Only the loudspeaker frames: the copy for the
        // recorder / decoders (mixedBlockReady) and the radio's codec keep
        // the gap they had. Not while the transmit monitor plays to the
        // loudspeaker (it fills the stream itself); then the hold stays a gap
        // and the 597 cushion covers its end.
        int first = -1;
        for (int i = 0; i < kMaxReceivers && first < 0; ++i)
            if (p.dsp[static_cast<std::size_t>(i)])
                first = i;
        const bool monitorPlaying = m_passClock.isValid() && m_passClock.elapsed() < 200;
        if (!monitorPlaying) {
            if (receiver == first && p.live && m_streaming && !pcm.empty()) {
                QVector<float> silence(static_cast<qsizetype>(pcm.size() & ~std::size_t{1}), 0.0f);
                if (const auto frame = m_producer.produce(std::move(silence))) {
                    if (m_outClock.isValid())
                        m_diagMaxGapMs = std::max(m_diagMaxGapMs, m_outClock.restart());
                    else
                        m_outClock.start();
                    ++m_diagFrames;
                    emit speakerFrameReady(*frame);
                }
            }
            m_resumePending = false;   // the device is running: no cushion
            m_resumeFlushPending = true;   // build 599
        } else {
            m_resumePending = true;    // build 597
            m_resumeSilentFrames = 0;
        }
        // Build 513: a sub receiver's block queued just ahead of this one
        // goes with it, or it would stay one block behind from here on.
        for (int i = kMaxReceivers; i < kSlots; ++i)
            m_pending[static_cast<std::size_t>(i)].clear();
        return;
    }

    const auto ri = static_cast<std::size_t>(receiver);
    auto& q = m_pending[ri];
    q.insert(q.end(), pcm.begin(), pcm.end());

    int contributors = 0;
    for (int i = 0; i < kSlots; ++i) {   // build 513: a sub receiver is one too
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
    // Build 513: every slot, the sub receivers' too. Those did not take part
    // in n above (the loop over kMaxReceivers): they are mixed with what they
    // have, each with its own fader and balance.
    for (int i = 0; i < kSlots; ++i) {
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
