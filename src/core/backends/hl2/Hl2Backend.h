#pragma once

#include "core/backends/AutoRfGainControl.h"
#include "core/backends/IRadioBackend.h"
#include "core/dsp/WdspChannel.h"
#include "core/dsp/WdspSMeter.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include "core/backends/hl2/Hl2AdcPairing.h"
#include "core/backends/hl2/Hl2AutoGainPolicy.h"
#include "core/backends/hl2/Hl2BandMemoryPolicy.h"
#include "core/backends/hl2/Hl2CapabilityAnnouncer.h"
#include "core/backends/hl2/Hl2DbReference.h"
#include "core/backends/hl2/Hl2HardwareOptions.h"
#include "core/backends/hl2/Hl2IoBoardPolicy.h"
#include "core/backends/hl2/Hl2TelemetryCadence.h"  // Hl2LinkState (#15)
#include "core/backends/hl2/Hl2TelemetryService.h"  // borrowed, owned by RadioModel
#include "core/backends/hl2/Hl2TelemetrySource.h"   // the shared attribution rule
#include "core/backends/hl2/Hl2RateCommit.h"
#include "core/backends/hl2/Hl2Receivers.h"
#include "core/backends/hl2/MetisProtocol.h"   // Hl2Telemetry
#include "core/backends/anan/AnanTxMonitor.h"
#include "core/backends/anan/P2Protocol.h"   // AnanModel / ModelTraits
#include "core/backends/anan/P1Protocol.h"   // P1PsResampler (TCI IQ, build 222)

#include <QByteArray>

#include <span>
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <array>
#include <mutex>
#include <limits>
#include <memory>
#include <utility>
#include <optional>
#include <vector>

namespace AetherSDR::fdv {
class FdvEngine;
class HalfbandResampler;
}

namespace aether_drm { class DrmEngine; }

namespace AetherSDR { class ClientComp; }
namespace AetherSDR::anan { class AnanPureSignal; class AnanTxEngine; }

namespace AetherSDR::hl2 {

class MetisClient;
class Hl2RxDsp;
class Hl2TxDsp;

// The IQ rates the HL2's DDC can run, ascending. One list serves as the
// capability advertisement (sampleRatesHz), the pan zoom limits and the snap set
// (nearestIqSampleRateHz), because on this radio the pan span IS the sample rate
// (Hl2Backend::emitPanState). In the header so the capability test asserts
// against this array, not a copy.
inline constexpr int kIqSampleRatesHz[] = {48000, 96000, 192000, 384000};
// ANAN-100D port: Protocol 2 DDC rates (one DDC per receiver, no shared link
// encoding), up to 1536 kHz.
inline constexpr int kAnanIqSampleRatesHz[] = {48000, 96000, 192000, 384000, 768000, 1536000};

// The wideband converter view this backend declares when connected. A free
// function so a typo in the named extension verb is caught by a socket-free test.
[[nodiscard]] AetherSDR::WidebandConverterView widebandConverterViewRecord() noexcept;

// IRadioBackend for the Hermes-Lite 2 (HPSDR Protocol 1, raw IQ). Owns a
// MetisClient (UDP wire) and the Hl2RxDsp/Hl2TxDsp chains (RFC §5.5).
// THIS BACKEND CAN KEY THE RADIO. capabilities().canTransmit reports TX
// availability; automation defers to the bridge's TX gate and MetisClient
// refuses independently at the wire, so neither gate is the only one.
// Wire and DSP run on a dedicated I/O thread; this backend paces EP2, and the
// gateware watchdog halts the stream if EP2 stops. IAutoRfGainControl methods
// are thin forwarders to this class's own names.
class Hl2Backend : public IRadioBackend, public IAutoRfGainControl {
    Q_OBJECT

public:
    // ANAN-100D port: the Protocol 2 transport (radio I/O thread), for the
    // Wideband Monitor's widebandFrame signal and setWideband().
    QObject* ananTransport() const;
    // ANAN-100D port: the PureSignal controller (GUI thread).
    anan::AnanPureSignal* pureSignal() const;
    // ANAN-100D port: this backend now drives only the ANAN over openHPSDR
    // Protocol 2 (anan::P2Transport); Hermes-Lite 2 support was removed. The
    // class keeps its historical name (see the port journal, stage H2).
    explicit Hl2Backend(QObject* parent = nullptr);
    ~Hl2Backend() override;

    RadioCapabilities capabilities() const override;
    // Build 302: what a not-yet-connected openHPSDR backend declares, with no
    // backend at all (RadioModel serves it while nothing is connected).
    [[nodiscard]] static RadioCapabilities presumedCapabilities();
    // Demodulates in-process (Hl2RxDsp); there is no VITA-49 stream at all.
    bool ownsRxAudio() const override { return true; }

    void connectRadio(const RadioConnectRequest& request) override;
    // RFC #4603: the client is this radio's memory. Restored state is validated and
    // stashed here pre-connect, applied during connect/pushInitialState; capture
    // reports through currentOperatingState() + operatingStateChanged().
    void applyRestoredState(const RestoredRadioState& state) override;
    // The validated document applyRestoredState() kept (test seam).
    // currentOperatingState() reads the receivers, seeded only at linkUp, so
    // pre-connect tests assert here instead (#5031).
    const RestoredRadioState& restoredStateForTest() const { return m_restoredState; }
    RestoredRadioState currentOperatingState() const override;
    void disconnectRadio() override;
    bool isConnected() const override;

    void setSliceFrequency(int sliceId, double hz) override;
    // `requested`: alias spellings (CWU/NFM/WFM) are canonicalised onto
    // publishedModeStrings() before a slice holds them.
    void setSliceMode(int sliceId, const QString& requested) override;
    void setSliceFilter(int sliceId, int lowHz, int highHz) override;
    void setCwPitch(int hz) override;
    void setSliceAgc(int sliceId, const QString& mode, int thresholdDb) override;
    // Impulse noise blankers, run in host WDSP (the HL2 has no firmware DSP); at
    // most one of the two runs. NR and ANF are deliberately not implemented and
    // stay hidden.
    void setSliceNoiseBlanker(int sliceId, AetherSDR::NoiseBlankerKind kind,
                              int level, AetherSDR::NoiseBlankerFill fill) override;
    ReceiveDispatch requestSliceDsp(int sliceId, const SliceDspRequest& request) override;
    ReceiveDispatch requestSliceControl(int sliceId, const SliceControlRequest& request) override;
    // Build 248: the VFO flag's RX antenna (the ANT panel's AnanRxAnt).
    ReceiveDispatch requestSliceRxAntenna(int sliceId, const QString& antenna) override;
    // Build 317: transverters kept on the host (anan/AnanXvtr.h).
    ReceiveDispatch requestTransverter(const TransverterRequest& request,
                                       const RadioSetupCompletion& completion = {}) override;
    ReceiveDispatch requestTransmitControl(const TransmitControlRequest& request,
        TunePowerContext context = TunePowerContext::Deferred) override;
    ReceiveDispatch requestSliceAudio(int sliceId, const SliceAudioRequest& request) override;
    ReceiveDispatch requestSliceSquelch(int sliceId, const SliceSquelchRequest& request) override;
    void setSliceSquelch(int sliceId, bool on, int level) override;
    // Host-side CW APF and AGC-off level, per receiver; see Hl2RxDsp.
    void setSliceApf(int sliceId, bool on, int level) override;
    // Handles SliceAgcRequest::Field::OffLevel (the WDSP fixed gain) and drops
    // a recalled AGC mode in DIGU/DIGL; the rest goes to the base, i.e.
    // setSliceAgc().
    void requestSliceAgc(int sliceId, const SliceAgcRequest& request) override;
    void setSliceAudioMute(int sliceId, bool mute) override;
    void setSliceAudioGain(int sliceId, int gainPercent) override;
    void setSliceAudioPan(int sliceId, int panPercent) override;
    void setTxSlice(int sliceId) override;
    void setActiveSlice(int sliceId) override;
    void setPanCenter(const QString& panId, double hz,
                      PanCenterIntent intent) override;
    void setPanBandwidth(const QString& panId, double hz) override;
    void setPanRfGain(const QString& panId, int gainDb) override;
    void setAutoRfGain(bool on);

private:
    // The span change after the throttle settles. The DDC rate is radio-wide
    // (0x00[25:24]), so every panadapter shares one span.
    void applyPanBandwidth(double hz);

public:
    void setPanFrameRate(const QString& panId, int fps) override;
    // The operator's FFT AVG (0..100) and weighted toggle. This backend owns
    // the panadapter's averaging (RFC #5782), so both land in the receiver's
    // Hl2Spectrum; see averageTimeMsForStep() and Hl2Spectrum::setAverageTimeMs().
    void setPanAverage(const QString& panId, int average) override;
    void setPanWeightedAverage(const QString& panId, bool on) override;

    // One FFT AVG step is 10 ms of averaging time constant, so 0..100 spans
    // 0..1 s: the ANAN's unit (AnanBackend's kMsPerAverageStep), so one setting
    // means one time constant on both families. A time, not a frame count: a
    // depth in frames would move with the fps slider. It does not match a Flex,
    // whose `average=` has no documented unit.
    static constexpr int kMsPerAverageStep = 10;
    [[nodiscard]] static constexpr int averageTimeMsForStep(int average) noexcept
    {
        return (average < 0 ? 0 : (average > 100 ? 100 : average)) * kMsPerAverageStep;
    }
    bool createPanadapter() override;
    bool removePanadapter(const QString& panId) override;
    void createNotch(double centerHz, double widthHz) override;
    void setNotch(int notchId, const AetherSDR::NotchDelta& delta) override;
    void removeNotch(int notchId) override;
    void setNotchesEnabled(bool on) override;
    void setKeying(bool key, const AetherSDR::TxCoordinator::Operation& operation, const AetherSDR::TxCoordinator::Completion& completion = {}) override;
    void setCwKeying(bool down, bool breakIn, int breakInDelayMs, const AetherSDR::TxCoordinator::Operation& operation, const AetherSDR::TxCoordinator::Completion& completion = {}) override;
    void submitTxAudio(const QByteArray& int16Stereo, int sampleRateHz,
                       TxAudioSource source,
                       const TxCoordinator::Context& context) override;
    void setTxPower(int percent) override;
    void setTunePower(int percent) override;
    void setTxFilter(int lowHz, int highHz) override;
    void setMicGain(int level) override;
    // No default argument: defaults on virtuals bind statically and would diverge
    // from the base's. The sole call site passes it explicitly.
    void setTune(bool on, int tunePowerPercent, const AetherSDR::TxCoordinator::Operation& operation, const AetherSDR::TxCoordinator::Completion& completion = {}) override;
    void setTxAudioMonitor(bool on) override;
    void setTxFrequency(double hz);
    void setTxDriveLevel(int level);
    // Baseband TX test tone, offsetHz from the carrier, amplitude 0..1.
    // Opt-in only — never enabled by a default.
    void setTxTestTone(double offsetHz, double amplitude, const TxCoordinator::Operation& operation);

    void invokeExtension(const QString& ns, const QString& verb, quint64 requestId,
                         const QVariant& arg) override;

    HealthSnapshot healthSnapshot() const override;
    QVariantList dspChains() const override;

    // LNA gain split (Hl2GainSplit.h). `lnaBaselineDb` is the operator's: stored per
    // band, persisted, written only by setPanRfGain and band-memory restore.
    // `lnaAutoOffsetDb` is a non-negative session-only attenuation below it, owned by
    // the automatic control. The register, Hl2DbReference and panRfGainChanged carry
    // `lnaEffectiveDb`. An offset of 0 restores the operator's number in one action.
    void setLnaAutoOffsetDb(int offsetDb);
    [[nodiscard]] int lnaAutoOffsetDb() const noexcept { return m_lnaAutoOffsetDb; }
    [[nodiscard]] int lnaBaselineDb() const noexcept { return m_lnaGainDb; }
    [[nodiscard]] int lnaEffectiveDb() const noexcept;
    [[nodiscard]] bool autoRfGainEnabled() const noexcept { return m_autoRfGainEnabled; }

    // Max attenuation the loop may apply, in dB below the operator's baseline. The
    // floor belongs to the law: 24 dB for "bandscope" (what a backend starts with)
    // and "probe", 26 for "ramp" and "binary". The 26 was sized from #5354's sweep,
    // whose gain labels read 32 dB low (one unit's defect, #5943), so it is not a
    // measured bound. Everything else in Hl2AutoGainPolicy.h is deliberately not
    // operator-settable.
    void setAutoRfGainFloorDb(int floorDb);
    [[nodiscard]] int autoRfGainFloorDb() const noexcept
    {
        return m_autoGainConfig.maxOffsetDb;
    }
    // The deepest floor the operator may configure: the whole native span, so
    // from any armable baseline the loop can be allowed to dig to the register
    // floor. The default floor belongs to the installed law (above).
    static constexpr int kAutoRfGainFloorMaxDb = hl2::kLnaGainMaxDb - hl2::kLnaGainMinDb;

