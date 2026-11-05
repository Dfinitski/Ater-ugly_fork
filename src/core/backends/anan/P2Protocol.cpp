#include "core/backends/anan/P2Protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace AetherSDR::anan {

namespace {

std::uint16_t readU16be(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint32_t readU32be(const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8)  |  static_cast<std::uint32_t>(p[3]);
}

void writeU16be(std::uint8_t* p, std::uint16_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<std::uint8_t>(v & 0xFF);
}

void writeU32be(std::uint8_t* p, std::uint32_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<std::uint8_t>(v & 0xFF);
}

// Sign-extend a 24-bit two's-complement value carried in the low 24 bits of
// a 32-bit int.
int signExtend24(std::int32_t raw) noexcept
{
    if (raw & 0x800000)
        raw -= (1 << 24);
    return raw;
}

}  // namespace

std::optional<BoardProfile> boardProfileFor(std::uint8_t boardId) noexcept
{
    BoardProfile p;
    p.boardId = boardId;
    switch (boardId) {
    case kBoardIdAngelia:
        p.modelName = "ANAN-100D";
        return p;   // the defaults ARE the ANGELIA profile
    case kBoardIdOrion:
        p.modelName = "ANAN-200D";
        return p;   // same front end and DDC plan as ANGELIA
    // ANAN-100D port: SATURN (ANAN-G2) support removed with its backend.
    default:
        return std::nullopt;
    }
}

BoardProfile defaultBoardProfile() noexcept
{
    return *boardProfileFor(kBoardIdAngelia);
}

std::array<std::uint8_t, 60> buildDiscovery() noexcept
{
    std::array<std::uint8_t, 60> pkt{};
    pkt[4] = 0x02;  // Discovery Command (spec p.42-43)
    return pkt;
}

std::optional<DiscoveryReply> parseDiscoveryReply(std::span<const std::uint8_t> data) noexcept
{
    // Bounds-check BEFORE indexing (Principle VII) -- this parses
    // unauthenticated UDP. 24 bytes covers the header through byte 23.
    if (data.size() < 24)
        return std::nullopt;
    if (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 0)
        return std::nullopt;
    if (data[4] != 0x02 && data[4] != 0x03)
        return std::nullopt;

    DiscoveryReply r;
    r.streaming = data[4] == 0x03;
    for (std::size_t i = 0; i < r.mac.size(); ++i)
        r.mac[i] = data[5 + i];
    r.boardId = data[11];
    r.protoVersionRaw = data[12];
    r.firmwareVer = data[13];
    r.numDdc = data[20];
    r.freqIsPhaseWord = data[21] != 0;
    r.endianByteRaw = data[22];
    r.p2appBuild = data[23];
    return r;
}

std::vector<float> parseMicPacket(std::span<const std::uint8_t> bytes)
{
    std::vector<float> out;
    if (bytes.size() != kMicPacketBytes)
        return out;
    out.resize(kCodecFramesPerPacket);
    for (int i = 0; i < kCodecFramesPerPacket; ++i) {
        const auto s = static_cast<std::int16_t>(
            (static_cast<std::uint16_t>(bytes[4 + 2 * i]) << 8) | bytes[4 + 2 * i + 1]);
        out[static_cast<std::size_t>(i)] = static_cast<float>(s) / 32768.0f;
    }
    return out;
}

std::uint8_t cwModeControlByte(const CwKeyerOptions& k) noexcept
{
    if (!k.enabled)
        return 0x00;
    std::uint8_t b = 0x02;                        // bit 1: CW keyer on
    if (k.paddleSwap)        b |= 0x04;           // bit 2: reverse paddles
    if (k.mode >= 1)         b |= 0x08;           // bit 3: iambic (else straight key)
    if (k.sidetoneLevel > 0) b |= 0x10;           // bit 4: sidetone
    if (k.mode == 2)         b |= 0x20;           // bit 5: mode B
    if (k.strictSpacing)     b |= 0x40;           // bit 6: strict character spacing
    if (k.breakIn)           b |= 0x80;           // bit 7: break-in
    return b;
}

