#pragma once

// ANAN-100D port: openHPSDR Protocol 2 transport for Hl2Backend.
//
// Hl2Backend (receivers, modes, filters, NR/notch/APF, TX chain, ...) drives
// its radio only through the hl2::MetisClient transport interface; this class
// implements it over Protocol 2 -- Discovery / General / DDC-Specific / High
// Priority, DDC IQ from 1035 + n, DUC IQ to 1029, codec audio 1028/1026 --
// built with the packet codecs verified on an ANAN-100D (P2Protocol.h).
//
// Mapping of the interface concepts (inherited from the HL2's Protocol 1):
//   receiver i              -> hardware DDC (profile.rxDdcBase + i)
//   setSampleRate           -> every enabled DDC's rate (DDC-Specific)
//   setLnaGainDb ("RF gain") -> step attenuator: att = -gain (0..31 dB)
//   setBandFilter           -> ignored; Alex filters follow receiver 0's frequency
//   sequence gaps           -> per-DDC sequence numbers
// HL2-only controls (CL1 clock, IO board, ATU request, EP4 bandscope, RQST/ACK
// registers) are no-ops here.
//
// ANAN port (build 194): the same boards with Protocol 1 firmware. One radio
// state for both; the protocol is the one the radio answers discovery with
// (Protocol 2 preferred), and only the wire differs: Protocol 1 carries the
// state as the C&C rotation of the EP2 packets (P1Protocol.h, after Thetis
// networkproto1.c), sent on the radio's clock (one per 126 codec samples of
// EP6 traffic) with the speaker audio; the receivers come from the DDCs of
// the Thetis plan in the EP6 packets, into the same alignment queues.
// Transmit (build 197): the same TX path at 192 ksps, decimated 4:1 to the
// 48 ksps I/Q of the EP2 packets; the radio mic from EP6 drives the modulator
// as on Protocol 2. PureSignal: not yet.

#include "core/backends/hl2/MetisClient.h"
#include "core/backends/anan/P2Protocol.h"
#include "core/backends/anan/P1Protocol.h"
#include "core/backends/anan/AnanTxScope.h"

#include <QByteArray>
#include <QElapsedTimer>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <QHostAddress>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <array>
#include <climits>
#include <atomic>
#include <complex>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

class QThread;
class QTimer;
class QUdpSocket;

namespace AetherSDR::anan {

class P2Transport : public hl2::MetisClient {
    Q_OBJECT
public:
    explicit P2Transport(QObject* parent = nullptr);
    ~P2Transport() override;

    // Hl2Backend's "RF gain" on ANAN is -attenuation (-31..0 dB).
    [[nodiscard]] static int attenuationForLnaGainDb(int gainDb) noexcept;
    // Receivers this board can stream from its DDC base, capped by kMaxDdcs.
    [[nodiscard]] static int receiversForBoard(const BoardProfile& profile, int numDdc) noexcept;

    QList<Discovered> discover(int timeoutMs = 2000,
                               const QHostAddress& broadcast = QHostAddress::Broadcast,
                               quint16 port = kRadioPort) override;

    Q_INVOKABLE bool start(const hl2::MetisClient::Params& params) override;
    Q_INVOKABLE void stop() override;

    Q_INVOKABLE void setRxFrequencyHz(std::uint32_t hz) override;
    Q_INVOKABLE void setRxFrequencyHz(int rxIndex, std::uint32_t hz) override;
    // ANAN port: the frequency receiver rxIndex is actually tuned to (the
    // slice / its filter), as opposed to its DDC NCO, which sits at the
    // panadapter centre. The User Out RX row follows this band (operator,
    // 09.11), as Thetis follows the VFO. 0 = not known, the NCO is used.
    Q_INVOKABLE void setRxTunedHz(int rxIndex, std::uint32_t hz);
    Q_INVOKABLE void setSampleRate(hl2::SampleRate rate) override;
    Q_INVOKABLE void setReceiverCount(int count) override;
    Q_INVOKABLE void setLnaGainDb(int db) override;
    Q_INVOKABLE void setBandFilter(int ocFilterByte) override;
    Q_INVOKABLE void setDitherRandomBits(bool dither, bool random) override;
    Q_INVOKABLE void setLocalCodec(bool present) override;
    Q_INVOKABLE void submitSpeakerAudio(const QByteArray& interleavedInt16) override;
    Q_INVOKABLE void clearSpeakerAudio() override;
    Q_INVOKABLE void setCl1RefClock(bool externalRef) override;
    Q_INVOKABLE void setAtuTuneRequest(bool request) override;
    Q_INVOKABLE void setIoBoardTxFrequencyHz(quint64 hz) override;
    Q_INVOKABLE void requestPipelineReset() override;
    Q_INVOKABLE bool requestRegister(int addr, quint32 data, bool subsystemRead = false) override;
    Q_INVOKABLE void requestIoBoardStatus() override;
    Q_INVOKABLE void setBandscopeEnabled(bool on) override;
    Q_INVOKABLE void requestBandscopeFrame() override;