    // Which Hl2AutoGainPolicy.h configuration the loop runs:
    // "bandscope" (default) bandscopeReleaseConfig(): probing law whose release
    //             needs a measured wideband headroom reading; arms the bandscope gate.
    // "ramp"      3-6 dB attack, 1 dB release on a dwell.
    // "probe"     probingReleaseConfig(): 6 dB both ways; failed probes double the
    //             interval. What "bandscope" degenerates to without the measurement.
    // "binary"    binaryHighLowConfig(): two-state per-band switch.
    // Same state machine, different numbers; selecting a mode installs its floor.
    // "default" names the law a backend starts with. A chosen law and floor are
    // persisted per radio (rfGain); "default" clears the choice. Returns false,
    // changing nothing, on an unknown name.
    bool setAutoRfGainMode(const QString& mode);

    // IAutoRfGainControl (AutoRfGainControl.h), thin forwarders. autoRfGainControl()
    // answers unconditionally; whether to show it is RadioModel::autoRfGain()'s call.
    IAutoRfGainControl* autoRfGainControl() override { return this; }
    void setArmed(bool on) override { setAutoRfGain(on); }
    [[nodiscard]] bool isArmed() const override { return m_autoRfGainEnabled; }
    [[nodiscard]] QString lastArmRefusalReason() const override
    {
        return m_autoRfGainRefusal;
    }
    void setFloorDb(int floorDb) override { setAutoRfGainFloorDb(floorDb); }
    [[nodiscard]] int floorDb() const override { return autoRfGainFloorDb(); }
    [[nodiscard]] int maxFloorDb() const override { return kAutoRfGainFloorMaxDb; }
    bool setLaw(const QString& name) override { return setAutoRfGainMode(name); }
    [[nodiscard]] QString law() const override { return m_autoGainMode; }
    [[nodiscard]] QStringList laws() const override
    {
        // Order is the recommendation: the first is the default. See setAutoRfGainMode.
        return {QStringLiteral("bandscope"), QStringLiteral("ramp"),
                QStringLiteral("probe"), QStringLiteral("binary")};
    }
    [[nodiscard]] QString autoRfGainMode() const { return m_autoGainMode; }

    // The highest baseline from which the automatic control will arm: the top of the
    // native range, so every baseline the slider offers can arm. Outside it the
    // control refuses rather than clamps (#5395): it never moves the operator's number.
    static constexpr int kAutoRfGainMaxBaselineDb = hl2::kLnaGainMaxDb;

    // dspChains()' gather. Static so it cannot reach m_rx: this runs on the I/O
    // thread and m_rx is GUI-thread-owned (push_back/erase reallocate under a
    // reader, #5401). Public as the read-back test seam.
    static QVariantList gatherDspChains(const std::vector<Hl2RxDsp*>& rxDsps,
                                        Hl2TxDsp* txDsp);
    LinkStats linkStats() const override;

    // Point the stream-free telemetry poller at a radio, independent of
    // connectRadio(): the target case is a radio someone else holds. A null address
    // stops the poller and releases its socket. `heldByOther` is the caller's
    // assertion; telemetryLinkState() also reads the radio's own in-use bit.
    void setTelemetryPollTarget(const QHostAddress& addr, bool heldByOther);
    // Borrowed, never owned. Null: this backend does not drive the service.
    void setTelemetryService(Hl2TelemetryService* svc) { m_telemetryService = svc; }

    // Take the model's offline health source if it is ours. The dynamic_cast inside
    // this family is deliberate: RadioModel stays family-blind (#5554 §2.8). A null
    // or foreign source disables the in-band drive; it is not an error.
    void setOfflineHealthSource(IOfflineHealthSource* src) override
    {
        setTelemetryService(dynamic_cast<Hl2TelemetryService*>(src));
    }

signals:
    // Build 222: a receiver's I/Q for TCI (CW Skimmer, SDC, ...): interleaved
    // float32 I/Q in host order (little-endian), centred on the receiver's
    // pan centre, at the rate TCI asked for. Emitted on the I/O thread.
    void tciIqReady(int channel, const QByteArray& iq, int sampleRate);
    // ANAN port: DRM decoder state for a slice, about four times a second
    // while it runs (keys: see Hl2Backend::publishDrm), and each new
    // slideshow picture as sent (JPEG / PNG bytes).
    void drmStatusReady(int sliceId, const QVariantMap& status);
    void drmSlideReady(int sliceId, const QByteArray& image, const QString& name);
    // ANAN-100D port: the PTT of the radio's own microphone. The ANAN does not
    // key itself: the host turns this into an ordinary PTT (RadioModel ->
    // MainWindow), which keys MOX through the TX coordinator.
    void ananHardwarePtt(bool pressed);
    // ANAN-100D port: level of the radio's own microphone (dBFS, before the
    // mic gain), ~20 per second while it is the selected source -- for the
    // Mic/CW level meter, which otherwise shows the computer's microphone.
    void ananCodecMicLevel(float peakDb, float avgDb);
    // ANAN-100D port (06.11): the radio codec's microphone (CDC) for the TX
    // voice chain, 48 kHz mono float -- emitted on the I/O thread while CDC is
    // the source; and whether it is (AudioEngine switches its chain input).
    void ananCodecMicPcm(const QByteArray& float48Mono);
    void ananCodecMicInputChanged(bool cdc);
    // ANAN-100D port: spectrum of the IQ sent to the radio while transmitting
    // (MetisClient::txScopeFrameReady), centre converted to MHz.
    void ananTxScopeFrame(const QList<float>& dbfs, double centreMhz);
    // Connect-time progress for the client-side WDSP build; not on the seam because
    // WDSP is this family's alone. `stage` is operator-facing text without a counter;
    // `done`/`total` count receiver WDSP channel opens (TX opens none). Emitted on
    // the GUI thread.
    void dspSetupProgress(const QString& stage, int done, int total);
    void dspSetupFinished();

private:
    friend struct Hl2DspReadbackTestAccess;
    friend struct Hl2PanCreateTestAccess;
    friend struct Hl2PcmTestAccess;
    friend struct Hl2TxGateTestAccess;
    friend struct Hl2UnkeyHoldTestAccess;
    // Hands receiver 0 a configured Hl2RxDsp so the APF and AGC-off verbs can
    // be followed from the seam into WDSP without a socket.
    friend struct Hl2ApfAgcOffTestAccess;
    // The same hand-off, so hl2_rxdsp_squelch_test follows an LNA change into
    // the squelch gate.
    friend struct Hl2SquelchTestAccess;
    // Delivers one bandscope block through MetisClient's signal and ages the mirror,
    // so converter-row expiry is testable without a radio.
    friend struct Hl2HealthBlockTestAccess;
    // Marks the backend connected and reaches its MetisClient, so the
    // `ioboard.status` verb can be followed to the client and back without a socket.
    friend struct Hl2IoBoardStatusTestAccess;
    // Reads the receive shift/NCO and adds a second receiver's state without a
    // socket or DSP, for hl2_rit_xit_test. Reaches nothing else.
    friend struct Hl2RitXitTestAccess;
    friend struct Hl2Cl1ReferenceTestAccess;
    // Fires link edges through MetisClient's signals and seeds the connect
    // baseline, so hl2_auto_gain_law_test reads the installed law without a radio.
    friend struct Hl2AutoGainLawTestAccess;
    friend struct Hl2SessionMemoryTestAccess;
    // Feeds EP6 status frames to the backend's own MetisClient on its I/O thread,
    // for hl2_hardware_ptt_state_test.
    friend struct Hl2HardwarePttTestAccess;
    // The radio keyed or unkeyed itself (MetisClient::radioPttChanged). Observation
    // only: it publishes the transmit state and mutes receive, and keys nothing.
    void onRadioPttChanged(bool keyed);
    // Emit transmitChanged{mox} when m_keyed || m_radioPtt changes, and only then.
    void publishTransmitState();
    // Reads the snapshot at a chosen instant, so hl2_adc_age_gate_test puts a
    // held slice peak on either side of kSliceStaleMs without waiting.
    friend struct Hl2AdcAgeTestAccess;
    // The snapshot at `nowNs`, on steadyNowNs()'s timeline. The converter rows
    // take their age from it and read no clock, so the age row and the pairing
    // gate describe one instant.
    HealthSnapshot healthSnapshot(std::int64_t nowNs) const;
    void applyKeying(bool key, const TxCoordinator::Operation& operation,
                     const TxCoordinator::Completion& completion, bool cwBreakIn);
    void invalidateTxDspConfiguration();
    // Publish linkStats() on the seam's fixed cadence. Timer-driven so the tick
    // survives the radio going silent, which the heartbeat must detect.
    void publishLinkStats();
    // Re-select the companion filter board's band filter for the current slice
    // frequency. Idempotent and change-gated, so it is safe to call from every
    // path that can move the dial.
    void applyBandFilter(const char* reason);
    // Push the TX frequency to the HL2 IO Board, throttled. Called from
    // applyBandFilter() above its `oc == m_ocFilterByte` early return: the filter
    // byte changes per band, the IO board wants every frequency change.
    void applyIoBoardFrequency();
    // The single point where either throttle edge reaches the wire, so the
    // disconnected guard covers both. Returns false when refused.
    [[nodiscard]] bool sendIoBoardFrequency(quint64 hz);
    // Drop the IO-board schedule on linkDown: armed timer, coalesced value and
    // remembered band all describe a session and must not survive one.
    void resetIoBoardSchedule();
    // Reset every bandscope mirror to "never had one". Called from all three link
    // edges so a snapshot between a drop and the next connect cannot report a dead
    // session's bandscope.
    void resetBandscopeMirrors();
    // Age of the mirrored bandscope block; negative when none has arrived, read as
    // "never observed" by all three readers.
    [[nodiscard]] std::int64_t bandscopeBlockAgeMs() const;
    // Per-band memory (RFC #4603): apply the remembered LNA + drive for freqHz's
    // band and record current values for the band being left. Drive falls back to
    // m_driveDefaultPercent (restored per-profile latch); LNA falls back to
    // hl2::kLnaDefaultGainDb, nothing restored (#5829).
    void applyPerBandStateFor(double freqHz, const char* reason);
    void applyLnaGainDb(int gainDb);   // the one true LNA BASELINE application
    // Push m_lnaGainDb - m_lnaAutoOffsetDb to the register, the dB reference and
    // every pan. Called by both writers of the split (see Hl2GainSplit.h): the
    // baseline path above, and the automatic-offset path below.
    void pushEffectiveLnaGain();
    void rememberCurrentBandState();
    void notifyOperatingStateChanged();

    // The connect's three phases. connectRadio() seeds every member synchronously
    // (hl2_state_restore_test reads currentOperatingState() right after it returns),
    // then beginDspSetup() opens the WDSP channels on the I/O thread without blocking
    // the GUI thread (cold FFTW wisdom costs tens of seconds), and finishDspSetup()
    // resumes on the GUI thread and starts the wire. Every chain is still open before
    // MetisClient::start(), serially: EP2 must not stop (docs/HERMES.md §20.8).
    // finishRateChange() is the GUI-thread half of a rate change, after the build
    // thread has produced new chains and the I/O thread swapped them in; see
    // applyPanBandwidth().
    void finishRateChange(bool ok, quint64 generation, int targetRate,
                          int previousRate,
                          const std::vector<QPointer<Hl2RxDsp>>& covered,
                          std::size_t failedIndex,
                          const std::string& error);


    // Which rate is on the wire (not merely attempted) and which crossing is
    // current. The generation is atomic because the I/O-thread install step checks
    // it last before touching anything live; a superseded crossing installs and
    // publishes nothing. See Hl2RateCommit.h.
    AetherSDR::hl2::RateCommitLedger m_rateLedger {48000};

    void beginDspSetup();
    void armDspSetupWatchdog();
    void onDspSetupWatchdog();

    // State the async build carries across event-loop turns (wire params, RX/TX
    // configs, owning connect). Defined in the .cpp to keep forward declarations.
    struct PendingConnect;
    struct DspSetupResult;
    void finishDspSetup(const DspSetupResult& result);

    std::unique_ptr<PendingConnect> m_pendingConnect;
    // Watches the window between beginDspSetup() returning and finishDspSetup()
    // being posted back — the one stretch of the connect that no other timer
    // covers, because MetisClient's watchdog is armed after it (#5413).
    QTimer* m_dspSetupWatchdog = nullptr;
    // A connect that arrived while m_pendingConnect was still building. Held
    // rather than served inline — see the guard at the top of connectRadio().
    std::unique_ptr<RadioConnectRequest> m_queuedConnect;
    quint64 m_connectGeneration = 0;

