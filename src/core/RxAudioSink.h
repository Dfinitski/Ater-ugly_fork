#pragma once

// ANAN fork build 602 (operator, 21.11: "add to the sound options, where the
// sound card is set up, this exclusive mode setting").
//
// AudioEngine's RX output behind one small interface, so the receive drain
// (a 10 ms timer pushing Float stereo into a QIODevice up to bytesFree())
// runs unchanged on either of two outputs:
//
//   QtRxSink           the QAudioSink it always used (WASAPI shared mode on
//                      Windows: the Windows mixer and its period stand between
//                      us and the card);
//   PaExclusiveRxSink  Windows + PortAudio: the card in WASAPI EXCLUSIVE mode,
//                      as Thetis' VAC offers it (ChannelMaster/ivac.c,
//                      paWinWasapiExclusive | paWinWasapiThreadPriority). The
//                      card's callback pulls from a short ring the drain fills.
//                      The card is ours alone while it runs: other programs --
//                      and our own sidetone / Quindar sinks on the same card --
//                      cannot play on it.
//
// No Q_OBJECT here (header only, no moc): the state change is a callback.

#include <QAudio>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QList>
#include <QStringList>
#include <QObject>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#if defined(Q_OS_WIN) && defined(HAVE_PORTAUDIO)
// Build 602 fix (MSVC log 21.11 22:58): no <windows.h> here -- through
// <mmdeviceapi.h> its min / max (and other) macros broke AudioEngine.cpp.
// PortAudio's own WASAPI header needs none of it; the endpoint is matched by
// its name.
#  include <portaudio.h>
#  include <pa_win_wasapi.h>
#  define AETHER_RX_EXCLUSIVE_AVAILABLE 1
#endif

namespace AetherSDR {

class RxSink : public QObject {
public:
    explicit RxSink(QObject* parent = nullptr) : QObject(parent) {}
    ~RxSink() override = default;

    virtual QIODevice* start() = 0;          // push mode
    virtual void stop() = 0;
    virtual void reset() = 0;
    virtual QAudio::State state() const = 0;
    virtual QAudio::Error error() const = 0;
    virtual qsizetype bytesFree() const = 0;
    virtual qsizetype bufferSize() const = 0;
    virtual qint64 processedUSecs() const = 0;
    virtual void setVolume(float v) = 0;
    virtual bool exclusive() const { return false; }
    virtual QString describe() const { return {}; }
    virtual unsigned underruns() const { return 0; }

    // reset() + start() without reporting the state changes in between.
    QIODevice* restartQuietly()
    {
        m_quiet = true;
        reset();
        QIODevice* io = start();
        m_quiet = false;
        return io;
    }

    std::function<void(QAudio::State)> onStateChanged;

protected:
    void notifyState(QAudio::State s)
    {
        if (!m_quiet && onStateChanged)
            onStateChanged(s);
    }
    bool m_quiet = false;
};

// ── The QAudioSink, as before ───────────────────────────────────────────────
class QtRxSink final : public RxSink {
public:
    QtRxSink(const QAudioDevice& dev, const QAudioFormat& fmt, QObject* parent = nullptr)
        : RxSink(parent), m_sink(new QAudioSink(dev, fmt, this))
    {
        QObject::connect(m_sink, &QAudioSink::stateChanged, this,
                         [this](QAudio::State s) { notifyState(s); });
    }
    void setBufferSize(qsizetype bytes) { m_sink->setBufferSize(bytes); }
    QIODevice* start() override { return m_sink->start(); }
    void stop() override { m_sink->stop(); }
    void reset() override { m_sink->reset(); }
    QAudio::State state() const override { return m_sink->state(); }
    QAudio::Error error() const override { return m_sink->error(); }
    qsizetype bytesFree() const override { return m_sink->bytesFree(); }
    qsizetype bufferSize() const override { return m_sink->bufferSize(); }
    qint64 processedUSecs() const override { return m_sink->processedUSecs(); }
    void setVolume(float v) override { m_sink->setVolume(v); }
    QString describe() const override { return QStringLiteral("QAudioSink (shared)"); }

private:
    QAudioSink* m_sink;
};

#ifdef AETHER_RX_EXCLUSIVE_AVAILABLE

// ── WASAPI exclusive through PortAudio ──────────────────────────────────────
class PaExclusiveRxSink final : public RxSink {
public:
    // The ring the drain fills: two drain periods (10 ms timer) of cushion.
    static constexpr int kRingMs = 20;