std::array<std::uint8_t, 60> buildTransmitSpecific(std::uint32_t seq, bool micBoost,
                                                   const CwKeyerOptions& cw,
                                                   int txAttenuationDb,
                                                   int psFeedbackAttDb,
                                                   int psFeedbackAdc) noexcept
{
    std::array<std::uint8_t, 60> pkt{};
    writeU32be(&pkt[0], seq);
    pkt[4] = 1;      // one DAC
    // 5..13, 17: the FPGA CW keyer (Thetis CmdTx layout).
    pkt[5] = cwModeControlByte(cw);
    if (cw.enabled) {
        const int st = std::clamp(cw.sidetoneHz, 100, 3000);
        const int hang = std::clamp(cw.hangMs, 0, 0xFFFF);
        pkt[6] = static_cast<std::uint8_t>(std::clamp(cw.sidetoneLevel, 0, 255));
        pkt[7] = static_cast<std::uint8_t>((st >> 8) & 0xFF);
        pkt[8] = static_cast<std::uint8_t>(st & 0xFF);
        pkt[9] = static_cast<std::uint8_t>(std::clamp(cw.speedWpm, 1, 99));
        pkt[10] = static_cast<std::uint8_t>(std::clamp(cw.weight, 0, 100));
        pkt[11] = static_cast<std::uint8_t>((hang >> 8) & 0xFF);
        pkt[12] = static_cast<std::uint8_t>(hang & 0xFF);
    }
    // RF Delay: a transmitter setting (TX applet), sent in every mode.
    pkt[13] = static_cast<std::uint8_t>(std::clamp(cw.rfDelayMs, 0, 255));
    pkt[17] = static_cast<std::uint8_t>(std::clamp(cw.edgeMs, 0, 255));
    // 14..16: DUC rate / bits -- fixed 192 ksps / 24 bit on this hardware.
    // mic: no line-in (bit 0), boost +20 dB (bit 1) on request, mic PTT
    // ENABLED (bit 2 = 0).
    pkt[50] = micBoost ? 0x02 : 0x00;
    pkt[51] = 0;     // line-in gain
    // 57..59: step attenuator of ADC2 / ADC1 / ADC0 during transmit
    // (Thetis CmdTx, SetTxAttenData; default 31 there).
    const auto att = static_cast<std::uint8_t>(std::clamp(txAttenuationDb, 0, 31));
    pkt[57] = att;
    pkt[58] = att;
    pkt[59] = att;
    if (psFeedbackAttDb >= 0)
        pkt[psFeedbackAdc == 1 ? 58 : 59] = static_cast<std::uint8_t>(std::clamp(psFeedbackAttDb, 0, 31));
    return pkt;
}

std::array<std::uint8_t, 1444> buildDucIq(std::uint32_t seq,
                                          std::span<const std::complex<float>> iq) noexcept
{
    std::array<std::uint8_t, 1444> pkt{};
    writeU32be(&pkt[0], seq);
    const std::size_t n = std::min(iq.size(), static_cast<std::size_t>(kDucSamplesPerPacket));
    auto put24 = [](std::uint8_t* p, float v) {
        const float c = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
        const auto s = static_cast<std::int32_t>(std::lround(c * static_cast<float>(kFullScale24Bit)));
        p[0] = static_cast<std::uint8_t>((s >> 16) & 0xFF);
        p[1] = static_cast<std::uint8_t>((s >> 8) & 0xFF);
        p[2] = static_cast<std::uint8_t>(s & 0xFF);
    };
    for (std::size_t i = 0; i < n; ++i) {
        put24(&pkt[4 + 6 * i], iq[i].real());
        put24(&pkt[4 + 6 * i + 3], iq[i].imag());
    }
    return pkt;
}

