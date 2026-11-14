#pragma once
#include "core/TxCoordinator.h"

#include <QHostAddress>
#include <QList>
#include <QObject>

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

#include "core/backends/hl2/Hl2BandMemoryPolicy.h"
#include "core/backends/hl2/MetisProtocol.h"

namespace AetherSDR::hl2 {

// THE RADIO TRANSPORT INTERFACE of Hl2Backend: discovery, start/stop, the live
// receive/transmit controls, and the IQ / telemetry signals. Hl2Backend owns
// one, runs it on its I/O thread and talks to it only through this interface.
//
// ANAN-100D port: the implementation is anan::P2Transport (openHPSDR Protocol
// 2). This class used to BE the Hermes-Lite 2's Protocol 1 ("Metis") client;
// with HL2 support removed only the interface is left, under its historical
// name (see the port journal, stages H1/H2). Several controls below are
// HL2-only concepts that P2Transport implements as no-ops (CL1 clock, IO board,
// ATU request, EP4 bandscope, RQST/ACK registers).
//
// Every Q_INVOKABLE must execute on the transport's own thread: Hl2Backend
// marshals them with QMetaObject::invokeMethod. TX is fail-closed:
// enableTransmit() must open the gate before MOX or transmit samples can leave.
class MetisClient : public QObject {
    Q_OBJECT

public:
    explicit MetisClient(QObject* parent = nullptr);
    ~MetisClient() override = default;

    struct Params {
        QHostAddress host;
        // When set, bind the streaming socket to this local interface. This is
        // the interface on which discovery reached the radio; retaining it
        // avoids handing a multi-homed host back to the route table.
        QHostAddress localAddress;
        quint16 port = kMetisPort;
        SampleRate sampleRate = SampleRate::R48k;
        // RX1's NCO. Receivers beyond the first start here too and are moved by
        // setRxFrequencyHz(rxIndex, hz); starting them all on one frequency is
        // deliberate — an unconfigured receiver parked at 0 Hz would render a
        // panadapter of DC and look like a hardware fault.
        std::uint32_t rxFrequencyHz = 10'000'000;
        int lnaGainDb = kLnaDefaultGainDb;
        // How many receivers to actually RUN. Phase 1 runs one. This is the
        // value the config register must carry -- not the board's capability.
        int numRx = 1;
        // What the board reported in its discovery reply (byte 20), or 0 if the
        // reply was a short one that omits it. Used only to clamp numRx: asking
        // a board for more receivers than it has is a configuration the
        // gateware cannot honour, and it does not report the refusal.
        int boardMaxRx = 0;
        // J16 open-collector filter selection (MetisProtocol kOc* bits). Rides
        // the config register, so it is part of the same Params the config
        // register is rebuilt from — see setSampleRate() for why a field that
        // shares a register with another has to be carried, not re-defaulted.
        std::uint8_t ocFilterByte = kOcNone;
        // Whether the duty-cycle gate should run: the operator's standing intent, not
        // the instantaneous run byte. Carried in Params (like ocFilterByte) because
        // setReceiverCount()'s stop/start clears wide_spectrum along with `run`; a field
        // sharing a rebuild must be carried, not re-defaulted.
        bool bandscope = false;

        // ---- hardware-variant options (Hl2HardwareOptions decides these) ----
        //
        // THE CONFIG-REGISTER PAIR RIDES HERE FOR THE SAME MEASURED REASON
        // ocFilterByte and bandscope do: setSampleRate() and setReceiverCount()
        // REBUILD the config bank from Params, so a field that shares that
        // register and is not carried here gets silently dropped the next time
        // an unrelated control changes. For the dither bit that would mean a
        // SquareSDR 2's loudspeaker switching itself off when the operator
        // changed the panadapter span.
        bool ditherBit = false;
        bool randomBit = false;

        // Whether this radio has a local audio codec (HL2+ or SquareSDR 2), and
        // so whether the EP2 audio slot may carry samples at all. FALSE is not
        // "no audio", it is "that slot is EADDR" — see ep2WriteTxAudio().
        bool hasCodec = false;

        // Whether the VersaClock should be locked to an external 10 MHz
        // reference at CL1. Carried here so start() can re-send the sequence:
        // the radio boots on its crystal, so every connect is a change.
        bool cl1RefClock = false;

        // WHICH radio this session is for, by serial. Carried for exactly one
        // reason: the CL1 latch below is per-radio and this object is not. A
        // MetisClient is built once in Hl2Backend's constructor and destroyed
        // in its destructor, so it outlives every connect AND every swap
        // between two HL2s — a latch without an identity on it would follow
        // the operator from one radio to the next. Empty when the serial is
        // not known yet, which the latch treats as "do not act".
        QString radioSerial;
    };