    // Transmit (ANAN-100D port): MOX = High Priority PTT0; modulated IQ
    // (192 ksps from Hl2TxDsp) is paced to the DUC I&Q port 1029; TUNE tone
    // and CW carrier are generated here, as MetisClient does for the HL2.
    void setMox(bool keyed, const TxCoordinator::Operation& operation) override;
    void setCwMox(bool keyed, const TxCoordinator::Operation& operation) override;
private:
    // A lost link ends a transmission here (the coordinator refuses the
    // host's unkey after a connection reset). I/O thread.
    void unkeyOnLinkLoss();
public:
    Q_INVOKABLE void setTxFrequencyHz(std::uint32_t hz) override;
    Q_INVOKABLE void setTxDriveLevel(int level) override;
    // The drive level last commanded (0..255; any thread). PureSignal follows
    // it with the feedback attenuator before the feedback level moves.
    int driveLevel() const { return m_driveAtomic.load(std::memory_order_relaxed); }
    void setCwKeyDown(bool down, const TxCoordinator::Operation& operation) override;
    Q_INVOKABLE void clearCwKeying() override;
    void queueTxIq(std::span<const std::complex<float>> iq,
                   const TxCoordinator::Context& context) override;
    Q_INVOKABLE void flushTxIq() override;
    void setTxTestTone(double offsetHz, double amplitude,
                       const TxCoordinator::Operation& operation) override;

    // ANAN-100D port (01.11, operator: "every packet exchange synchronised
    // with the device"): the DUC stream is clocked by the radio, not by the
    // PC. Each microphone packet the radio sends (64 frames at 48 kHz, its
    // codec clock) stands for 256 DUC samples of the radio's time; AnanTxEngine
    // calls this on the transmit thread after it has modulated that packet,
    // and the DUC packets due by then (plus the FIFO lead) go out at once.
    // Any thread. Without a mic stream (> 30 ms) a 2 ms host timer stands in.
    void txDeviceTick(int ducSamples);
    // Samples the TX queue must hold before it is drained (any thread): the
    // modulator's block plus margin -- small when the radio microphone drives
    // the modulator in step with the DUC clock, larger for computer audio.
    // pcClock: audio on the PC's clock fed the modulator (DAX, TCI, the
    // computer mic, FreeDV through the GUI): its larger prime holds for 1 s
    // against the radio-mic setting.
    void setTxPrimeSamples(int samples, bool pcClock = false)
    {
        const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch()).count();
        if (pcClock)
            m_pcPrimeMs.store(now, std::memory_order_relaxed);
        else if (now - m_pcPrimeMs.load(std::memory_order_relaxed) < 1000)
            return;
        m_txPrime.store(std::max(256, samples), std::memory_order_relaxed);
    }