std::array<std::uint8_t, 60> buildGeneral(std::uint8_t alexEnableMask, bool paEnable,
                                          const WidebandOptions& wideband) noexcept
{
    std::array<std::uint8_t, 60> pkt{};
    // Bytes 40/41 stay 0: the RGMII delay control of the test firmware v4..v7
    // is gone in v8 (the delays are fixed in the firmware).
    pkt[58] = paEnable ? 0x01 : 0x00;   // bit[0] = PA enable (spec p.21)
    pkt[4] = 0x00;  // Command byte for the General Packet (spec p.19)
    // Bytes 5-22 (port table) left zero: "if set to zero the default port
    // will be used" (p.19-20). No renegotiation in Phase 1b.
    pkt[37] = 0x08; // bit[3]: DDC/DUC frequency as PHASE WORD (p.21) --
                     // matches Discovery byte 21 == 1, not a guess.
    pkt[38] = 0x01; // bit[0]: hardware reset timer / watchdog ON (p.8, p.21).
                     // See buildGeneral()'s header comment for why this stays
                     // on deliberately.
    pkt[39] = 0x00; // BE + 3-byte -- the only format Discovery byte22==0
                     // declares (p.22, p.45). See discoveryDeclaresBigEndian3Byte().
    pkt[59] = alexEnableMask; // bit[n] enables Alex board n (spec p.16, App. D)
    // Wideband (bytes 23..28); all zero while off, as before.
    if (wideband.enableMask != 0) {
        pkt[23] = wideband.enableMask;
        writeU16be(&pkt[24], static_cast<std::uint16_t>(kWidebandSamplesPerPacket));
        pkt[26] = 16;
        pkt[27] = wideband.updateMs;
        pkt[28] = wideband.packetsPerFrame;
    }
    return pkt;
}

bool parseWidebandPacket(std::span<const std::uint8_t> data, std::uint32_t& seq,
                         float* samples512) noexcept
{
    if (data.size() != static_cast<std::size_t>(kWidebandPacketBytes) || !samples512)
        return false;
    seq = readU32be(&data[0]);
    constexpr float kScale = 1.0f / 32768.0f;
    for (int i = 0; i < kWidebandSamplesPerPacket; ++i) {
        const auto hi = static_cast<std::uint16_t>(data[4 + 2 * i]);
        const auto lo = static_cast<std::uint16_t>(data[5 + 2 * i]);
        samples512[i] = static_cast<float>(static_cast<std::int16_t>((hi << 8) | lo)) * kScale;
    }
    return true;
}

std::array<std::uint8_t, 1444> buildDdcSpecific(std::span<const DdcConfig> ddcs, int numAdcs,
                                                bool ditherEnabled, bool randomEnabled,
                                                int hwDdcBase, const PsDdcOptions& ps) noexcept
{
    std::array<std::uint8_t, 1444> pkt{};  // full spec length (80 DDCs); unused DDCs
                                            // stay zero/disabled
    pkt[4] = static_cast<std::uint8_t>(numAdcs);  // "number of ADCs the hardware
                                                   // supports" (p.25)
    // Bytes 5/6: one Dither/Random control on this radio, not a per-ADC
    // pair, so both ADC0 and ADC1 bits track it together regardless of
    // which ADC any given DDC actually selects.
    pkt[5] = ditherEnabled ? 0x03 : 0x00;
    pkt[6] = randomEnabled ? 0x03 : 0x00;

    // Clamp rather than trust the caller: writing rows for more DDCs than
    // kMaxDdcs would keep landing inside this 1444-byte packet (the spec has
    // room for 80), so this is not a buffer guard -- it is a refusal to
    // enable hardware receivers the rest of this backend is not driving.
    const std::size_t count =
        std::min(ddcs.size(), static_cast<std::size_t>(kMaxDdcs));
    // Byte 7 holds the enable bits for DDC0..7; the base keeps every enabled
    // DDC inside it (rxDdcBase 2 + kMaxDdcs 4 = DDC5 at most).
    const std::size_t base = static_cast<std::size_t>(std::clamp(hwDdcBase, 0, 8 - kMaxDdcs));

    std::uint8_t enableBits = 0;
    for (std::size_t n = 0; n < count; ++n) {
        const std::size_t hw = base + n;
        enableBits |= static_cast<std::uint8_t>(1u << hw);
        // One 6-byte row per DDC: ADC select, 2-byte rate, CIC1, CIC2,
        // sample size. Offsets confirmed against the single-DDC layout this
        // replaced (DDC0: 17 / 18-19 / 20-21 / 22), which is bench-proven.
        const std::size_t row = 17 + 6 * hw;
        pkt[row] = static_cast<std::uint8_t>(ddcs[n].adcIndex);
        writeU16be(&pkt[row + 1], static_cast<std::uint16_t>(ddcs[n].rateKsps));
        // row+3, row+4 = CIC1/CIC2, "For Future use" (p.25) -- left zero.
        pkt[row + 5] = 24;   // sample size, 24 bits (default, made explicit)
    }
    if (ps.active) {
        // DDC0 = feedback ADC, DDC1 = the TX DAC (ADC index numAdcs),
        // 192 ksps, 24 bit; DDC1 synchronised to DDC0; only DDC0 enabled.
        // feedbackAdc == numAdcs: DDC0 on the DAC too (firmware loopback test).
        pkt[17] = static_cast<std::uint8_t>(std::clamp(ps.feedbackAdc, 0, numAdcs));
        writeU16be(&pkt[18], static_cast<std::uint16_t>(kPsFeedbackRateKsps));
        pkt[22] = 24;
        pkt[23] = static_cast<std::uint8_t>(numAdcs);
        writeU16be(&pkt[24], static_cast<std::uint16_t>(kPsFeedbackRateKsps));
        pkt[28] = 24;
        pkt[1363] = 0x02;
        enableBits = static_cast<std::uint8_t>(enableBits | 0x01);
    }
    pkt[7] = enableBits;   // bit[n] enables hardware DDC n
    return pkt;
}