    void emitSliceState(int ddc);   // sliceChanged(delta) from a receiver's state
    void emitPanState(int ddc);     // panCenterBandwidthChanged from its NCO + rate
    void emitAllSliceState();
    void emitAllPanState();
    void pushInitialState();
    // Put every receiver's AGC pair where this session should come up: remembered
    // values, else construction defaults (#4909). Called conditionally from
    // connectRadio() only; a same-radio reconnect must not reseed.
    void seedReceiverAgc();
    void defineMeters();

    // Declare/withdraw the S-meter of one receiver above the first. Receiver 0's
    // meter stays in defineMeters() because MeterModel::defineMeter uses the
    // preceding "SLC" definition as context for the TX meters. These are declared at
    // receiver creation, so no meter exists for a receiver that does not.
    void defineSliceLevelMeter(int uiNumber);
    void withdrawSliceLevelMeter(int uiNumber);
    void publishTelemetry(const Hl2Telemetry& t);

    // Drive the poller's LinkState from what the IQ path is doing
    // (Hl2TelemetryCadence.h). Called from publishLinkStats() and connect/disconnect.
    void updateTelemetryPollState();
    // What the IQ path is doing, for both the cadence rule and the health
    // snapshot's attribution row, so they cannot disagree.
    [[nodiscard]] Hl2LinkState telemetryLinkState() const;
    // Clamp 0..100, map onto the drive register, honour the transmit gate.
    // Shared by setTxPower() and setTune() so the mapping exists exactly once.
    void applyDrive(int percent);
    void beginTxTail();
    void cancelTxTail();
    void finishTxTail();
    static double temperatureCelsius(int raw);
    // Coupler counts -> watts is AetherSDR::hl2::directionalWatts() in MetisProtocol.
    // Watts -> dBm for the meter seam, floored so 0 W does not become -inf.
    static double wattsToDbm(double watts);

    MetisClient* m_metis = nullptr;
    // ANAN-100D port: m_ananBoardId is Discovery byte 11 (0 = not yet
    // known; the name then defaults to ANAN-100D).
    std::uint8_t m_ananBoardId = 0;
    // ANAN-100D port: which radio the board is (one board id covers several
    // models; since build 267 the board's default unless forcedly assigned,
    // anan/AnanForcedModel.h).
    anan::AnanModel m_ananModel = anan::AnanModel::Anan100D;
    void resolveAnanModel();
    bool m_ananModelLogged = false;
    // Build 267 (operator, 11.11): "Forcedly assign the radio" -- the operator
    // names the radio type when the board reports the wrong one (a modified
    // ANAN-100D answering Protocol 1 discovery as Hermes). AppSettings
    // "AnanForceModel" (True/False) and "AnanForcedModel" (model key). The
    // transport then drives the session with that model's board profile.
    [[nodiscard]] bool ananForcedModel(anan::AnanModel* out = nullptr) const;
    [[nodiscard]] std::vector<anan::AnanModel> ananModelsForProtocol() const;
    void pushAnanForcedBoard();
    [[nodiscard]] const anan::ModelTraits& ananTraits() const noexcept
    {
        return anan::modelTraits(m_ananModel);
    }
    // Build 277: the board profile of the radio as driven (the forcedly
    // assigned type's, else the reported board's) -- its ADC count and
    // band-filter scheme, the same profile the transport runs the session on.
    [[nodiscard]] anan::BoardProfile ananProfile() const
    {
        return anan::boardProfileFor(ananTraits().boardId).value_or(anan::defaultBoardProfile());
    }
    // Mic connector / ADC2 options of this model to the transport.
    void pushAnanModelOptions();
    // hw.get / hw.set: the model and its connector options.
    [[nodiscard]] QVariantMap ananHwFields() const;
    void applyAnanHwFields(const QVariantMap& in);
    double m_paVolts = 0.0;
    bool m_havePaVolts = false;
    double m_paAmps = 0.0;
    bool m_havePaAmps = false;
    double m_tuningMinHz = 100'000.0;
    double m_tuningMaxHz = 38'400'000.0;
    struct CapsInputs {   // build 302: what the capability record depends on
        const Hl2DbReference& dbRef;
        const anan::ModelTraits& traits;
        int receiverCeiling = 1;
        std::span<const int> iqRates;
        double tuningMinHz = 0.0;
        double tuningMaxHz = 0.0;
        bool txAllowed = false;
    };
    [[nodiscard]] static RadioCapabilities buildCapabilities(const CapsInputs& in);
    [[nodiscard]] QString radioDisplayName() const;
    // ANAN-100D port: the ANAN's rate ladder.
    [[nodiscard]] std::span<const int> iqRates() const noexcept;
    [[nodiscard]] int maxIqRateHz() const noexcept;
    [[nodiscard]] int nearestIqRateHz(double requestedHz) const noexcept;
    // ANAN-100D port: coupler counts -> watts / SWR for THIS radio (HL2:
    // MetisProtocol's Quisk curve; ANAN-100D: Thetis' Alex bridge formula).
    [[nodiscard]] double couplerWatts(int raw) const noexcept;
    [[nodiscard]] double couplerRevWatts(int raw) const noexcept;
    [[nodiscard]] bool ananOnSixMeters() const noexcept;
    [[nodiscard]] std::optional<double> couplerSwr(int fwdRaw, int revRaw) const noexcept;
    // AGC follows the noise floor (AnanUiState::agcAuto, 04.11): every 500 ms
    // on receive the ceiling is set from the floor + offset and the filter.
    void ananAutoAgcStep(int rx);
    void ananPublishAgcGate(int rx);   // build 301
    QElapsedTimer m_ananAgcClock[2];
    // ANAN-100D port: per-receiver attenuation and the ADC <-> attenuator
    // rule (AnanUiState.h): RX1 owns its ADC's attenuator; RX2 owns its own
    // only when it listens to the other ADC.
    [[nodiscard]] int ananRxAtt(int rx) const;
    [[nodiscard]] bool ananOwnsAtt(int rx) const;
    void ananPushAttenuators();
    void ananSyncUiState();
    int m_ananAdc[2] = {0, 0};
    bool m_ananAutoOn[2] = {false, false};
    int m_ananAutoAtt[2] = {0, 0};
    int m_ananRx2ManualAtt = 0;
    // AUTO ATT (operator 06.11): the noise floor in dBm / 500 Hz at the ADC
    // (ananNoiseLineStep: from the raw frame, independent of the FFT size, the
    // rate and the zoom), and the receiver's own noise the same way, captured
    // on request with the antenna disconnected (ananOwnNoiseCaptureStep). The
    // target is own + the margin (AnanUiState). The Auto AGC follows the
    // same floor.
    double m_ananShownFloor[2] = {0.0, 0.0};
    bool m_ananShownFloorOk[2] = {false, false};
    // Own-noise capture (Setup > Receive): all receivers that own their
    // attenuator at ATT 31 dB for a moment (ananOwnNoiseCaptureStep).
    struct OwnNoiseCapture {
        bool running = false;
        qint64 untilMs = 0;
        bool wanted[2] = {false, false};
        bool done[2] = {false, false};
        QString result;
    };
    OwnNoiseCapture m_ananCapture;
    mutable double m_ananOwnNoiseAdc[2] = {0.0, 0.0};
    mutable bool m_ananOwnNoiseLoaded[2] = {false, false};
    [[nodiscard]] double ananOwnNoiseOfAdc(int adc) const;
    void ananOwnNoiseCaptureStep(int rx, qint64 t);
    QElapsedTimer m_ananOwnClock;
    // Build 297: viewFirst / viewKeep = the bins of the frame on screen.
    // Build 335: [unsettledLo, unsettledHi) -- bins whose average has not
    // built up yet after a pan move; left out of the statistic.
    void ananNoiseLineStep(int rx, const std::vector<float>& rawDbfs, double rateHz,
                           std::size_t viewFirst, std::size_t viewKeep,
                           int unsettledLo = 0, int unsettledHi = 0);
    [[nodiscard]] int ananAutoAttMarginDb(int rx) const;
    // Build 295: the AUTO attenuator after MicronSDR (autoAttMeasure): the
    // floor averaged over windows of frames made at the commanded ATT, held
    // at an absolute target with a hysteresis; a step that does not lower
    // the floor marks the receiver's own noise and is taken back.
    [[nodiscard]] int ananAutoAttTargetDbm(int rx) const;
    struct AutoAttLoop {
        double acc = 0.0;
        int n = 0;
        qint64 winStartMs = 0;
        qint64 skipUntilMs = 0;   // frames still at the old ATT
        int lastStep = 0;         // the last step, dB of attenuation (+ = more)
        double lastAvg = 0.0;
        bool lowBlock = false;    // no more attenuation: the floor is our own noise
        double lowBlockAvg = 0.0;
        bool moving = false;
    };
    AutoAttLoop m_ananAutoLoop[2];
    void ananAutoAttStep(int rx, double floorCalDbm, qint64 t);
    [[nodiscard]] static double ananRbwTo500HzDb(double rateHz, std::size_t fftSize) noexcept;
    [[nodiscard]] double ananDisplayCalDb() const;
    // The receiver's own noise, dBm / 500 Hz at the ADC, without the meter's
    // calibration (published with it as AnanUiState::ownFloorDbm).
    double m_ananOwnNoise[2] = {std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::quiet_NaN()};
    static constexpr int kAnanOwnNoiseMeasureMs = 800;   // averaging, after the settle
    static constexpr int kAnanAttSettleMs = 300;          // frames still at the old ATT
    qint64 m_ananNoiseRestartUntilMs[2] = {0, 0};
    qint64 m_ananNoiseLogMs[2] = {-100000, -100000};
    double m_ananSMeterRaw[2] = {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::quiet_NaN()};
    QElapsedTimer m_ananAutoClock[2];
    int m_ananAdcAttPushed[2] = {-1, -1};
    QHash<QString, int> m_ananPanEcho;
    // ANAN-100D port: RX1/RX2 ADC choice from the ANT panel (AppSettings),
    // pushed to P2Transport when it changes.
    QTimer* m_ananAdcTimer = nullptr;
    // ANAN-100D port: FreeDV ("FDV", libfdv from MicronSDR) on one receiver.
    // FdvState (GUI) picks the slice / sub-mode / callsign; fdvTick() applies
    // them and publishes the engine status back. RX: mixReceiverAudio() runs
    // that receiver's audio through the decoder; TX: submitTxAudio() runs the
    // microphone through the modulator while keyed.
    fdv::FdvEngine* m_fdv = nullptr;
    QTimer* m_fdvTimer = nullptr;
    bool m_fdvInMix = false;
    bool m_fdvStopSent = false;
    int m_fdvTxBandKey = -1;   // build 339: sub-mode whose TX filter is pushed, -1 = not FreeDV
    // Build 343: the speech processing of the microphone ahead of the FreeDV
    // encoder (the compressor PROC uses, with its limiter). GUI thread.
    void fdvSpeechProcess(std::vector<float>& mono);
    bool m_fdvSpeechPrimed = false;
    // ANAN-100D port: the radio's codec. Speaker: the existing codec tap
    // (forwardSpeakerAudioToCodec) at "AnanCodecVolume"; microphone: port 1026
    // samples, 48 -> 24 kHz, replace the computer microphone in submitTxAudio()
    // when "AnanMicSource" is 1. "AnanMicBoost": +20 dB at the radio.
    bool m_ananMicFromRadio = false;   // AnanTxEngine modulates the radio mic (TEST only, 06.11)
    // CDC (AnanMicSource 1, 06.11): the radio mic goes through the TX voice
    // chain (AudioEngine), as the computer's does; read on the I/O thread.
    std::atomic<bool> m_ananCodecToChain{false};
    // Its 120 Hz high-pass at 48 kHz (input conditioning, I/O thread).
    double m_cdcHpX1 = 0.0, m_cdcHpX2 = 0.0, m_cdcHpY1 = 0.0, m_cdcHpY2 = 0.0;
public:
    [[nodiscard]] bool ananCodecMicActive() const noexcept { return m_ananCodecToChain.load(); }
private:
    int m_ananMicBoostPushed = -1;
    std::unique_ptr<fdv::HalfbandResampler> m_codecMicDown;
    std::vector<float> m_codecMicScratch;
    // ANAN-100D port: the radio microphone's DC blocker (runs on every
    // sample, keyed or not, so its state has settled before a key) and the
    // per-over fade-in (blank, then a raised-cosine ramp) that keeps the
    // PTT switch's step on the mic line out of the modulator and MON.
    // 2nd-order Butterworth high-pass, 120 Hz at 24 kHz (biquad state).
    float m_codecHpX1 = 0.0f, m_codecHpX2 = 0.0f, m_codecHpY1 = 0.0f, m_codecHpY2 = 0.0f;
    // Mic source TEST (AnanMicSource 2): a two-tone (700 + 1900 Hz, peak
    // full scale) replaces the radio microphone, through the whole voice
    // chain -- the Thetis 2-TONE test.
    bool m_ananTestTone = false;
    double m_testPh1 = 0.0, m_testPh2 = 0.0;
    // Per-over statistics of what the modulator got (log line at unkey).
    double m_overMicPeak = 0.0, m_overMicSumSq = 0.0, m_overLfSumSq = 0.0, m_overLf = 0.0;
    long long m_overMicCount = 0;
    bool m_codecMicWasKeyed = false;
    int m_codecMicKeyedSamples = 0;
    float m_codecMeterPeak = 0.0f;
    double m_codecMeterSumSq = 0.0;
    int m_codecMeterCount = 0;
    void ananApplyCodecSettings();
    // ANAN-100D port: the CW applet's keyer settings -> the FPGA keyer.
    void ananPushCwKeyer();
    // ANAN-100D port, unkey test (31.10): what the receivers do from the key
    // to 6 s after the unkey -- 25 ms rows of S-meter, panadapter, AGC gain,
    // DSP in/out; every backend command in that window; RX1 IQ -0.5..+3 s
    // into logs/rxcap-unkey-iq.wav.
    void ananUnkeyEvent(const QString& what);
    void ananUnkeyTraceSample();
    void ananUnkeyTraceDump();
    void ananUnkeyIqFeed(const std::vector<std::complex<float>>& iq);   // I/O thread
    // ANAN-100D port: PROC -> WDSP TXA compressor (radio mic).
    void pushTxCompressor();
    bool m_codecProcOn = false;
    int m_codecProcLevel = 0;
    bool m_txCompPushedOn = false;
    double m_txCompPushedGain = -1.0;
    // ANAN-100D port: TX capture (txcap-mic.wav, see Hl2Backend.cpp).
    void ananCaptureRaw(const std::vector<float>& raw);
    void ananCaptureMod(const std::vector<float>* mod, std::size_t n);
    void ananCaptureMark(bool radioPtt);
    void ananCaptureWrite();
    bool m_radioPttDown = false;
    int m_ananTxAttPushed = -1;     // AnanTxAttenuationDb last pushed
    std::vector<double> m_trRaw, m_trMod, m_trModPeak;
    double m_trRawSum = 0.0, m_trModSum = 0.0, m_trModPk = 0.0;
    int m_trRawN = 0, m_trModN = 0;
    std::deque<float> m_capPre;
    std::vector<float> m_capRaw;
    std::vector<float> m_capMod;
    bool m_capActive = false;
    bool m_capArmed = false;
    long long m_capPttAt = -1;
    long long m_capKeyAt = -1;
    // ANAN-100D port: PROC for the radio's microphone. The computer microphone
    // is compressed in AudioEngine's ClientComp before submitTxAudio(); the
    // codec microphone never passes there, so it gets its own compressor with
    // the same NOR / DX / DX+ presets (applyCodecProcessor()).
    std::unique_ptr<ClientComp> m_codecComp;
    void applyCodecProcessor(bool on, int level);
    // ANAN-100D port: MON. The receivers are muted while keyed, so the
    // operator's own (processed) microphone is played to the computer speaker
    // and the radio's codec at "MON" level while transmitting.
    bool m_monEnabled = false;
    int m_monLevel = 50;
    std::vector<float> m_monScratch;
    // MON: the demodulated transmit signal (24 kHz) to the PC and the codec.
    void ananMonitorTx(const std::vector<float>& mono);
    // Media context of the codec-microphone producer (MainWindow registers it
    // like the computer microphone): codec mic blocks go straight to the
    // modulator, timed by the radio's mic stream, no computer audio involved.
    TxCoordinator::Context m_codecMicContext;
public:
    void setCodecMicContext(const TxCoordinator::Context& context);
    // ANAN-100D port: the title bar's Codec volume, applied at once (it is
    // also re-read from AppSettings by the ANAN timer).
    void setCodecVolumePercent(int pct) { m_hw.speakerLevelPercent = std::clamp(pct, 0, 100); }
private:
    void fdvTick();
    int m_ananAdcPushed[2] = {-1, -1};
    // ANAN-100D port: Alex antennas last pushed (AppSettings "AnanRxAnt" /
    // "AnanTxAnt", 0..2 = ANT1..ANT3).
    // Build 248: "ANT1".."ANT3" -> 0..2, else -1; write an ANT panel setting.
    [[nodiscard]] static int ananAntennaIndex(const QString& token);
    void setAnanAntennaSetting(const QString& key, int index);
    int m_ananRxAntPushed = -1;
    int m_ananTxAntPushed = -1;
    QByteArray m_ananOcPushed;
    // ANAN port: the tuned frequency last sent to the transport per DDC, for
    // the User Out RX row (pushAnanTunedFrequencies()).
    std::vector<std::uint32_t> m_ananTunedPushed;
    void pushAnanTunedFrequencies();
    anan::AnanPureSignal* m_ps = nullptr;   // PureSignal (child QObject)
    // ANAN-100D port: MON demodulates the IQ sent to the DUC (I/O thread);
    // wanted = MON on, level > 0, receivers not already monitoring.
    anan::AnanTxMonitor m_txMon;
    std::vector<float> m_txMonOut;
    std::atomic<bool> m_monIoWanted{false};
    bool m_ananCfirPushed = false;
    std::size_t m_monDiagN = 0;   // MON samples played this over (GUI thread)
    float m_monDiagPk = 0.0f;   // CFIR on the TXA channel (Thetis, P2)
    // MON wanted / its gain (MON 1..100 -> -40..0 dB), for the transmit thread.
    void ananUpdateMonWanted();
    std::atomic<float> m_monGainIo{0.0f};
    std::atomic<bool> m_keyedIo{false};   // m_keyed for the transmit thread
    // ANAN-100D port (01.11): the transmit thread (AnanTxEngine + Hl2TxDsp),
    // time critical, clocked by the radio's microphone packets.
    QThread* m_txThread = nullptr;
    static constexpr int kTxPrimeRadioMic = 1536;   // TXA block 1024 + 2 DUC packets
    static constexpr int kTxPrimeComputer = 4096;   // PC-clocked audio: 21 ms
    anan::AnanTxEngine* m_txEngine = nullptr;
    // MON from the transmit thread into the codec mixer's FIFO.
    void ananMonToCodec(const std::vector<float>& mono);
    // txcap-mic.wav, from the engine's capture (GUI thread).
    void ananWriteTxCapture(std::vector<float> raw, std::vector<float> mod, long long pttAt, long long keyAt);
    int m_ananLnaPushed = -1;   // ANAN-100D port: AnanLna last pushed
    QByteArray m_ananCwKeyerPushed;   // ANAN-100D port: last keyer options sent
    // ANAN-100D port: TX receiver in CW -> the host's TX IQ is silence (the
    // FPGA keyer makes the carrier). Written on the GUI thread, read on I/O.
    std::atomic<bool> m_ananCwTxSilent{false};
    bool m_ananPanGuardPending{false};   // a frame fell inside the unkey guard
    std::array<std::deque<std::pair<qint64, double>>, 4> m_smHist;   // S-meter, before the key
    std::array<double, 4> m_smPreTx{-200.0, -200.0, -200.0, -200.0};
    QElapsedTimer m_smClock;
    // S-meter rise trace (operator 05.11, "the meter takes long to come up"):
    // when a reading jumps 15 dB over the last 0.6 s minimum, the next 1.2 s
    // of readings (time, WDSP value, published value) go to the log once.
    struct SmTrace {
        qint64 startMs = -1;
        qint64 lastEndMs = -100000;
        double base = 0.0;
        int readings = 0;
        QString line;
    };
    std::array<SmTrace, 4> m_smTrace;
    bool m_ananDuplex{false};
    // Build 194: the radio runs Protocol 1 firmware (rates stop at 384 ksps).
    // Set at connect from the discovery sweep and at linkUp from the transport.
    std::atomic<bool> m_ananP1{false};
    // PA Level Calibration (ANAN Hardware page): see applyDrive().
    int ananPaCalPercent() const;
public:
    // PA power law as its mid-scale point: the drive (% of the calibrated full
    // drive) at slider 50 %, for RF Power and Tune (build 193). Public so the
    // setup page shares the limits and the default.
    static constexpr double kAnanPaMidDriveDefault = 46.7;   // gamma 0.91
    static constexpr double kAnanPaMidDriveMin = 35.0;
    static constexpr double kAnanPaMidDriveMax = 80.0;
    static double ananPaMidDrivePercent();
private:
    double ananFwdDisplayWatts(double watts) const;   // Power Meter Calibration
    int m_ananLastDrivePercent{-1};
    QString m_ananPaCalApplied;   // DUP (Mic applet): pans show the receivers while keyed
    struct AnanUkRow {
        float sMeter = -999.0f, panMax = -999.0f, panFloor = -999.0f;
        float agc = -999.0f, inPk = 0.0f, outPk = 0.0f;
    };
    QTimer* m_ukTimer{nullptr};
    QElapsedTimer m_ukClock;                 // from the unkey
    bool m_ukKeyed{false}, m_ukPost{false};
    std::deque<AnanUkRow> m_ukRows;
    AnanUkRow m_ukCur;
    int m_ukUnkeyRow{-1};
    QStringList m_ukEvents;
    std::atomic<bool> m_ukIqArm{false};
    std::atomic<int> m_ukIqRate{192000};
    bool m_ukIqActive{false};
    std::vector<std::complex<float>> m_ukIqRing, m_ukIq;
    std::size_t m_ukIqPos{0}, m_ukIqMark{0};
    Hl2TxDsp* m_txDsp = nullptr;
    bool m_connected = false;