    [[nodiscard]] const BoardProfile& boardProfile() const noexcept { return m_profile; }
    // The session runs (or last ran) Protocol 1. Any thread.
    [[nodiscard]] bool isProtocol1() const noexcept { return m_p1Atomic.load(std::memory_order_relaxed); }
    // ANAN-100D port: which ADC (0 = ADC1, 1 = ADC2) feeds receiver `rx`.
    Q_INVOKABLE void setReceiverAdc(int rx, int adc);
    // ANAN-100D port: step attenuator of one ADC (0 or 1), 0..31 dB.
    Q_INVOKABLE void setAdcAttenuation(int adc, int db);
    // ANAN-100D port: Alex antenna relays, 0..2 = ANT1..ANT3. The receive
    // antenna feeds ADC1 (ADC2 has its own RX2 input); the transmit antenna is
    // switched in on PTT.
    Q_INVOKABLE void setRxAntenna(int antenna);
    Q_INVOKABLE void setTxAntenna(int antenna);
    // ANAN-100D port: +20 dB microphone boost of the radio's mic input
    // (Transmitter Specific byte 50 bit 1).
    Q_INVOKABLE void setMicBoost(bool on);
    // ANAN-100D port: Transmitter Specific byte 50 bits 2-5 (kMicPttDisabled,
    // kMicTipIsPtt, kMicBiasOn, kMicXlr); masked to what the board reads.
    Q_INVOKABLE void setMicControl(int bits);
    // Orion2BandPass boards: ADC1's input grounded while transmitting.
    Q_INVOKABLE void setAdc1GroundOnTx(bool on);
    // Build 267 (operator, 11.11, replaces build 245's 7000-style filter
    // option): "Forcedly assign the radio" -- a radio that reports board
    // `reportedBoardId` runs its session with board `forcedBoardId`'s profile
    // (DDC plan, ADCs, Alex filter scheme, PureSignal, status layout). 0 = the
    // reported board's own. Thread-safe; read when a session is configured.
    Q_INVOKABLE void setForcedBoardId(int reportedBoardId, int forcedBoardId);
    // The board this session's radio reported (0 = none yet).
    [[nodiscard]] int reportedBoardId() const noexcept { return m_reportedBoardId.load(std::memory_order_relaxed); }
    // ANAN-100D port: Transmitter Specific bytes 57..59 -- the ADC step
    // attenuators while transmitting (default 31 dB, as Thetis).
    Q_INVOKABLE void setTxAttenuation(int db);
    // ANAN-100D port: High Priority byte 1400 bit 2 (tuner start) while TUNE
    // is on, and the Alex "6M Preamp" bit 3 (the LNA button).
    Q_INVOKABLE void setTunerStart(bool on);
    Q_INVOKABLE void setLna(bool on);
    // ANAN-100D port: User Out (open collector) matrices -- 11 RX band masks,
    // 11 TX band masks (bit n = OC n+1) and 7 TX pin actions (OcTxAction).
    // High Priority byte 1401 is computed from them on every send.
    // ANAN-100D port: wideband (raw ADC) frames for the Wideband Monitor.
    // adcMask bit n = ADCn (0 = off), updateMs 20..255. Frames of 16384
    // contiguous samples come back on widebandFrame (this thread).
    Q_INVOKABLE void setWideband(int adcMask, int updateMs);
    // The radio's last high priority status packet as received (60 bytes,
    // the Odyssey diagnostics in it -- status layout v8, docs/Odyssey status
    // bytes), for the HW Diag window. Any thread.
    struct StatusSnapshot {
        bool valid = false;
        std::array<std::uint8_t, 60> b{};
        quint64 count = 0;           // status packets this session
        qint64 ageMs = -1;           // since the last one
        quint64 ducPacketsSent = 0;  // DUC I/Q packets sent this session
        bool mox = false;
        int drive = 0;               // TX drive level sent (0..255)
    };
    StatusSnapshot statusSnapshot() const;
    // ANAN-100D port: PureSignal. enabled: the Alex PS bits, and while MOX
    // DDC0 = feedback (ADC0) / DDC1 = TX DAC at 192 ksps on the transmit
    // frequency; feedbackSource 0 internal coupler, 1 EXT1, 2 RX bypass;
    // attDb 0..31 = the ADC0 attenuator while transmitting.
    Q_INVOKABLE void setPureSignal(bool enabled, int feedbackSource, int attDb);
    // anan160: the feedback attenuator follows the drive HERE, in the same
    // instant (back-to-back packets) the new drive goes out -- done from the
    // PureSignal tick it came up to 50 ms late and that collection mixed two
    // levels (log 04.11 18:07:09: an scheck jump of 5.6). AnanPureSignal
    // adopts psAttNow().
    Q_INVOKABLE void setPsAttFollowsDrive(bool on);
    int psAttNow() const { return m_psAttNow.load(std::memory_order_relaxed); }
    // Firmware test (01.11): DDC0 on the TX DAC like DDC1 (ADC index 2), so
    // the synchronised pair must match sample for sample.
    Q_INVOKABLE void setPsLoopbackTest(bool on);
    // PureSignal feedback ADC: 0 = ADC1 (ANT / coupler path), 1 = ADC2 (RX2 in).
    Q_INVOKABLE void setPsFeedbackAdc(int adc);
    // The last 16384 DUC IQ samples sent (interleaved I/Q doubles, oldest
    // first); false until that many have gone out this over. Any thread.
    bool ducSnapshot(std::vector<double>& iq) const;
    // ADC overload bits (bit 0 ADC0, bit 1 ADC1) seen in the status packets
    // since the last call; any thread (PureSignal auto-attenuate).
    unsigned takeOverloadLatch() { return m_ovlLatch.exchange(0u, std::memory_order_acq_rel); }
    // The same bits for the panadapter's overload warning (its own latch).
    unsigned takeDisplayOverload() { return m_ovlDisplay.exchange(0u, std::memory_order_acq_rel); }
    // Odyssey firmware buffer status (any thread): session totals, and a
    // sequence number that steps on every TX-buffer error (ran empty or
    // overflowed) while transmitting -- PureSignal pauses its learning then.
    struct RadioBufferTotals {
        bool reported = false;   // status layout v1 seen this session
        unsigned txEmpty = 0, txOverflow = 0, spkEmpty = 0, spkOverflow = 0;
        // Link counters (status layout v3, test firmware 02.11), this session:
        // command packets not applied, audio / I/Q packets dropped, frames with
        // a bad FCS the PHY did not flag, frames the PHY flagged (RXER).
        bool link = false;
        unsigned cmdRejected = 0, audioDropped = 0, iqDropped = 0, badFcs = 0, phyRxEr = 0;
    };
    RadioBufferTotals radioBufferTotals() const
    {
        RadioBufferTotals t;
        t.reported = m_bufReported.load(std::memory_order_relaxed);
        t.txEmpty = m_bufTxEmpty.load(std::memory_order_relaxed);
        t.txOverflow = m_bufTxOvf.load(std::memory_order_relaxed);
        t.spkEmpty = m_bufSpkEmpty.load(std::memory_order_relaxed);
        t.spkOverflow = m_bufSpkOvf.load(std::memory_order_relaxed);
        t.link = m_linkReported.load(std::memory_order_relaxed);
        t.cmdRejected = m_link[0].load(std::memory_order_relaxed);
        t.audioDropped = m_link[1].load(std::memory_order_relaxed);
        t.iqDropped = m_link[2].load(std::memory_order_relaxed);
        t.badFcs = m_link[3].load(std::memory_order_relaxed);
        t.phyRxEr = m_link[4].load(std::memory_order_relaxed);
        return t;
    }
    // Feedback pairs (interleaved I/Q doubles, tx = DAC reference, rx =
    // coupler), n pairs, called on this object's thread while MOX with PS on.
    // hwPeak > 0: a fresh measurement of the DDC1 (TX DAC) amplitude at a
    // full-scale DUC sample -- peak |DDC1| over peak |IQ sent| in the last
    // 0.5 s -- i.e. the PureSignal "HW peak"; -1 otherwise.
    // Set before the transport moves to its thread.
    void setPsSink(std::function<void(const double* tx, const double* rx, int n, double hwPeak)> sink)
    {
        m_psSink = std::move(sink);
    }
    Q_INVOKABLE void setOpenCollectorMatrix(const QByteArray& rx, const QByteArray& tx,
                                            const QByteArray& actions);
    // ANAN-100D port: the FPGA CW keyer (CW applet, MicronSDR's Keyer page).
    // Called on this object's thread (queued functor from Hl2Backend). With
    // the keyer on, a paddle (status byte 4 bits 1/2) counts as radio PTT and
    // holds it for the hang time after the last element, so the host follows
    // the radio's own break-in. CW demo: no PA, no T/R relay, drive 0.
    void setCwKeyer(const CwKeyerOptions& k);
    // Build 224 (operator, 09.11): the radio's own CW keyer is switched off
    // (keyer-enable bit 0: P2 Transmitter Specific byte 5 bit 1, P1 C0 0x1E
    // C1 bit 0) while the TX frequency is outside the amateur bands. Receive
    // is not affected.
    void setCwBandInhibit(bool inhibit);
    // ANAN-100D port: called on this object's thread the instant the radio's
    // status reports its PTT pressed -- before the host has heard of it.
    // Set once, before the transport starts.
    void setRadioPttPressHook(std::function<void()> hook) { m_radioPttPressHook = std::move(hook); }

signals:
    // ANAN-100D port: microphone samples from the radio's own mic jack
    // (port 1026), 48 kHz mono, 64 per packet. Emitted on the I/O thread.
    void codecMicReady(const std::vector<float>& samples48k);
    // ANAN-100D port: one wideband frame (16384 contiguous raw ADC samples in
    // [-1, 1), 122.88 Msps) from ADC `adc`.
    void widebandFrame(int adc, const QVector<float>& samples);

private slots:
    void onReadyRead();
    void onKeepaliveTick();
    void onBoardWaitTimeout();
    void onWatchdogTick();
    void onTxPacerTick();
    void onSpeakerPacerTick();

private:
    static constexpr int kKeepaliveMs = 100;
    static constexpr int kBoardWaitMs = 300;
    static constexpr int kConnectTimeoutMs = 2000;
    static constexpr int kSilenceTimeoutMs = 2000;
    static constexpr int kWatchdogTickMs = 250;
    static constexpr int kLinkPublishMs = 1000;
    static constexpr int kDiagPeriodMs = 5000;
    // Packets one DDC may run ahead of the others before the oldest is dropped.
    static constexpr std::size_t kMaxAheadPackets = 8;

