// FreeDV digital voice engine (libfdv wrapper), see FdvEngine.h.
// Ported from MicronSDR FdvUnit.cpp.
#include "FdvEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

Q_LOGGING_CATEGORY(lcFdv, "aether.fdv")

namespace AetherSDR::fdv {

// ---------------------------------------------------------------------------
// HalfbandResampler

HalfbandResampler::HalfbandResampler()
{
    // Low-pass at a quarter of 48 kHz (12 kHz), Blackman window.
    const int mid = kTaps / 2;
    double sum = 0.0;
    for (int i = 0; i < kTaps; ++i) {
        const int k = i - mid;
        const double x = 0.5 * static_cast<double>(k);   // fc = 0.25 cycles/sample
        const double sinc = k == 0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
        const double w = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kTaps - 1))
                       + 0.08 * std::cos(4.0 * std::numbers::pi * i / (kTaps - 1));
        m_h[static_cast<std::size_t>(i)] = static_cast<float>(sinc * w);
        sum += sinc * w;
    }
    for (float& h : m_h)
        h = static_cast<float>(h / sum);   // unity DC gain
}

void HalfbandResampler::reset()
{
    m_hist.fill(0.0f);
    m_pos = 0;
}

float HalfbandResampler::push(float x)
{
    m_hist[static_cast<std::size_t>(m_pos)] = x;
    float acc = 0.0f;
    int idx = m_pos;
    for (int i = 0; i < kTaps; ++i) {
        acc += m_h[static_cast<std::size_t>(i)] * m_hist[static_cast<std::size_t>(idx)];
        idx = idx == 0 ? kTaps - 1 : idx - 1;
    }
    m_pos = (m_pos + 1) % kTaps;
    return acc;
}

void HalfbandResampler::up(const float* in, std::size_t n, std::vector<float>& out)
{
    out.resize(2 * n);
    for (std::size_t i = 0; i < n; ++i) {
        out[2 * i] = 2.0f * push(in[i]);       // zero-stuffing halves the level
        out[2 * i + 1] = 2.0f * push(0.0f);
    }
}

void HalfbandResampler::down(const float* in, std::size_t n, std::vector<float>& out)
{
    out.resize(n / 2);
    for (std::size_t i = 0; i + 1 < n; i += 2) {
        out[i / 2] = push(in[i]);
        push(in[i + 1]);
    }
}

// ---------------------------------------------------------------------------
// FdvEngine

// The C API of libfdv (fdv_api.h); fdv_status_t has the layout of Status.
struct FdvEngine::Api {
    int (*version)(void) = nullptr;
    void* (*create)(int mode) = nullptr;
    void (*destroy)(void* f) = nullptr;
    void (*setMode)(void* f, int mode) = nullptr;
    void (*setCallsign)(void* f, const char* cs) = nullptr;
    void (*rxWrite)(void* f, const float* in, int n) = nullptr;
    int (*rxRead)(void* f, float* out, int n) = nullptr;
    void (*txStart)(void* f) = nullptr;
    void (*txStop)(void* f) = nullptr;
    void (*txWrite)(void* f, const float* mic, int n) = nullptr;
    int (*txRead)(void* f, float* out, int n) = nullptr;
    void (*status)(void* f, Status* st) = nullptr;
    void (*setTextMode)(void* f, int reliableCallsign) = nullptr;   // 1.10 on
    QLibrary* lib = nullptr;
};

// The decoded speech (FARGAN / codec2, about -18 dBFS rms) up to the level of
// ordinary receive audio -- MicronSDR's RX_SPEECH_GAIN.
static constexpr float kRxSpeechGain = 2.0f;

FdvEngine::FdvEngine() = default;

FdvEngine::~FdvEngine()
{
    if (m_api && m_obj)
        m_api->destroy(m_obj);
    delete m_api;   // the QLibrary stays loaded for the process (no unload)
}

