#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// openHPSDR Ethernet Protocol 2 wire primitives for the ANAN-G2 backend; a port
// of the bench-validated anan/spike/phase1a.py spike (Saturn, protocol 4.3,
// gateware 27). Sources: openHPSDR Ethernet Protocol v4.4 (VK6PH, see
// THIRD_PARTY_LICENSES), Saturn FPGA RTL, and p2app, the radio-side server
// (provenance: docs/architecture/anan-p2-backend-design.md §2.9). Page numbers
// below are spec pages. Socket- and Qt-free; P2Client owns the sockets.
// RX-ONLY BY CONSTRUCTION: no DUC-Specific, TX drive or PTT encoder exists here
// (TX is RFC §2.11 Phase 3, behind the engine's TX arbiter). buildSpeakerAudio()
// is not transmit: it sends demodulated receiver audio to the radio's speaker
// codec, never reaches the DUC/PA/T-R relay, and cannot key anything.

namespace AetherSDR::anan {

inline constexpr std::uint16_t kRadioPort = 1024;

// The radio's fixed listening ports (spec p.19-20 default port table).
// Load-bearing: DDC-Specific and High-Priority carry no packet-type byte, so the
// radio identifies them by arrival port; sent to kRadioPort they are silently
// ignored by p2app.
inline constexpr std::uint16_t kDdcSpecificPort = 1025;
inline constexpr std::uint16_t kHighPriorityPort = 1027;

// The radio's listening port for DDC Audio -- demodulated receiver audio sent
// BACK to the radio so its own codec and speaker reproduce it (spec p.36; the
// default port table, p.19-20, indexes it 4). p2app receives it in
// IncomingSpkrAudio() and DMAs it to the speaker codec FIFO.
//
// Load-bearing for the same reason kDdcSpecificPort and kHighPriorityPort are:
// a speaker packet carries no type discriminator, only a 4-byte sequence, so
// the port IS the packet's identity. Sent anywhere else it is silently dropped.
inline constexpr std::uint16_t kSpeakerAudioPort = 1028;

// The port DDC0 IQ nominally originates FROM per the General Packet's byte
// 17-18 default (spec p.19-20). NOT load-bearing for where the PC receives
// it: Phase 1a measured DDC0 IQ arriving at the port the client's Discovery
// packet was SENT FROM, matching the spec's literal "Destination Port: the
// Source Port of the Host that initiated Discovery" rule for every radio->PC
// stream (p.43, p.51, p.54) -- not a fixed per-DDC port on the PC side.
// Kept here only as a documented fallback a client MAY also listen on; do
// not treat it as the primary demultiplexing port.
inline constexpr std::uint16_t kDdc0DefaultPort = 1035;

// Which DDC an inbound IQ datagram belongs to, by the port the RADIO sent it
// FROM; nullopt outside [basePort, basePort + numDdc) (Mic Data, HP Status and
// the Discovery reply share the socket, so that is ordinary). basePort + n
// matches p2app: generalpacket.c's SetPort(VPORTDDCIQ0+i, Port+i) for a non-zero
// General bytes 17-18, and DefaultPorts[] VPORTDDCIQ0..9 = 1035..1044 when zero
// (what buildGeneral() sends). The port is the ONLY DDC discriminator: the I&Q
// packet has no DDC index (pp.53-54), and p2app never emits the interleaved
// "Synchronous DDC" framing.
[[nodiscard]] std::optional<int> ddcIndexForSenderPort(
    std::uint16_t senderPort, int numDdc,
    std::uint16_t basePort = kDdc0DefaultPort) noexcept;

// The six DDC0 rates in ksps, ascending. DDC0 is stepped, not continuous
// (AnanBackend::nearestDdc0RateKsps() snaps zoom to these). The single source for
// capabilities, pan limits, AnanRxDsp's droop-table check, the calibrator sweep
// and ConnectionPanel's picker: a rate missing from any of them silently
// rejects that rate's correction tables.
inline constexpr std::array<int, 6> kDdc0RatesKsps{48, 96, 192, 384, 768, 1536};

// Saturn boards' DSP clock. RFC §2.4; saturnregisters.c VSAMPLERATE. ANGELIA
// (ANAN-100D) and ORION (ANAN-200D) run the same 122.88 MHz clock (spec
// Appendix A, "DSP clock frequency").
inline constexpr std::uint32_t kDspClockHz = 122'880'000;

// ---- Board profiles (ANAN-100D port) ----
//
// Discovery Reply byte 11 (spec "Board Type"): 3 = ANGELIA (ANAN-100D),
// 4 = ORION (ANAN-200D), 10 = SATURN (ANAN-G2). The three share the Protocol 2
// wire format; what differs is collected here so the packet builders, and
// nothing above them, branch on the board.
inline constexpr std::uint8_t kBoardIdHermes = 1;      // ANAN-10 / ANAN-100
inline constexpr std::uint8_t kBoardIdHermesII = 2;    // ANAN-10E / ANAN-100B
inline constexpr std::uint8_t kBoardIdAngelia = 3;     // ANAN-100D
inline constexpr std::uint8_t kBoardIdOrion = 4;       // ANAN-200D
inline constexpr std::uint8_t kBoardIdOrion2 = 5;      // ANAN-7000DLE / 8000DLE / Orion MkII / Anvelina Pro3
inline constexpr std::uint8_t kBoardIdSaturn = 10;     // ANAN-G2 / G2-1K
inline constexpr std::uint8_t kBoardIdSaturnMkII = 11; // ANAN-G2 (MkII board)
inline constexpr std::uint8_t kBoardIdHermesC10 = 20;  // ANAN-G2E

// How the 32-bit Alex words are interpreted by the radio.
enum class AlexScheme {
    // ANAN-G2 under p2app: bits 1-6/12 select a receive BAND-PASS bank per ADC
    // (Alex0 low half = ADC0, Alex1 low half = ADC1); the RX path does not pass
    // the TX low-pass filters. Bench-validated in the original backend.
    SaturnBandPass,
    // ANAN-100/100D/200D "classic" Alex (spec Appendix D; deskHPSDR
    // new_protocol.c default case; alex.h): the same bits 1-6/12 select
    // HIGH-pass filters ahead of ADC0 only, and a signal from Ant1/2/3 ALSO
    // passes the TX low-pass bank (bits 20-23, 29-31), which therefore has to be
    // set for the receive frequency while receiving.
    LegacyHighPassLowPass,
    // ANAN-7000DLE/8000DLE/G2/G2-1K/G2E (Thetis "MKII BPF"; deskHPSDR
    // new_protocol.c NEW_DEVICE_ORION2/SATURN/G2E): bits 1-6/12 select a
    // receive BAND-PASS filter (1.5, 2.1, 5.5, 11, 22, 35 MHz edges) for ADC0
    // in Alex0 and for ADC1 in Alex1; the receive path does NOT pass the TX
    // low-pass bank, which follows the transmit frequency only. RX-only
    // inputs use the master RX select bit 14; Alex1 bit 8 grounds ADC1 on TX.
    Orion2BandPass,
};

struct BoardProfile {
    std::uint8_t boardId = kBoardIdAngelia;
    const char* modelName = "ANAN-100D";
    // Receiver n streams from hardware DDC (rxDdcBase + n). SATURN under p2app
    // streams receive on DDC0 (bench-validated). ANGELIA/ORION reserve
    // DDC0/DDC1 for PureSignal feedback and Diversity: deskHPSDR and Thetis
    // both put RX1 on DDC2 and RX2 on DDC3 ("receiver[i] is associated with
    // DDC(i+2)", new_protocol.c).
    int rxDdcBase = 2;
    AlexScheme alexScheme = AlexScheme::LegacyHighPassLowPass;
    // General Packet byte 59: Alex boards to enable (spec p.16, Appendix D:
    // "in order to use an Alex filter it is necessary to set the relevant
    // bit in Byte 59"). ANGELIA/ORION: Alex0 only. SATURN: left 0, as the
    // bench-validated original backend sent it.
    std::uint8_t alexEnableMask = 0x01;
    // High Priority byte 1400 bit 1 (speaker amplifier mute) exists on
    // ORION2/SATURN only (deskHPSDR gates it the same way).
    bool hasSpeakerAmpMute = false;
    // A separate receive filter bank for ADC1 (Alex1 low half). SATURN has
    // one; ANGELIA/ORION's second ADC has no Alex filters.
    bool hasAdc1FilterBank = false;
    // ANAN-100D port: receive ADCs on the board (Hermes boards and the G2E
    // have one). The TX DAC is ADC index numAdcs in the DDC-Specific packet.
    int numAdcs = 2;
    // PureSignal as this client runs it: feedback on DDC0 and the DAC on DDC1
    // while the receivers stay on DDC2+ (rxDdcBase >= 2). The single-ADC
    // boards receive on DDC0/DDC1 themselves, so it is not offered there.
    bool psSupported = true;
    // The status packet bytes 8-53 carry the Odyssey firmware's buffer and
    // link counters (layout version in byte 47). Only on the ANAN-100D /
    // 200D, which that firmware runs on; elsewhere those bytes are the
    // stock firmware's user ADCs.
    bool odysseyStatus = true;
};

// The profile for a Discovery board id, or nullopt for a board this backend
// does not drive (Hermes/Atlas/HL2/...).
[[nodiscard]] std::optional<BoardProfile> boardProfileFor(std::uint8_t boardId) noexcept;

// The profile used when no Discovery reply identifies the board: this port's
// target radio, the ANAN-100D.
[[nodiscard]] BoardProfile defaultBoardProfile() noexcept;

// ---- Models (ANAN-100D port) ----
//
// One board id covers several radios (Thetis HardwareSpecific: ORIONMKII = 5
// is the 7000DLE, 8000DLE, Orion MkII and Anvelina Pro3; SATURN = 10 the G2
// and G2-1K), which the radio does not report, so the operator picks the
// model (Setup > Radio, the radio's page); defaultModelFor() is the first
// guess. What differs per model is collected in ModelTraits.
enum class AnanModel : int {
    Anan10 = 0, Anan100, Anan10E, Anan100B, Anan100D, Anan200D,
    Anan7000DLE, Anan8000DLE, OrionMkII, AnvelinaPro3,
    AnanG2, AnanG2_1K, AnanG2E,
};
inline constexpr int kAnanModelCount = 13;

// A directional coupler reading -> watts (Thetis console.cs computeAlexFwdPower
// / computeRefPower): V = (raw - offset) / 4095 * refVolts, W = V^2 / bridge.
struct CouplerCal {
    double bridgeVolts = 0.095;
    double bridgeVolts6m = 0.095;   // the reverse bridge differs on 6 m on some models
    double refVolts = 3.3;
    int adcOffset = 6;
};

// Where the voltage readout comes from.
enum class VoltsSource {
    HermesSupply,   // status bytes 49-50, 3.3 V ADC, (4.7 k + 0.82 k) / 0.82 k divider
    PaUserAdc0,     // status bytes 57-58 (User ADC0), (22 k + 1 k) / 1.1 k divider
};

struct ModelTraits {
    AnanModel model = AnanModel::Anan100D;
    const char* key = "ANAN-100D";     // settings value and display name
    std::uint8_t boardId = kBoardIdAngelia;
    CouplerCal fwd;
    CouplerCal rev;
    VoltsSource volts = VoltsSource::HermesSupply;
    double userAdcRefVolts = 5.0;      // PaUserAdc0 / drain current ADC full scale
    // PA drain current (User ADC1, status bytes 55-56): A = (mV - voff) / sens
    // (Thetis convertToAmps, HardwareSpecific.GetDefaultVoltCalibration).
    bool hasAmps = false;
    double ampVoffMv = 360.0;
    double ampSensMvPerA = 120.0;
    // Transmitter Specific byte 50 bits 2-4 (Thetis "Orion" mic: PTT disable,
    // tip/ring, bias) and bit 5 (G2 balanced XLR input).
    bool orionMic = false;
    bool xlrMic = false;
    // Thetis HardwareSpecific.RXMeterCalbrationOffsetDefaults.
    double rxMeterOffsetDb = 0.98;
};

[[nodiscard]] const ModelTraits& modelTraits(AnanModel m) noexcept;
// The models a board id can be, the first being the default; empty for a
// board this backend does not drive.
[[nodiscard]] std::vector<AnanModel> modelsForBoard(std::uint8_t boardId);
[[nodiscard]] AnanModel defaultModelFor(std::uint8_t boardId) noexcept;
// Model by its key ("ANAN-7000DLE"), or nullopt.
[[nodiscard]] std::optional<AnanModel> modelFromKey(const char* key, std::size_t len) noexcept;
// Watts for a coupler reading on a band (6 m: hz > 35 MHz).
[[nodiscard]] double couplerWatts(const CouplerCal& cal, int raw, bool sixMeters) noexcept;

// 24-bit signed full scale. (1<<23)-1, not 1<<23 -- see MetisProtocol.h's
// kFullScale for the reasoning (the largest magnitude a 24-bit two's
// complement sample can actually take, matching the reference clients'
// dBFS scale rather than landing a fraction of a dB adrift).
inline constexpr int kFullScale24Bit = (1 << 23) - 1;

// Phase word: delta = 2^32 * F / Fdsp (spec p.21, p.44; RFC §2.4). This
// board REQUIRES phase words, not Hz -- Discovery Reply byte 21 == 1 on
// every G2 observed -- and General Packet byte 37 bit[3] must agree
// (buildGeneral() sets it). Verified in the field: phaseWord(10e6) tuned
// DDC0 to WWV and the carrier read back at 0 Hz baseband, 80.0 dB above the
// noise floor.
constexpr std::uint32_t phaseWord(double freqHz, std::uint32_t dspClockHz = kDspClockHz) noexcept
{
    constexpr double kTwoTo32 = 4294967296.0;
    const double delta = kTwoTo32 * freqHz / static_cast<double>(dspClockHz);
    // Round to nearest. freqHz is always non-negative for a receive tune, so
    // there is no negative-rounding case to handle here.
    return static_cast<std::uint32_t>(delta + 0.5);
}

// ---- Discovery (spec p.42-45) ----

// 60-byte discovery request: seq(4)=0, byte 4 = 0x02 (Discovery Command).
std::array<std::uint8_t, 60> buildDiscovery() noexcept;

struct DiscoveryReply {
    bool streaming = false;           // byte 4 == 0x03: hardware already has a session
    std::array<std::uint8_t, 6> mac{};// bytes 5-10, MSB first (p.43)
    std::uint8_t boardId = 0;         // byte 11; 10 = SATURN (ANAN-G2)
    std::uint8_t protoVersionRaw = 0; // byte 12, decimal-tenths (43 -> "4.3") -- the one
                                       // place the spec's own worked example is right
    std::uint8_t firmwareVer = 0;     // byte 13, an INTEGER gateware/build number, NOT
                                       // tenths despite the spec's worked example implying
                                       // one -- p2app writes GetFirmwareVersion() here
    std::uint8_t numDdc = 0;          // byte 20 -- CLAMP TO THIS, never probe past it. The
                                       // gateware may carry more DDC registers (VNUMDDC 10)
                                       // than p2app advertises (RFC Gap C).
    bool freqIsPhaseWord = false;     // byte 21: 0 = Hz, 1 = phase word
    std::uint8_t endianByteRaw = 0;   // byte 22, RAW -- see discoveryDeclaresBigEndian3Byte()
    std::uint8_t p2appBuild = 0;      // byte 23. "Beta version" per spec; p2app overwrites
                                       // it with P2APPVERSION on Saturn

