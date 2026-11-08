#pragma once

// ANAN port (build 194): openHPSDR Protocol 1 (the "old" / USB-over-UDP
// protocol) for the same ANAN boards with Protocol 1 firmware.
//
// The framing is the HL2's (hl2/MetisProtocol.h: discovery, start/stop, EP2
// packets, EP6 decode -- the same protocol). What is ANAN-specific is here,
// taken from Thetis (ChannelMaster/networkproto1.c, netInterface.c and
// console.cs UpdateDDCs):
//   - the 17 C&C registers the host sends in rotation (WriteMainLoop),
//   - the DDC plan per board (nddc, which DDC is RX1 / RX2),
//   - the telemetry the radio returns in the EP6 C&C bytes.
// P2Transport keeps one radio state for both protocols; only the wire differs
// (P2Transport.cpp, "Protocol 1").
//
// Socket- and Qt-free.

#include "core/backends/anan/P2Protocol.h"
#include "core/backends/hl2/MetisProtocol.h"

#include <array>
#include <complex>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace AetherSDR::anan {

// Discovery reply byte 10 (Protocol 1 board type). Not the Protocol 2 ids.
inline constexpr std::uint8_t kP1BoardMetis = 0;
inline constexpr std::uint8_t kP1BoardHermes = 1;      // ANAN-10 / ANAN-100 (also 10E / 100B)
inline constexpr std::uint8_t kP1BoardGriffin = 2;
inline constexpr std::uint8_t kP1BoardAngelia = 4;     // ANAN-100D
inline constexpr std::uint8_t kP1BoardOrion = 5;       // ANAN-200D
inline constexpr std::uint8_t kP1BoardHermesLite = 6;  // HL2: not driven by this backend
inline constexpr std::uint8_t kP1BoardOrion2 = 10;     // ANAN-7000DLE / 8000DLE / Orion MkII

// The Protocol 2 board id (P2Protocol.h) of a Protocol 1 board, for the board
// profile and the model list; nullopt for a board this backend does not drive.
[[nodiscard]] std::optional<std::uint8_t> p2BoardIdForP1(std::uint8_t p1Board) noexcept;

// Thetis UpdateDDCs (console.cs), Protocol 1: how many DDCs the radio runs
// and which carry the receivers.
//   ANGELIA / ORION / ORION MkII: nddc 5 -- RX1 = DDC0 (DDC1 its diversity /
//     sync partner), RX2 = DDC2, DDC3 / DDC4 the PureSignal pair.
//   HERMES (ANAN-10 / 100): nddc 4 -- RX1 = DDC0, RX2 = DDC1, DDC2 / DDC3 PS.
//   HERMES II (ANAN-10E / 100B): nddc 2 -- RX1 = DDC0, RX2 = DDC1.
struct P1DdcPlan {
    int nddc = 5;
    std::array<int, 2> rxDdc{{0, 2}};
};
[[nodiscard]] P1DdcPlan p1DdcPlanFor(std::uint8_t p2BoardId) noexcept;

// Everything the C&C rotation carries (Thetis prn / prbpfilter fields).
struct P1CcState {
    bool mox = false;
    int rateCode = 0;                      // 0..3 = 48/96/192/384 ksps, all DDCs
    int nddc = 5;
    std::array<std::uint32_t, 7> ddcHz{};  // DDC0..DDC6 NCO frequency, Hz
    std::uint32_t txHz = 0;
    std::uint8_t openCollector = 0;        // 7 bits (OC1..OC7)
    bool dither = false;
    bool random = false;
    int antenna = 0;                       // 0..2 = ANT1..ANT3 (the TX one while MOX)
    bool duplex = true;
    // 2 bits per DDC (DDC0 in bits 0-1): the ADC feeding it (Thetis
    // SetADC_cntrl_P1, "66554433221100").
    std::uint16_t adcControl = 0;
    int txAdc0AttDb = 0;                   // C0 0x1C C3: ADC0 attenuation while transmitting
    int driveLevel = 0;                    // 0..255
    bool micBoost = false;
    bool lineIn = false;
    // Alex, Protocol 1 bit order (Thetis SetAlexHPFBits / SetAlexLPFBits):
    // HPF 0x01 13 MHz, 0x02 20 MHz, 0x04 9.5 MHz, 0x08 6.5 MHz, 0x10 1.5 MHz,
    // 0x20 bypass, 0x40 6 m preamp; LPF 0x01 30/20, 0x02 60/40, 0x04 80,
    // 0x08 160, 0x10 6, 0x20 12/10, 0x40 17/15.
    std::uint8_t alexHpf = 0x20;
    std::uint8_t alexLpf = 0x10;
    std::uint8_t alex2Hpf = 0;             // ORION MkII: the ADC1 band-pass bank (C0 0x24)
    bool rx2GroundOnTx = false;
    bool paEnabled = false;
    std::uint8_t micControl = 0;           // C0 0x14 C1 bits 4-6: TRS, bias, PTT
    int lineInGain = 0;
    bool pureSignal = false;
    std::array<int, 3> rxAttDb{};          // step attenuator ADC0..ADC2, 0..31 dB
    // CW keyer (C0 0x16 / 0x1E / 0x20).
    bool cwKeyer = false;
    bool cwPaddleSwap = false;
    int cwMode = 2;                        // 0 straight, 1 iambic A, 2 iambic B
    int cwSpeedWpm = 20;
    int cwWeight = 50;
    bool cwStrictSpacing = false;
    int cwSidetoneLevel = 0;
    int cwRfDelayMs = 0;
    int cwHangMs = 250;
    int cwSidetoneHz = 700;
};