    // Opens (does not start) the stream; false with a reason when the card or
    // the format cannot be had exclusively.
    static std::unique_ptr<PaExclusiveRxSink> open(const QAudioDevice& dev, int rate,
                                                   QString* why)
    {
        std::unique_ptr<PaExclusiveRxSink> s(new PaExclusiveRxSink);
        if (!s->openStream(dev, rate, why))
            return {};
        return s;
    }

    ~PaExclusiveRxSink() override
    {
        if (m_stream) {
            Pa_AbortStream(m_stream);
            Pa_CloseStream(m_stream);
            m_stream = nullptr;
        }
        // PortAudio stays initialised (see openStream).
    }

    int rate() const { return m_rate; }

    QIODevice* start() override
    {
        if (!m_stream)
            return nullptr;
        if (!m_writer.isOpen())
            m_writer.open(QIODevice::WriteOnly);
        if (Pa_IsStreamActive(m_stream) != 1) {
            const PaError e = Pa_StartStream(m_stream);
            if (e != paNoError) {
                m_error = QAudio::OpenError;
                m_lastError = QString::fromUtf8(Pa_GetErrorText(e));
                return nullptr;
            }
        }
        m_state = QAudio::ActiveState;
        notifyState(m_state);
        return &m_writer;
    }
    void stop() override
    {
        if (m_stream && Pa_IsStreamStopped(m_stream) == 0)
            Pa_StopStream(m_stream);
        m_writer.close();
        m_state = QAudio::StoppedState;
        notifyState(m_state);
    }
    void reset() override { m_flush.store(true, std::memory_order_release); }
    QAudio::State state() const override { return m_state; }
    QAudio::Error error() const override { return m_error; }
    qsizetype bytesFree() const override
    {
        if (m_flush.load(std::memory_order_acquire))
            return bufferSize();
        return static_cast<qsizetype>(m_capFrames - fill()) * kFrameBytes;
    }
    qsizetype bufferSize() const override { return static_cast<qsizetype>(m_capFrames) * kFrameBytes; }
    qint64 processedUSecs() const override
    {
        return m_rate > 0
            ? static_cast<qint64>(m_consumed.load(std::memory_order_relaxed) * 1000000ULL / m_rate)
            : 0;
    }
    void setVolume(float v) override { m_gain.store(v, std::memory_order_relaxed); }
    bool exclusive() const override { return true; }
    QString describe() const override { return m_describe; }
    unsigned underruns() const override { return m_underruns.load(std::memory_order_relaxed); }
    QString lastError() const { return m_lastError; }
    QString refusedAttempts() const { return m_refused; }

private:
    static constexpr qsizetype kFrameBytes = 2 * static_cast<qsizetype>(sizeof(float));

    // The drain's QIODevice: writeData goes into the ring (single producer).
    class Writer final : public QIODevice {
    public:
        explicit Writer(PaExclusiveRxSink* o) : m_o(o) {}
        bool isSequential() const override { return true; }
    protected:
        qint64 readData(char*, qint64) override { return -1; }
        qint64 writeData(const char* data, qint64 len) override { return m_o->push(data, len); }
    private:
        PaExclusiveRxSink* m_o;
    };

    PaExclusiveRxSink() : RxSink(nullptr), m_writer(this) {}

    std::size_t fill() const
    {
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        return w - r;
    }

    qint64 push(const char* data, qint64 len)
    {
        if (m_ring.empty() || len <= 0)
            return 0;
        if (m_flush.load(std::memory_order_acquire))
            return len;   // being emptied: what comes now is dropped too
        const std::size_t frames = static_cast<std::size_t>(len / kFrameBytes);
        const std::size_t room = m_capFrames - fill();
        const std::size_t n = std::min(frames, room);
        const auto* in = reinterpret_cast<const float*>(data);
        std::size_t w = m_write.load(std::memory_order_relaxed);
        for (std::size_t k = 0; k < n; ++k, ++w) {
            const std::size_t i = (w % m_capFrames) * 2;
            m_ring[i] = in[2 * k];
            m_ring[i + 1] = in[2 * k + 1];
        }
        m_write.store(w, std::memory_order_release);
        return static_cast<qint64>(n) * kFrameBytes;
    }