    void handleDatagram(std::span<const std::uint8_t> bytes, quint16 senderPort);
    // ---- Protocol 1 (build 194) ----
    void handleP1Ep6(std::span<const std::uint8_t> bytes);
    void deliverPsPairs();
    void noteBindWorked();
    QString noteBindSilent();
    // EP4 wide bandscope (Metis "How it works" V1.33: 512 x 16-bit raw ADC
    // samples per packet, a buffer of 16384 starting at a sequence number
    // whose low 5 bits are 0; ADC0 only).
    void handleP1Ep4(std::span<const std::uint8_t> bytes);
    int m_p1WbOrder = 0;            // 1 once the first EP4 packet was logged
    QElapsedTimer m_p1WbClock;      // host-side frame rate (updateMs)
    bool m_p1WbAdc2Logged = false;
    void sendP1Ep2();
    void sendP1Start();   // Thetis SendStartToMetis: C&C priming, then EF FE 04 01
    [[nodiscard]] P1CcState p1State() const;
    [[nodiscard]] int p1RateCode() const noexcept;
    bool m_p1 = false;                       // this session is Protocol 1
    std::atomic<bool> m_p1Atomic{false};
    std::optional<BoardProfile> m_p1Pending; // a P1 reply, waiting for a P2 one
    std::uint8_t m_p1Gateware = 0;
    P1DdcPlan m_p1Plan;
    int m_p1CcIdx = 0;
    std::uint32_t m_p1Seq = 0;
    quint64 m_p1AudioAcc = 0;                // EP2 pacing: rounds x 48000
    int m_p1StartTries = 0;
    P1TxDecimator m_p1TxDec;                 // 192 -> 48 ksps TX I/Q (m_txMutex)
    P1PsResampler m_p1PsRx, m_p1PsTx;        // PS feedback / TX reference -> 192 ksps (I/O thread)
    bool m_cwBandInhibit = false;            // build 224: keyer off outside the amateur bands
    [[nodiscard]] CwKeyerOptions effectiveCwKeyer() const
    {
        CwKeyerOptions k = m_cwKeyer;
        if (m_cwBandInhibit)
            k.enabled = false;
        return k;
    }
    QHostAddress m_bindAddr;                 // build 221: the session socket's local address
    QHostAddress m_learnedBind;              // build 221: the adapter the radio was found on
    // Build 240: where commands are SENT -- the radio's address, or the
    // limited broadcast (255.255.255.255) out of m_bindAddr when the radio is
    // in another subnet than that adapter (Windows then refuses unicast from
    // it: "Unable to send a message"). Replies still come from m_p.host.
    QHostAddress m_txHost;
    bool m_p1PsActive = false;               // PS pairs flowing this over (I/O thread)
    std::vector<float> m_p1Mic;              // radio mic, gathered into 64-sample packets
    int m_p1MicPhase = 0;
    bool m_p1RateLogged = false;
    std::optional<int> m_p1Firmware;
    std::vector<std::vector<std::complex<float>>> m_p1Ddc;
    void configureSession(const BoardProfile& profile, bool fromReply);
    void stopSession(bool toPowerOnState);   // stop(): true; start(): false
    void sendGeneral();
    void sendDdcSpecific();
    void sendHighPriority(bool run);
    void deliverAlignedBlocks();
    void publishLinkCounters();
    void diagTick();
    void handleStatus(std::span<const std::uint8_t> bytes);
    // Odyssey firmware buffer status (High Priority status bytes 32..47,
    // layout version byte 47 = 1; 0 = older firmware without it).
    void handleBufferStatus(std::span<const std::uint8_t> b);
    struct FpgaBuffers {
        bool seen = false;          // layout v1 in this session
        bool haveBase = false;
        std::uint16_t txEmpty = 0, txOvf = 0, spkFade = 0, spkEmptyRd = 0, spkOvf = 0;   // last raw
        // Per diag period:
        int txMin = 1 << 30, txMax = -1, spkMin = 1 << 30, spkMax = -1;
        unsigned dTxEmpty = 0, dTxOvf = 0, dSpkFade = 0, dSpkEmptyRd = 0, dSpkOvf = 0;
        std::uint8_t flags = 0;
        qint64 lastTxEmptyLogMs = 0;
        bool linkBase = false;      // layout v3 link counters (since power-on)
        std::array<std::uint16_t, 5> link{};
        std::array<unsigned, 5> dLink{};   // per diag period
        bool phyBase = false;       // layout 6: PHY diagnostics
        std::uint16_t phyIdle = 0, phyRx = 0;
        unsigned dPhyIdle = 0, dPhyRx = 0;
        int clkDevMin = INT_MAX, clkDevMax = INT_MIN;   // per diag period (v7)
    } m_fpgaBuf;
    std::atomic<bool> m_linkReported{false};
    std::array<std::atomic<unsigned>, 5> m_link{};
    std::atomic<bool> m_bufReported{false};
    std::atomic<unsigned> m_bufTxEmpty{0}, m_bufTxOvf{0}, m_bufSpkEmpty{0}, m_bufSpkOvf{0};
    // DUC lead correction (packets) from the radio's reported FIFO level.
    std::atomic<int> m_leadAdj{0};
    std::atomic<int> m_phyClkDev{INT_MIN};     // bytes 53-54 (v7), Hz / 8
    quint64 m_diagStatusSeen = 0;              // m_statusPackets at the last diag line
    std::atomic<int> m_driveAtomic{0}, m_fwdAtomic{0};
    std::atomic<int> m_eeeAdv{-1}, m_gbStatus{-1};
    // Layout v9, bytes 28-29: 10 MHz reference -- the 10 MHz input's periods
    // counted over one second of the 122.88 MHz clock, minus 10 000 000
    // (signed, 1 = 1 Hz = 0.1 ppm). 0 / +-1: the loop is locked; a steady
    // other value: not locked, the oscillator's error against the reference;
    // -32768: no 10 MHz at the input. kRefNone until a v9 status.
    static constexpr int kRefNone = INT_MIN;
    std::atomic<int> m_ref10M{kRefNone};
    int m_ref10MState = -2;   // logged state: -1 no input, 0 locked, 1 not locked
    std::atomic<quint64> m_ducSentTotal{0};
    mutable std::mutex m_statusSnapMutex;
    std::array<std::uint8_t, 60> m_statusSnap{};
    bool m_statusSnapValid = false;
    qint64 m_statusSnapMs = 0;
    std::atomic<unsigned> m_radioTxLevelSeq{0};    // +1 per status packet (HW Diag)
    void sendTransmitSpecific();
    void beginTxStream();
    void fillTxPacket(std::complex<float>* out, int n = kDucSamplesPerPacket);
    // Level statistics, the DUC snapshot and the TX panorama for n samples
    // just filled at 192 ksps (both protocols). m_txMutex held.
    void accountTxSamples(const std::complex<float>* buf, int n);
    // Sends the DUC packets due by m_txDevSamples (+ lead). m_txMutex held.
    void txPumpLocked();
    // sendto() on the session socket from any thread (AnanNativeUdp.h);
    // -1 while there is no session socket.
    qint64 sendNative(const std::uint8_t* data, std::size_t len, quint16 port);
    // Speaker sender thread body.
    void speakerSenderLoop();
    void publishTelemetry();
    [[nodiscard]] int rateKsps() const noexcept;

