#include "core/backends/anan/AnanTxEngine.h"

#include "core/LogManager.h"
#include "core/backends/TxAudioSource.h"
#include "core/backends/anan/AnanNativeUdp.h"
#include "core/backends/anan/P2Transport.h"
#include "core/backends/hl2/Hl2TxDsp.h"

#include <QString>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace AetherSDR::anan {

AnanTxEngine::AnanTxEngine(P2Transport* transport, hl2::Hl2TxDsp* txDsp)
    : QObject(nullptr), m_transport(transport), m_txDsp(txDsp)
{
}

void AnanTxEngine::wire()
{
    // Same thread (both live on "anan-tx"): a direct call from inside
    // Hl2TxDsp::processAudioBlock().
    connect(m_txDsp, &hl2::Hl2TxDsp::iqReady, this,
            [this](const std::vector<std::complex<float>>& iq, const TxCoordinator::Context& ctx) {
        onIq(iq, ctx);
    }, Qt::DirectConnection);
}

void AnanTxEngine::onIq(const std::vector<std::complex<float>>& iq, const TxCoordinator::Context& context)
{
    // The FPGA keyer's carrier only (CW): the modulator's IQ goes out as zeros.
    if (m_cwSilent.load(std::memory_order_relaxed)) {
        m_zeros.assign(iq.size(), std::complex<float>{});
        m_transport->queueTxIq(m_zeros, context);
        return;
    }
    m_transport->queueTxIq(iq, context);
    // MON (operator, 01.11: "the sound that goes to the transmitter"): this
    // very IQ, demodulated -- every mode, every source.
    if (m_monWanted.load(std::memory_order_relaxed) && onMonitor) {
        m_monOut.clear();
        m_mon.process(iq.data(), iq.size(), m_txDsp->txMode(), m_monOut);
        if (!m_monOut.empty())
            onMonitor(m_monOut);
    }
}

void AnanTxEngine::onMicPacket(const std::vector<float>& s48)
{
    if (!m_raised) {   // first packet: this is the transmit thread
        m_raised = true;
        raiseCurrentThreadToTimeCritical();
    }
    const bool fromRadio = m_micFromRadio.load(std::memory_order_relaxed);
    if (fromRadio && !s48.empty()) {
        m_down.down(s48.data(), s48.size(), m_scratch);
        const bool keyed = m_keyed.load(std::memory_order_relaxed);
        TxCoordinator::Context ctx;
        {
            std::lock_guard<std::mutex> lock(m_ctxMutex);
            ctx = m_ctx;
        }
        micChain(m_scratch, keyed, ctx);
    }
    // The radio's clock: this packet is 64 frames at 48 kHz = 256 DUC
    // samples at 192 kHz. After the modulation above, so its IQ is queued.
    m_transport->txDeviceTick(static_cast<int>(s48.size()) * 4);
}