    // Discovery-time PICKER filter only -- NOT for gating backend behaviour
    // once connected. The RFC is explicit that board type is a runtime
    // option on this platform (p2app.c:588 accepts -i saturn / -i orionmk2)
    // and must not decide what the backend does; the capability bytes and
    // firmware version are the load-bearing fields for that. This predicate
    // only answers "should a radio picker show this reply as an ANAN-G2".
    [[nodiscard]] constexpr bool isSaturn() const noexcept { return boardId == kBoardIdSaturn; }
    // ANAN-100D port: the boards boardProfileFor() drives (every Protocol 2
    // ANAN: Hermes, Hermes II, Angelia, Orion, Orion II, Saturn, G2E). Same
    // picker-only rule as isSaturn() above.
    [[nodiscard]] bool isSupportedAnan() const noexcept
    {
        return boardProfileFor(boardId).has_value();
    }
};

// Byte 22 per spec p.45: ZERO means big-endian 3-byte I&Q (not "none", not
// little-endian); nonzero is a bitmask with bit 0 = BE. A real G2 sends 0 and
// decodes BE at 80.0 dB SNR vs 26.0 dB byte-swapped (WWV 10 MHz); buildGeneral()
// requests this via byte 39.
[[nodiscard]] constexpr bool discoveryDeclaresBigEndian3Byte(std::uint8_t byte22) noexcept
{
    return byte22 == 0 || (byte22 & 0x01) != 0;
}

// Parse a Discovery Reply (>=24 bytes: header through byte 23). Bounds-checks
// before indexing: this parses unauthenticated UDP.
std::optional<DiscoveryReply> parseDiscoveryReply(std::span<const std::uint8_t> data) noexcept;

// ---- General Packet (spec p.18-22) ----

// 60-byte General Packet. Port fields (bytes 5-22) zero = defaults (p.19-20).
// Byte 37 bit[3] = 1 (phase word, matching Discovery byte 21). Byte 38 bit[0] = 1:
// hardware watchdog ON (p.8: without a C&C packet at least every second the
// radio drops out of RUN), which self-heals an abandoned session (a `kill -9`d
// client recovers cleanly). Byte 39 = 0x00 (BE + 3-byte, per byte 22 == 0).
// Byte 59 = `alexEnableMask` (BoardProfile::alexEnableMask).
// ANAN-100D port: wideband (raw ADC) data, General bytes 23..28 (spec p.20,
// Thetis network.c CmdGeneral). Ports bytes 21-22 stay 0 = default 1027 for
// ADC0, 1028 for ADC1. 512 x 16-bit samples per packet; packetsPerFrame
// packets of contiguous samples make one frame, one frame per updateMs.
struct WidebandOptions {
    std::uint8_t enableMask = 0;       // bit n = ADCn
    std::uint8_t updateMs = 50;        // byte 27, 0..255 ms per frame
    std::uint8_t packetsPerFrame = 32; // byte 28 (32 x 512 = 16384 samples)
};
inline constexpr std::uint16_t kWidebandBasePort = 1027;
inline constexpr int kWidebandSamplesPerPacket = 512;
inline constexpr int kWidebandPacketBytes = 4 + 2 * kWidebandSamplesPerPacket;   // 1028

std::array<std::uint8_t, 60> buildGeneral(std::uint8_t alexEnableMask = 0,
                                          bool paEnable = false,
                                          const WidebandOptions& wideband = {}) noexcept;

// One wideband packet: sequence number and 512 raw ADC samples scaled to
// [-1, 1). False for anything that is not exactly kWidebandPacketBytes.
bool parseWidebandPacket(std::span<const std::uint8_t> data, std::uint32_t& seq,
                         float* samples512) noexcept;

// ---- Transmit (ANAN-100D port) -------------------------------------------
// Ports PC -> radio: Transmitter Specific 1026, DUC I&Q 1029 (spec p.20,
// deskHPSDR new_protocol.h).
inline constexpr std::uint16_t kTransmitSpecificPort = 1026;
inline constexpr std::uint16_t kDucIqPort = 1029;
// DUC I&Q: fixed 192 ksps, 24-bit, 240 samples per 1444-byte packet.
inline constexpr int kDucSampleRateHz = 192000;
inline constexpr int kDucSamplesPerPacket = 240;
// Alex bit 27: T/R relay to TX (alex.h ALEX_TX_RELAY).
inline constexpr std::uint32_t kAlexTxRelay = std::uint32_t{1} << 27;

// ANAN-100D port: the FPGA CW keyer (Transmitter Specific bytes 5..13, 17),
// set from the CW applet the way MicronSDR's Options -> Keyer page sets its
// radio. Field layout and bit names as Thetis (ChannelMaster network.c CmdTx,
// network.h prn->cw.mode_control).
struct CwKeyerOptions {
    bool enabled = false;        // byte 5 bit 1: keyer on (the TX mode is CW)
    int mode = 2;                // 0 = straight key, 1 = iambic A, 2 = iambic B
    bool paddleSwap = false;     // bit 2
    bool strictSpacing = false;  // bit 6: auto letter spacing
    bool breakIn = true;         // bit 7: the radio keys T/R itself (hang time)
    int sidetoneLevel = 127;     // byte 6, 0..255 (0 = off, bit 4 clear)
    int sidetoneHz = 700;        // bytes 7-8
    int speedWpm = 20;           // byte 9
    int weight = 50;             // byte 10 (50 = normal)
    int hangMs = 250;            // bytes 11-12
    int rfDelayMs = 25;          // byte 13: T/R relay settle before RF (TX applet, all modes)
    int edgeMs = 7;              // byte 17: CW envelope rise/fall (Thetis default)
    bool demo = false;           // host side only: no PA, no relay, no drive
    friend bool operator==(const CwKeyerOptions&, const CwKeyerOptions&) = default;
};

// Byte 5 of the Transmitter Specific packet for these options.
std::uint8_t cwModeControlByte(const CwKeyerOptions& k) noexcept;

// Transmitter Specific packet (60 bytes): one DAC, the FPGA CW keyer as `cw`
// (all keyer bytes zero when it is off), mic PTT enabled, mic boost on request.
// txAttenuationDb: bytes 57..59, the step attenuator of ADC2/1/0 while
// transmitting (0..31 dB; Thetis sets 31 -- full duplex without the RX
// being blasted by our own transmitter).
std::array<std::uint8_t, 60> buildTransmitSpecific(std::uint32_t seq = 0,
                                                   bool micBoost = false,
                                                   const CwKeyerOptions& cw = {},
                                                   int txAttenuationDb = 0,
                                                   int psFeedbackAttDb = -1,
                                                   int psFeedbackAdc = 0,
                                                   std::uint8_t micControlBits = 0) noexcept;
// micControlBits: byte 50 bits 2-5 as Thetis network.c CmdTx (mic_control):
// bit 2 mic PTT disabled, bit 3 PTT on tip / mic on ring, bit 4 mic bias on,
// bit 5 balanced (XLR) input (G2). Only the Orion / Orion II / Saturn boards
// read them; bits 0-1 (line in, boost) come from micBoost.
inline constexpr std::uint8_t kMicPttDisabled = 0x04;
inline constexpr std::uint8_t kMicTipIsPtt = 0x08;
inline constexpr std::uint8_t kMicBiasOn = 0x10;
inline constexpr std::uint8_t kMicXlr = 0x20;
// psFeedbackAdc 1: the PS feedback comes in on ADC1 (RX2 input) -- the
// feedback attenuator goes to byte 58 (ADC1) instead of 59 (ADC0).
// psFeedbackAttDb >= 0 (PureSignal on): byte 59, the ADC0 step attenuator
// while transmitting, is the PS feedback attenuator instead (deskHPSDR
// transmitter->attenuation, Thetis ATTOnTX).

// ---- Codec microphone (ANAN-100D port) ----
// Radio -> PC port 1026: 132 bytes, sequence + 64 mono 16-bit big-endian mic
// samples at 48 kHz. (Speaker audio to the codec: buildSpeakerAudio(), port
// kSpeakerAudioPort, below.)
inline constexpr std::uint16_t kMicFromRadioPort = 1026;
inline constexpr int kCodecRateHz = 48000;
inline constexpr int kCodecFramesPerPacket = 64;
inline constexpr std::size_t kMicPacketBytes = 4 + 2 * kCodecFramesPerPacket;      // 132
// The 64 mic samples of a 132-byte packet, scaled to [-1, 1); empty if the
// size is wrong.
std::vector<float> parseMicPacket(std::span<const std::uint8_t> bytes);

// One DUC I&Q packet: seq + 240 x (24-bit I, 24-bit Q) big-endian. Samples
// are clamped to [-1, 1]; fewer than 240 are zero-padded.
std::array<std::uint8_t, 1444> buildDucIq(std::uint32_t seq,
                                          std::span<const std::complex<float>> iq) noexcept;

// ---- DDC-Specific Packet (spec p.23-26) ----

// 1444-byte DDC-Specific packet enabling DDC0 only at `ddc0RateKsps`
// (48/96/192/384/768/1536, p.24). `numAdcs` = 2, the G2's phase-synchronous ADCs.
// `ditherEnabled`/`randomEnabled` set bytes 5/6 (bit N = ADCn, p.25) for BOTH
// ADCs in lockstep -- the radio exposes one control. `ddc0AdcIndex` sets byte 17
// (p.25): 0 = ADC0 behind the Ant1/2/3 relay bank, 1 = ADC1 on its own RX2 jack
// (Appendix D, p.90).
std::array<std::uint8_t, 1444> buildDdcSpecific(int ddc0RateKsps = 48, int numAdcs = 2,
                                                bool ditherEnabled = true,
                                                bool randomEnabled = true,
                                                int ddc0AdcIndex = 0) noexcept;

// ---- Multi-DDC ----
// Codec safety bound on DDC count: a real G2 reports 4 (p2app's DiscoveryReply[]
// hardcodes it, though gateware VNUMDDC is 10); callers still clamp to the
// Discovery value. The one-byte enable bitmap caps the format at 8.
inline constexpr int kMaxDdcs = 4;

// One DDC's slot in the DDC-Specific packet. `rateKsps` must be one of
// 48/96/192/384/768/1536 (p.24); `adcIndex` selects which ADC feeds it,
// same meaning as buildDdcSpecific()'s ddc0AdcIndex above.
struct DdcConfig {
    int rateKsps = 48;
    int adcIndex = 0;
};

// Multi-DDC form: enables DDC 0..N-1 (N capped at kMaxDdcs; excess IGNORED),
// setting byte-7 enable bits and one 6-byte row per DDC at 17 + 6*n (ADC select,
// 2-byte rate, CIC1/CIC2 = 0 "for future use", sample size). An EMPTY span
// disables all DDCs -- how a session stops streaming without teardown. The
// single-DDC overload delegates here, so there is one row-layout implementation.
//
// `hwDdcBase` (ANAN-100D port): entry n configures HARDWARE DDC hwDdcBase + n
// (BoardProfile::rxDdcBase). 0 reproduces the original bytes exactly.
// ANAN-100D port: PureSignal while transmitting (deskHPSDR new_protocol.c
// receive_specific, Thetis): DDC0 = the PS feedback ADC, DDC1 = the TX DAC
// (ADC index numAdcs), both 192 ksps / 24 bit, DDC1 synchronised to DDC0
// (byte 1363 bit 1) so their samples arrive interleaved in DDC0's packets;
// DDC0 enabled. The receivers above stay as they are (full duplex).
struct PsDdcOptions {
    bool active = false;
    int feedbackAdc = 0;
};
inline constexpr int kPsFeedbackRateKsps = 192;

std::array<std::uint8_t, 1444> buildDdcSpecific(std::span<const DdcConfig> ddcs,
                                                int numAdcs = 2,
                                                bool ditherEnabled = true,
                                                bool randomEnabled = true,
                                                int hwDdcBase = 0,
                                                const PsDdcOptions& ps = {}) noexcept;

// One synchronised DDC0/DDC1 packet (PureSignal): 238 24-bit samples
// alternating DDC0 (feedback, rx) / DDC1 (TX DAC reference, tx). Appends the
// pairs as interleaved I/Q doubles (scale 2^-23, as deskHPSDR / Thetis).
// False when the packet is not a 24-bit DDC frame.
bool decodePsPairs(std::span<const std::uint8_t> data, std::vector<double>& tx,
                   std::vector<double>& rx) noexcept;

// ---- High Priority to Hardware (spec p.31-34) ----

// One selection of a receive band-pass bank, named as the spec's SATURN/ANAN-G2
// Alex0 and Alex1 bit tables name it (p.91). Both words use the same bits, so
// one enum serves ADC0 and ADC1. The names are the spec's, not band names: the
// spec publishes the bits but not the passbands (see alexRxFilterFor()).
enum class AlexRxFilter {
    Bypass,      // bit 12, "HF Bypass": the bank out of circuit
    Bpf1_5MHz,   // bit 6
    Bpf6_5MHz,   // bit 5
    Bpf9_5MHz,   // bit 4
    Bpf13MHz,    // bit 1
    Bpf20MHz,    // bit 2
    Preamp6m,    // bit 3, "6M Preamp"
};

// The 16-bit receive word selecting `filter` alone. Exactly one bit is set: the
// bank's relays are in series ahead of the ADC (p.90).
[[nodiscard]] std::uint16_t alexRxFilterBits(AlexRxFilter filter) noexcept;

// The filter for a receiver tuned to `tunedHz`. The edges are deskHPSDR's for
// SATURN and ORION2 boards (new_protocol.c): 1.5, 2.1, 5.5, 11, 22 and 35 MHz.
// They are not derivable from the filter names, and a filter that does not
// cover the tuned frequency removes the signal path, so the selection stays
// opt-in. Below 1.5 MHz, and for a non-finite frequency, it is Bypass.
[[nodiscard]] AlexRxFilter alexRxFilterFor(double tunedHz) noexcept;

// The ANAN-100/100D/200D high-pass selection for `tunedHz` (AlexScheme::
// LegacyHighPassLowPass). Same enumerators -- the bit positions are shared --
// but they are HIGH-pass filters here, so the edges are deskHPSDR's for those
// boards (new_protocol.c default case): below 1.8 MHz bypass, then 1.5 MHz
// HPF to 6.5, 6.5 HPF to 9.5, 9.5 HPF to 13, 13 HPF to 20, 20 HPF to 50,
// 6 m preamp above. Non-finite: Bypass.
[[nodiscard]] AlexRxFilter legacyAlexHpfFor(double tunedHz) noexcept;

// The ANAN-100/100D/200D TX low-pass bank bits (bits 20-23, 29-31) for `hz`,
// exactly one bit. deskHPSDR's edges: >35.6 MHz 6 m/bypass, >24 12/10 m,
// >16.5 17/15 m, >8 30/20 m, >5 60/40 m, >2.5 80 m, else 160 m. Non-finite:
// the 6 m/bypass LPF (the widest).
[[nodiscard]] std::uint32_t legacyAlexLpfBitsFor(double hz) noexcept;

// The High Priority fields that apply live, beyond the bypass flags. Defaults
// reproduce the bytes this client sends with no options.
struct HighPriorityOptions {
    // A band-pass selection for that ADC's bank. It replaces the matching
    // bypass flag: the bank takes one relay, so bypass and a filter are
    // alternatives. nullopt leaves the bypass flag in charge.
    std::optional<AlexRxFilter> adc0Filter;
    std::optional<AlexRxFilter> adc1Filter;
    // Byte 1400 bit 1: mutes the radio's speaker amplifier (p2app SetSpkrMute()).
    // Sending silent speaker audio leaves the amplifier running and hissing.
    // Byte 1400's bit 0 (XVTR route on PTT) and bit 2 (tuner start) stay clear.
    bool muteSpeakerAmp = false;
    // ANAN-100D port: byte 1400 bit 2, the tuner start line, held while the
    // operator's TUNE is on (Hl2Backend::setTune).
    bool tunerStart = false;
    // ANAN-100D port: Alex bit 3 ("6M Preamp", spec Appendix D) -- the only
    // preamp/LNA bit Protocol 2 carries for these boards; receive state only.
    bool alexPreamp = false;

