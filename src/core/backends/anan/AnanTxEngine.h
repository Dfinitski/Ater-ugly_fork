#pragma once

// ANAN-100D port (operator, 01.11): "sending packets to the radio is the most
// time-critical task; the transmitter in its own thread at the highest
// priority, synchronised with the device -- as Thetis does".
//
// The transmit engine lives on its own thread ("anan-tx", time critical) with
// the modulator (Hl2TxDsp, WDSP TXA). The radio's microphone packets (64
// frames at 48 kHz, port 1026, the radio's codec clock) come straight from
// the I/O thread -- no GUI thread anywhere on the way:
//
//   radio mic packet --> high-pass, MIC gain, fade-in --> TXA --> DUC queue
//                                                               |
//   ... and the same packet is the clock: 256 DUC samples of radio time,
//   P2Transport::txDeviceTick() sends the DUC packets due by then.
//
// Before (log 22:58): the mic path crossed the GUI thread, and every stall of
// the window (a repaint, a drag, an audio restart) left the DUC queue empty
// -- 4-6 clicks per over on the air. Now the modulator runs in step with the
// DUC by construction; the queue level cannot drift.
//
// Computer audio (mic, TCI, DAX) still reaches Hl2TxDsp by queued calls on
// the PC's clock; the radio's mic packets keep clocking the DUC regardless.
// FreeDV's modem (libfdv, one-thread API on the GUI thread) is the only
// detour left: while it transmits, mic blocks go to the GUI and back.

#include "core/TxCoordinator.h"
#include "core/backends/anan/AnanTxMonitor.h"
#include "core/fdv/FdvEngine.h"

#include <QObject>

#include <atomic>
#include <complex>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace AetherSDR::hl2 {
class Hl2TxDsp;
}

namespace AetherSDR::anan {

class P2Transport;

class AnanTxEngine : public QObject {
    Q_OBJECT
public:
    // No parent: it moves to the transmit thread.
    AnanTxEngine(P2Transport* transport, hl2::Hl2TxDsp* txDsp);

    // ---- settings, any thread ----
    void setMicFromRadio(bool on) { m_micFromRadio.store(on, std::memory_order_relaxed); }
    void setTestTone(bool on) { m_testTone.store(on, std::memory_order_relaxed); }
    void setMicGainLinear(float g) { m_micGain.store(g, std::memory_order_relaxed); }
    void setKeyed(bool on) { m_keyed.store(on, std::memory_order_relaxed); }
    void setCwSilent(bool on) { m_cwSilent.store(on, std::memory_order_relaxed); }
    void setMonitorWanted(bool on) { m_monWanted.store(on, std::memory_order_relaxed); }
    void setFdvTxRunning(bool on) { m_fdvTx.store(on, std::memory_order_relaxed); }
    void setContext(const TxCoordinator::Context& ctx)
    {
        std::lock_guard<std::mutex> lock(m_ctxMutex);
        m_ctx = ctx;
    }
    // The radio's PTT was pressed: start the TX capture now (diagnostics).
    void armCapture() { m_capArmed.store(true, std::memory_order_relaxed); }

    // ---- callbacks, set before the thread starts; called on the TX thread ----
    std::function<void(float peakDb, float rmsDb)> onMicLevel;
    // TX capture finished (txcap-mic.wav): raw mic, what the modulator got,
    // radio PTT / host key offsets in samples (-1 = none).
    std::function<void(std::vector<float> raw, std::vector<float> mod, long long pttAt, long long keyAt)>
        onCaptureDone;
    // FreeDV transmitting: the mic block for libfdv (GUI thread), which hands
    // the modem audio to the modulator itself.
    std::function<void(std::vector<float> mono, TxCoordinator::Context ctx)> onFdvTx;
    // MON: the transmitted IQ demodulated (24 kHz mono), while wanted.
    std::function<void(const std::vector<float>& mon)> onMonitor;

    // Connects to the modulator's output (call once, before moveToThread).
    void wire();

public slots:
    // I/O thread -> here (queued): one radio mic packet, 48 kHz mono.
    void onMicPacket(const std::vector<float>& samples48k);

private:
    void onIq(const std::vector<std::complex<float>>& iq, const TxCoordinator::Context& context);
    void micChain(std::vector<float>& mono24, bool keyed, const TxCoordinator::Context& ctx);
    void captureRaw(const std::vector<float>& raw, bool keyed);
    void captureMod(const std::vector<float>* mod, std::size_t n);
    void captureWrite();
    void logOver();

    P2Transport* m_transport = nullptr;
    hl2::Hl2TxDsp* m_txDsp = nullptr;

    std::atomic<bool> m_micFromRadio{false};
    std::atomic<bool> m_testTone{false};
    std::atomic<float> m_micGain{1.0f};
    std::atomic<bool> m_keyed{false};
    std::atomic<bool> m_cwSilent{false};
    std::atomic<bool> m_monWanted{false};
    std::atomic<bool> m_fdvTx{false};
    std::atomic<bool> m_capArmed{false};
    std::mutex m_ctxMutex;
    TxCoordinator::Context m_ctx;

    // ---- transmit thread only ----
    fdv::HalfbandResampler m_down;
    std::vector<float> m_scratch;
    double m_testPh1 = 0.0, m_testPh2 = 0.0;
    float m_hpX1 = 0.0f, m_hpX2 = 0.0f, m_hpY1 = 0.0f, m_hpY2 = 0.0f;
    float m_meterPeak = 0.0f;
    double m_meterSumSq = 0.0;
    int m_meterCount = 0;
    bool m_wasKeyed = false;
    int m_keyedSamples = 0;
    // Per-over statistics (log at unkey) and the 250 ms traces.
    double m_overPeak = 0.0, m_overSumSq = 0.0, m_overLfSumSq = 0.0, m_overLf = 0.0;
    long long m_overCount = 0;
    std::vector<double> m_trRaw, m_trMod, m_trModPeak;
    double m_trRawSum = 0.0, m_trModSum = 0.0, m_trModPk = 0.0;
    int m_trRawN = 0, m_trModN = 0;
    // TX capture (txcap-mic.wav).
    std::deque<float> m_capPre;
    std::vector<float> m_capRaw, m_capMod;
    bool m_capActive = false;
    long long m_capPttAt = -1, m_capKeyAt = -1;
    // MON.
    AnanTxMonitor m_mon;
    std::vector<float> m_monOut;
    std::vector<std::complex<float>> m_zeros;
    bool m_fdvWarned = false;
    bool m_raised = false;
};

}  // namespace AetherSDR::anan