bool decodePsPairs(std::span<const std::uint8_t> data, std::vector<double>& tx,
                   std::vector<double>& rx) noexcept
{
    const auto frame = parseDdcFrame(data);
    if (!frame)
        return false;
    const std::size_t samples = frame->samples;
    const std::uint8_t* p = frame->iqRaw.data();
    constexpr double kScale = 1.0 / 8388608.0;
    auto s24 = [](const std::uint8_t* b) {
        std::int32_t v = (std::int32_t(b[0]) << 16) | (std::int32_t(b[1]) << 8) | std::int32_t(b[2]);
        if (v & 0x800000)
            v -= 0x1000000;
        return v;
    };
    for (std::size_t i = 0; i + 1 < samples; i += 2) {
        const std::uint8_t* d0 = p + 6 * i;        // DDC0: feedback
        const std::uint8_t* d1 = p + 6 * (i + 1);  // DDC1: TX DAC
        rx.push_back(s24(d0) * kScale);
        rx.push_back(s24(d0 + 3) * kScale);
        tx.push_back(s24(d1) * kScale);
        tx.push_back(s24(d1 + 3) * kScale);
    }
    return true;
}

std::array<std::uint8_t, 1444> buildDdcSpecific(int ddc0RateKsps, int numAdcs,
                                                bool ditherEnabled, bool randomEnabled,
                                                int ddc0AdcIndex) noexcept
{
    // Delegates so the row layout has exactly one implementation; the
    // single-DDC output stays byte-identical to what this function built
    // before the multi-DDC overload existed (pinned by the existing tests).
    const DdcConfig one{ddc0RateKsps, ddc0AdcIndex};
    return buildDdcSpecific(std::span<const DdcConfig>(&one, 1),
                            numAdcs, ditherEnabled, randomEnabled);
}

std::uint16_t alexRxFilterBits(AlexRxFilter filter) noexcept
{
    // Bits from the spec's SATURN/ANAN-G2 Alex0 and Alex1 tables (p.91). The
    // shift yields unsigned int; the casts narrow it to the 16-bit word.
    switch (filter) {
    case AlexRxFilter::Bypass:    return std::uint16_t(1u << 12);
    case AlexRxFilter::Bpf1_5MHz: return std::uint16_t(1u << 6);
    case AlexRxFilter::Bpf6_5MHz: return std::uint16_t(1u << 5);
    case AlexRxFilter::Bpf9_5MHz: return std::uint16_t(1u << 4);
    case AlexRxFilter::Bpf13MHz:  return std::uint16_t(1u << 1);
    case AlexRxFilter::Bpf20MHz:  return std::uint16_t(1u << 2);
    case AlexRxFilter::Preamp6m:  return std::uint16_t(1u << 3);
    }
    // Unreachable for a valid enumerator. Bypass, not zero: zero energises no
    // relay, and with the bank in series that is no signal path (p.90).
    return std::uint16_t(1u << 12);
}