    // A discovered radio: its Metis reply plus the address to connect to.
    struct Discovered {
        DiscoveryReply reply;
        QHostAddress address;
        QHostAddress localAddress;
    };

    // Transport counters for the network readouts, measured on this socket. Gaps are
    // timed per socket wakeup, not per datagram: one wakeup drains datagrams queued
    // microseconds apart, so per-datagram timing would read 0 ms through a stall.
    struct LinkCounters {
        quint64 rxBytes = 0;
        quint64 txBytes = 0;
        quint64 rxPackets = 0;      // EP6 datagrams accepted
        quint64 txPackets = 0;      // datagrams sent (EP2 + start/stop)
        quint64 drops = 0;          // cumulative EP6 sequence gaps
        // ---- the wideband bandscope (EP4), off by default ----
        //
        // A THIRD set of counters and not a widening of the three above: EP4 is
        // a different endpoint with its own 20-bit sequence counter and its own
        // reset, so folding it into rxPackets/drops would report a gap on every
        // packet and hide which stream a loss belongs to.
        quint64 ep4Packets = 0;     // bandscope datagrams accepted
        quint64 ep4Drops = 0;       // cumulative EP4 sequence gaps
        // Backward jumps of ep4_seq_no, which are a RESET and not a loss. Kept
        // apart from ep4Drops because exactly one is expected per stream start
        // (the gateware re-aligns the counter's low two bits while the capture
        // FIFO fills) and none afterwards — 15,003 recorded packets saw one per
        // start and zero thereafter. A second one mid-session is a real
        // anomaly, and it would be invisible inside a counter that is supposed
        // to stay at zero.
        quint64 ep4Rewinds = 0;
        // Duty-cycle gate. bandscopeEnabled is read off m_params.bandscope at publish
        // time (up to kLinkPublishIntervalMs late), so the row reports what the client is
        // running. bandscopeBlocks counts blocks the gate accepted (four in-phase packets
        // after the flush), not ep4Packets/4.
        bool bandscopeEnabled = false;
        quint64 bandscopeBlocks = 0;
        // Arming cycles abandoned because no complete block arrived inside
        // bandscopeGuardMs(). Should read zero in steady state, but a non-zero
        // value does NOT on its own mean the radio stopped answering the run
        // byte: bandscopeGuardMs() names two unmeasured multi-receiver cases
        // that can fire it benignly — the arming delay being clocked by EP6
        // samples rather than packets, and the EP4 rate at two, and at four or
        // more, receivers. Read its assumptions before suspecting hardware.
        quint64 bandscopeTimeouts = 0;
        // EP6 silence recovery: run-command re-sends by the silence watchdog, and how
        // many the stream came back from; attempts without completions means this is the
        // wrong recovery for the fault. A successful recovery is otherwise invisible
        // (m_linkUp never drops), so these healthSnapshot() rows are its only record.
        // Zeroed at start(), not at stop().
        quint64 silenceRecoveryAttempts = 0;
        quint64 silenceRecoveriesCompleted = 0;
        // Over the publish window only, so a stall that has ended stops being
        // reported as if it were still happening. Negative = nothing measured.
        int meanGapMs = -1;
        int maxGapMs = -1;
        // "ip:port" of our bound socket. The address is the one the KERNEL
        // delivered on, taken from the first datagram that carries a
        // destination — a wildcard bind knows only the port, and "0.0.0.0"
        // names no interface to an operator debugging a multi-homed host.
        // "*:<port>" until (or unless) the platform supplies it.
        QString localEndpoint;
    };
    // Blocking discovery; returns the radios seen within timeoutMs. Safe to
    // call before start().
    virtual QList<Discovered> discover(int timeoutMs = 2000,
                               const QHostAddress& broadcast = QHostAddress::Broadcast,
                               quint16 port = kMetisPort) = 0;

    Q_INVOKABLE virtual bool start(const Params& params) = 0;
    Q_INVOKABLE virtual void stop() = 0;