    QUdpSocket* m_sock = nullptr;
    QTimer* m_keepalive = nullptr;
    QTimer* m_boardWait = nullptr;
    QTimer* m_connectTimer = nullptr;
    QTimer* m_watchdog = nullptr;

    hl2::MetisClient::Params m_p;
    BoardProfile m_profile = defaultBoardProfile();
    bool m_run = false;
    bool m_up = false;
    bool m_configured = false;
    int m_discoveredNumDdc = 0;
    int m_numRx = 1;
    // Step attenuator per ADC (HP bytes 1443 = ADC0, 1442 = ADC1), set by
    // Hl2Backend through setAdcAttenuation(). Kept across sessions.
    std::array<int, 2> m_adcAtt{};
    bool m_dither = false;
    bool m_random = false;
    std::array<std::uint32_t, kMaxDdcs> m_rxHz{};
    std::array<std::uint32_t, kMaxDdcs> m_rxTunedHz{};   // setRxTunedHz()
    [[nodiscard]] double ocRxHz(std::size_t rx) const noexcept
    {
        return static_cast<double>(m_rxTunedHz[rx] ? m_rxTunedHz[rx] : m_rxHz[rx]);
    }
    std::array<int, kMaxDdcs> m_rxAdc{};   // per receiver, 0 = ADC1
    [[nodiscard]] int effectiveAdc(int rx) const;   // build 277
    // Build 279/280: one ADC's receive filter bank, decided only by the
    // receivers listening to that ADC (any receiver may use any ADC; the
    // filters are wired to the ADC). When they want different filters it is
    // bypassed (WIDE), and the classic-Alex receive low-pass is then the
    // widest, so every receiver on the ADC hears its band.
    struct BankChoice {
        AlexRxFilter filter = AlexRxFilter::Bypass;
        double lpfHz = 0.0;     // classic Alex RX low-pass: the receivers' highest frequency
        bool spanned = false;   // receivers on different filters: bypassed
        bool used = false;      // a receiver listens to this ADC
    };
    [[nodiscard]] BankChoice bankFor(int adc) const;
    int m_rxAnt = 0;   // 0..2 = ANT1..ANT3
    bool m_tunerStart = false;
    bool m_lna = false;
    OcMatrix m_oc;
    WidebandOptions m_wb;
    bool m_psEnabled = false;
    bool m_psLoopback = false;
    // DUC IQ ring for ducSnapshot() (under m_ducSnapMutex).
    static constexpr int kDucSnap = 16384;
    mutable std::mutex m_ducSnapMutex;
    std::vector<double> m_ducSnap;
    int m_ducSnapPos = 0, m_ducSnapFill = 0;
    int m_psSource = 0;
    int m_psAdc = 0;   // feedback ADC index (0 / 1)
    int m_psAttDb = 0;
    bool m_psAttFollowDrive = false;
    double m_psDriveRef = 0.0;         // the drive the present m_psAttDb belongs to (0 = next)
    std::atomic<int> m_psAttNow{0};
    std::function<void(const double*, const double*, int, double)> m_psSink;
    // HW peak measurement (0.5 s windows, peaks of |x|^2). m_hwDucLast is
    // written by the DUC sender (transmit thread), read by the PS code here.
    double m_hwDucPk = 0.0, m_hwFbPk = 0.0;
    std::atomic<double> m_hwDucLast{0.0};
    int m_hwDucN = 0, m_hwFbN = 0;
    std::vector<double> m_psTxBuf, m_psRxBuf;
    quint64 m_psPackets = 0;
    // TX panorama from the PureSignal feedback (see the port 1035 branch).
    double m_scopeFbPk = 0.0, m_scopeFbGain = 0.0;
    // Set by the PS code (I/O thread), read by the DUC sender.
    std::atomic<bool> m_scopeFromFb{false};
    std::atomic<qint64> m_scopeFbMs{0};
    std::vector<std::complex<float>> m_scopeFbBuf;
    std::array<QVector<float>, 2> m_wbBuf;
    std::array<bool, 2> m_wbLogged{{false, false}};
    std::array<qint64, 2> m_wbNext{{-1, -1}};
    // Diagnostics (ANAN diag line): wideband-sized packets per source port
    // 1027 / 1028 (counted before the mask check), from any other port, and
    // whole frames emitted per ADC.
    std::array<int, 2> m_diagWbPkts{{0, 0}};
    std::array<int, 2> m_diagWbFrames{{0, 0}};
    int m_diagWbOther = 0;
    quint16 m_diagWbOtherPort = 0;
    std::array<float, kWidebandSamplesPerPacket> m_wbScratch{};
    void handleWideband(int adc, std::span<const std::uint8_t> bytes);
    std::uint8_t m_ocLast = 0xff;   // last byte 1401 logged
    int m_txAnt = 0;