AlexRxFilter legacyAlexHpfFor(double tunedHz) noexcept
{
    const double mhz = tunedHz / 1e6;
    if (!std::isfinite(mhz) || mhz < 1.8) {
        return AlexRxFilter::Bypass;
    }
    if (mhz < 6.5) {
        return AlexRxFilter::Bpf1_5MHz;   // bit 6: 1.5 MHz HPF
    }
    if (mhz < 9.5) {
        return AlexRxFilter::Bpf6_5MHz;   // bit 5: 6.5 MHz HPF
    }
    if (mhz < 13.0) {
        return AlexRxFilter::Bpf9_5MHz;   // bit 4: 9.5 MHz HPF
    }
    if (mhz < 20.0) {
        return AlexRxFilter::Bpf13MHz;    // bit 1: 13 MHz HPF
    }
    // ANAN-100D port (operator, 31.10): 6 m used to select bit 3, the "6M
    // Preamp", as its filter -- the LNA came on by itself on 6 m and the LNA
    // button could not turn it off. 6 m keeps a filter in circuit (the 20 MHz
    // HPF, the highest section, which passes 6 m) and the preamp is the LNA
    // button's alone (HighPriorityOptions::alexPreamp, the LNA bit).
    return AlexRxFilter::Bpf20MHz;        // bit 2: 20 MHz HPF (also 6 m)
}

std::uint32_t legacyAlexLpfBitsFor(double hz) noexcept
{
    constexpr std::uint32_t kLpf6Bypass = std::uint32_t{1} << 29;
    constexpr std::uint32_t kLpf12_10   = std::uint32_t{1} << 30;
    constexpr std::uint32_t kLpf17_15   = std::uint32_t{1} << 31;
    constexpr std::uint32_t kLpf30_20   = std::uint32_t{1} << 20;
    constexpr std::uint32_t kLpf60_40   = std::uint32_t{1} << 21;
    constexpr std::uint32_t kLpf80      = std::uint32_t{1} << 22;
    constexpr std::uint32_t kLpf160     = std::uint32_t{1} << 23;
    if (!std::isfinite(hz) || hz > 35'600'000.0) {
        return kLpf6Bypass;
    }
    if (hz > 24'000'000.0) {
        return kLpf12_10;
    }
    if (hz > 16'500'000.0) {
        return kLpf17_15;
    }
    if (hz > 8'000'000.0) {
        return kLpf30_20;
    }
    if (hz > 5'000'000.0) {
        return kLpf60_40;
    }
    if (hz > 2'500'000.0) {
        return kLpf80;
    }
    return kLpf160;
}

AlexRxFilter alexRxFilterFor(double tunedHz) noexcept
{
    const double mhz = tunedHz / 1e6;
    // Every comparison with NaN is false, so a non-finite frequency would fall
    // through to the 6 m preamp, which on HF is silence.
    if (!std::isfinite(mhz)) {
        return AlexRxFilter::Bypass;
    }
    if (mhz < 1.5) {
        return AlexRxFilter::Bypass;      // below the bank's lowest filter
    }
    if (mhz < 2.1) {
        return AlexRxFilter::Bpf1_5MHz;   // 160 m
    }
    if (mhz < 5.5) {
        return AlexRxFilter::Bpf6_5MHz;   // 80 m, 60 m
    }
    if (mhz < 11.0) {
        return AlexRxFilter::Bpf9_5MHz;   // 40 m, 30 m
    }
    if (mhz < 22.0) {
        return AlexRxFilter::Bpf13MHz;    // 20 m, 17 m, 15 m
    }
    if (mhz < 35.0) {
        return AlexRxFilter::Bpf20MHz;    // 12 m, 10 m
    }
    return AlexRxFilter::Preamp6m;        // 6 m
}