    // Stream-free telemetry: reads the radio over the alternate control port, on its
    // own socket, when the EP6 path cannot (another client holds it, our stream
    // stalled, or not connected). BORROWED: the service's lifetime is RadioModel's,
    // because it must answer when this backend does not exist.
    Hl2TelemetryService* m_telemetryService = nullptr;
    // What the picker last said about this radio; meaningful only while
    // disconnected (idle radio vs someone else's session).
    bool m_pollTargetHeldByOther = false;
    // When the mirrored EP6 counter last advanced (not a tick-to-tick diff, which
    // aliases two 1 Hz clocks into a false stall; hl2_link_state_alias_test).
    // Restarted from the mirror, so it is independent of the tick rate. Rule and
    // threshold live in Hl2TelemetryCadence.h.
    QElapsedTimer m_rxAdvanceClock;
    // ANAN-100D port: receive-audio diagnostic, logged every 5 s.
    QElapsedTimer m_audioDiagClock;
    quint64 m_audioDiagCalls = 0;
    quint64 m_audioDiagDropped = 0;
    float m_audioDiagPeak = 0.0f;
    quint64 m_rxPacketsAtLastAdvance = 0;
    // Independent of the link-stats cadence on purpose; see the timer's construction.
    static constexpr int kTelemetryPollStateIntervalMs = 1000;

    // Manual frequency calibration (Hl2FreqCal). Every frequency sent to the radio
    // goes through these two: the correction is one scalar and NCO registers cannot
    // be read back, so a bypassing write site mistunes silently. Receiver::sliceFreqHz
    // and ncoHz stay in true RF; only wire and DSP values are scaled.

    // NCO register value (0x01 TX, 0x02+ RX) for a true-RF frequency.
    [[nodiscard]] std::uint32_t ncoCommandHz(double trueHz) const noexcept;
    [[nodiscard]] double xvtrOffsetHz(double rfHz) const noexcept;   // build 317
    void publishTransverters();                                       // build 317
    // DSP shift that puts sliceTrueHz at baseband, given the true-RF NCO. Rounds
    // the NCO command first and computes the shift against that rounded value,
    // so the register's 1 Hz quantisation cancels instead of leaking into audio.
    [[nodiscard]] double dspShiftHz(double sliceTrueHz, double ncoTrueHz) const noexcept;
    // Re-send every NCO (RX banks + TX) from unchanged true-RF state, so a mid-session
    // calibration change is heard immediately.
    void repushAllFrequencies();
    // Clamp, persist, adopt, re-push — the single path for a calibration change
    // whoever asked for it (setup dialog, automation bridge, connect).
    void applyFreqCalPpb(int ppb, bool persist);
    // Enforce "CL1 on means zero manual ppb" — §4 of
    // docs/architecture/hl2-frequency-calibration.md. Returns true when it had to
    // change something, so a live caller knows to re-push frequencies. Called
    // from applyHardwareOptions() AND from connectRadio(), because the two
    // documents are persisted separately and can disagree on disk.
    bool normalizeCl1Calibration(const char* why);
    // Read the actual pending transport snapshot without starting a socket.
    std::optional<std::pair<bool, std::uint32_t>> pendingCl1ReferenceForTest() const;

    // This radio's calibration and the derived scale. 0 / 1.0 is uncalibrated.
    int m_freqCalPpb = 0;
    double m_freqCalScale = 1.0;
    // The connected radio's MAC, so calibration is stored per crystal. Empty until
    // connectRadio().
    QString m_radioSerial;

