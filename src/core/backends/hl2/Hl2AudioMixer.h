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
//  - FreeDV: the modem signal of the FreeDV receiver is handed to it for
//    decoding (libfdv is driven from the GUI thread) and comes back as speech.
//    That one path still passes through the GUI thread; moving the decoder is
//    a separate step.

#include "core/PcmFrame.h"

#include <QElapsedTimer>
#include <QObject>

#include <array>
#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace AetherSDR::hl2 {

class Hl2AudioMixer : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxReceivers = 8;

    // Written by the owner (any thread), read by the mixer thread.
    struct Params {
        // The receiver chain of each DDC that is in the mix: present and not
        // muted. nullptr = not in the mix (its queue is dropped).
        std::array<const void*, kMaxReceivers> dsp{};
        std::array<float, kMaxReceivers> gain{};      // the receiver's fader
        std::array<int, kMaxReceivers> pan{};         // 0..100, 50 = centre
        // The receiver whose audio is the FreeDV modem signal (-1 = none): its
        // blocks go to the owner (fdvInput) and return through decodedAudio().
        int fdvReceiver = -1;
        // The connection is up and the speaker stream may be produced.
        bool live = false;
    };

    explicit Hl2AudioMixer(QObject* parent = nullptr);

    void setParams(const Params& params);
    // The receive-audio hold (transmit, the hold after it). Takes effect on
    // the next block: an atomic, not a queued call.
    void setMuted(bool muted) noexcept { m_muted.store(muted, std::memory_order_release); }
    // Drop what is queued for the mix (the blocks before a key-down or a
    // mute would otherwise play out after it).
    void flushAll() noexcept { m_flushSeq.fetch_add(1, std::memory_order_release); }

public slots:
    // A block of one receiver's demodulated audio (interleaved stereo, 24 kHz).
    void receiverAudio(const void* dsp, const std::vector<float>& pcm);
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

private:
    void mixIn(int receiver, const std::vector<float>& pcm, const Params& p);
    void emitOut(const std::vector<float>& pcm, const Params& p);
    void diag(const std::vector<float>& pcm, bool dropped);

    std::mutex m_paramsMutex;
    Params m_params;
    std::atomic<bool> m_muted{false};
    std::atomic<unsigned> m_flushSeq{0};
    unsigned m_flushSeen = 0;

    // Mixer thread only.
    std::array<std::deque<float>, kMaxReceivers> m_pending;
    std::vector<float> m_accum;
    PcmProducer m_producer;
    bool m_streaming = false;

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