std::array<std::uint8_t, 1444> buildHighPriority(bool run,
                                                  std::span<const std::uint32_t> ddcFreqWords,
                                                  bool bypassAdc0Filters,
                                                  bool bypassAdc1Filters,
                                                  int adc0AttenuationDb,
                                                  int adc1AttenuationDb,
                                                  const HighPriorityOptions& opts) noexcept
{
    std::array<std::uint8_t, 1444> pkt{};  // full spec length
    pkt[4] = run ? 0x01 : 0x00;  // bit[0] = run
    // ANAN-100D port: PTT0 (bit 1), DUC0 phase word, drive level.
    if (run && opts.transmitting)
        pkt[4] |= 0x02;
    writeU32be(&pkt[329], opts.ducPhaseWord);
    pkt[345] = static_cast<std::uint8_t>(std::clamp(opts.driveLevel, 0, 255));
    // One 4-byte frequency/phase word per DDC at 9 + 4*n (p.32). Same
    // clamp-and-ignore contract as buildDdcSpecific().
    const std::size_t count =
        std::min(ddcFreqWords.size(), static_cast<std::size_t>(kMaxDdcs));
    const std::size_t base =
        static_cast<std::size_t>(std::clamp(opts.hwDdcBase, 0, 8 - kMaxDdcs));
    for (std::size_t n = 0; n < count; ++n)
        writeU32be(&pkt[9 + 4 * (base + n)], ddcFreqWords[n]);
    if (opts.mirrorRxWordsBelowBase && count > 0) {
        for (std::size_t n = 0; n < base; ++n)
            writeU32be(&pkt[9 + 4 * n], ddcFreqWords[std::min(n, count - 1)]);
    }
    // PureSignal while transmitting: DDC0 and DDC1 on the transmit frequency.
    if (run && opts.transmitting && opts.psEnabled) {
        writeU32be(&pkt[9], opts.ducPhaseWord);
        writeU32be(&pkt[13], opts.ducPhaseWord);
    }

    if (opts.alexScheme == AlexScheme::LegacyHighPassLowPass) {
        // ANAN-100/100D/200D (deskHPSDR new_protocol.c, default case; alex.h).
        // Alex0 = the receive state: ANT1 (bit 24), the ADC0 high-pass
        // selection, and the TX low-pass filter the Ant1/2/3 receive path
        // runs through. A bypassed ADC0 also takes the widest LPF.
        std::uint32_t alex0 = 0;
        bool adc0Bypassed = false;
        if (opts.adc0Filter) {
            alex0 |= alexRxFilterBits(*opts.adc0Filter);
            adc0Bypassed = *opts.adc0Filter == AlexRxFilter::Bypass;
        } else if (bypassAdc0Filters) {
            alex0 |= std::uint32_t{1} << 12;
            adc0Bypassed = true;
        }
        const std::uint32_t lpf = adc0Bypassed
            ? legacyAlexLpfBitsFor(std::numeric_limits<double>::infinity())
            : legacyAlexLpfBitsFor(opts.rxLpfFrequencyHz);
        // The transmit LPF follows the TX frequency (the PA's harmonic filter:
        // never the receive one while transmitting).
        const double txHz = opts.txFrequencyHz > 0.0 ? opts.txFrequencyHz
                                                     : opts.rxLpfFrequencyHz;
        const std::uint32_t txLpf = legacyAlexLpfBitsFor(txHz);
        const std::uint32_t relay = opts.paEnabled ? kAlexTxRelay : 0u;
        const std::uint32_t txAnt = legacyAlexAntennaBit(opts.txAntenna);
        if (run && opts.transmitting) {
            alex0 |= txAnt | txLpf | relay;
            if (opts.psEnabled) {
                alex0 |= kAlexPsBit;
                if (opts.psFeedbackSource == 1)
                    alex0 |= kAlexRxExt1 | kAlexRxBypass;   // EXT1 (old PA board)
                else if (opts.psFeedbackSource == 2)
                    alex0 |= kAlexRxBypass;                 // RX bypass input
            }
        } else
            alex0 |= legacyAlexAntennaBit(opts.rxAntenna) | lpf
                     | (opts.alexPreamp ? std::uint32_t{1} << 3 : 0u);
        writeU32be(&pkt[1432], alex0);
        // Alex1 = the TX state ("Jan 2023 protocol update", new_protocol.c):
        // TX antenna, the LPF for the transmit frequency, the T/R relay.
        writeU32be(&pkt[1428], txAnt | txLpf | relay | (opts.psEnabled ? kAlexPsBit : 0u));
        // No ADC1 filter bank, so bypassAdc1Filters/adc1Filter have nothing
        // to drive; no speaker amplifier mute bit on these boards.
        if (opts.muteSpeakerAmp && opts.boardHasSpeakerAmpMute) {
            pkt[1400] = std::uint8_t(1u << 1);
        }
        if (opts.tunerStart)
            pkt[1400] |= std::uint8_t(1u << 2);
        // User Out / open collectors (spec p.27, Thetis network.c).
        pkt[1401] = std::uint8_t((opts.openCollector << 1) & 0xfe);
        pkt[1443] = static_cast<std::uint8_t>(
            std::clamp(adc0AttenuationDb, 0, kMaxStepAttenuationDb));
        pkt[1442] = static_cast<std::uint8_t>(
            std::clamp(adc1AttenuationDb, 0, kMaxStepAttenuationDb));
        return pkt;
    }

    // Alex0, bytes 1432-1435: ANT1 (bit 24) in the TX/antenna half, ADC0's
    // receive selection in the low half. A filter replaces bypass: the series
    // bank takes one relay. See this function's declaration comment.
    std::uint32_t alex0 = std::uint32_t{1} << 24;
    if (opts.adc0Filter) {
        alex0 |= alexRxFilterBits(*opts.adc0Filter);
    } else if (bypassAdc0Filters) {
        alex0 |= std::uint32_t{1} << 12;
    }
    writeU32be(&pkt[1432], alex0);
    // Alex1 (RX2/BPF2), bytes 1430-1431: ADC1's receive word, the same bits
    // (p.91's Alex1 table).
    if (opts.adc1Filter) {
        writeU16be(&pkt[1430], alexRxFilterBits(*opts.adc1Filter));
    } else if (bypassAdc1Filters) {
        writeU16be(&pkt[1430], std::uint16_t{1} << 12);
    }
    // Byte 1400 bit 1 mutes the speaker amplifier; bits 0 and 2 stay clear
    // (HighPriorityOptions::muteSpeakerAmp).
    if (opts.muteSpeakerAmp) {
        pkt[1400] = std::uint8_t(1u << 1);
    }
    if (opts.tunerStart)
        pkt[1400] |= std::uint8_t(1u << 2);
    pkt[1401] = std::uint8_t((opts.openCollector << 1) & 0xfe);
    // Step attenuators, pp.34,36: byte 1443 = ADC0, byte 1442 = ADC1, 0-31 dB.
    pkt[1443] = static_cast<std::uint8_t>(
        std::clamp(adc0AttenuationDb, 0, kMaxStepAttenuationDb));
    pkt[1442] = static_cast<std::uint8_t>(
        std::clamp(adc1AttenuationDb, 0, kMaxStepAttenuationDb));
    return pkt;
}