    // Which HL2 variant this is, loaded per radio at connect: a bare HL2, an HL2+ and
    // a SquareSDR 2 look identical on the wire and the dither bit means different
    // things on each. See Hl2HardwareOptions.
    Hl2HardwareOptions m_hw;
    // Adopt a new set: persist (or not), push every field that changed to the
    // wire, and re-evaluate the band filter. The single path for a hardware
    // change whoever asked for it (setup dialog, automation bridge, connect).
    void applyHardwareOptions(const Hl2HardwareOptions& next, bool persist);
    // Raise or clear the gateware ATU request, honouring m_hw.atuGateware.
    // Called from the same two places that start and end a TUNE.
    void applyAtuTuneRequest(bool tuning);
    // Hand the mixed speaker feed to the radio's own codec, resampled to the
    // EP2 rate. No-op unless this radio has a codec.
    // fromMonitor: the MON feed (GUI thread, while keyed). The receive mix
    // goes through the I/O-thread codec mixer below instead whenever that is
    // in charge (m_codecIoMixActive).
    void forwardSpeakerAudioToCodec(const std::vector<float>& mixed, bool fromMonitor = false);

    // ANAN-100D port: the receive audio for the radio's codec is mixed and
    // sent ON THE I/O THREAD, straight from Hl2RxDsp::audioReady, never
    // through the GUI thread: a click on the window's title bar (Windows
    // holds the GUI thread ~0.5 s in DragDetect / the move loop) or a heavy
    // repaint used to starve the codec FIFO in the radio -- gaps and rustle.
    // The GUI thread only publishes the mix parameters (ananPushCodecMix(),
    // ANAN timer and every mute edge). Not used while FDV runs (decoded
    // speech is made on the GUI thread) or while the receivers are muted
    // for TX (MON then feeds the codec from the GUI thread).
    static constexpr int kCodecMixMax = 8;
    struct CodecMixParams {
        bool active = false;
        float gain = 0.0f;           // linear, the Codec volume law included
        bool law = false;            // ANAN soft knee
        // The codec plays AudioEngine's post-AetherRX output (AnanCodecFeed)
        // instead of this mix: AppSettings "AnanCodecAetherRx", default off
        // (01.11: that audio crosses the GUI thread -- rustle and clicks on
        // the radio speaker whenever the window is busy).
        bool aetherRx = false;
        std::array<const void*, kCodecMixMax> dsp{};   // receiver chain per ddc
        std::array<float, kCodecMixMax> lw{};
        std::array<float, kCodecMixMax> rw{};
    };
    std::mutex m_codecMixMutex;
    CodecMixParams m_codecMixParams;   // guarded by m_codecMixMutex
    bool m_codecIoMixActive = false;   // GUI thread's copy of .active
    void ananPushCodecMix();
    // I/O thread only:
    void ananIoCodecAudio(const void* dsp, const std::vector<float>& pcm);
    std::array<std::vector<float>, kCodecMixMax> m_ioMixQ;
    std::vector<float> m_ioMixOut;
    float m_ioLastL = 0.0f, m_ioLastR = 0.0f;
    bool m_ioHavePrev = false;
    bool m_ioWasActive = false;
    // ANAN-100D port: receiver audio blanked on the radio speaker after the
    // radio's own PTT press (samples, 24 kHz stereo); I/O thread only.
    std::size_t m_ioRxBlank = 0;
    std::vector<float> m_ioMonOut;
    std::vector<float> m_ioProcOut;   // post-AetherRX feed, 48 kHz (I/O thread)
    float m_ioMonPrev = 0.0f;
    std::deque<std::int16_t> m_ioSpkHold;   // radio-speaker hold, 80 ms (I/O thread)
    // Speaker capture (txcap-spk.wav), I/O thread only.
    void spkCapture(float gain);
    std::deque<float> m_spkCapPre;
    std::vector<float> m_spkCapPreBuf, m_spkCapBuf, m_spkCap;
    std::atomic<bool> m_spkCapArmed{false};
    bool m_spkCapActive = false;
    std::size_t m_spkCapPressAt = 0;
    // Carry for the 24 -> 48 kHz doubling in forwardSpeakerAudioToCodec(): the last
    // stereo frame of the previous block, so the boundary sample is interpolated.
    float m_codecLastL = 0.0f;
    float m_codecLastR = 0.0f;
    bool m_codecHavePrev = false;

    // One per running DDC, holding only per-receiver state. Anything the hardware
    // shares (sample rate, LNA gain, filter board) stays in the flat members below.
    struct Receiver {
        // Owns its demod + spectrum chain; created on connect, destroyed on disconnect,
        // lives on the I/O thread.
        Hl2RxDsp* dsp = nullptr;
        Hl2RxDsp* binauralDsp = nullptr;   // build 307: the DSP BIN was last sent to
        bool binauralSent = false;
        // ANAN port: the DRM decoder on this receiver (DrmApplet), or null.
        // Shared with the dsp, which feeds it on the I/O thread.
        std::shared_ptr<aether_drm::DrmEngine> drm;
        uint64_t drmSlideSeen = 0;
        int drmJournalinePage = 0;

        // Authoritative RX state (nothing on the wire echoes it). The slice frequency
        // and the NCO are independent: the slice is tuned inside the passband by a WDSP
        // shift, and the NCO moves only when the target would leave the window.
        double sliceFreqHz = 10'000'000.0;   // slice
        double ncoHz       = 10'000'000.0;   // DDC / pan centre
        // True while the NCO sits off the dial only because RIT pushed the
        // receive frequency out of the window (the dial alone fitted), so
        // clearing RIT re-centres it on the dial. A pan drag clears it.
        bool ncoMovedForRit = false;
        // ANAN fork build 326: CTUN OFF ("anan.ctun", only while a TCI client
        // takes this receiver's I/Q with "Center Lock when I/Q" ticked): every
        // tune puts the NCO on the passband's middle, the view on the NCO --
        // the filter stays in the centre and the hardware receiver retunes,
        // as Thetis with CTUN off. See ctunCenter().
        bool ctunOff = false;
        // ANAN-100D port: display zoom -- the span the pan shows, centred on
        // the NCO, 0 = the whole DDC rate. The DDC rate itself is chosen only
        // by the rate buttons ("anan.rate"); a pan bandwidth request zooms.
        double zoomViewHz = 0.0;
        // ANAN port (build 209): the view's centre may sit off the NCO by
        // viewOffsetHz while the view still lies inside the captured band,
        // so a pan drag moves the picture without retuning the DDC. Valid
        // only while ncoHz == viewForNcoHz: any other NCO move re-centres the
        // view on it. See panCenterHz().
        double viewOffsetHz = 0.0;
        double viewForNcoHz = 0.0;

        // RIT / XIT, Hz from the dial. Nothing on the wire echoes them, so
        // emitSliceState() publishes these back as the readout.
        bool ritOn = false;
        int  ritHz = 0;
        bool xitOn = false;
        int  xitHz = 0;

        QString mode = QStringLiteral("USB");
        // Overwritten from defaultPassbandForMode(mode) on the first linkUp of each
        // connect (#4484). These initial values match no mode's passband.
        int filterLowHz = 150;
        int filterHighHz = 3000;
        // Authoritative AGC state, mirroring the DSP defaults in Hl2RxDsp::Config
        // so the first sliceChanged reports what WDSP was actually opened with.
        QString agcMode = QStringLiteral("med");
        int agcThresholdDb = 65;
        // The AGC mode held while DIGU/DIGL run AGC off (followModeAgc());
        // restored on leaving them unless the operator set AGC since. Empty
        // when nothing is waiting. A receiver seeded from another copies it.
        QString agcModeBeforeDigital;

        // Authoritative noise-blanker state: nothing echoes it, and a rebuilt receiver
        // must be told again. Defaults mirror SliceModel's (off, level 50, zero fill).
        AetherSDR::NoiseBlankerKind nbKind = AetherSDR::NoiseBlankerKind::Off;
        int  nbLevel = 50;
        AetherSDR::NoiseBlankerFill nbFill = AetherSDR::kDefaultNoiseBlankerFill;
        // A restored blanker still to be published once (emitSliceState).
        bool nbEchoPending = false;

        // APF request and AGC-off level, held like the blanker: nothing echoes
        // them and a fresh chain must be told again. Literal defaults match
        // Hl2RxDsp::kDefaultApfLevel / kDefaultAgcOffLevel (the test pins it).
        bool apfOn = false;
        int  apfLevel = 50;
        // ANF request, held the same way. Defaults match SliceModel's (off,
        // level 50) and Hl2RxDsp::kDefaultAnfLevel.
        bool anfOn = false;
        int  anfLevel = 50;
        int  agcOffLevel = 10;

        // The operator's panadapter averaging, held for the same reason: a
        // chain built on reconnect or for an added pan starts at none.
        // panAverage is the operator's 0..100; see averageTimeMsForStep().
        int  panAverage = 0;
        bool panWeightedAverage = false;

        // Authoritative squelch state, for the blanker's reasons: nothing on
        // this radio echoes it and every rebuilt chain opens with it off.
        // Defaults mirror SliceModel's (off, level 20). The mode decides which
        // WDSP stage carries it — WdspChannel::setSquelch() — not this struct.
        bool squelchOn = false;
        int  squelchLevel = 20;

        // Host-side per-slice audio: the HL2 mixes nothing. gain is a linear multiplier
        // from the operator's 0..100; pan is 0=left .. 50=centre .. 100=right (SliceModel).
        bool audioMuted = false;
        float audioGain = 0.01f;   // build 268: 1 % until the operator sets it
        int audioPanPercent = 50;

        // The IQ rate this chain was built for, or 0. Differs from m_sampleRateHz (the
        // rate being attempted) for the length of a rebuild; finishRateChange()
        // reconciles them on success.
        int configuredRateHz = 0;

        // A pending initial build must not be synchronously reconciled by a rate crossing.
        bool dspBuildInFlight = false;
        // ANAN-100D port: the AGC ceiling from the noise floor (NaN = the
        // manual AGC-T law), see Hl2Backend::ananAutoAgcStep().
        double autoAgcTopDb = std::numeric_limits<double>::quiet_NaN();

        // UI numbers are reused. Only the generation stamped for this DSP can complete it.
        quint64 dspBuildGeneration = 0;

        // Per-receiver S-meter ballistics, so one receiver's signal never moves another's
        // needle.

        SMeterSmoother sMeter;
    };

    // CW BFO. Receiver filter cuts and sliceFreqHz are carrier-relative (the marker
    // is where the CW signal is); the demodulator needs the audio domain. These
    // helpers are the only translation, so every DSP push goes through them; for
    // non-CW modes the BFO is zero and they are the identity.

    // Where a signal on the marker comes out, in Hz of audio: +pitch for CWU,
    // -pitch for CWL, 0 otherwise.
    [[nodiscard]] double cwBfoHz(const QString& mode) const noexcept;
    // The receiver's passband in the demodulator's audio domain: carrier-relative
    // cuts slid up (CWU) or down (CWL) onto the pitch.
    [[nodiscard]] std::pair<double, double> dspFilterHz(const Receiver& r) const noexcept;
    // Moves r's AGC with its mode: off on entering DIGU/DIGL from
    // previousMode, the held AGC on leaving them. Every site that writes
    // Receiver::mode calls it. True when agcMode changed.
    static bool followModeAgc(Receiver& r, const QString& previousMode);
    // WDSP shift: the slice's offset from the NCO less the BFO, so the marker lands
    // on the pitch.
    [[nodiscard]] double rxShiftHz(const Receiver& r) const noexcept;
    // Where a receiver actually listens: its dial plus its own RIT. Feeds the
    // NCO window and the shift; sliceFreqHz stays the dial.
    [[nodiscard]] double rxTunedHz(const Receiver& r) const noexcept;
    // Re-run one receiver's tune after its RIT changed.
    void retuneReceiver(int ddc);
    // The offset clamped to kRitXitMaxHz, logging when the clamp bites.
    [[nodiscard]] static int clampRitXit(const char* what, int hz);
    // qCInfo naming the receiver and slice an RIT/XIT change landed on.
    void logRitXit(const char* what, int ddc, bool on, int hz) const;
    // SmartCatProtocol's kRitMaxHz. Only SmartCAT clamps to it: SliceModel::
    // setRit() and the VFO's RIT/XIT steppers do not, so an offset past it can
    // reach the setters, and they log when this clamp bites.
    static constexpr int kRitXitMaxHz = 9999;

    // The operator's CW pitch via setCwPitch(). Defaults to TransmitModel's 600.
    int m_cwPitchHz = 600;
    // GUI THREAD ONLY. The sample path reads m_ioDsps instead; see publishIoDsps().
    std::vector<Receiver> m_rx;
    // Whether the last buildReceivers() carried previous receiver state (an
    // auto-reconnect) rather than building fresh. connectRadio()'s AGC seeding needs
    // the difference.
    bool m_rxCarriedState = false;