    // Per-receiver sequence tracking and the alignment queues: Hl2Backend
    // takes one block per receiver per emission, as EP6 delivers them.
    std::array<std::optional<std::uint32_t>, kMaxDdcs> m_expectedSeq{};
    std::array<std::deque<std::vector<std::complex<float>>>, kMaxDdcs> m_pending;
    std::vector<std::vector<std::complex<float>>> m_out;
    std::vector<std::complex<float>> m_scratch;

    QElapsedTimer m_sinceIq;
    QElapsedTimer m_linkClock;
    hl2::MetisClient::LinkCounters m_counters;
    quint64 m_dropTotal = 0;
    QSet<quint16> m_warnedPorts;
    bool m_warnedTx = false;

    // ---- Native sends (DUC and speaker from their own threads) ----
    // Exclusive while the session socket is created / closed; shared around
    // every native send, so no send can hit a closed (or reused) descriptor.
    std::shared_mutex m_sendGuard;
    qintptr m_nativeFd = -1;     // under m_sendGuard
    quint32 m_nativeIp = 0;      // under m_sendGuard
    std::atomic<bool> m_sessionUp{false};   // configured: senders may send
    std::atomic<quint64> m_nativeTxBytes{0}, m_nativeTxPackets{0};

    // ---- Transmit ----
    // Every TX-state member below (queue, tone, CW, rise, over statistics,
    // scope, sequence, device clock) is under m_txMutex: the I/O thread
    // (MOX, tone, CW, flush) and the transmit thread (queueTxIq, txDeviceTick)
    // both reach it.
    std::mutex m_txMutex;
    quint64 m_txDevSamples = 0;           // radio time since MOX, DUC samples
    std::atomic<qint64> m_lastDevTickMs{0};
    QElapsedTimer m_txFallbackClock;      // host stand-in without a mic stream
    qint64 m_txFallbackNs = 0;
    quint64 m_txFallbackCredit = 0;       // host-advanced samples not yet matched by mic packets
    bool m_txFallbackActive = false;
    // Mic packets that reached the transmit thread's queue (I/O thread) and
    // that it has modulated (txDeviceTick, under m_txMutex): the backlog.
    std::atomic<quint64> m_micArrived{0};
    quint64 m_micProcessed = 0;
    quint64 m_txLateTicks = 0, m_txLateLogged = 0;
    // Mic sequence (I/O thread): lost packets become silence (time kept).
    std::uint32_t m_micSeqNext = 0;
    bool m_micSeqValid = false;
    quint64 m_diagMicLost = 0;
    std::atomic<quint64> m_nativeSendFails{0};
    std::atomic<int> m_txPrime{4096};
    std::atomic<qint64> m_pcPrimeMs{-1000000};
    std::atomic<int> m_rfDelayMs{0};
    quint64 m_txUnderflowsLogged = 0;
    // Packets kept ahead of real time in the radio's DUC FIFO (4096 samples):
    // 6 x 240 = 1440 samples, 7.5 ms -- deskHPSDR keeps ~1250 on a wired link.
    static constexpr int kTxLeadPackets = 6;
    // The queue prime is m_txPrime (setTxPrimeSamples): Hl2TxDsp emits
    // 1024-sample blocks (128 in at 24 kHz, x8). Driven by the radio mic on
    // the device clock the queue level is deterministic, one block plus a
    // little suffices; computer audio arrives on the PC's clock and needs
    // more. (Log 22:58: with the mic path through the GUI thread, 2048 gave
    // 4-6 underflows per over -- clicks on the air.)
    std::uint64_t m_txSamplesOut = 0;   // DUC samples sent this over (RF Delay)
    bool m_timerPeriodSet = false;      // timeBeginPeriod(1) held (Windows)
    int m_txRise = 0;                   // samples of the 2 ms rise done
    // 02.11: gaps of 0.5-0.9 ms on the air (zeros in DDC1 AND the coupler)
    // with the radio's TX FIFO at 5400-6300 and no underflow of ours. Does
    // the host send them? Runs of >= 64 near-zero samples in the DUC stream
    // while live (m_txMutex): length, the DUC sequence, the queue then.
    int m_hostZeroRun = 0;
    quint64 m_hostZeroRuns = 0, m_hostZeroRunsLogged = 0;
    int m_hostZeroLastLen = 0;
    quint32 m_hostZeroLastSeq = 0;
    std::size_t m_hostZeroLastQueue = 0;
    static constexpr std::size_t kTxQueueMax = kDucSampleRateHz / 4;   // 250 ms
    QTimer* m_txTimer = nullptr;
    QElapsedTimer m_txClock;
    quint64 m_txPacketsSent = 0;
    std::uint32_t m_txSeq = 0;
    std::uint32_t m_txSpecSeq = 0;
    bool m_mox = false;
    std::atomic<bool> m_moxAtomic{false};   // m_mox for other threads
    std::uint32_t m_txHz = 0;
    int m_drive = 0;
    bool m_paEnabled = true;
    std::deque<std::complex<float>> m_txQueue;
    TxCoordinator::Context m_txContext;
    bool m_txPrimed = false;
    quint64 m_txUnderflows = 0;
    // Per-over level statistics of the DUC IQ (the "TX over" log line):
    // peak |IQ|, mean power, and the loudest 100 ms window's mean power.
    double m_ovPeak = 0.0;
    double m_ovSumSq = 0.0;
    quint64 m_ovCount = 0;
    double m_ovWinSumSq = 0.0;
    int m_ovWinCount = 0;
    double m_ovWinMax = 0.0;
    std::vector<double> m_ovTrace;   // mean power per 250 ms
    double m_envPeak = 0.0;          // txEnvelopePeak, power
    int m_envN = 0;
    double m_ovTrSum = 0.0;
    int m_ovTrN = 0;
    void logTxOver();
    // TUNE test tone and host CW
    double m_toneHz = 0.0;
    double m_toneAmp = 0.0;
    double m_tonePhase = 0.0;
    bool m_cwMode = false;
    bool m_cwDown = false;
    double m_cwEnv = 0.0;
    // Spectrum of what is actually sent (txScopeFrameReady).
    TxScope m_txScope;
    std::vector<float> m_txScopeBins;
    // Fwd / rev / exciter power from the status packet (raw ADC counts).
    int m_fwdRaw = 0;
    int m_revRaw = 0;
    // Build 231 (users: the power meter jumps): the highest coupler reading
    // since the last publish (P2 status / P1 EP6 status, hundreds a second),
    // and a 40 ms publish while transmitting (was the 100 ms keepalive with
    // the last sample only).
    int m_fwdPeakWin = -1;
    int m_revPeakWin = -1;
    QTimer* m_meterTimer = nullptr;
    int m_excRaw = 0;
    int m_supplyRaw = 0;     // status bytes 49-50
    int m_userAdc0Raw = 0;   // status bytes 57-58 (PA volts on 7000/8000/G2)
    int m_userAdc1Raw = 0;   // status bytes 55-56 (PA drain current)