void AnanTxEngine::micChain(std::vector<float>& mono, bool keyed, const TxCoordinator::Context& ctx)
{
    // TX capture (diagnostics): the raw microphone, before any filter.
    captureRaw(mono, keyed);
    const bool testTone = m_testTone.load(std::memory_order_relaxed);
    if (testTone) {
        // Mic source TEST: the two-tone instead of the microphone.
        constexpr double kTwoPi = 6.283185307179586;
        for (float& v : mono) {
            v = static_cast<float>(0.5 * std::sin(m_testPh1) + 0.5 * std::sin(m_testPh2));
            m_testPh1 += kTwoPi * 700.0 / 24000.0;
            m_testPh2 += kTwoPi * 1900.0 / 24000.0;
            if (m_testPh1 > kTwoPi) m_testPh1 -= kTwoPi;
            if (m_testPh2 > kTwoPi) m_testPh2 -= kTwoPi;
        }
    } else {
        // Speech high-pass, 2nd-order Butterworth at 120 Hz (24 kHz): the mic
        // line carries bias, hum and the PTT switch's step; the peak ALC
        // would level on them. Runs always, so it has settled before a key.
        constexpr float b0 = 0.97803048f, b1 = -1.95606096f, b2 = 0.97803048f;
        constexpr float a1 = -1.95557824f, a2 = 0.95654368f;
        for (float& v : mono) {
            float y = b0 * v + b1 * m_hpX1 + b2 * m_hpX2 - a1 * m_hpY1 - a2 * m_hpY2;
            if (std::abs(y) < 1e-20f)
                y = 0.0f;
            m_hpX2 = m_hpX1;
            m_hpX1 = v;
            m_hpY2 = m_hpY1;
            m_hpY1 = y;
            v = y;
        }
    }
    for (float v : mono) {
        m_meterPeak = std::max(m_meterPeak, std::abs(v));
        m_meterSumSq += double(v) * v;
    }
    m_meterCount += static_cast<int>(mono.size());
    if (m_meterCount >= 1200) {   // 50 ms at 24 kHz
        const float rms = static_cast<float>(std::sqrt(m_meterSumSq / m_meterCount));
        if (onMicLevel)
            onMicLevel(20.0f * std::log10(std::max(m_meterPeak, 1e-7f)), 20.0f * std::log10(std::max(rms, 1e-7f)));
        m_meterPeak = 0.0f;
        m_meterSumSq = 0.0;
        m_meterCount = 0;
    }
    if (!keyed && m_wasKeyed && m_overCount > 0)
        logOver();
    if (!keyed)
        m_wasKeyed = false;
    if (!keyed || mono.empty() || !ctx.permitsDispatch(TxCoordinator::monotonicMs())) {
        captureMod(nullptr, mono.size());
        return;
    }
    for (float v : mono) {
        m_trRawSum += double(v) * v;
        if (++m_trRawN == 6000) {   // 250 ms
            if (m_trRaw.size() < 24)
                m_trRaw.push_back(std::sqrt(m_trRawSum / 6000.0));
            m_trRawSum = 0.0;
            m_trRawN = 0;
        }
        m_overPeak = std::max(m_overPeak, double(std::abs(v)));
        m_overSumSq += double(v) * v;
        m_overLf += 0.0385 * (double(v) - m_overLf);   // ~150 Hz one-pole low-pass
        m_overLfSumSq += m_overLf * m_overLf;
    }
    m_overCount += static_cast<long long>(mono.size());
    if (!m_wasKeyed) {
        m_wasKeyed = true;
        m_keyedSamples = 0;
    }
    // A 5 ms rise at the key so the compressor does not meet a step (the
    // transmitter-ready wait is P2Transport's RF Delay).
    constexpr int kRamp = 120;
    if (m_keyedSamples < kRamp) {
        for (float& v : mono) {
            const int n = m_keyedSamples++;
            if (n < kRamp)
                v *= 0.5f - 0.5f * std::cos(3.14159265f * float(n) / float(kRamp));
        }
    }
    // MIC gain here, before PROC (TXA's compressor): the radio microphone
    // arrives ~40 dB below full scale. The TEST two-tone is full scale.
    if (!testTone) {
        const float g = m_micGain.load(std::memory_order_relaxed);
        for (float& v : mono)
            v *= g;
    }
    captureMod(&mono, mono.size());
    for (float v : mono) {
        m_trModSum += double(v) * v;
        m_trModPk = std::max(m_trModPk, double(std::abs(v)));
        if (++m_trModN == 6000) {
            if (m_trMod.size() < 24) {
                m_trMod.push_back(std::sqrt(m_trModSum / 6000.0));
                m_trModPeak.push_back(m_trModPk);
            }
            m_trModSum = 0.0;
            m_trModPk = 0.0;
            m_trModN = 0;
        }
    }
    if (m_fdvTx.load(std::memory_order_relaxed) && onFdvTx) {
        if (!m_fdvWarned) {
            m_fdvWarned = true;
            qCInfo(lcHl2) << "ANAN TX engine: FreeDV transmitting -- mic blocks through libfdv (GUI thread)";
        }
        m_transport->setTxPrimeSamples(4096, true);   // through the GUI and back
        onFdvTx(mono, ctx);
        return;
    }
    m_fdvWarned = false;
    // In step with the DUC clock: one TXA block (1024) + two packets.
    m_transport->setTxPrimeSamples(1536);
    m_txDsp->processAudioBlock(mono, TxAudioSource::EngineGenerated, ctx);
}