std::array<std::uint8_t, 1444> buildHighPriority(bool run, std::uint32_t ddc0FreqWord,
                                                  bool bypassAdc0Filters,
                                                  bool bypassAdc1Filters,
                                                  int adc0AttenuationDb,
                                                  int adc1AttenuationDb,
                                                  const HighPriorityOptions& opts) noexcept
{
    // Delegates, same reasoning as buildDdcSpecific()'s single-DDC overload:
    // one implementation of the row layout, and byte-identical output to
    // what this built before (pinned by the existing tests).
    return buildHighPriority(run, std::span<const std::uint32_t>(&ddc0FreqWord, 1),
                             bypassAdc0Filters, bypassAdc1Filters,
                             adc0AttenuationDb, adc1AttenuationDb, opts);
}

std::array<std::uint8_t, kSpeakerPacketBytes> buildSpeakerAudio(
    std::uint32_t sequence, std::span<const std::int16_t> interleavedLr) noexcept
{
    std::array<std::uint8_t, kSpeakerPacketBytes> pkt{};
    writeU32be(pkt.data(), sequence);
    // Zero-initialised above, so the short-input case needs no separate fill
    // path -- everything past `count` is already silence. See the header for why
    // silence is the right answer here and a refusal is not.
    constexpr std::size_t kMaxSamples = kSpeakerSampleBytes / sizeof(std::int16_t);
    const std::size_t count = std::min(interleavedLr.size(), kMaxSamples);
    for (std::size_t i = 0; i < count; ++i) {
        // Reinterpreted through the unsigned type, not shifted as signed: a
        // right shift of a negative value is implementation-defined, and every
        // sample below -1 dBFS is negative half the time. The two's-complement
        // bit pattern is what the wire wants and what the cast produces.
        writeU16be(pkt.data() + 4 + i * sizeof(std::int16_t),
                   static_cast<std::uint16_t>(interleavedLr[i]));
    }
    return pkt;
}