    // Manual notches, GUI thread only. Above the seam a notch has a stable id; in
    // WDSP it has a positional index that shifts on deletion. This vector keeps
    // WDSP's order, so the index is the position here. Notches are radio-wide: every
    // receiver gets the same set, and a later receiver is seeded (seedNotches).
    struct NotchRecord {
        int id = 0;
        double centerHz = 0.0;
        double widthHz = 0.0;
        bool active = true;
    };
    std::vector<NotchRecord> m_notches;
    // Never reused, so a stale UI reference cannot address a different notch.
    int m_nextNotchId = 1;
    bool m_notchesEnabled = true;

    // Index of `notchId` in m_notches — which IS its WDSP handle — or -1.
    [[nodiscard]] int notchIndexFor(int notchId) const;

    // Session memory beyond the flat fields (Receivers and Notches domains),
    // validated by applyRestoredState() and replayed once per connect by
    // replayRestoredSession(). Index 0 is receiver A; ids in the notches are unused.
    std::vector<Receiver> m_restoredReceivers;
    std::vector<NotchRecord> m_restoredNotches;
    int m_restoredTxReceiver = 0;   // the list index that held transmit
    // Remembered receivers the replay could not reopen (ceiling, failed open).
    // Captured after the live ones so a short connect does not erase them;
    // dropped once the operator adds or closes a receiver.
    std::vector<Receiver> m_replayUnreopened;
    bool m_sessionReplayPending = false;
    void replayRestoredSession();
    // The open receivers' own setpoints from the document, before their DSPs
    // open: receiver A's extras, and the whole entry for any a numRx param opened.
    void seedOpenReceiversFromMemory();
    void seedReceiverExtras(Receiver& r, const Receiver& memory) const;
    [[nodiscard]] static QJsonObject receiverMemoryJson(const Receiver& r);
    // Nullopt for an entry with no usable frequency or mode; other bad fields drop alone.
    [[nodiscard]] std::optional<Receiver> receiverFromMemory(const QJsonObject& o) const;
    // createPanadapter() with the new receiver's state given rather than copied.
    bool addReceiver(const Receiver& seed);
    // Push the whole notch set + tune frequency into a receiver added later.
    void seedNotches(const Receiver& r);
    // Re-point a receiver's notch axis at its current NCO; call wherever ncoHz moves.
    void pushNotchTune(const Receiver& r);
    // Push this receiver's NB state into its chain. Needed wherever a chain is
    // (re)built: a fresh Hl2RxDsp opens with the blanker off.
    void pushNoiseBlanker(const Receiver& r);
    // Same, for the squelch, and needed at the same places for the same reason.
    void pushSquelch(const Receiver& r);
    // Same, for the panadapter averaging (Receiver::panAverage / weighted).
    void pushPanAveraging(const Receiver& r);
    // This receiver's NCO just moved: the averaged bins describe the old
    // frequency axis. Called beside pushNotchTune() at the two retune sites.
    void dropPanAverage(const Receiver& r);
    // ANAN-100D port: display zoom helpers (see Receiver::zoomViewHz).
    [[nodiscard]] double panViewHz(const Receiver& r) const;
    // ANAN port (build 209): the view centre (what the panadapter shows) and
    // its offset from the NCO; see Receiver::viewOffsetHz.
    [[nodiscard]] static double viewOffsetHz(const Receiver& r) noexcept
    {
        return (r.viewOffsetHz != 0.0 && r.viewForNcoHz == r.ncoHz) ? r.viewOffsetHz : 0.0;
    }
    [[nodiscard]] static double panCenterHz(const Receiver& r) noexcept
    {
        return r.ncoHz + viewOffsetHz(r);
    }
    // Retune the DDC of `r` (a pan drag that left the captured band, or the
    // settle after one), keeping the view centre where it is.
    void moveNcoForView(int ddc, Receiver& r, double newNcoHz, double viewCenterHz);
    // Build 326: CTUN OFF -- the NCO for the passband's middle (see Receiver::ctunOff).
    [[nodiscard]] double ctunNcoHz(const Receiver& r) const noexcept;
    // Re-centre a CTUN-off receiver after a filter / mode change; one pan state.
    void ctunCenter(int ddc);
    void settlePanViews();
    void applyZoomFftSize(Receiver& r);
    // ANAN-100D port: every receiver's FFT history (average and partial
    // window), whatever its averaging setting -- around an over it holds the
    // transmitter, not the band.
    void dropAllPanHistory();
    // Same, for the APF (centred on the current CW pitch) and the AGC-off level.
    void pushApf(const Receiver& r);
    void pushAnf(const Receiver& r);
    void setSliceAnf(int sliceId, bool on, int level);
    void pushAgcOffLevel(const Receiver& r);

    // I/O THREAD ONLY: the chains the EP6 fan-out feeds, indexed by DDC. Never m_rx,
    // whose push_back/erase can move storage under the fan-out. Rebuilt by
    // publishIoDsps() when the receiver set changes, never per packet.
    std::vector<Hl2RxDsp*> m_ioDsps;
    // Build 222: TCI I/Q taps, one per receiver (DDC index). I/O THREAD ONLY;
    // started / stopped through invokeExtension("hl2", "tci.iq", ...).
    struct TciIqTap {
        int channel = 0;            // 0 = off; the TCI IQ channel (trx + 1)
        int outRate = 0;
        anan::P1PsResampler rs;
        std::vector<double> out;
        std::vector<std::complex<float>> upright;   // build 266: conjugated, view-centred
        QByteArray pending;         // float32 I/Q waiting for a full frame
        double phase = 0.0;         // the view-offset mixer
    };
    static constexpr std::size_t kTciIqTaps = 8;
    std::array<TciIqTap, kTciIqTaps> m_tciTaps;

    // ANAN port: DRM ("drm" extension verb; Receiver::drm).
    QTimer* m_drmTimer = nullptr;
    void drmCommand(const QVariantMap& m);
    void attachDrm(Receiver& r);
    void publishDrm();
    // Written on the GUI thread (emitPanState), read on the I/O thread.
    std::array<std::atomic<double>, kTciIqTaps> m_tciIqOffsetHz{};
    std::atomic<int> m_tciIqInRateHz{48000};
    void tciIqTap(std::size_t ddc, const std::vector<std::complex<float>>& iq);
    // Build 223 (operator, 09.11): transmit only inside the amateur bands of
    // the band plan (sent by MainWindow, "tx.bands"); until it arrives, a
    // built-in table of the amateur bands of all three IARU regions.
    std::vector<std::pair<double, double>> m_txBandsHz;   // [low, high] Hz, GUI thread
    QString m_txBandPlanName;
    double m_txTrueHz = 0.0;            // the TX frequency written last (setTxFrequency)
    double m_txWantedHz = 0.0;          // build 224: the TX frequency asked for (dial + XIT)
    double m_txWantedDialHz = 0.0;      // build 224: its dial, written on unkey if held
    [[nodiscard]] bool hzInAmateurBand(double lo, double hi) const;
    void updateCwBandInhibit();
    [[nodiscard]] double ananMinViewHz() const;   // build 226
    // Build 243: the widest DDC rate the running receivers can share -- the
    // pan's upper bandwidth limit (the [+] rate button), not the current rate.
    [[nodiscard]] int widestRateForReceiversHz() const;
    [[nodiscard]] static int spectrumFftSizeFor(double zoomViewHz, int rateHz);   // build 230
    double m_fwdMeterW = 0.0;   // build 231/232: the power gauge (gliding to the 250 ms peak)
    std::deque<std::pair<qint64, double>> m_fwdPeakHist;   // build 232: (ms, W) of the last 250 ms
    qint64 m_txBandWarnMs = 0;          // rate limit of the operator warning
    [[nodiscard]] std::pair<double, double> txSpanHz(double hz) const;
    [[nodiscard]] bool txInAmateurBand(double hz) const;
public:
    // Build 264: the band gate applyKeying() enforces, asked in advance.
    [[nodiscard]] QString transmitRefusalMessage() const override;
private:
    void warnTxOutOfBand(double hz);

    // The four index spaces, never derived from one another (Hl2Receivers.h). GUI
    // thread only; the signal handlers that use it are queued onto this thread.
    Hl2ReceiverMap m_ids;

    // The receiver whose slice is the TX slice: one transmitter, chosen among the
    // receivers.
    int m_txDdc = 0;

    // The AGC pair the operator last set, on any receiver: what
    // currentOperatingState() persists, since the restore writes every receiver.
    // Empty mode = untouched this session.
    QString m_agcMode;
    int     m_agcThresholdDb = 0;
    // The remembered AGC-off level per receiver index; -1 = none. It seeds a
    // receiver at connect and when a panadapter opens, and it keeps the level
    // of a receiver that is not open. m_agcOffLevelsLive: the open receivers
    // hold the current levels (set by the seed and by the operator).
    QList<int> m_agcOffLevels;
    bool       m_agcOffLevelsLive = false;
    int  rememberedAgcOffLevel(int receiverIndex) const;
    void rememberAgcOffLevel(int receiverIndex, int level);
    // The serial seedReceiverAgc() last ran for. A different radio is seeded; the
    // same radio reconnecting is not (buildReceivers() kept its live AGC). Empty
    // until the first connect.
    QString m_agcSeededSerial;

    // The receiver the operator is working on, separate from m_txDdc. The HL2 has no
    // selected-receiver notion and echoes nothing, so this is the client's single
    // answer to "the active slice" (a Flex arbitrates it with `slice set N active=1`).
    int m_activeDdc = 0;

    [[nodiscard]] Receiver* rx(int ddc);
    // The AGC ceiling a receiver runs with: the noise-floor law when it is
    // active (ANAN Auto AGC), else the AGC-T law of m_dbRef.
    [[nodiscard]] double agcCeilingFor(const Receiver& r) const;
    [[nodiscard]] const Receiver* rx(int ddc) const;
    // Resolve a seam slice/pan id to a DDC index, or -1. Callers must check;
    // defaulting to receiver 0 would move the wrong panadapter.
    [[nodiscard]] int ddcForSlice(int sliceId) const;
    [[nodiscard]] int ddcForPan(const QString& panId) const;

    // Create/destroy the receiver set. buildReceivers() tears the previous set down
    // first so a reconnect at a different count leaves no orphaned WDSP channels.
    void buildReceivers(int count);
    // Create and wire one receiver's DSP chain at `ddc`. Shared by buildReceivers()
    // and createPanadapter() so both paths wire identically.
    bool openReceiverDsp(int ddc, std::string* error);

    // Snapshot on GUI, mark/swap on I/O, open on the existing DSP build thread.
    // Carry UI number (DDC indices move) plus generation (UI numbers are reused).
    // Derive config here for both initial setup and rate catch-up.
    void startReceiverDspBuild(int uiNumber);
    void finishReceiverDspBuild(int uiNumber, quint64 generation, bool ok,
                                int channelId, int builtRateHz,
                                const std::string& error);
    // Backend-lifetime counter: never reset on reconnect, which also reuses UI ids.
    // releaseReceiverDsps() clears each retiring receiver's stamp before copying it.
    quint64 m_nextDspBuildGeneration = 0;
    // How many receivers this radio may run right now: the board's reported
    // count, capped by the link budget at the current sample rate.
    [[nodiscard]] int receiverCeiling() const;
    // Announce a capability revision only if receiverCeiling() moved (#5594). The
    // ceiling (maxSlices/maxPanadapters) falls on zoom-out: at 384 kHz only 3
    // receivers fit 100BASE-T. Guarded so a zoom drag does not storm announcements.
    void announceReceiverCeilingRevision();
    // Re-evaluate the shared band filter and publish the resulting WIDE state.
    void publishWideState();
    // Destroy the DSP chains but KEEP each receiver's operator-set state. The
    // two have different lifetimes — see buildReceivers().
    void releaseReceiverDsps();
    void tearDownReceivers();

    // Hand the I/O thread a fresh copy of the chains. Call after any change to the
    // receiver set. A copy, not locked access: m_rx is GUI-only and m_ioDsps is
    // I/O-only, so nothing is shared, nothing can deadlock against the
    // BlockingQueuedConnection in createPanadapter(), and TSan has nothing to flag.
    // BLOCKS until the I/O thread has taken the list, because callers destroy chains
    // from the old one on return.
    void publishIoDsps();

    // Withdraw every chain from the sample path and block until taken. Use whenever
    // the receiver set changes shape: a shortened list while the wire still sends
    // the old slot count would feed one receiver another's IQ. Empty is correct
    // whatever the wire sends next.
    void withdrawIoDsps();

    // Shared tail. Takes the list by value so it can never alias m_rx.
    void publishIoDspList(std::vector<Hl2RxDsp*> next);

    // Sum one receiver's audio into the host mix (the HL2 has no on-radio mixer).
    void mixReceiverAudio(int ddc, const std::vector<float>& pcm);