    // ---- Codec speaker (port 1028) ----
    // 48 kHz stereo int16 from Hl2Backend's speaker tap, paced by wall clock.
    static constexpr std::size_t kSpkPrimeSamples = 2 * 2880;    // 60 ms stereo
    static constexpr std::size_t kSpkQueueMax = 2 * 14400;      // 300 ms stereo
    QTimer* m_spkTimer = nullptr;
    QElapsedTimer m_spkClock;
    quint64 m_spkPacketsSent = 0;
    std::uint32_t m_spkSeq = 0;            // speaker sender thread only
    // Filled on the I/O thread (submitSpeakerAudio), drained by the speaker
    // sender thread (time critical) as soon as a 64-frame packet is complete
    // -- as Thetis does; the audio is produced from the radio's IQ, so its
    // rate is the radio's own.
    std::mutex m_spkMutex;
    std::condition_variable m_spkCv;
    bool m_spkQuit = false;                // under m_spkMutex
    QThread* m_spkThread = nullptr;
    std::deque<std::int16_t> m_spkQueue;   // under m_spkMutex
    bool m_spkPrimed = false;
    // Speaker packets follow the radio's own clock: one per mic packet it
    // sends (both 64 frames at 48 kHz). The wall-clock pacer only covers a
    // radio that sends no mic stream.
    QElapsedTimer m_sinceMic;
    // Packets the speaker stream runs AHEAD of the radio's playback. Paced 1:1
    // by the mic stream the lead never grows, so it is set once when that
    // pacing starts; without it the codec FIFO sits at its empty edge and
    // goes silent after the first glitch (heard on the radio: ~1 s, then
    // nothing).
    static constexpr int kSpkLeadPackets = 24;   // 32 ms
    bool m_spkLeadSent = false;
    // Codec diagnostics per diag period (ANAN diag line).
    quint64 m_diagMicPkts = 0;
    quint64 m_connMicPkts0 = 0;   // m_diagMicPkts at start() (connect-timeout report)
    std::atomic<quint64> m_diagSpkPkts{0};
    quint64 m_diagSpkSilent = 0;
    quint64 m_diagSpkSubmits = 0;
    int m_diagSpkPeak = 0;
    bool m_micBoost = false;
    std::uint8_t m_micControl = 0;
    bool m_adc1GndOnTx = true;
    std::array<std::atomic<int>, 32> m_forcedBoardFor{};   // reported id -> forced id
    std::atomic<int> m_reportedBoardId{0};
    // Build 267: the reported profile, or the forced one when it can run over
    // this session's protocol.
    [[nodiscard]] BoardProfile forcedProfileOr(const BoardProfile& reported) const;
    int m_txAttDb = 31;
    // Radio PTT release: the radio sends status only while PTT/TX is active,
    // and its last packet can still say "PTT"; silence means released.
    QElapsedTimer m_sinceStatus;
    static constexpr int kPttStaleMs = 250;