    static int callback(const void*, void* output, unsigned long frames,
                        const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user)
    {
        auto* o = static_cast<PaExclusiveRxSink*>(user);
        auto* out = static_cast<float*>(output);
        if (o->m_flush.exchange(false, std::memory_order_acq_rel))
            o->m_read.store(o->m_write.load(std::memory_order_acquire), std::memory_order_release);
        const float g = o->m_gain.load(std::memory_order_relaxed);
        std::size_t r = o->m_read.load(std::memory_order_relaxed);
        const std::size_t have = o->m_write.load(std::memory_order_acquire) - r;
        const std::size_t n = std::min<std::size_t>(have, frames);
        for (std::size_t k = 0; k < n; ++k, ++r) {
            const std::size_t i = (r % o->m_capFrames) * 2;
            out[2 * k] = o->m_ring[i] * g;
            out[2 * k + 1] = o->m_ring[i + 1] * g;
        }
        o->m_read.store(r, std::memory_order_release);
        if (n < frames) {
            std::memset(out + 2 * n, 0, (frames - n) * kFrameBytes);
            if (o->m_wasFull)   // ran dry while audio was flowing
                o->m_underruns.fetch_add(1, std::memory_order_relaxed);
        }
        o->m_wasFull = (n == frames);
        o->m_consumed.fetch_add(frames, std::memory_order_relaxed);
        return paContinue;
    }

    // PortAudio's error and, for WASAPI, the HRESULT behind it (log 21.11
    // 23:07: only "Invalid device", its word for a refused IAudioClient).
    static QString hostReason(PaError e)
    {
        QString out = QString::fromUtf8(Pa_GetErrorText(e));
        const PaHostErrorInfo* he = Pa_GetLastHostErrorInfo();
        if (!he || he->hostApiType != paWASAPI || he->errorCode == 0)
            return out;
        const auto hr = static_cast<unsigned long>(he->errorCode);
        QString what;
        if (hr == 0x8889000AUL)
            what = QStringLiteral("the card is in use by another program");
        else if (hr == 0x8889000EUL)
            what = QStringLiteral("Windows does not allow exclusive mode on this card "
                                  "(Sound -> device -> Properties -> Advanced)");
        else if (hr == 0x88890008UL)
            what = QStringLiteral("format not supported exclusively");
        else if (hr == 0x88890019UL)
            what = QStringLiteral("buffer size not aligned");
        else if (hr == 0x88890020UL)
            what = QStringLiteral("buffer size error");
        out += QStringLiteral(" [WASAPI 0x%1").arg(hr, 8, 16, QLatin1Char('0'));
        if (he->errorText && *he->errorText)
            out += QStringLiteral(" ") + QString::fromUtf8(he->errorText).trimmed();
        if (!what.isEmpty())
            out += QStringLiteral(": ") + what;
        out += QStringLiteral("]");
        return out;
    }

    bool openStream(const QAudioDevice& dev, int rate, QString* why)
    {
        const auto fail = [why](const QString& w) {
            if (why)
                *why = w;
            return false;
        };
        // Build 604: initialised once and kept -- Pa_Initialize enumerates
        // every host API and took 2.5 s on the audio thread (log 21.11
        // 23:07:02 -> 05), each time the output was reopened.
        static const bool paReady = (Pa_Initialize() == paNoError);
        if (!paReady)
            return fail(QStringLiteral("Pa_Initialize failed"));
        const PaHostApiIndex api = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
        if (api < 0)
            return fail(QStringLiteral("no WASAPI host API in PortAudio"));
        // The WASAPI endpoint the operator chose, by its friendly name (the
        // same string Qt shows), among WASAPI outputs.
        PaDeviceIndex pick = paNoDevice;
        const PaDeviceIndex count = Pa_GetDeviceCount();
        for (PaDeviceIndex i = 0; i < count && pick == paNoDevice; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->hostApi == api && info->maxOutputChannels >= 2 && info->name
                && QString::fromUtf8(info->name).trimmed() == dev.description().trimmed())
                pick = i;
        }
        if (dev.isNull() && pick == paNoDevice) {
            const PaHostApiInfo* a = Pa_GetHostApiInfo(api);
            if (a)
                pick = a->defaultOutputDevice;
        }
        if (pick == paNoDevice)
            return fail(QStringLiteral("no WASAPI output matches \"%1\"").arg(dev.description()));
        const PaDeviceInfo* info = Pa_GetDeviceInfo(pick);

