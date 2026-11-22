#pragma once

// ANAN fork build 373 (operator, 14.11: "move the audio processing to a thread
// of its own with a high priority, so that work in the interface does not stop
// the sound").
//
// Where the receive audio went until now:
//
//   hl2-io thread   Hl2RxDsp: IQ -> WDSP demodulator -> audioReady(block)
//        |  queued
//   GUI thread      Hl2Backend::mixReceiverAudio(): gain, balance, mute, the
//        |          sum of the receivers -> publishLegacyAudio()
//        |          RadioModel: gate -> backendAudioFrameReady
//        |  queued
//   AudioEngine thread   client DSP (NR, EQ ...), resampler, the sound device
//
// Both ends already had threads of their own; the middle did not. Every block
// of audio waited for the GUI thread's event loop, so whatever kept that loop
// busy -- a repaint of the panadapter during a zoom, a dialog opening, a
// layout pass -- held the audio back, and the sound device ran dry: clicks.
//
// This object is that middle, on a thread of its own ("anan-audio"). It takes
// the receivers' blocks straight from the I/O thread, mixes them exactly as
// mixReceiverAudio() did, and hands the frames to the AudioEngine thread
// itself. The GUI thread is no longer between a demodulator and the device.
//
// What the GUI thread still does, and why it may:
//  - it WRITES the mix parameters (gain, balance, mutes, which receivers
//    exist): a snapshot under a mutex, as the codec mixer on the I/O thread
//    has had since the radio's own loudspeaker got its path (CodecMixParams);
//  - it receives a COPY of each mixed block for the consumers that are not
//    the loudspeaker (the recorder, the decoders' audio bus, the radio's codec
//    in FreeDV) -- late there is harmless, and it cannot hold this thread up;
//
// Build 374: FreeDV receive is decoded HERE as well. In 373 the modem signal
// of the FreeDV receiver went to the GUI thread for libfdv and came back as
// speech; FdvEngine is thread-safe now (FdvEngine.h), so this thread calls
// the decoder itself and the FreeDV audio no longer waits for the interface
// either. This thread also carries the computer's transmit audio (microphone,
// DAX, TCI) from the AudioEngine thread to the modulator's thread -- see
// Hl2Backend::submitTxAudioDirect().

#include "core/PcmFrame.h"

#include <QElapsedTimer>
#include <QObject>

#include <array>
#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace AetherSDR::fdv {
class FdvEngine;
}
namespace AetherSDR {
class NnrFilter;
class DeepFilterFilter;
}

namespace AetherSDR::hl2 {

class Hl2AudioMixer : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxReceivers = 8;
    // ANAN fork build 513: the sub receivers (A' / B') are contributors of
    // their own -- slot kMaxReceivers + n, with their own fader and balance.
    // Their audio comes from the chain of the receiver they belong to
    // (Hl2RxDsp::subAudioReady -> subAudio()), one block just ahead of each of
    // that receiver's, so they never hold the mix up: a sub slot is only
    // queued, and mixed with what it has when its receiver's block is.
    static constexpr int kSubSlots = 2;
    static constexpr int kSlots = kMaxReceivers + kSubSlots;

    // Written by the owner (any thread), read by the mixer thread.
    struct Params {
        // The receiver chain of each DDC that is in the mix: present and not
        // muted. nullptr = not in the mix (its queue is dropped).
        // [kMaxReceivers + n]: the chain of the receiver whose sub receiver n
        // is in the mix (the key subAudio() is called with).
        std::array<const void*, kSlots> dsp{};
        std::array<float, kSlots> gain{};      // the receiver's fader
        std::array<int, kSlots> pan{};         // 0..100, 50 = centre
        // The receiver whose audio is the FreeDV modem signal (-1 = none): its
        // blocks pass through the decoder (setFdvEngine) before the mix.
        int fdvReceiver = -1;
        // Build 393: when NO receiver is in the mix (the only one is muted, or
        // the FTX mode has it), the chain whose blocks clock silence to the
        // output instead of nothing. Log 14.11 15:57: with the only receiver
        // in FTX the engine got no audio at all and its watchdog restarted
        // the sound device every 15 s. nullptr = none.
        const void* silenceClock = nullptr;
        // The connection is up and the speaker stream may be produced.
        bool live = false;
    };