    // ---- ANAN-100D port ----
    // Frequency words for receiver n land in HARDWARE DDC hwDdcBase + n
    // (BoardProfile::rxDdcBase). 0 reproduces the original bytes exactly.
    int hwDdcBase = 0;
    // How the Alex words are built. SaturnBandPass reproduces the original
    // bytes exactly; LegacyHighPassLowPass is the ANAN-100D layout.
    AlexScheme alexScheme = AlexScheme::SaturnBandPass;
    // LegacyHighPassLowPass only: the receive frequency that picks the TX
    // low-pass filter the Ant1/2/3 receive path runs through. A bypassed ADC0
    // (no adc0Filter, bypass flag set) uses the 6 m/bypass LPF instead, so
    // nothing is band-limited -- deskHPSDR's "disable LPF" for filter bypass.
    double rxLpfFrequencyHz = 0.0;
    // ANAN-100D port: also write receiver n's word into DDC n when the
    // receivers sit higher (hwDdcBase >= 2). The DDCs below stay disabled;
    // Brick3/ANAN-100D boards track the band from DDC0/DDC1 (deskHPSDR's
    // "Angelia compatibility mode", p2_angelia_ddc0_map).
    bool mirrorRxWordsBelowBase = false;
    // LegacyHighPassLowPass only: drop speaker-mute and ADC1 filter fields the
    // board does not have (BoardProfile::hasSpeakerAmpMute/hasAdc1FilterBank).
    bool boardHasSpeakerAmpMute = true;
    bool boardHasAdc1FilterBank = true;