    // Receive-audio hold. applyRxAudioMute() is the only writer of m_rxAudioMuted
    // and the path for every mute, so the mixer gate and demod mute stay in step.
    // pushInitialState()'s link-up loop also queues setAudioMuted per new receiver,
    // and agrees because applyRxAudioMute() runs later in that function.
    void applyRxAudioMute(bool muted);
    // Release the hold, but not before the radio has had time to drop out of
    // transmit. Immediate when the hold is zero. Cancelled by any re-key.
    void releaseRxAudioMuteAfterHold();

    // Per-slice meter name ("SLC:LEVEL" for the first receiver). The suffix on the
    // rest is read back by MeterModel::splitMeterId as the sourceIndex, the only way
    // an index crosses meterUpdate's two-argument signature.
    static QString sliceMeterName(int uiNumber);

    // Meter index for a receiver's "SLC"/"LEVEL". Receiver 0 keeps index 1 (as
    // defineMeters() declares); the rest use a band clear of the fixed 1..9.
    static int sliceLevelMeterIndex(int uiNumber);

    // Mixing scratch: per-receiver samples awaiting peers, and a reused summing
    // buffer (~47 calls/s per receiver).
    std::vector<std::deque<float>> m_mixPending;
    std::vector<float> m_mixAccum;
    // How far ahead the other receivers may get before a starved one is mixed as
    // silence: 2048 samples of interleaved L,R at 24 kHz = 1024 frames, ~43 ms.
    static constexpr std::size_t kMixStarvationSamples = 2048;
    // The DDC rate, which IS the panadapter span (emitPanState). Starts at the
    // narrowest; connectRadio() restores the operator's last span (Hl2Settings).
    // The widest costs ~8x (25.2 vs 3.1 Mbps, 3048 vs 381 packets/s), so it is opted
    // into, never imposed. Radio-wide (0x00[25:24]); bounds the receiver count via
    // kEp6LinkBudgetFraction / maxReceiversAtRate() (4 RX at 384 kHz is ~89 Mbit/s
    // on 100BASE-T, refused).
    int m_sampleRateHz = 48000;
    // Receivers to run: operator request (Hl2Settings), clamped by discovery byte
    // 0x13 and the link budget. Never hardcoded: skimmer gateware reports 9..12,
    // hl2b5up_main reports 4.
    int m_requestedNumRx = 1;
    // The board's count (discovery byte 0x13), or 0 from a short reply. Mirrored
    // here because createPanadapter() answers on this thread.
    int m_boardMaxRx = 0;
    // Guards capabilitiesChanged() against a zoom sweep (#5594); the decision is in
    // Hl2CapabilityAnnouncer.h.
    ReceiverCeilingAnnouncer m_ceilingAnnouncer;
    // Assumed when the board never reported its count. hl2b5up_main is built with
    // NR=4 (variants/hl2b5up_main/hermeslite.v). Erring high would stream all-zero
    // slots that look like a dead antenna.
    static constexpr int kAssumedBoardMaxRx = 4;
    // Zoom-sweep throttle for setPanBandwidth (#4470). A span change is a blocking
    // WDSP rebuild plus a settings write, and a drag crosses every intermediate rate.
    // Leading edge applies at once; later requests inside the cooldown coalesce and
    // the last applies on expiry.
    static constexpr int kBandwidthThrottleMs = 150;
    QTimer* m_bandwidthThrottle = nullptr;
    QTimer* m_zoomFftTimer = nullptr;   // ANAN-100D port: FFT size after a zoom settles
    QTimer* m_panSettleTimer = nullptr; // build 209: view back inside the clean band after a drag
    double m_pendingBandwidthHz = 0.0;   // 0 = nothing coalesced

    // The IO board's README asks for at most one frequency update per 0.5 s, only
    // on change. Leading edge queues at once; requests inside the cooldown coalesce
    // and the LAST applies on expiry, never dropped, so the amplifier is not left on
    // the old band. Delivery and relay settling are unacknowledged: this is not an
    // amplifier-ready interlock.
    static constexpr int kIoBoardThrottleMs = 500;
    QTimer* m_ioBoardThrottle = nullptr;
    hl2::IoBoardSchedule m_ioBoardSchedule;
    // The band the IO board was last told (bandKeyForHz()). Empty = nothing told this
    // session, so the first push after connect takes the leading edge. Kept separate
    // from m_currentBandKey, which follows tuning for LNA/drive recall.
    QString m_ioBoardBandKey;

    // Whether this connect already derived the passband from the mode (#4484).
    // pushInitialState() runs on every linkUp, including MetisClient's re-emit after
    // an EP6 silence timeout, which must not reset the operator's filter. Cleared in
    // connectRadio(). Radio-wide: it gates the derivation pass over all receivers.
    bool m_passbandDerivedThisConnect = false;

    // Shared hardware: the HL2 has ONE AD9866, so these are radio-wide.
    // m_lnaGainDb is the operator's BASELINE, not the register (Hl2GainSplit.h): the
    // AD9866 gets this minus m_lnaAutoOffsetDb. Written only by setPanRfGain, the
    // band-memory restore and the connect seed, never by an automatic control.
    int m_lnaGainDb = hl2::kLnaDefaultGainDb;
    // Automatic attenuation below the baseline, dB, never negative. Session state:
    // absent from currentOperatingState() and m_lnaDbByBand so it cannot persist as
    // a chosen gain. Reset to 0 by resetPersistedState().
    int m_lnaAutoOffsetDb = 0;
    // Automatic control (Hl2AutoGainPolicy.h). This flag is "the loop is RUNNING";
    // m_autoRfGainWanted is the operator's wish, OFF by default (no `autoEnabled` key
    // reads false). The shipped +20 dB LNA default is armable; default-on is a
    // separate decision (#5535). No timer: the policy steps on the telemetry publish,
    // so when the stream stops the offset holds; silence is not a clean converter.
    bool m_autoRfGainEnabled = false;
    // The operator's preference, persisted in currentOperatingState()'s rfGain
    // object (family state, per docs/HERMES.md). Stays true when arming is declined,
    // so the next connect from a trusted baseline arms without asking again.
    bool m_autoRfGainWanted = false;
    // Why the last arm attempt was declined; empty otherwise, cleared on success.
    QString m_autoRfGainRefusal;
    AetherSDR::hl2::AutoGainState m_autoGainState;
    AetherSDR::hl2::AutoGainConfig m_autoGainConfig;

    // The loop's visible indicator (#5535 requires it; FrontEndOverload.h).
    // Published only on change, since the inputs move at 10 Hz.
    void publishFrontEndOverload();
    AetherSDR::FrontEndOverload m_lastFrontEndOverload;
    // The configuration's name, for the health row and reset path (the config struct
    // is just numbers). No initialiser: installDefaultAutoGainLaw() sets name and
    // config together, so there is one copy of the default.
    QString m_autoGainMode;
    // Whether the law and the floor are the operator's choice, so only a choice is
    // persisted: a stored default would freeze it against a later build's default.
    bool m_autoGainLawChosen = false;
    bool m_autoGainFloorChosen = false;
    // A law is a name and its numbers, kept as one value so neither is installed
    // without the other.
    struct AutoGainLaw {
        QString name;
        AetherSDR::hl2::AutoGainConfig config;
    };
    // The law a backend starts with: the one source for the constructor,
    // applyRestoredState() and the name "default".
    [[nodiscard]] static AutoGainLaw defaultAutoGainLaw();
    void installDefaultAutoGainLaw();
    // Band and baseline as the loop last saw them, so changes reach the policy as
    // inputs.
    QString m_autoGainBandKey;
    int m_autoGainBaselineDb = 0;
    int m_autoGainSampleRateHz = 0;
    AetherSDR::hl2::AutoGainReason m_autoGainReason =
        AetherSDR::hl2::AutoGainReason::Disarmed;
    // Restarted on every unkey. The policy's post-unkey hold-off is measured
    // from here; invalid means "not keyed since this control was armed".
    QElapsedTimer m_sinceUnkey;
    // Arm or release the bandscope gate for a law that needs the wideband
    // headroom reading. See the definition for the ownership rule.
    void applyBandscopeForAutoGain();
    // True only when the automatic control started the bandscope; disarming must not
    // stop an operator's own `bandscope.enable`.
    bool m_bandscopeOwnedByAutoGain = false;
    // Last J16 open-collector filter byte. 0xFF = nothing sent yet; kOcNone (0x00)
    // is a real value (all relays released).
    int m_ocFilterByte = 0xFF;
    // Owns the LNA gain <-> dBm coupling so a gain change cannot move the trace.
    Hl2DbReference m_dbRef;

    // Wire and DSP thread, off the GUI thread (see MetisClient.h for EP2 pacing).
    // Owned; joined in the destructor.
    QThread* m_ioThread = nullptr;

    // Rate-change build thread. m_ioThread carries the EP2 2 ms pacer, the EP6 drain
    // and every Hl2RxDsp::processIqBlock() via DirectConnection, so a rebuild there
    // would starve audio and stop EP2, which the gateware watchdog answers by halting
    // the stream (docs/HERMES.md §20.8). m_dspBuildContext owns no state; it is the
    // invokeMethod target living on that thread, parentless because moveToThread()
    // refuses a parented object. Same shape as AnanBackend.
    QThread* m_dspBuildThread = nullptr;
    QObject* m_dspBuildContext = nullptr;

    // Process-wide TX availability, decided at construction: interactive runs may
    // transmit; automation defers to AETHER_AUTOMATION_ALLOW_TX. Mirrored into
    // MetisClient, which refuses independently at the wire.
    bool m_txAllowed = false;
    Hl2Telemetry m_telemetry;
    // Cumulative EP6 sequence gaps, mirrored here from MetisClient::dropsUpdated
    // because MetisClient lives on the I/O thread and healthSnapshot() runs on GUI.
    quint64 m_drops = 0;
    // Bandscope (EP4) counters, mirrored the same way. Not on LinkStats: EP4 is
    // HL2-only, so these reach only this backend's healthSnapshot() rows.
    quint64 m_ep4Packets = 0;
    quint64 m_ep4Drops = 0;
    quint64 m_ep4Rewinds = 0;
    quint64 m_ep4Blocks = 0;
    quint64 m_ep4Timeouts = 0;
    // EP6 silence-watchdog recovery counters, mirrored the same way. Link state, not
    // bandscope: resetBandscopeMirrors() leaves them; MetisClient zeroes them at start().
    quint64 m_silenceRecoveryAttempts = 0;
    quint64 m_silenceRecoveriesCompleted = 0;
    // The bandscope gate's state as MetisClient reports it, never our own request.
    bool m_bandscopeEnabled = false;
    // The outstanding `bandscope.frame` requestId, or 0. One at a time: a reply is
    // one 2048-sample snapshot.
    quint64 m_bandscopeFrameRequest = 0;
    // The outstanding `ioboard.status` requestId, or 0. One at a time, because
    // the radio's response register holds one reply.
    quint64 m_ioBoardStatusRequest = 0;
    // The most recent accepted bandscope block, mirrored from
    // MetisClient::bandscopeBlockReady. DISPLAY ONLY (IRadioBackend.h); levels are
    // uncalibrated and pre-DDC, comparable only with the gateware clip flag.
    // `samples == 0` = none seen, so the rows stay absent ("absent means not reported").
    AetherSDR::hl2::Ep4Stats m_bandscopeBlock;
    // When that block arrived. Invalid until the first one does.
    QElapsedTimer m_bandscopeBlockClock;
    // Transport counters mirrored from MetisClient::linkCountersUpdated, held as the
    // seam type: MetisClient is only forward-declared here, and translating in the
    // receive lambda keeps the mapping in one place.
    LinkStats m_link;
    QTimer* m_linkStatsTimer = nullptr;
    // rxPackets at the previous tick; the difference answers "is the radio sending".
    quint64 m_linkRxPacketsAtLastTick = 0;
    static constexpr int kLinkStatsIntervalMs = 1000;
    bool m_adcOverload = false;
    // The overload bit is a per-frame comparator sample that dithers on a strong
    // band. telemetryUpdated is coalesced to 10 Hz (#4449), so the edge gate is
    // followed by a rate limit: warn on the first transition, then once per window
    // with the count of assertions seen at 10 Hz (not comparator edges).
    QElapsedTimer m_adcOverloadClock;
    int m_adcOverloadAssertions = 0;
    static constexpr qint64 kAdcOverloadWarnIntervalMs = 10000;