void AnanTxEngine::logOver()
{
    // One line per over: what the radio microphone delivered (after the
    // high-pass, before mic gain). Pairs with P2Transport's "TX over" line.
    const double rms = std::sqrt(m_overSumSq / double(m_overCount));
    const double lf = std::sqrt(m_overLfSumSq / double(m_overCount));
    qCInfo(lcHl2).nospace()
        << "ANAN TX over (radio mic" << (m_testTone.load() ? ", TEST two-tone" : "")
        << ", TX thread): mic peak " << QString::number(20.0 * std::log10(std::max(m_overPeak, 1e-9)), 'f', 1)
        << " dBFS, rms " << QString::number(20.0 * std::log10(std::max(rms, 1e-9)), 'f', 1)
        << " dBFS, below 150 Hz " << QString::number(20.0 * std::log10(std::max(lf, 1e-9)), 'f', 1)
        << " dBFS, mic gain x" << QString::number(m_micGain.load(), 'f', 2);
    auto trace = [](const std::vector<double>& v) {
        QString t;
        for (double x : v)
            t += QString::number(20.0 * std::log10(std::max(x, 1e-9)), 'f', 0) + QLatin1Char(' ');
        return t;
    };
    qCInfo(lcHl2).nospace() << "ANAN TX trace per 250 ms: raw mic rms [" << trace(m_trRaw)
                            << "] -> modulator rms [" << trace(m_trMod) << "] peak [" << trace(m_trModPeak)
                            << "]";
    m_trRaw.clear();
    m_trMod.clear();
    m_trModPeak.clear();
    m_trRawSum = m_trModSum = m_trModPk = 0.0;
    m_trRawN = m_trModN = 0;
    m_overPeak = m_overSumSq = m_overLfSumSq = 0.0;
    m_overCount = 0;
}

// ---- TX capture (txcap-mic.wav, written by Hl2Backend on the GUI thread) ----
// 24 kHz: left = the raw radio microphone, right = what the modulator got.
// From 1.5 s before the key (or the radio PTT press) to the unkey, <= 20 s.
void AnanTxEngine::captureRaw(const std::vector<float>& raw, bool keyed)
{
    constexpr std::size_t kPre = 36000;     // 1.5 s
    constexpr std::size_t kMax = 480000;    // 20 s
    const bool armed = m_capArmed.exchange(false, std::memory_order_relaxed);
    if (m_capActive && armed && m_capPttAt < 0)
        m_capPttAt = static_cast<long long>(m_capRaw.size());
    if (!m_capActive && (keyed || armed)) {
        m_capActive = true;
        m_capRaw.assign(m_capPre.begin(), m_capPre.end());
        m_capMod.assign(m_capRaw.size(), 0.0f);
        m_capPttAt = armed ? static_cast<long long>(m_capRaw.size()) : -1;
        m_capKeyAt = -1;
    }
    if (m_capActive) {
        if (keyed && m_capKeyAt < 0)
            m_capKeyAt = static_cast<long long>(m_capRaw.size());
        if (m_capRaw.size() < kMax)
            m_capRaw.insert(m_capRaw.end(), raw.begin(), raw.end());
        const long long len = static_cast<long long>(m_capRaw.size());
        if (!keyed && m_capKeyAt >= 0)
            captureWrite();                       // the unkey
        else if (!keyed && m_capKeyAt < 0 && len - std::max(0LL, m_capPttAt) > 12000)
            captureWrite();                       // radio PTT, no key within 0.5 s
        else if (static_cast<std::size_t>(len) >= kMax)
            captureWrite();
    } else {
        m_capPre.insert(m_capPre.end(), raw.begin(), raw.end());
        while (m_capPre.size() > kPre)
            m_capPre.pop_front();
    }
}

void AnanTxEngine::captureMod(const std::vector<float>* mod, std::size_t n)
{
    if (!m_capActive)
        return;
    if (m_capMod.size() + n > m_capRaw.size())
        n = m_capRaw.size() > m_capMod.size() ? m_capRaw.size() - m_capMod.size() : 0;
    if (mod)
        m_capMod.insert(m_capMod.end(), mod->begin(), mod->begin() + static_cast<std::ptrdiff_t>(n));
    else
        m_capMod.insert(m_capMod.end(), n, 0.0f);
}

void AnanTxEngine::captureWrite()
{
    m_capActive = false;
    m_capPre.clear();
    if (onCaptureDone)
        onCaptureDone(std::move(m_capRaw), std::move(m_capMod), m_capPttAt, m_capKeyAt);
    m_capRaw.clear();
    m_capMod.clear();
}

}  // namespace AetherSDR::anan