    // Live control.
    Q_INVOKABLE virtual void setRxFrequencyHz(std::uint32_t hz) = 0;
    Q_INVOKABLE virtual void setRxFrequencyHz(int rxIndex, std::uint32_t hz) = 0;
    Q_INVOKABLE virtual void setSampleRate(SampleRate rate) = 0;
    Q_INVOKABLE virtual void setReceiverCount(int count) = 0;
    Q_INVOKABLE virtual void setLnaGainDb(int db) = 0;
    // int, not uint8_t: cross-thread Q_ARG matches moc's recorded type name.
    Q_INVOKABLE virtual void setBandFilter(int ocFilterByte) = 0;
    Q_INVOKABLE virtual void setDitherRandomBits(bool dither, bool random) = 0;
    Q_INVOKABLE virtual void setLocalCodec(bool present) = 0;
    // Stereo speaker audio for the radio's codec: interleaved int16 L,R, 48 kHz.
    Q_INVOKABLE virtual void submitSpeakerAudio(const QByteArray& interleavedInt16) = 0;
    Q_INVOKABLE virtual void clearSpeakerAudio() = 0;
    Q_INVOKABLE virtual void setCl1RefClock(bool externalRef) = 0;
    Q_INVOKABLE virtual void setAtuTuneRequest(bool request) = 0;
    Q_INVOKABLE virtual void setIoBoardTxFrequencyHz(quint64 hz) = 0;
    Q_INVOKABLE virtual void requestPipelineReset() = 0;
    Q_INVOKABLE virtual bool requestRegister(int addr, quint32 data, bool subsystemRead = false) = 0;
    Q_INVOKABLE virtual void requestIoBoardStatus() = 0;
    Q_INVOKABLE virtual void setBandscopeEnabled(bool on) = 0;
    Q_INVOKABLE virtual void requestBandscopeFrame() = 0;

    // The HL2 bandscope gate's sampling period (Hl2BandscopeHeadroom.h).
    [[nodiscard]] static constexpr int bandscopeSamplePeriodMs() noexcept { return 1000; }

    // Receivers that can be run and tuned (the HL2's RX1..RX7 NCO run).
    static constexpr int kMaxTunableRx = 7;

    // numRx clamped to what the board reports and to kMaxTunableRx. Answers
    // from Params alone because the DSP chains are built before start().
    static int effectiveNumRx(const Params& p)
    {
        int n = p.numRx < 1 ? 1 : p.numRx;
        if (p.boardMaxRx > 0 && n > p.boardMaxRx)
            n = p.boardMaxRx;
        if (n > kMaxTunableRx)
            n = kMaxTunableRx;
        return n;
    }

    // Transmit gate. With the gate closed MOX and transmit samples are refused,
    // so a refused key fails safe. Policy lives in Hl2Backend.
    void enableTransmit(bool allowed) noexcept { m_txAllowed = allowed; }
    [[nodiscard]] bool transmitEnabled() const noexcept { return m_txAllowed; }