    // Clip rate with its denominator (see Hl2Telemetry): the per-window pair from
    // MetisClient plus session totals, published in healthSnapshot() and driving
    // nothing. The gateware clears the counter only in the EP6 response cycle, so
    // these stop when the stream stops; m_adcWindowClock says how old the last real
    // observation is (stale zero is not a quiet band).
    int m_adcWindowSamples = 0;
    int m_adcOverloadWindowSamples = 0;
    int m_adcWindowMs = 0;
    quint64 m_adcTotalSamples = 0;
    quint64 m_adcTotalOverloadSamples = 0;
    QElapsedTimer m_adcWindowClock;
    // Session totals of Hl2Telemetry::forwardPowerSamples + forwardPowerSkipped,
    // and of the skipped ones alone. Published in healthSnapshot(), driving
    // nothing; the first is what makes a skipped count of 0 a reading.
    quint64 m_fwdTotalResponses = 0;
    quint64 m_fwdTotalSkipped = 0;
    // Below this many observations a window has no rate, only a numerator.
    static constexpr int kAdcMinWindowSamples = 4;
    bool m_keyed = false;
    // The radio reports its own PTT/key (EP6 ptt_resp). Never an input to keying.
    bool m_radioPtt = false;
    // What transmitChanged last carried as mox: m_keyed || m_radioPtt.
    bool m_publishedTransmit = false;
    bool m_tuning = false;
    TxCoordinator::Operation m_tuneOperation;   // the TUNE carrier's admission
    bool m_cwAutoKeyed = false;
    QTimer* m_cwHangTimer = nullptr;
    TxCoordinator::Operation m_cwHangOperation;
    TxCoordinator::Operation m_lastTxOperation;
    TxCoordinator::Completion m_cwHangCompletion;
    bool m_txMonitor = false;
    // The receive-audio hold flag, read by mixReceiverAudio() and mirrored to every
    // Hl2RxDsp by applyRxAudioMute(). Not (m_keyed && !m_txMonitor): on key-up it
    // stays true for m_unkeyUnmuteHoldMs after MOX-off is queued (#5497).
    bool m_rxAudioMuted = false;
    // How long the RX mute outlives the unkey, ms (#5497). Measured, not chosen: W,
    // from demod unmute to the last sample of our own TX reaching it, on one HL2
    // (gateware 74.2) into a dummy load: n = 11, median 59.40 ms, range 51.66-66.15;
    // 70 ms covers all 11 with 3.85 ms spare. Every ms is lost receive (#5498:
    // post-unkey dropout 113.74 -> 250.65 ms median), so do not pad it. In CW full
    // break-in the hold is skipped only when the hang is shorter than it (#5850);
    // applyKeying()'s cwBreakIn arm holds the predicate. A member so tests can zero it.
    static constexpr int kUnkeyUnmuteHoldMs = 70;
    int m_unkeyUnmuteHoldMs = kUnkeyUnmuteHoldMs;
    // Single-shot, owned, on this thread, so applyKeying() cancels it without a lock.
    QTimer* m_unkeyUnmuteTimer = nullptr;
    // How long the band filter and an owed RF drive wait after the MOX-off is
    // queued. Gateware defaults put the radio's tail at 20 ms FIFO + 12 ms hang
    // (radio.v:934-935; 0x17 is never written); one unit measured 40-54 ms
    // end-to-end, and the RX hold above measured 51.66-66.15 ms of the same
    // tail. Never below kUnkeyUnmuteHoldMs. A member so tests can shorten it.
    static constexpr int kTxTailHoldMs = kUnkeyUnmuteHoldMs;
    int m_txTailHoldMs = kTxTailHoldMs;
    QTimer* m_txTailTimer = nullptr;
    bool m_txTailPending = false;
    // The drive register holds a tune level; the operator's RF drive is owed
    // once no tune carrier or tail is radiating.
    bool m_rfDriveOwed = false;
    // The flags above flip synchronously while setAudioMuted rides a queued
    // connection, so at key-up they claim "sampling" a block early. This gate answers
    // from the reading's stamp instead; healthSnapshot() feeds it to adcPairing().
    hl2::SliceSamplingGate m_sliceSampling;
    bool m_toneFromTune = false;
    // Last setTxPower() drive, restored after TUNE. Seeded to TransmitModel's
    // rfPower default so an early TUNE restores something sane.
    int m_rfPowerPercent = 100;
    // The applied drive for the health snapshot (#4912): the raw 0..kTxDriveMax value
    // last handed to MetisClient; negative = never written, row absent. The "gated"
    // row is derived from m_txAllowed at read time.
    int m_txDriveRegister = -1;
    // RFC #4603 state memory. m_restoredState is the validated pre-connect snapshot;
    // the per-band maps (Hl2Bands.h keys) are the session's working copies, with
    // defaults for unvisited bands. m_currentBandKey says which band live edits
    // belong to.
    bool m_haveRestoredState = false;
    RestoredRadioState m_restoredState;
    QMap<QString, int> m_lnaDbByBand;
    QMap<QString, int> m_driveByBand;
    // No m_lnaDefaultDb: the LNA fallback is hl2::kLnaDefaultGainDb (#5829), unlike
    // drive's per-profile m_driveDefaultPercent latch.
    // m_lnaSessionPin: the connect param pinned a gain the start band had stored
    // differently; live value honoured, persistence refused (Hl2BandMemoryPolicy.h).
    // Cleared when the operator changes gain or leaves the start band.
    bool m_lnaSessionPin = false;
    int m_driveDefaultPercent = -1;  // <0: no restored default; leave drive alone
    QString m_currentBandKey;
    // True while band-memory/restore code drives setTxPower(): only operator intent
    // bootstraps the baseline or records into the per-band map.
    bool m_applyingBandMemory = false;
    // TUNE power per band, with the same first-set baseline rule as drive. -1 in
    // m_tunePowerPercent: nothing known yet, TransmitModel's value stands.
    QMap<QString, int> m_tuneByBand;
    int m_tuneDefaultPercent = -1;
    int m_tunePowerPercent = -1;
    // Records an operator's TUNE power under the current band; false when unchanged.
    bool recordTunePower(int percent);
    // The band's remembered TUNE power, echoed to TransmitModel; nothing when unknown.
    void applyTuneMemoryFor(const QString& bandKey);

    // The operator's TX passband and whether they have set one.
    // defaultTxPassbandForMode() is re-pushed on every mode set and TX-slice move
    // and cannot tell "unchosen" from "chose 300..2700"; the flag keeps an operator
    // passband (e.g. eSSB) across mode changes.
    bool m_txFilterFromOperator = false;
    int m_txFilterLowHz = 300;
    int m_txFilterHighHz = 2700;

    // Loudest mic peak of this transmission, dBFS, for the unkey "went out quiet"
    // diagnostic (the ALC only reduces). -140 is Hl2TxDsp::micPeak's silence floor
    // = nothing measured.
    float m_txMicPeakMaxDbfs = -140.0f;

    // True once this transmission carried client-leveled (TCI/DAX) audio. Gates the
    // unkey mic-gain diagnostic off and is reported in healthSnapshot(). Set in
    // submitTxAudio(), cleared on each key edge in setKeying().
    bool m_txAudioClientLeveled = false;
    // True once this transmission carried EngineGenerated audio (the WSPR pump).
    // Gates the unkey mic-gain diagnostic off: no mic slider is in that path. The
    // AX.25 modem is tagged Microphone, since the slider is its only control.
    bool m_txAudioEngineGenerated = false;

    // The passband to push at the modulator for `mode`: the operator's if they
    // have chosen one, otherwise that mode's default.
    std::pair<int, int> effectiveTxPassband(const QString& mode) const;
    // Apply that passband and announce it as a TransmitDelta, so the applet shows
    // what the transmitter runs. setTxFilter() is the one push that bypasses this;
    // see the definition.
    void pushTxPassband(const QString& mode);
    int m_lastFwdRaw = -1;

    // Meter ballistics: the S-meter's rate gate and EMA are SMeterSmoother's
    // (WdspSMeter.h), shared with AnanBackend (100 ms tick, attack 0.5, decay 0.15),
    // fed ~47 readings/s at any sample rate. Indices 1..9 are defineMeters()' fixed
    // catalogue; receivers above the first take kSliceLevelMeterBase + uiNumber.
    static constexpr int kSliceLevelMeterBase = 100;
    // S-meter state is per receiver (Receiver::sMeter). PA temperature rides the
    // 10 Hz telemetry and needs only a symmetric EMA against ADC low-bit flicker.
    static constexpr double kPaTempAlpha = 0.2;
    double m_paTempC = 0.0;
    // ANAN-100D port: MON samples (24 kHz mono, MON level applied) waiting
    // for the I/O-thread codec mixer; guarded by m_codecMixMutex.
    std::deque<float> m_monFifo;
    bool m_monPrimed = false;   // under m_codecMixMutex
    // ANAN-100D port: smoothed supply rail (publishTelemetry).
    double m_supplyVolts = 0.0;
    bool m_haveSupplyVolts = false;
    bool   m_havePaTemp = false;

    // Forward-power peak hold: a PEP ESTIMATE. The HL2 has no peak detector: forward
    // power is one 12-bit slow_adc I2C conversion, round-robined with reverse power,
    // temperature and bias (rtl/slow_adc.v, rtl/control.v ~L262), reported in RADDR 1
    // up to ~190 times a second. Keyed, the input is each publish window's maximum
    // (Hl2Telemetry::forwardPowerPeakRaw); the hold carries it across windows:
    // instant attack, release 0.05 per kTelemetryMinIntervalMs window (~2 s, like an
    // outboard PEP meter), in WATTS because the calibration curve is non-linear. Raw
    // counts are still logged and published. TxApplet's PEP tick (#2561) therefore
    // tracks the gauge fill on an HL2.
    static constexpr double kFwdPeakReleaseAlpha = 0.05;
    double m_fwdPeakWatts = 0.0;

    // Operator's MIC slider position, 0..100 (50 = unity; see setMicGain()), kept
    // for reporting alongside the linear gain it maps to.
    int m_micLevel = 50;

    // Mic level from this radio's restored document, applied once by
    // pushInitialState() (m_txDsp does not exist at applyRestoredState() time).
    // -1 = nothing stored or already consumed; not 0, which is the slider's MUTE
    // (hl2::micSliderToLinear).
    int m_restoredMicLevel = -1;

    // Voice-chain mirrors for healthSnapshot(): originate on the DSP worker and are
    // mirrored on signal delivery, so the GUI thread reads local values and can
    // answer after the over. Also published as meters. NaN = never reported (0 dB
    // of ALC gain is a real state).
    double m_alcGainDb = std::numeric_limits<double>::quiet_NaN();
    double m_alcPeakDbfs = std::numeric_limits<double>::quiet_NaN();
    // The modulator's linear mic gain as echoed by Hl2TxDsp. NaN until confirmed, so
    // "never landed" differs from "landed at unity".
    double m_appliedMicGainLinear = std::numeric_limits<double>::quiet_NaN();

    // The ALC target peak connectRadio() configured, read by healthSnapshot() and
    // setKeying()'s unkey mic diagnostic. The ALC only reduces, so the mic peak is
    // the on-air level up to this target. Seeded with Config's default as a literal
    // (Hl2TxDsp is forward-declared); a static_assert in Hl2Backend.cpp pins them.
    double m_alcTargetPeak = 0.85;

    // The AGC threshold -> WDSP gain ceiling map is
    // Hl2DbReference::kAgcCeilingDbPerUnit via m_dbRef.agcCeilingDb(), so the
    // ceiling is referred to LNA gain like the displayed dBm.

    // Fraction of the half-span the slice may occupy before the NCO re-centres.
    // 0.8 leaves the outer 20% of each side for filter roll-off.
    static constexpr double kUsablePassbandFraction = 0.8;
    // Ceiling on host-mixed slice audio. N demodulated receivers are summed
    // here, so N loud slices can sum past full scale where one never could.
    static constexpr float kMixCeiling = 1.0f;
    // Centre of SliceModel's 0..100 balance range.
    static constexpr int kAudioPanCentre = 50;

    // AD9866 LNA gain limits, dB: the range ccRxGain() encodes (C4 = 0x40 | (dB + 12),
    // 6-bit), so clamping elsewhere would truncate silently on the wire.
    static constexpr int kLnaGainMinDb  = hl2::kLnaGainMinDb;
    static constexpr int kLnaGainMaxDb  = hl2::kLnaGainMaxDb;
    static constexpr int kLnaGainStepDb = hl2::kLnaGainStepDb;
    // ANAN-100D port: the "RF gain" control's range. On ANAN it is the step
    // attenuator shown as gain: -31..0 dB (attenuation = -gain), default 0.
    static constexpr int kAnanGainMinDb = -31;
    static constexpr int kAnanGainMaxDb = 0;
    [[nodiscard]] int gainMinDb() const noexcept { return kAnanGainMinDb; }
    [[nodiscard]] int gainMaxDb() const noexcept { return kAnanGainMaxDb; }
    [[nodiscard]] int gainDefaultDb() const noexcept { return 0; }

    // TX passband ceiling: Nyquist of the 24 kHz TX AUDIO rate (AudioEngine), not
    // the 48 kHz EP2 rate. Shared by setTxFilter() and applyRestoredState() so a
    // restore admits exactly what the setter can produce.
    static constexpr int kTxAudioMaxHz = 12000;
};

}  // namespace AetherSDR::hl2