    explicit Hl2AudioMixer(QObject* parent = nullptr);
    ~Hl2AudioMixer() override;   // build 591: the NR filters are complete types there

    // The FreeDV engine (thread-safe), set once before the thread starts and
    // outliving it. Without one the modem blocks go to the owner (fdvInput)
    // and come back through decodedAudio(), as in build 373.
    void setFdvEngine(fdv::FdvEngine* engine) noexcept { m_fdv = engine; }
    // Build 375: called on the mixer thread with every block that goes to the
    // loudspeaker (the mix and the passthrough), for the radio's own codec.
    // Set once before the thread starts.
    void setCodecSink(std::function<void(const std::vector<float>&)> sink) { m_codecSink = std::move(sink); }
    void setParams(const Params& params);

    // ANAN fork build 591 (operator, 21.11: "NNR, NR2 and DFNR on each
    // receiver's own audio, independent for each receiver"). NR2 runs in the
    // receiver's WDSP chain (build 587); NNR and DFNR run HERE, on the
    // receiver's own block before it is mixed -- one filter per receiver and
    // per sub receiver (which takes its receiver's choice). Mono unless the
    // receiver is binaural. The filters are built off this thread (a model
    // load, FFTW planning), the receiver plays unfiltered meanwhile.
    struct RxNrConfig {
        enum Method { None = 0, Nnr = 1, Dfnr = 2 };
        int method = None;
        bool stereo = false;   // the receiver's BIN
        int nnrStrength = 50;
        int nnrModel = 0;
        double nnrAlpha = 1.0;
        double nnrKnee = 10.0;
        double nnrTau = 2.0;
        double nnrMaxGain = 12.0;
        double nnrAttackMs = 0.0;
        double nnrReleaseMs = 0.0;
        float dfnrAttenDb = 100.0f;
        float dfnrBeta = 0.0f;
        bool operator==(const RxNrConfig&) const = default;
    };
    // Any thread. ddc: the receiver (0 .. kMaxReceivers - 1).
    void setRxNr(int ddc, const RxNrConfig& config);
    // The receive-audio hold (transmit, the hold after it). Takes effect on
    // the next block: an atomic, not a queued call.
    void setMuted(bool muted) noexcept { m_muted.store(muted, std::memory_order_release); }
    // Drop what is queued for the mix (the blocks before a key-down or a
    // mute would otherwise play out after it).
    void flushAll() noexcept { m_flushSeq.fetch_add(1, std::memory_order_release); }

public slots:
    // A block of one receiver's demodulated audio (interleaved stereo, 24 kHz).
    void receiverAudio(const void* dsp, const std::vector<float>& pcm);
    // Build 592: the receiver's output is held (an over and its guard):
    // its NNR / DFNR take no audio meanwhile (Hl2RxDsp::audioHoldChanged).
    void receiverHold(const void* dsp, bool held);
    // Build 513: a block of the sub receiver of the receiver whose chain is
    // ownerDsp; it arrives just ahead of that receiver's own block.
    void subAudio(const void* ownerDsp, const std::vector<float>& pcm);
    // The FreeDV receiver's audio after the decoder.
    void decodedAudio(int receiver, const std::vector<float>& pcm);
    // Audio that is not a receiver's (the transmit monitor): out as it is.
    void passthrough(const std::vector<float>& pcm);
    // A new connection: a new stream session; the end of one: the frames
    // already queued for the engine fail closed.
    void startStream();
    void stopStream();

signals:
    // To the AudioEngine thread, directly.
    void speakerFrameReady(const AetherSDR::PcmFrame& frame);
    // A copy of what went to the loudspeaker, for the owner's other consumers.
    void mixedBlockReady(const std::vector<float>& pcm);
    void fdvInput(int receiver, const std::vector<float>& pcm);
    // Build 591: a receiver's NNR / DFNR gain (0..1, output over input,
    // amplitude), about 20 times a second while it runs.
    void rxNrGain(int receiver, float gain);
    // Build 599: the first sound after a transmit hold that was filled with
    // silence (598) is the next speaker frame: what the engine has queued
    // ahead of it is that silence and can go (AudioEngine::dropQueuedRxSilence).
    void rxResumeFromHold();

private:
    // Build 591: the per-receiver NNR / DFNR stage (mixer thread).
    struct NrBuild;   // a filter being built off this thread
    struct NrStage {
        int method = RxNrConfig::None;   // what the filter below is
        bool stereo = false;
        std::unique_ptr<NnrFilter> nnr;
        std::unique_ptr<DeepFilterFilter> dfnr;
        RxNrConfig applied;               // the parameters it was given
        std::shared_ptr<NrBuild> build;   // in flight, or null
        int buildMethod = RxNrConfig::None;
        bool buildStereo = false;
        int failedMethod = -1;            // could not be made: not retried
        bool failedStereo = false;        //   until the choice changes
        std::deque<float> fifo;           // NNR's output, block for block
        bool primed = false;
        bool frozen = false;              // build 592: held across an over
        std::size_t dropSamples = 0;      // build 592: pre-over audio still in the filter
        double sumIn = 0.0, sumOut = 0.0;
        std::size_t gainSamples = 0;
    };
    // Filters this slot's block in place with the configuration of ddc.
    void runRxNr(int slot, int ddc, std::vector<float>& pcm, bool held);
    bool isHeld(const void* dsp) const;
    std::vector<const void*> m_heldDsps;           // build 592, mixer thread
    // Build 595: unkey trace of the first receiver in the mix (diagnostics):
    // 10 ms rows for 800 ms from the end of the transmit hold.
    struct UnkeyTrace {
        static constexpr int kRows = 80;
        bool prevMuted = false;
        int frames = -1;   // -1: not running
        std::array<float, kRows> inPk{}, outPk{};
        std::array<char, kRows> held{}, drop{}, method{};
    } m_ukTr;
    void unkeyTrace(int receiver, const std::vector<float>& in, const std::vector<float>& out, bool held);
    std::array<NrStage, kSlots> m_nr;              // mixer thread
    std::mutex m_nrMutex;
    std::array<RxNrConfig, kMaxReceivers> m_nrCfg; // under m_nrMutex
    std::vector<float> m_nrBuf;                    // mixer thread

private:
    void mixIn(int receiver, const std::vector<float>& pcm, const Params& p);
    void applyFlush();   // build 513: a pending flushAll(), once
    void emitOut(const std::vector<float>& pcm, const Params& p);
    void diag(const std::vector<float>& pcm, bool dropped);