    virtual void setMox(bool keyed, const TxCoordinator::Operation& operation) = 0;
    // Only the backend's explicit CW break-in path uses CW holds to sustain MOX.
    virtual void setCwMox(bool keyed, const TxCoordinator::Operation& operation) = 0;
    Q_INVOKABLE virtual void setTxFrequencyHz(std::uint32_t hz) = 0;
    Q_INVOKABLE virtual void setTxDriveLevel(int level) = 0;
    // Software CW for a PC/USB/MIDI keyer; requires MOX.
    virtual void setCwKeyDown(bool down, const TxCoordinator::Operation& operation) = 0;
    Q_INVOKABLE virtual void clearCwKeying() = 0;
    // Transmit IQ from Hl2TxDsp. Underflow is silence, overflow drops the oldest.
    virtual void queueTxIq(std::span<const std::complex<float>> iq,
                           const TxCoordinator::Context& context) = 0;
    // Discard pending transmit IQ (unkey).
    Q_INVOKABLE virtual void flushTxIq() = 0;
    // Baseband test tone, offsetHz from the TX carrier, amplitude 0..1 (<= 0
    // disables). Never enabled implicitly.
    virtual void setTxTestTone(double offsetHz, double amplitude,
                               const TxCoordinator::Operation& operation) = 0;

signals:
    void linkUp();                                                  // first EP6 seen
    void linkDown();                                               // stopped
    // One per EP6 packet, carrying RX1 only. Kept for the single-receiver
    // consumers (bring-up probes, the TX-side tests) that have no notion of a
    // receiver index; it is emitted alongside iqBlocksReady, not instead of it.
    void iqBlockReady(const std::vector<std::complex<float>>& block);
    // One per EP6 packet, carrying EVERY active receiver: blocks[i] is DDC i,
    // and blocks.size() == effectiveNumRx(). The span is a view into a buffer
    // this object reuses, so a receiver must copy anything it keeps.
    void iqBlocksReady(const std::vector<std::vector<std::complex<float>>>& blocks);
    void dropsUpdated(quint64 drops);                             // cumulative EP6 gaps
    // The next IQ block is discontinuous with the previous one. Emitted
    // before iqBlockReady/iqBlocksReady in the same handleDatagram() call,
    // including accepted rewinds and duplicates. Direct consumers can clear
    // partial FFTs before accepting the new samples. `lost` is the forward
    // packet gap, or zero for a rewind/duplicate; dropsUpdated stays loss-only.
    void rxSequenceGap(quint32 lost);
    // Transport counters, published about once a second from the receive path.
    // Rate-limited for the same reason telemetryUpdated is: this would otherwise
    // cross to the GUI thread thousands of times a second to move a byte count.
    //
    // It stops arriving when EP6 stops, and that is load-bearing — the consumer
    // reads the absence as the link having gone quiet. Do NOT "fix" this by
    // driving it from a timer here.
    void linkCountersUpdated(const AetherSDR::hl2::MetisClient::LinkCounters& c);
    // Radio telemetry decoded from the EP6 C&C bytes: forward/reverse power,
    // temperature, TX FIFO status, ADC overload, PTT. Free-running, so it
    // arrives without us issuing a request.
    //
    // "status", not "depth": the wire carries a recovery flag plus the top 7
    // bits of the fill level, and no sample count at all. See
    // Hl2Telemetry::apply().
    void telemetryUpdated(const AetherSDR::hl2::Hl2Telemetry& t);
    // The radio's own keying changed: ptt_resp = cw_on | ext_ptt (control.v:456,
    // gateware 74), the PTT/key jack or the internal keyer. Host MOX never raises
    // it. An observation, per edge and unthrottled; false is also emitted when the
    // stream ends or goes silent with the bit set.
    void radioPttChanged(bool keyed);
    // One accepted bandscope block: 2048 contiguous converter samples merged from the
    // four in-phase EP4 packets the gate kept (never per packet). Uncalibrated and
    // pre-DDC, on the AD9866's own scale: nothing downstream may treat these as
    // absolute or make a decision from them (IRadioBackend.h).
    void bandscopeBlockReady(const AetherSDR::hl2::Ep4Stats& block);
    // The block requestBandscopeFrame() asked for: kEp4BlockSamples converter codes,
    // contiguous (26.67 us of the 76.8 MSPS ADC), normalised to [-1, 1) by
    // kEp4FullScale. Separate from bandscopeBlockReady so only a request pays for the
    // decode; a QList so it can cross threads queued. Uncalibrated and pre-DDC.
    void bandscopeFrameReady(const QList<float>& samples);
    // The request could not be answered, with the reason in the operator's
    // words. Emitted once per failed request, never alongside a Ready.
    void bandscopeFrameFailed(const QString& reason);
    // ANAN-100D port: spectrum of the DUC IQ as sent to the radio while MOX
    // is on (anan::TxScope): TxScope::kBins dBFS values, lowest frequency
    // first, spanning centreHz +-12 kHz, where centreHz is the TX (DUC)
    // frequency the samples were sent at. 0 dBFS = full scale of the wire
    // samples. ~23 per second, from the transport's thread.
    // Build 346: one more value after the bins -- the envelope peak of the
    // samples sent since the previous frame, dBFS (kBins + 1 values in all).
    void txScopeFrameReady(const QList<float>& dbfs, double centreHz);
    // ANAN-100D port: peak |IQ| of what goes to the DUC, dBFS, every 50 ms
    // while transmitting (the TX:ALC meter: the envelope after the whole
    // transmit chain).
    void txEnvelopePeak(float dbfs);
    // No EP6 arrived within kConnectTimeoutMs of start() — the radio is off,
    // unreachable, or already streaming to a different client.
    void connectFailed(const QString& reason);

    // A RQST issued through requestRegister() was acknowledged. `addr` is the
    // address ASKED FOR, not the one in the ACK, so a caller never has to
    // reason about the 0x3F refusal encoding; `data` is the register's echo,
    // or the read value for a subsystem read.
    void controlReplyReady(int addr, quint32 data);
    // A RQST did not come back. `refused` distinguishes the radio saying no (a
    // subsystem was not ready) from the radio saying nothing at all. Both are
    // ordinary outcomes on this protocol, not errors — and after a timeout the
    // machine is in quarantine, so the next requestRegister() will refuse for
    // a while. Consumers must not retry immediately in this handler.
    void controlRequestFailed(int addr, bool refused);
    // requestIoBoardStatus()'s answer: registers 6..9 as read (decodeIoBoardStatus),
    // never the echo of an ignored read, or why there is no reading.
    void ioBoardStatusReady(quint32 registers);
    void ioBoardStatusFailed(const QString& reason);

private:
    bool m_txAllowed = false;   // gate; see enableTransmit()
};

}  // namespace AetherSDR::hl2

// Hl2Telemetry, LinkCounters and Ep4Stats ride queued signals from the I/O
// thread to the GUI thread; MetisProtocol.h is Qt-free, so they are declared
// here.
Q_DECLARE_METATYPE(AetherSDR::hl2::Hl2Telemetry)
Q_DECLARE_METATYPE(AetherSDR::hl2::MetisClient::LinkCounters)
Q_DECLARE_METATYPE(AetherSDR::hl2::Ep4Stats)
