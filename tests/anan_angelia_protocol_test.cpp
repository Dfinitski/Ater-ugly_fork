// ANAN-100D (ANGELIA) packet layout checks for the ANAN-100D port.
//
// Standalone (no Qt): P2Protocol.cpp only. Expected values are derived from
// deskHPSDR new_protocol.c / alex.h (the default "ANAN-100/200" case) and the
// openHPSDR Ethernet Protocol spec (General byte 59, Appendix A/D).

#include "core/backends/anan/P2Protocol.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string>

using namespace AetherSDR::anan;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

std::uint32_t be32(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16)
         | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

std::uint16_t be16(const std::uint8_t* p)
{
    return std::uint16_t((std::uint16_t(p[0]) << 8) | p[1]);
}

constexpr std::uint32_t kAnt1 = 1u << 24;
constexpr std::uint32_t kHpfBypass = 1u << 12;
constexpr std::uint32_t kHpf1_5 = 1u << 6;
constexpr std::uint32_t kHpf6_5 = 1u << 5;
constexpr std::uint32_t kHpf9_5 = 1u << 4;
constexpr std::uint32_t kHpf13 = 1u << 1;
constexpr std::uint32_t kHpf20 = 1u << 2;
constexpr std::uint32_t kPre6m = 1u << 3;
constexpr std::uint32_t kLpf30_20 = 1u << 20;
constexpr std::uint32_t kLpf60_40 = 1u << 21;
constexpr std::uint32_t kLpf80 = 1u << 22;
constexpr std::uint32_t kLpf160 = 1u << 23;
constexpr std::uint32_t kLpf6 = 1u << 29;
constexpr std::uint32_t kLpf12_10 = 1u << 30;
constexpr std::uint32_t kLpf17_15 = 1u << 31;

HighPriorityOptions angeliaOpts(double rxHz, bool filters)
{
    const BoardProfile b = *boardProfileFor(kBoardIdAngelia);
    HighPriorityOptions o;
    o.hwDdcBase = b.rxDdcBase;
    o.alexScheme = b.alexScheme;
    o.rxLpfFrequencyHz = rxHz;
    o.boardHasSpeakerAmpMute = b.hasSpeakerAmpMute;
    o.boardHasAdc1FilterBank = b.hasAdc1FilterBank;
    if (filters) {
        o.adc0Filter = legacyAlexHpfFor(rxHz);
    }
    return o;
}

}  // namespace