    fdv::FdvEngine* m_fdv = nullptr;
    std::function<void(const std::vector<float>&)> m_codecSink;
    std::vector<float> m_speech;   // mixer thread only
    std::mutex m_paramsMutex;
    Params m_params;
    std::atomic<bool> m_muted{false};
    std::atomic<unsigned> m_flushSeq{0};
    unsigned m_flushSeen = 0;

    // Mixer thread only.
    std::array<std::deque<float>, kSlots> m_pending;
    std::vector<float> m_accum;
    PcmProducer m_producer;
    bool m_streaming = false;
    // Build 597: after the transmit hold the sound device restarts from
    // empty; it gets at least kResumeCushionFrames of silence before the
    // first sound (mixer thread).
    static constexpr std::size_t kResumeCushionFrames = 1200;   // 50 ms at 24 kHz
    bool m_resumePending = false;
    std::size_t m_resumeSilentFrames = 0;
    // Build 598: through the hold the sound device gets silence at the
    // receiver's own pace instead of nothing -- unless the transmit monitor
    // is playing to it then (passthrough within the last 200 ms).
    QElapsedTimer m_passClock;
    bool m_resumeFlushPending = false;   // build 599, mixer thread

    // Diagnostics: one log line every 5 s. "longest gap" is the longest time
    // between two frames handed to the engine -- the figure this thread
    // exists to keep small whatever the GUI does.
    QElapsedTimer m_diagClock;
    QElapsedTimer m_outClock;
    int m_diagBlocks = 0;
    int m_diagDropped = 0;
    int m_diagFrames = 0;
    float m_diagPeak = 0.0f;
    qint64 m_diagMaxGapMs = 0;

    static constexpr std::size_t kStarvationSamples = 2048;   // as mixReceiverAudio()
    static constexpr float kCeiling = 1.0f;
    static constexpr int kPanCentre = 50;
};

} // namespace AetherSDR::hl2