std::optional<HighPriorityStatus> parseHighPriorityStatus(
    std::span<const std::uint8_t> data) noexcept
{
    if (data.size() != kHighPriorityStatusBytes)
        return std::nullopt;
    HighPriorityStatus s;
    s.seq = readU32be(data.data());
    s.speakerUnderflow = (data[30] & 0b0000'1000) != 0;
    s.speakerFifoLevel = readU16be(data.data() + 37);
    // In bounds by the exact-size guard above, which admits only 60 bytes.
    s.supplyRailCounts = readU16be(data.data() + 57);
    return s;
}

std::optional<int> ddcIndexForSenderPort(std::uint16_t senderPort, int numDdc,
                                          std::uint16_t basePort) noexcept
{
    if (numDdc <= 0 || senderPort < basePort)
        return std::nullopt;
    // Widened to int before subtracting: both operands are uint16_t, and the
    // guard above already rules out the wrap case, but doing the arithmetic
    // in int keeps that correctness local rather than resting on the guard.
    const int index = static_cast<int>(senderPort) - static_cast<int>(basePort);
    if (index >= numDdc)
        return std::nullopt;
    return index;
}

std::optional<DdcFrame> parseDdcFrame(std::span<const std::uint8_t> data) noexcept
{
    // Bounds-check BEFORE indexing (Principle VII) -- the declared sample
    // count is used to REJECT a malformed/foreign datagram below, never to
    // index past what the buffer actually contains.
    if (data.size() < static_cast<std::size_t>(kDdcHeaderLen))
        return std::nullopt;

    const std::uint32_t seq = readU32be(&data[0]);
    // bytes 4-11 (timestamp) are unused in Phase 1b -- header field only, p.54
    const std::uint16_t bitsPerSample = readU16be(&data[12]);
    const std::uint16_t samplesDeclared = readU16be(&data[14]);

    // STRICT shape check, not a clamp -- see this function's declaration
    // comment in P2Protocol.h for why.
    const std::size_t expectedLen = static_cast<std::size_t>(kDdcHeaderLen)
        + static_cast<std::size_t>(samplesDeclared) * static_cast<std::size_t>(kDdcSampleBytes);
    if (bitsPerSample != 24 || data.size() != expectedLen)
        return std::nullopt;

    DdcFrame frame;
    frame.seq = seq;
    frame.samples = samplesDeclared;
    frame.iqRaw = data.subspan(static_cast<std::size_t>(kDdcHeaderLen),
                               data.size() - static_cast<std::size_t>(kDdcHeaderLen));
    return frame;
}

std::complex<float> decodeIqSample(const std::uint8_t* be6) noexcept
{
    const int iRaw = (static_cast<std::int32_t>(be6[0]) << 16)
                    | (static_cast<std::int32_t>(be6[1]) << 8)
                    |  static_cast<std::int32_t>(be6[2]);
    const int qRaw = (static_cast<std::int32_t>(be6[3]) << 16)
                    | (static_cast<std::int32_t>(be6[4]) << 8)
                    |  static_cast<std::int32_t>(be6[5]);
    const float scale = 1.0f / static_cast<float>(kFullScale24Bit);
    return {static_cast<float>(signExtend24(iRaw)) * scale,
            static_cast<float>(signExtend24(qRaw)) * scale};
}

void decodeIq(const DdcFrame& frame, std::vector<std::complex<float>>& out)
{
    out.reserve(out.size() + static_cast<std::size_t>(frame.samples));
    for (int i = 0; i < frame.samples; ++i)
        out.push_back(decodeIqSample(&frame.iqRaw[static_cast<std::size_t>(i) * kDdcSampleBytes]));
}

}  // namespace AetherSDR::anan