    // ---- Transmit (ANAN-100D port) ----
    // byte 4 bit 1 = PTT0 (MOX); bytes 329-332 = DUC0 phase word; byte 345 =
    // drive 0..255. With the legacy Alex scheme: Alex1 (TX state) = ANT1 +
    // LPF for txFrequencyHz (+ T/R relay when paEnabled); while transmitting
    // Alex0 takes the same LPF and the relay too (deskHPSDR new_protocol.c).
    bool transmitting = false;
    std::uint32_t ducPhaseWord = 0;
    int driveLevel = 0;
    double txFrequencyHz = 0.0;
    bool paEnabled = false;
    // Legacy Alex antenna relays (alex.h ALEX_ANT1/2/3 = bits 24/25/26), 0..2.
    // rxAntenna feeds ADC0 while receiving; txAntenna is switched in while
    // transmitting and is always the antenna in Alex1 (TX state).
    int rxAntenna = 0;
    int txAntenna = 0;
    // ANAN-100D port: byte 1401, the seven open-collector "User Out" lines,
    // bit 0 = OC1 .. bit 6 = OC7 here; written shifted left by one like the
    // spec ("[1] = Open Collector 1") and Thetis network.c. Computed by the
    // transport from the band matrices (openCollectorBits()).
    std::uint8_t openCollector = 0;
    // ANAN-100D port: PureSignal (deskHPSDR new_protocol.c, alex.h).
    // psEnabled: Alex1 PS bit (18) always; while transmitting also Alex0 PS
    // bit, the feedback input (psFeedbackSource 0 = internal coupler,
    // 1 = EXT1, 2 = RX bypass) and DDC0/DDC1 on the transmit frequency.
    bool psEnabled = false;
    int psFeedbackSource = 0;
    // Orion2BandPass only: Alex1 bit 8, ADC1's input grounded while
    // transmitting (Thetis "BPF2 Gnd on TX"); not while ADC1 carries the
    // PureSignal feedback (psFeedbackOnAdc1).
    bool adc1GroundOnTx = false;
    bool psFeedbackOnAdc1 = false;
};
inline constexpr std::uint32_t kAlexPsBit = std::uint32_t{1} << 18;
inline constexpr std::uint32_t kAlexRxExt1 = std::uint32_t{1} << 9;
inline constexpr std::uint32_t kAlexRxBypass = std::uint32_t{1} << 11;

// ---- User Out (open collector) matrices, Thetis Penny.cs semantics ----
// Ham bands only (no broadcast/VHF rows): 160 80 60 40 30 20 17 15 12 10 6 m.
inline constexpr int kOcBandCount = 11;
inline constexpr int kOcPinCount = 7;
// TX pin action (Thetis TXPinActions without two-tone): when the TX-matrix
// bit of a pin is allowed to come on.
enum class OcTxAction : std::uint8_t { Mox = 0, Tune = 1, MoxTune = 2 };
// Row labels; also the AppSettings key suffixes ("AnanOcRx_160m",
// "AnanOcTx_160m"; pin actions "AnanOcTxAction1".."AnanOcTxAction7").
inline constexpr const char* kOcBandNames[kOcBandCount] = {
    "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m"};

// Band row for the matrices, or -1 outside the amateur bands (Thetis: "GEN"
// and the broadcast bands -> all outputs off). Edges are the widest IARU
// allocations.
[[nodiscard]] constexpr int ocBandIndex(double hz) noexcept
{
    struct Edge { double lo, hi; };
    constexpr Edge kBands[kOcBandCount] = {
        {1.800e6, 2.000e6},   {3.500e6, 4.000e6},   {5.060e6, 5.450e6},
        {7.000e6, 7.300e6},   {10.100e6, 10.150e6}, {14.000e6, 14.350e6},
        {18.068e6, 18.168e6}, {21.000e6, 21.450e6}, {24.890e6, 24.990e6},
        {28.000e6, 29.700e6}, {50.000e6, 54.000e6}};
    for (int i = 0; i < kOcBandCount; ++i) {
        if (hz >= kBands[i].lo && hz <= kBands[i].hi)
            return i;
    }
    return -1;
}

struct OcMatrix {
    std::uint8_t rx[kOcBandCount]{};          // bit n = pin n+1
    std::uint8_t tx[kOcBandCount]{};
    OcTxAction action[kOcPinCount]{OcTxAction::MoxTune, OcTxAction::MoxTune,
                                   OcTxAction::MoxTune, OcTxAction::MoxTune,
                                   OcTxAction::MoxTune, OcTxAction::MoxTune,
                                   OcTxAction::MoxTune};
};

// Thetis Penny.UpdateExtCtrl + adjustForTXAction: receive -> RX row of the
// receive band; transmit -> TX row of the transmit band, each pin gated by
// its action (MOX = transmitting and not TUNE, TUNE = TUNE only).
[[nodiscard]] constexpr std::uint8_t openCollectorBits(const OcMatrix& m, double rxHz,
                                                       double txHz, bool transmitting,
                                                       bool tune) noexcept
{
    if (!transmitting) {
        const int b = ocBandIndex(rxHz);
        return b < 0 ? std::uint8_t{0} : std::uint8_t(m.rx[b] & 0x7f);
    }
    const int b = ocBandIndex(txHz);
    if (b < 0)
        return 0;
    std::uint8_t mask = 0;
    for (int pin = 0; pin < kOcPinCount; ++pin) {
        bool on = false;
        switch (m.action[pin]) {
        case OcTxAction::Mox:     on = !tune; break;
        case OcTxAction::Tune:    on = tune; break;
        case OcTxAction::MoxTune: on = true; break;
        }
        if (on)
            mask = std::uint8_t(mask | (1u << pin));
    }
    return std::uint8_t(m.tx[b] & mask);
}

// Alex ANT1/ANT2/ANT3 relay bit for antenna index 0..2 (clamped).
[[nodiscard]] constexpr std::uint32_t legacyAlexAntennaBit(int antenna) noexcept
{
    return std::uint32_t{1} << (24 + (antenna < 0 ? 0 : antenna > 2 ? 2 : antenna));
}

// 1444-byte High Priority packet. RX-ONLY: `run` sets byte 4 bit[0] only and
// there is no PTT parameter. `ddc0FreqWord` sets bytes 9-12 (p.32). Must be
// re-sent at least once a second (100 ms recommended, p.8) to stay in RUN;
// P2Client owns that cadence.
//
// The "32-bit Alex0" at 1432-1435 is two 16-bit words to the radio: p2app reads
// 1432-1433 as the TX filter and antenna word and 1434-1435 as ADC0's receive
// filter word (InHighPriority.c); 1430-1431 is ADC1's receive word (Alex1). A
// receive filter written into 1432-1433 would reach the TX relays, not the ADC.
//
// Alex0 is always written: ANT1 (bit 24 of the 32-bit word), plus ADC0's
// receive selection. Power-on Alex is all zero (p.79) and the RX1 BPF bank sits
// in series before the ADC (p.90), so with neither a band filter nor Bypass
// energised there is no signal path. ANT2/3 are not exposed; RX2 is a single
// dedicated jack with no antenna select.
//
// `bypassAdc0Filters`/`bypassAdc1Filters` set HF Bypass (bit 12) in ADC0's and
// ADC1's receive words, unless `opts` selects a filter for that ADC.
// `adc0AttenuationDb`/`adc1AttenuationDb`: step attenuators, byte 1443 (ADC0) and
// 1442 (ADC1), 0-31 dB in 1 dB steps (p.34, p.36), clamped; 0 = none.
std::array<std::uint8_t, 1444> buildHighPriority(bool run, std::uint32_t ddc0FreqWord,
                                                  bool bypassAdc0Filters = true,
                                                  bool bypassAdc1Filters = true,
                                                  int adc0AttenuationDb = 0,
                                                  int adc1AttenuationDb = 0,
                                                  const HighPriorityOptions& opts = {}) noexcept;

// Multi-DDC form: one 4-byte frequency/phase word per DDC at 9 + 4*n (p.32),
// N capped at kMaxDdcs, excess ignored. Still RX-only (no PTT; byte 4 is bit[0]
// run only). Alex0/Alex1 are per-ADC, written as in the single-DDC overload.
std::array<std::uint8_t, 1444> buildHighPriority(bool run,
                                                  std::span<const std::uint32_t> ddcFreqWords,
                                                  bool bypassAdc0Filters = true,
                                                  bool bypassAdc1Filters = true,
                                                  int adc0AttenuationDb = 0,
                                                  int adc1AttenuationDb = 0,
                                                  const HighPriorityOptions& opts = {}) noexcept;

// The step attenuators' range (spec pp.34,36), shared by the encoder's clamp and
// by the backend that offers the control.
inline constexpr int kMaxStepAttenuationDb = 31;

// ---- DDC Audio, PC -> radio (spec p.36) ----
// The only outbound data path here. Fixed geometry: 64 stereo frames/packet,
// int16 big-endian L then R, 48 ksps -> 256 sample bytes + 4-byte BE sequence =
// 260 bytes every 1333 us. Independent of the DDC rate (this is demodulated
// audio); p2app DMAs fixed 256-byte blocks with no rate field.
inline constexpr int kSpeakerSampleRateHz = 48000;
inline constexpr int kSpeakerFramesPerPacket = 64;
inline constexpr int kSpeakerChannels = 2;
inline constexpr std::size_t kSpeakerSampleBytes = 256;  // 64 frames * 2ch * int16
inline constexpr std::size_t kSpeakerPacketBytes = 260;  // + seq(4)
inline constexpr int kSpeakerPacketIntervalUs = 1333;    // 64 / 48000

// Encode one speaker packet from host-order L,R,... int16. SHORT INPUT
// ZERO-FILLS (unlike parseDdcFrame()'s exact length): dropping the packet would
// stall the FIFO, and an unfilled tail would replay the previous 1.3 ms. Excess
// samples are ignored.
std::array<std::uint8_t, kSpeakerPacketBytes> buildSpeakerAudio(
    std::uint32_t sequence, std::span<const std::int16_t> interleavedLr) noexcept;

// ---- High Priority Status, radio -> PC (spec p.47) ----
// 60 bytes: 4-byte BE sequence, then hardware state. The two speaker-stream
// fields and the supply rail are decoded. Arrives on the same socket as DDC0 IQ (see
// kDdc0DefaultPort); parseDdcFrame() rejects it. p2app sends one every 200 ms
// while receiving (OutHighPriority.c).
inline constexpr std::size_t kHighPriorityStatusBytes = 60;

// Volts per count of HighPriorityStatus::supplyRailCounts: a (22+1)/1.1 divider
// into the 12-bit slow ADC at 5 V, as Thetis (console.cs convertToVolts) and
// deskHPSDR (rx_panadapter.c, Orion2/Saturn) scale the same input. Checked
// against a meter at one rail voltage on a G2: docs/architecture/radio-capabilities-map.md.
inline constexpr double kSupplyRailVoltsPerCount = (23.0 / 1.1) * (5.0 / 4095.0);

struct HighPriorityStatus {
    std::uint32_t seq = 0;
    // Byte 30 bit 3: the radio ran out of speaker audio. LATCHED by p2app since the
    // last status packet (~200 ms) and cleared on send.
    bool speakerUnderflow = false;
    // Bytes 37-38: speaker FIFO fill in FIFO LOCATIONS, not samples (p2app's
    // "2 samples per location" doubling never reaches the send, OutHighPriority.c).
    // Not comparable to kSpeakerFramesPerPacket; use as a trend.
    std::uint16_t speakerFifoLevel = 0;
    // Bytes 57-58: the DC supply rail in raw 12-bit counts. p2app labels it
    // "AIN3 user_analog1"; Thetis and deskHPSDR read the rail here. NOT bytes
    // 49-50 (AIN6), which the spec calls "supply voltage" but which fits no
    // published scale on a G2. No field in this payload is a temperature.
    std::uint16_t supplyRailCounts = 0;
};

// Decode, or nullopt if this is not a status packet. Bounds-checked
// (unauthenticated UDP); requires exactly 60 bytes. A Discovery reply is also 60 bytes
// (bytes 0-3 zero, byte 4 0x02/0x03) and can collide only at sequence 0, so
// callers must try the Discovery parse FIRST ON EVERY DATAGRAM, or a late or
// duplicate reply decodes as a phantom underflow (P2Client::handleDatagram()).
[[nodiscard]] std::optional<HighPriorityStatus> parseHighPriorityStatus(
    std::span<const std::uint8_t> data) noexcept;

// ---- DDC I&Q Data (spec p.53-54) ----

inline constexpr int kDdcHeaderLen = 16;   // seq(4) + timestamp(8) + bitsPerSample(2) + samplesPerFrame(2)
inline constexpr int kDdcSampleBytes = 6;  // 3-byte I + 3-byte Q

struct DdcFrame {
    std::uint32_t seq = 0;
    int samples = 0;
    // Exactly samples * kDdcSampleBytes bytes, big-endian 24-bit I then Q
    // per sample. Borrows from the `data` span passed to parseDdcFrame() --
    // must not outlive it.
    std::span<const std::uint8_t> iqRaw;
};

// Parse and STRICTLY VALIDATE a DDC I&Q datagram: nullopt unless bitsPerSample
// == 24 and data.size() == kDdcHeaderLen + samples * kDdcSampleBytes EXACTLY.
// Reject, never clamp: Mic Data (p.51, 64 mono samples, streamed whenever run=1)
// shares this port and would otherwise misdecode as tiny DDC frames with fake
// sequence gaps and sample rate.
std::optional<DdcFrame> parseDdcFrame(std::span<const std::uint8_t> data) noexcept;

// Decode one 24-bit signed big-endian I/Q sample pair (6 bytes) to normalized
// [-1, 1). BE only (see discoveryDeclaresBigEndian3Byte()).
std::complex<float> decodeIqSample(const std::uint8_t* be6) noexcept;

// Decode every sample in `frame.iqRaw` and append to `out`. Cannot fail --
// `frame` came from a successful parseDdcFrame(), whose exact-length check
// already guarantees `iqRaw.size() == frame.samples * kDdcSampleBytes`.
void decodeIq(const DdcFrame& frame, std::vector<std::complex<float>>& out);

}  // namespace AetherSDR::anan