    // High Priority status (port 1025). ANGELIA sends it only when something
    // changes, so the overload bit is latched per telemetry window.
    bool m_adc0Overload = false;
    std::atomic<unsigned> m_ovlLatch{0u};
    std::atomic<unsigned> m_ovlDisplay{0u};
    bool m_ovlLogOn = false;        // debounced ADC0 overload state, for the log
    qint64 m_ovlLogLastMs = 0;
    static constexpr qint64 kOvlLogDebounceMs = 200;
    bool m_overloadInWindow = false;
    bool m_radioPtt = false;
    // ANAN-100D port: FPGA keyer state.
    CwKeyerOptions m_cwKeyer;
    std::function<void()> m_radioPttPressHook;
    // ANAN-100D port, diagnostics: receiver input level around a radio PTT
    // press (one log line: RX1 IQ peak per 5 ms from -50 to +400 ms, and
    // when the host's MOX arrived).
    QElapsedTimer m_tlClock;
    std::deque<std::pair<qint64, float>> m_tlIq;   // (ms, peak |IQ|) per RX1 packet
    qint64 m_tlPressMs = -1;
    qint64 m_tlMoxMs = -1;
    void timelineTick();
    bool m_pttLine = false;      // status byte 4 bit 0 (mic / foot PTT)
    bool m_paddleDown = false;   // status byte 4 bits 1-2 while the keyer is on
    bool m_paddleHold = false;   // inside the hang time after the last element
    QTimer* m_paddleHangTimer = nullptr;
    void updateRadioPtt();
    [[nodiscard]] bool cwDemoActive() const noexcept { return m_cwKeyer.enabled && m_cwKeyer.demo; }
    [[nodiscard]] bool paEffective() const noexcept { return m_paEnabled && !cwDemoActive(); }
    QElapsedTimer m_telemetryClock;
    quint64 m_statusPackets = 0;

    QElapsedTimer m_diagClock;
    quint64 m_diagSamples = 0;
    quint64 m_diagGaps = 0;
    quint64 m_diagLost = 0;
};

}  // namespace AetherSDR::anan