        PaWasapiStreamInfo wi{};
        wi.size = sizeof(PaWasapiStreamInfo);
        wi.hostApiType = paWASAPI;
        wi.version = 1;
        wi.flags = paWinWasapiExclusive | paWinWasapiThreadPriority;
        wi.threadPriority = eThreadPriorityProAudio;
        PaStreamParameters op{};
        op.device = pick;
        op.channelCount = 2;
        op.sampleFormat = paFloat32;
        op.suggestedLatency = info->defaultLowOutputLatency;
        op.hostApiSpecificStreamInfo = &wi;

        // Build 605, as MicronSDR's pa19 does it (operator, 21.11: "I met
        // this problem in MicronSDR, see how the output is done there"): a
        // card can take 48 kHz shared (through the Windows mixer) and refuse
        // it exclusively -- many USB headsets run 44.1 kHz natively. So 48 kHz,
        // then 44.1 kHz (AudioEngine resamples to whatever rate the card
        // runs at), each actually opened AND started, not only asked
        // (Pa_IsFormatSupported passed 48 kHz and the open still failed, log
        // 21.11 23:07), with the card's own low latency and then one 10 ms
        // period.
        QList<int> rates{rate};
        if (!rates.contains(44100))
            rates << 44100;
        if (!rates.contains(static_cast<int>(info->defaultSampleRate)))
            rates << static_cast<int>(info->defaultSampleRate);
        const double latencies[2] = {info->defaultLowOutputLatency, 0.010};
        QStringList refused;
        for (int r : rates) {
            for (double lat : latencies) {
                op.suggestedLatency = lat;
                m_rate = r;
                m_capFrames = static_cast<std::size_t>(r) * kRingMs / 1000;
                m_ring.assign(2 * m_capFrames, 0.0f);
                PaError e = Pa_OpenStream(&m_stream, nullptr, &op, r,
                                          paFramesPerBufferUnspecified, paNoFlag,
                                          &PaExclusiveRxSink::callback, this);
                if (e == paNoError) {
                    e = Pa_StartStream(m_stream);
                    if (e != paNoError) {
                        Pa_CloseStream(m_stream);
                    }
                }
                if (e == paNoError)
                    break;
                m_stream = nullptr;
                refused << QStringLiteral("%1 Hz / %2 ms: %3").arg(r)
                               .arg(lat * 1000.0, 0, 'f', 1).arg(hostReason(e));
            }
            if (m_stream)
                break;
        }
        if (!refused.isEmpty())
            m_refused = refused.join(QStringLiteral("; "));
        const QString lastErr = m_refused;
        if (!m_stream)
            return fail(QStringLiteral("\"%1\": %2").arg(QString::fromUtf8(info->name), lastErr));
        const PaStreamInfo* si = Pa_GetStreamInfo(m_stream);
        m_describe = QStringLiteral("WASAPI exclusive \"%1\", %2 Hz, device latency %3 ms, ring %4 ms")
                         .arg(QString::fromUtf8(info->name))
                         .arg(m_rate)
                         .arg(si ? si->outputLatency * 1000.0 : -1.0, 0, 'f', 1)
                         .arg(kRingMs);
        if (!m_refused.isEmpty())
            m_describe += QStringLiteral(" (refused before: %1)").arg(m_refused);
        return true;
    }

    PaStream* m_stream = nullptr;
    int m_rate = 0;
    std::size_t m_capFrames = 0;
    std::vector<float> m_ring;
    std::atomic<std::size_t> m_write{0};
    std::atomic<std::size_t> m_read{0};
    std::atomic<bool> m_flush{false};
    std::atomic<float> m_gain{1.0f};
    std::atomic<unsigned long long> m_consumed{0};
    std::atomic<unsigned> m_underruns{0};
    bool m_wasFull = false;   // callback thread only
    QAudio::State m_state = QAudio::StoppedState;
    QAudio::Error m_error = QAudio::NoError;
    QString m_describe;
    QString m_lastError;
    QString m_refused;   // build 605: the attempts the card refused
    Writer m_writer;
};

#endif // AETHER_RX_EXCLUSIVE_AVAILABLE

} // namespace AetherSDR