int main()
{
    // ---- profiles ----
    {
        const auto a = boardProfileFor(kBoardIdAngelia);
        check(a.has_value(), "ANGELIA has a profile");
        check(a && a->rxDdcBase == 2, "ANGELIA receives on DDC2");
        check(a && a->alexScheme == AlexScheme::LegacyHighPassLowPass, "ANGELIA legacy Alex");
        check(a && a->alexEnableMask == 0x01, "ANGELIA enables Alex0");
        check(a && !a->hasSpeakerAmpMute, "ANGELIA has no speaker amp mute");
        check(a && std::string(a->modelName) == "ANAN-100D", "ANGELIA model name");
        check(!boardProfileFor(kBoardIdSaturn).has_value(), "SATURN (ANAN-G2) is not driven");
        check(!boardProfileFor(1).has_value(), "HERMES (board 1) is not driven");
        check(!boardProfileFor(6).has_value(), "Hermes-Lite (board 6) is not driven");
        check(defaultBoardProfile().boardId == kBoardIdAngelia, "default is the ANAN-100D");
    }

    // ---- discovery predicate ----
    {
        std::array<std::uint8_t, 60> reply{};
        reply[4] = 0x02;
        reply[11] = kBoardIdAngelia;
        const auto r = parseDiscoveryReply(std::span<const std::uint8_t>(reply));
        check(r && r->isSupportedAnan() && !r->isSaturn(), "ANGELIA reply is a supported ANAN");
        reply[11] = 6;
        const auto hl2 = parseDiscoveryReply(std::span<const std::uint8_t>(reply));
        check(hl2 && !hl2->isSupportedAnan(), "board 6 is not a supported ANAN");
    }

    // ---- General packet byte 59 ----
    {
        check(buildGeneral()[59] == 0x00, "default General keeps byte 59 clear");
        check(buildGeneral(0x01)[59] == 0x01, "General byte 59 enables Alex0");
        check(buildGeneral(0x01)[37] == 0x08 && buildGeneral(0x01)[38] == 0x01,
              "General phase-word and watchdog bits unchanged");
    }

    // ---- DDC-Specific on DDC2 ----
    {
        const DdcConfig one{192, 0};
        const auto pkt = buildDdcSpecific(std::span<const DdcConfig>(&one, 1), 2,
                                          true, true, /*hwDdcBase=*/2);
        check(pkt[7] == 0x04, "only DDC2 enabled");
        const std::size_t row = 17 + 6 * 2;
        check(pkt[row] == 0, "DDC2 row selects ADC0");
        check(be16(&pkt[row + 1]) == 192, "DDC2 rate 192 ksps");
        check(pkt[row + 5] == 24, "DDC2 24-bit samples");
        check(pkt[17] == 0 && be16(&pkt[18]) == 0 && pkt[22] == 0, "DDC0 row untouched");
        // Two receivers: DDC2 and DDC3.
        const std::array<DdcConfig, 2> two{DdcConfig{48, 0}, DdcConfig{96, 1}};
        const auto pkt2 = buildDdcSpecific(std::span<const DdcConfig>(two), 2, true, true, 2);
        check(pkt2[7] == 0x0C, "DDC2+DDC3 enabled");
        check(pkt2[17 + 6 * 3] == 1 && be16(&pkt2[17 + 6 * 3 + 1]) == 96, "DDC3 row");
        // Base 0 is byte-identical to the original single-DDC form.
        const auto legacy = buildDdcSpecific(48, 2, true, true, 0);
        const auto based = buildDdcSpecific(std::span<const DdcConfig>(&two[0], 1), 2, true, true, 0);
        check(legacy == based, "base 0 is byte-identical");
    }

    // ---- High Priority: frequency word lands in DDC2 ----
    {
        const std::uint32_t word = phaseWord(14'200'000.0);
        const auto pkt = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                           true, true, 0, 0, angeliaOpts(14'200'000.0, true));
        check(pkt[4] == 0x01, "run bit set, no PTT");
        check(be32(&pkt[9]) == 0, "DDC0 word left zero");
        check(be32(&pkt[9 + 4 * 2]) == word, "DDC2 word carries the tune");
        check(be32(&pkt[1432]) == (kAnt1 | kHpf13 | kLpf30_20),
              "20 m: ANT1 + 13 MHz HPF + 30/20 LPF");
        check(be32(&pkt[1428]) == (kAnt1 | kLpf30_20), "Alex1 TX state: ANT1 + 30/20 LPF");
        check(pkt[1400] == 0, "no speaker-mute bit on ANGELIA");
    }

    // ---- High Priority: optional DDC0/DDC1 mirror (Brick3 band tracking) ----
    {
        const std::uint32_t word = phaseWord(7'074'000.0);
        auto opts = angeliaOpts(7'074'000.0, true);
        opts.mirrorRxWordsBelowBase = true;
        const auto pkt = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                           true, true, 0, 0, opts);
        check(be32(&pkt[9]) == word, "mirror: DDC0 word = RX1");
        check(be32(&pkt[13]) == word, "mirror: DDC1 word = RX1 (one receiver)");
        check(be32(&pkt[17]) == word, "mirror: DDC2 word = RX1");
        check(be32(&pkt[21]) == 0, "mirror: DDC3 untouched");
    }

    // ---- High Priority: filter bypass ----
    {
        const std::uint32_t word = phaseWord(7'100'000.0);
        const auto pkt = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                           /*bypassAdc0=*/true, true, 10, 5,
                                           angeliaOpts(7'100'000.0, /*filters=*/false));
        check(be32(&pkt[1432]) == (kAnt1 | kHpfBypass | kLpf6),
              "bypass: ANT1 + HPF bypass + widest LPF");
        check(pkt[1443] == 10 && pkt[1442] == 5, "step attenuators");
        auto opts = angeliaOpts(7'100'000.0, false);
        opts.muteSpeakerAmp = true;
        const auto muted = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                             true, true, 0, 0, opts);
        check(muted[1400] == 0, "mute request ignored on a board without the bit");
    }

    // ---- HPF edges (deskHPSDR default case) ----
    {
        struct Case { double mhz; AlexRxFilter f; };
        const Case cases[] = {
            {0.5, AlexRxFilter::Bypass},   {1.79, AlexRxFilter::Bypass},
            {1.85, AlexRxFilter::Bpf1_5MHz}, {3.6, AlexRxFilter::Bpf1_5MHz},
            {5.35, AlexRxFilter::Bpf1_5MHz}, {7.1, AlexRxFilter::Bpf6_5MHz},
            {10.12, AlexRxFilter::Bpf9_5MHz}, {14.2, AlexRxFilter::Bpf13MHz},
            {18.1, AlexRxFilter::Bpf13MHz}, {21.2, AlexRxFilter::Bpf20MHz},
            {24.9, AlexRxFilter::Bpf20MHz}, {28.5, AlexRxFilter::Bpf20MHz},
            {50.1, AlexRxFilter::Bpf20MHz},
        };
        for (const Case& c : cases) {
            check(legacyAlexHpfFor(c.mhz * 1e6) == c.f,
                  "HPF for " + std::to_string(c.mhz) + " MHz");
        }
        check(legacyAlexHpfFor(std::numeric_limits<double>::quiet_NaN()) == AlexRxFilter::Bypass,
              "HPF NaN -> bypass");
    }

    // ---- LPF edges (deskHPSDR) ----
    {
        struct Case { double mhz; std::uint32_t bits; };
        const Case cases[] = {
            {1.85, kLpf160}, {2.5, kLpf160}, {3.6, kLpf80}, {5.0, kLpf80},
            {5.35, kLpf60_40}, {7.1, kLpf60_40}, {10.12, kLpf30_20}, {14.2, kLpf30_20},
            {18.1, kLpf17_15}, {21.2, kLpf17_15}, {24.9, kLpf12_10}, {28.5, kLpf12_10},
            {50.1, kLpf6},
        };
        for (const Case& c : cases) {
            check(legacyAlexLpfBitsFor(c.mhz * 1e6) == c.bits,
                  "LPF for " + std::to_string(c.mhz) + " MHz");
        }
        check(legacyAlexLpfBitsFor(std::numeric_limits<double>::infinity()) == kLpf6,
              "LPF infinity -> 6 m/bypass");
    }

    // ---- Every band selects exactly one HPF and one LPF ----
    {
        const double bands[] = {1.85, 3.6, 5.35, 7.1, 10.12, 14.2, 18.1, 21.2, 24.9, 28.5, 50.1};
        for (const double mhz : bands) {
            const std::uint32_t word = phaseWord(mhz * 1e6);
            const auto pkt = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                               true, true, 0, 0, angeliaOpts(mhz * 1e6, true));
            const std::uint32_t a0 = be32(&pkt[1432]);
            const std::uint32_t hpf = a0 & (kHpfBypass | kHpf1_5 | kHpf6_5 | kHpf9_5
                                            | kHpf13 | kHpf20 | kPre6m);
            const std::uint32_t lpf = a0 & (kLpf30_20 | kLpf60_40 | kLpf80 | kLpf160
                                            | kLpf6 | kLpf12_10 | kLpf17_15);
            check(hpf != 0 && (hpf & (hpf - 1)) == 0,
                  "one HPF at " + std::to_string(mhz) + " MHz");
            check(lpf != 0 && (lpf & (lpf - 1)) == 0,
                  "one LPF at " + std::to_string(mhz) + " MHz");
            check((a0 & kAnt1) != 0, "ANT1 at " + std::to_string(mhz) + " MHz");
        }
    }

    // ---- Transmit (ANAN-100D port) ----
    {
        const std::uint32_t word = phaseWord(7'074'000.0);
        auto opts = angeliaOpts(7'074'000.0, true);
        opts.transmitting = true;
        opts.paEnabled = true;
        opts.txFrequencyHz = 14'074'000.0;   // cross-band: TX LPF must follow TX
        opts.ducPhaseWord = phaseWord(14'074'000.0);
        opts.driveLevel = 200;
        const auto pkt = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                           true, true, 0, 0, opts);
        check(pkt[4] == 0x03, "TX: run + PTT0");
        check(be32(&pkt[329]) == phaseWord(14'074'000.0), "TX: DUC0 word at 329");
        check(pkt[345] == 200, "TX: drive at 345");
        const std::uint32_t a0 = be32(&pkt[1432]);
        check((a0 & kLpf30_20) && !(a0 & kLpf60_40), "TX: Alex0 LPF follows the TX frequency");
        check((a0 & kAlexTxRelay) != 0, "TX: Alex0 T/R relay");
        check(be32(&pkt[1428]) == (kAnt1 | kLpf30_20 | kAlexTxRelay), "TX: Alex1 = ANT1 + TX LPF + relay");
        opts.transmitting = false;
        const auto rx = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                          true, true, 0, 0, opts);
        check(rx[4] == 0x01, "RX: no PTT");
        check((be32(&rx[1432]) & kAlexTxRelay) == 0, "RX: Alex0 relay off");
        check(be32(&rx[1432]) & kLpf60_40, "RX: Alex0 LPF follows the receive frequency");
        // Antennas: RX on ANT2 while receiving, ANT3 switched in on PTT.
        opts.rxAntenna = 1;
        opts.txAntenna = 2;
        const auto rxA = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                           true, true, 0, 0, opts);
        check((be32(&rxA[1432]) & 0x07000000u) == 0x02000000u, "RX: Alex0 = ANT2");
        check((be32(&rxA[1428]) & 0x07000000u) == 0x04000000u, "RX: Alex1 TX state = ANT3");
        opts.transmitting = true;
        const auto txA = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                           true, true, 0, 0, opts);
        check((be32(&txA[1432]) & 0x07000000u) == 0x04000000u, "TX: Alex0 = ANT3");
        opts.transmitting = false;
        check(buildGeneral(0x01, true)[58] == 0x01, "General: PA enable");
        check(buildGeneral()[40] == 0 && buildGeneral(0x01, true)[41] == 0,
              "General: bytes 40/41 zero (no RGMII delay control since firmware v8)");
        const std::complex<float> s[2] = {{1.0f, -1.0f}, {0.5f, 0.0f}};
        const auto iqp = buildDucIq(7, std::span<const std::complex<float>>(s, 2));
        check(be32(&iqp[0]) == 7, "DUC IQ seq");
        check(iqp[4] == 0x7F && iqp[5] == 0xFF && iqp[6] == 0xFF, "DUC IQ +1.0 -> 0x7FFFFF");
        check(iqp[7] == 0x80 && iqp[8] == 0x00 && iqp[9] == 0x01, "DUC IQ -1.0 -> 0x800001");
        check(iqp[10] == 0x40 && iqp[11] == 0x00 && iqp[12] == 0x00, "DUC IQ 0.5 -> 0x400000");
        check(iqp[16] == 0 && iqp[1443] == 0, "DUC IQ zero padding");
        check(buildTransmitSpecific()[4] == 1 && buildTransmitSpecific()[5] == 0,
              "Transmitter Specific: 1 DAC, no FPGA keyer");
        CwKeyerOptions k;
        k.enabled = true; k.mode = 2; k.paddleSwap = true; k.strictSpacing = false;
        k.sidetoneLevel = 128; k.sidetoneHz = 700; k.speedWpm = 25; k.weight = 50; k.hangMs = 300;
        const auto ts = buildTransmitSpecific(7, true, k);
        check(ts[5] == (0x02 | 0x04 | 0x08 | 0x10 | 0x20 | 0x80), "keyer mode byte (iambic B, swap, sidetone, break-in)");
        check(ts[6] == 128 && ts[7] == 0x02 && ts[8] == 0xBC, "sidetone level / 700 Hz");
        check(ts[9] == 25 && ts[10] == 50 && ts[11] == 0x01 && ts[12] == 0x2C, "speed, weight, 300 ms hang");
        check(ts[13] == 25 && ts[17] == 7 && ts[50] == 0x02, "RF delay, edge, mic boost");
        {
            CwKeyerOptions off;   // keyer off: RF Delay still sent (all modes)
            off.rfDelayMs = 40;
            const auto t2 = buildTransmitSpecific(8, false, off);
            check(t2[13] == 40 && t2[5] == 0, "RF delay sent with the keyer off");
        }
        k.mode = 0; k.sidetoneLevel = 0;
        check(cwModeControlByte(k) == (0x02 | 0x04 | 0x80), "straight key, no sidetone");
    }

    // ---- Saturn path unchanged by the new options ----
    {
        const std::uint32_t word = phaseWord(10e6);
        const auto plain = buildHighPriority(true, word, true, true, 3, 4);
        HighPriorityOptions sat;   // defaults = SaturnBandPass, base 0
        const auto viaOpts = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                               true, true, 3, 4, sat);
        check(plain == viaOpts, "Saturn defaults byte-identical");
    }

    // ---- PureSignal (deskHPSDR new_protocol.c) ----
    {
        const DdcConfig rx[2] = {DdcConfig{192, 0}, DdcConfig{192, 1}};
        const auto off = buildDdcSpecific(std::span<const DdcConfig>(rx, 2), 2, false, false, 2);
        const auto on = buildDdcSpecific(std::span<const DdcConfig>(rx, 2), 2, false, false, 2,
                                         PsDdcOptions{true, 0});
        check(off[7] == 0x0C && on[7] == 0x0D, "PS: DDC0 enabled, DDC2/3 kept");
        check(on[17] == 0 && on[18] == 0 && on[19] == 192 && on[22] == 24, "PS: DDC0 = ADC0 192k");
        check(on[23] == 2 && on[25] == 192 && on[28] == 24, "PS: DDC1 = TX DAC 192k");
        check(on[1363] == 0x02 && off[1363] == 0, "PS: DDC1 synced to DDC0");
        // A synced packet: 238 samples alternating DDC0 / DDC1.
        std::vector<std::uint8_t> pkt(16 + 238 * 6, 0);
        pkt[13] = 24;
        pkt[14] = 0;
        pkt[15] = 238;
        pkt[16] = 0x40;             // DDC0 sample 0 I = +0.5
        pkt[16 + 6 + 3] = 0xC0;     // DDC1 sample 0 Q = -0.5
        std::vector<double> tx, rxv;
        check(decodePsPairs(pkt, tx, rxv) && rxv.size() == 238 && tx.size() == 238,
              "PS: 119 pairs decoded");
        check(std::abs(rxv[0] - 0.5) < 1e-9 && std::abs(tx[1] + 0.5) < 1e-9, "PS: pair order rx/tx");
        HighPriorityOptions o;
        o.alexScheme = AlexScheme::LegacyHighPassLowPass;
        o.hwDdcBase = 2;
        o.mirrorRxWordsBelowBase = true;
        o.transmitting = true;
        o.psEnabled = true;
        o.ducPhaseWord = phaseWord(7.1e6);
        o.txFrequencyHz = 7.1e6;
        const std::uint32_t w[2] = {phaseWord(14.2e6), phaseWord(14.2e6)};
        const auto hp = buildHighPriority(true, std::span<const std::uint32_t>(w, 2), false, false, 0, 0, o);
        auto rd = [&](int i) { return (std::uint32_t(hp[i]) << 24) | (std::uint32_t(hp[i + 1]) << 16)
                                      | (std::uint32_t(hp[i + 2]) << 8) | hp[i + 3]; };
        check(rd(9) == o.ducPhaseWord && rd(13) == o.ducPhaseWord && rd(17) == w[0],
              "PS: DDC0/1 on the TX frequency, receivers unchanged");
        check((rd(1432) & kAlexPsBit) && (rd(1428) & kAlexPsBit), "PS: Alex0/Alex1 PS bit");
        const auto ts = buildTransmitSpecific(0, false, {}, 31, 12);
        check(ts[59] == 12 && ts[58] == 31 && ts[57] == 31, "PS: ADC0 attenuator on TX");
        const auto ts1 = buildTransmitSpecific(0, false, {}, 31, 12, 1);
        check(ts1[58] == 12 && ts1[59] == 31 && ts1[57] == 31, "PS: feedback on ADC1 -> its attenuator");
    }

    // ---- User Out (open collector), Thetis Penny.cs ----
    {
        check(ocBandIndex(1.85e6) == 0 && ocBandIndex(14.2e6) == 5 && ocBandIndex(50.1e6) == 10,
              "OC band rows");
        check(ocBandIndex(9.6e6) == -1 && ocBandIndex(6.1e6) == -1, "broadcast / gen -> no row");
        OcMatrix m;
        m.rx[5] = 0x05;                 // 20 m RX: OC1 + OC3
        m.tx[5] = 0x03;                 // 20 m TX: OC1 + OC2
        m.action[0] = OcTxAction::Mox;  // OC1 MOX only
        m.action[1] = OcTxAction::Tune; // OC2 TUNE only
        check(openCollectorBits(m, 14.2e6, 14.2e6, false, false) == 0x05, "OC RX row");
        check(openCollectorBits(m, 14.2e6, 14.2e6, true, false) == 0x01, "OC TX MOX action");
        check(openCollectorBits(m, 14.2e6, 14.2e6, true, true) == 0x02, "OC TX TUNE action");
        check(openCollectorBits(m, 14.2e6, 9.6e6, true, false) == 0, "OC TX outside band");
        HighPriorityOptions o;
        o.alexScheme = AlexScheme::LegacyHighPassLowPass;
        o.hwDdcBase = 2;
        o.openCollector = 0x41;         // OC1 + OC7
        const std::uint32_t word = phaseWord(14.2e6);
        const auto hp = buildHighPriority(true, std::span<const std::uint32_t>(&word, 1),
                                          false, false, 0, 0, o);
        check(hp[1401] == 0x82, "byte 1401 = OC bits << 1");
    }

    if (g_failures == 0) {
        std::printf("anan_angelia_protocol_test: all checks passed\n");
        return 0;
    }
    std::printf("anan_angelia_protocol_test: %d failure(s)\n", g_failures);
    return 1;
}