bool FdvEngine::load()
{
    if (m_obj)
        return true;
    if (m_tried)
        return false;
    m_tried = true;
    // Next to the executable first (CMake copies it there), then the search path.
    const QString appDir = QCoreApplication::applicationDirPath();
    auto* lib = new QLibrary(QDir(appDir).filePath(QStringLiteral("libfdv")));
    if (!lib->load()) {
        const QString firstErr = lib->errorString();
        lib->setFileName(QStringLiteral("libfdv"));
        if (!lib->load()) {
            m_err = QStringLiteral("libfdv.dll / libcodec2.dll not found next to AetherSDR.exe (%1): %2")
                        .arg(QDir::toNativeSeparators(appDir), firstErr);
            qCWarning(lcFdv).noquote() << "FreeDV:" << m_err;
            delete lib;
            return false;
        }
    }
    auto* a = new Api;
    a->lib = lib;
    bool ok = true;
    auto r = [&](const char* name) {
        QFunctionPointer f = lib->resolve(name);
        if (!f) {
            ok = false;
            qCWarning(lcFdv) << "FreeDV: libfdv has no" << name;
        }
        return f;
    };
    a->version = reinterpret_cast<decltype(a->version)>(r("fdv_version"));
    a->create = reinterpret_cast<decltype(a->create)>(r("fdv_create"));
    a->destroy = reinterpret_cast<decltype(a->destroy)>(r("fdv_destroy"));
    a->setMode = reinterpret_cast<decltype(a->setMode)>(r("fdv_set_mode"));
    a->setCallsign = reinterpret_cast<decltype(a->setCallsign)>(r("fdv_set_callsign"));
    a->rxWrite = reinterpret_cast<decltype(a->rxWrite)>(r("fdv_rx_write"));
    a->rxRead = reinterpret_cast<decltype(a->rxRead)>(r("fdv_rx_read"));
    a->txStart = reinterpret_cast<decltype(a->txStart)>(r("fdv_tx_start"));
    a->txStop = reinterpret_cast<decltype(a->txStop)>(r("fdv_tx_stop"));
    a->txWrite = reinterpret_cast<decltype(a->txWrite)>(r("fdv_tx_write"));
    a->txRead = reinterpret_cast<decltype(a->txRead)>(r("fdv_tx_read"));
    a->status = reinterpret_cast<decltype(a->status)>(r("fdv_status"));
    if (!ok) {
        m_err = QStringLiteral("libfdv.dll is not the MicronSDR build");
        delete a;
        return false;
    }
    a->setTextMode = reinterpret_cast<decltype(a->setTextMode)>(lib->resolve("fdv_set_text_mode"));
    m_api = a;
    m_obj = m_api->create(m_mode);
    if (!m_obj) {
        m_err = QStringLiteral("fdv_create failed");
        return false;
    }
    m_api->setCallsign(m_obj, m_callsign.toLatin1().constData());
    // 1600 / 700D / 700E: send the callsign on the text channel the way
    // FreeDV-GUI does (reliable text), not a free message.
    if (m_api->setTextMode)
        m_api->setTextMode(m_obj, 1);
    qCInfo(lcFdv) << "FreeDV: libfdv" << m_api->version() << "loaded, mode" << m_mode;
    return true;
}

void FdvEngine::setLibMode(int libMode)
{
    if (libMode == m_mode && m_obj)
        return;
    m_mode = libMode;
    if (m_obj)
        m_api->setMode(m_obj, m_mode);
}

void FdvEngine::setCallsign(const QString& callsign)
{
    if (callsign.left(8) == m_callsign && m_tried)
        return;
    m_callsign = callsign.left(8);
    if (m_obj)
        m_api->setCallsign(m_obj, m_callsign.toLatin1().constData());
}

void FdvEngine::setRxReceiver(int rx)
{
    if (rx == m_rxReceiver)
        return;
    m_rxReceiver = rx;
    m_rxUp.reset();
    m_rxDown.reset();
}

void FdvEngine::processRx(std::vector<float>& stereo)
{
    if (!m_obj || stereo.size() < 2)
        return;
    const std::size_t frames = stereo.size() / 2;
    m_ra.resize(frames);
    for (std::size_t i = 0; i < frames; ++i)
        m_ra[i] = 0.5f * (stereo[2 * i] + stereo[2 * i + 1]);
    m_rxUp.up(m_ra.data(), frames, m_rb);                     // 48 kHz
    m_api->rxWrite(m_obj, m_rb.data(), static_cast<int>(m_rb.size()));
    m_api->rxRead(m_obj, m_rb.data(), static_cast<int>(m_rb.size()));
    m_rxDown.down(m_rb.data(), m_rb.size(), m_rc);             // 24 kHz
    for (std::size_t i = 0; i < frames && i < m_rc.size(); ++i) {
        const float s = std::clamp(m_rc[i] * kRxSpeechGain, -1.0f, 1.0f);
        stereo[2 * i] = s;
        stereo[2 * i + 1] = s;
    }
}

void FdvEngine::txStart()
{
    if (!m_obj)
        return;
    m_txUp.reset();
    m_txDown.reset();
    m_api->txStart(m_obj);
    m_txRunning = true;
}

void FdvEngine::txStop()
{
    if (m_obj && m_txRunning)
        m_api->txStop(m_obj);
}

void FdvEngine::processTx(std::vector<float>& mono)
{
    if (!m_obj || !m_txRunning || mono.empty())
        return;
    m_txUp.up(mono.data(), mono.size(), m_tb);               // 48 kHz
    m_api->txWrite(m_obj, m_tb.data(), static_cast<int>(m_tb.size()));
    m_api->txRead(m_obj, m_tb.data(), static_cast<int>(m_tb.size()));
    m_txDown.down(m_tb.data(), m_tb.size(), m_tc);             // 24 kHz
    for (std::size_t i = 0; i < mono.size() && i < m_tc.size(); ++i)
        mono[i] = m_tc[i];
}

bool FdvEngine::status(Status* st)
{
    if (!m_obj)
        return false;
    m_api->status(m_obj, st);
    if (!st->txActive)
        m_txRunning = false;
    return true;
}

}  // namespace AetherSDR::fdv