// Number of C&C registers in the rotation (Thetis cases 0..16).
inline constexpr int kP1CcCount = 17;

// The C&C bank `index` (0..16) for `s` (Thetis WriteMainLoop). C0 bit 0 = MOX.
[[nodiscard]] hl2::Cc p1Cc(int index, const P1CcState& s) noexcept;

// The Alex HPF byte (Protocol 1 order) for a receive filter selection.
[[nodiscard]] std::uint8_t p1AlexHpfBits(AlexRxFilter f) noexcept;
// The Alex LPF byte (Protocol 1 order) for a frequency (deskHPSDR edges, as
// legacyAlexLpfBitsFor()).
[[nodiscard]] std::uint8_t p1AlexLpfBitsFor(double hz) noexcept;

// What one EP6 frame's C&C bytes report (Thetis MetisReadThreadMainLoop).
struct P1Status {
    bool ptt = false;
    bool dash = false;
    bool dot = false;
    int address = 0;                      // C0 bits 3-7
    // Set only for the address that carries them.
    std::optional<int> exciterRaw, fwdRaw, revRaw, userAdc0Raw, userAdc1Raw, supplyRaw;
    unsigned overloadBits = 0;            // bit n = ADCn clipping in this frame
    std::optional<int> firmwareVersion;   // address 0, C4
};
// `frame` points at a 512-byte EP6 frame (7F 7F 7F C0..C4 ...).
[[nodiscard]] std::optional<P1Status> parseP1Status(const std::uint8_t* frame) noexcept;

// Microphone samples (48 kHz, [-1, 1)) in an EP6 packet: the 2-byte word that
// closes each round, one in `decimation` rounds (rate / 48 kHz). `phase`
// carries the decimation across packets. Appends to `out`; returns the count.
int p1MicSamples(std::span<const std::uint8_t> pkt, int nddc, int decimation, int& phase,
                 std::vector<float>& out) noexcept;

// Which protocol a radio answered discovery with, by its IPv4 address
// (host order): the discovery sweep and the transport's own probe record it,
// so the backend knows before a session that a radio is Protocol 1 (its DDCs
// stop at 384 ksps). Thread-safe.
void noteProtocol1Host(std::uint32_t ipv4, bool protocol1);
[[nodiscard]] bool isProtocol1Host(std::uint32_t ipv4);

// Transmit I/Q (build 197). The modulator (Hl2TxDsp) and the whole TX path
// run at the Protocol 2 DUC rate, 192 ksps; Protocol 1 carries TX I/Q at
// 48 ksps in the EP2 packets. 4:1 decimation, a 96-tap windowed-sinc low-pass
// (Blackman, cut-off 20 kHz: flat over any TX passband, -70 dB from 28 kHz),
// unity gain at DC. State carries across calls; reset() at the start of an over.
class P1TxDecimator {
public:
    static constexpr int kFactor = 4;
    static constexpr int kTaps = 96;
    P1TxDecimator();
    void reset() noexcept;
    // `in` holds n samples (n a multiple of 4); writes n / 4 to `out`.
    void process(const std::complex<float>* in, int n, std::complex<float>* out) noexcept;

private:
    std::array<float, kTaps> m_h{};
    std::array<std::complex<float>, 2 * kTaps> m_hist{};   // doubled ring: contiguous windows
    int m_pos = 0;
};

// IQ rounds per EP6 packet (both frames) for `nddc` DDCs.
[[nodiscard]] constexpr int p1RoundsPerPacket(int nddc) noexcept
{
    return 2 * static_cast<int>(hl2::kFramePayload / hl2::ep6RoundBytes(nddc));
}

}  // namespace AetherSDR::anan
