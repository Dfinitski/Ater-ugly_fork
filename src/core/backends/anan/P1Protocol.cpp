#include "core/backends/anan/P1Protocol.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <map>
#include <set>

namespace AetherSDR::anan {

std::optional<std::uint8_t> p2BoardIdForP1(std::uint8_t p1Board) noexcept
{
    switch (p1Board) {
    case kP1BoardHermes:  return kBoardIdHermes;
    case kP1BoardAngelia: return kBoardIdAngelia;
    case kP1BoardOrion:   return kBoardIdOrion;
    case kP1BoardOrion2:  return kBoardIdOrion2;
    default:              return std::nullopt;   // Metis, Griffin, HL2, unknown
    }
}

P1DdcPlan p1DdcPlanFor(std::uint8_t p2BoardId) noexcept
{
    P1DdcPlan p;
    switch (p2BoardId) {
    case kBoardIdHermes:
        p.nddc = 4;
        p.rxDdc = {{0, 1}};
        break;
    case kBoardIdHermesII:
        p.nddc = 2;
        p.rxDdc = {{0, 1}};
        break;
    default:   // ANGELIA, ORION, ORION MkII
        p.nddc = 5;
        p.rxDdc = {{0, 2}};
        break;
    }
    return p;
}

namespace {

std::uint8_t b(std::uint32_t v, int shift) noexcept
{
    return static_cast<std::uint8_t>((v >> shift) & 0xFF);
}

void freqBytes(hl2::Cc& c, std::uint32_t hz) noexcept
{
    c[1] = b(hz, 24);
    c[2] = b(hz, 16);
    c[3] = b(hz, 8);
    c[4] = b(hz, 0);
}

}  // namespace

hl2::Cc p1Cc(int index, const P1CcState& s) noexcept
{
    hl2::Cc c{};
    c[0] = s.mox ? 0x01 : 0x00;
    const auto att5 = [](int db) { return static_cast<std::uint8_t>(std::clamp(db, 0, 31)); };
    switch (index) {
    case 0:   // general settings
        c[1] = static_cast<std::uint8_t>(s.rateCode & 3);
        c[2] = static_cast<std::uint8_t>((s.openCollector << 1) & 0xFE);   // bit 0 EER off
        // C3: Alex 10/20 dB attenuators off, preamp off (Mercury only),
        // dither bit 3, random bit 4, the RX-only inputs (bits 5-6) and the
        // RX bypass out (bit 7): only for the PureSignal feedback on TX.
        c[3] = static_cast<std::uint8_t>((s.dither ? 0x08 : 0) | (s.random ? 0x10 : 0)
                                         | (s.rxInputBits & 0xE0));
        c[4] = static_cast<std::uint8_t>(s.antenna == 2 ? 0x02 : s.antenna == 1 ? 0x01 : 0x00);
        if (s.duplex)
            c[4] |= 0x04;
        c[4] |= static_cast<std::uint8_t>(((std::clamp(s.nddc, 1, 7) - 1) & 0x0F) << 3);
        break;
    case 1:   // TX NCO
        c[0] |= 0x02;
        freqBytes(c, s.txHz);
        break;
    case 2:   // DDC0
        c[0] |= 0x04;
        freqBytes(c, s.ddcHz[0]);
        break;
    case 3:   // DDC1
        c[0] |= 0x06;
        freqBytes(c, s.ddcHz[1]);
        break;
    case 4:   // ADC assignment, ADC0 attenuation while transmitting
        c[0] |= 0x1C;
        c[1] = static_cast<std::uint8_t>(s.adcControl & 0xFF);
        c[2] = static_cast<std::uint8_t>((s.adcControl >> 8) & 0x3F);
        c[3] = att5(s.txAdc0AttDb);
        break;
    case 5:   // DDC2
        c[0] |= 0x08;
        freqBytes(c, s.ddcHz[2]);
        break;
    case 6:   // DDC3
        c[0] |= 0x0A;
        freqBytes(c, s.ddcHz[3]);
        break;
    case 7:   // DDC4
        c[0] |= 0x0C;
        freqBytes(c, s.ddcHz[4]);
        break;
    case 8:   // DDC5
        c[0] |= 0x0E;
        freqBytes(c, s.ddcHz[5]);
        break;
    case 9:   // DDC6
        c[0] |= 0x10;
        freqBytes(c, s.ddcHz[6]);
        break;
    case 10:  // drive, mic, Alex filters, PA
        c[0] |= 0x12;
        c[1] = static_cast<std::uint8_t>(std::clamp(s.driveLevel, 0, 255));
        // Bit 6: the Alex filters are selected by the host (C3 / C4).
        c[2] = static_cast<std::uint8_t>(((s.micBoost ? 0x01 : 0) | (s.lineIn ? 0x02 : 0) | 0x40) & 0x7F);
        c[3] = static_cast<std::uint8_t>((s.alexHpf & 0x7F) | (s.paEnabled ? 0x80 : 0));
        c[4] = static_cast<std::uint8_t>(s.alexLpf & 0x7F);
        break;
    case 11:  // preamps, mic, PureSignal, ADC0 attenuator
        c[0] |= 0x14;
        c[1] = static_cast<std::uint8_t>(s.micControl & 0x70);
        c[2] = static_cast<std::uint8_t>((s.lineInGain & 0x1F) | (s.pureSignal ? 0x40 : 0));
        c[3] = 0;   // user digital outputs
        c[4] = static_cast<std::uint8_t>(att5(s.rxAttDb[0]) | 0x20);   // bit 5: attenuator on
        break;
    case 12:  // ADC1 / ADC2 attenuators, keyer speed and weight
        c[0] |= 0x16;
        // Thetis: ADC1 at 31 dB while transmitting (Red Pitaya excepted).
        c[1] = static_cast<std::uint8_t>((s.mox ? 0x1F : att5(s.rxAttDb[1])) | 0x20);
        c[2] = static_cast<std::uint8_t>(att5(s.rxAttDb[2]) | 0x20 | (s.cwPaddleSwap ? 0x40 : 0));
        c[3] = static_cast<std::uint8_t>((std::clamp(s.cwSpeedWpm, 1, 60) & 0x3F)
                                         | (s.cwMode == 0 ? 0x00 : s.cwMode == 1 ? 0x40 : 0x80));
        c[4] = static_cast<std::uint8_t>((std::clamp(s.cwWeight, 0, 100) & 0x7F) | (s.cwStrictSpacing ? 0x80 : 0));
        break;
    case 13:  // CW
        c[0] |= 0x1E;
        c[1] = s.cwKeyer ? 1 : 0;
        c[2] = static_cast<std::uint8_t>(std::clamp(s.cwSidetoneLevel, 0, 127));
        c[3] = static_cast<std::uint8_t>(std::clamp(s.cwRfDelayMs, 0, 255));
        break;
    case 14:  // CW hang time, sidetone frequency
        c[0] |= 0x20;
        {
            const int hang = std::clamp(s.cwHangMs, 0, 1023);
            const int tone = std::clamp(s.cwSidetoneHz, 0, 4095);
            c[1] = static_cast<std::uint8_t>((hang >> 2) & 0xFF);
            c[2] = static_cast<std::uint8_t>(hang & 0x03);
            c[3] = static_cast<std::uint8_t>((tone >> 4) & 0xFF);
            c[4] = static_cast<std::uint8_t>(tone & 0x0F);
        }
        break;
    case 15:  // EER PWM: not used
        c[0] |= 0x22;
        break;
    case 16:  // ADC1 band-pass bank (ORION MkII), transverter, PureSignal
        c[0] |= 0x24;
        c[1] = static_cast<std::uint8_t>((s.alex2Hpf & 0x7F) | (s.rx2GroundOnTx ? 0x80 : 0));
        c[2] = static_cast<std::uint8_t>(s.pureSignal ? 0x40 : 0);
        break;
    default:
        break;
    }
    return c;
}

std::uint8_t p1AlexHpfBits(AlexRxFilter f) noexcept
{
    switch (f) {
    case AlexRxFilter::Bpf13MHz:  return 0x01;
    case AlexRxFilter::Bpf20MHz:  return 0x02;
    case AlexRxFilter::Bpf9_5MHz: return 0x04;
    case AlexRxFilter::Bpf6_5MHz: return 0x08;
    case AlexRxFilter::Bpf1_5MHz: return 0x10;
    case AlexRxFilter::Preamp6m:  return 0x40;
    case AlexRxFilter::Bypass:
    default:                      return 0x20;
    }
}

std::uint8_t p1AlexLpfBitsFor(double hz) noexcept
{
    if (!std::isfinite(hz) || hz > 35'600'000.0)
        return 0x10;   // 6 m / bypass
    if (hz > 24'000'000.0)
        return 0x20;   // 12/10 m
    if (hz > 16'500'000.0)
        return 0x40;   // 17/15 m
    if (hz > 8'000'000.0)
        return 0x01;   // 30/20 m
    if (hz > 5'000'000.0)
        return 0x02;   // 60/40 m
    if (hz > 2'500'000.0)
        return 0x04;   // 80 m
    return 0x08;       // 160 m
}

std::optional<P1Status> parseP1Status(const std::uint8_t* frame) noexcept
{
    if (frame[0] != 0x7F || frame[1] != 0x7F || frame[2] != 0x7F)
        return std::nullopt;
    const std::uint8_t c0 = frame[3];
    const std::uint8_t c1 = frame[4], c2 = frame[5], c3 = frame[6], c4 = frame[7];
    P1Status s;
    s.ptt = (c0 & 0x01) != 0;
    s.dash = (c0 & 0x02) != 0;
    s.dot = (c0 & 0x04) != 0;
    s.address = (c0 >> 3) & 0x1F;
    const auto be16 = [](std::uint8_t hi, std::uint8_t lo) { return (int(hi) << 8) | int(lo); };
    switch (c0 & 0xF8) {
    case 0x00:
        s.overloadBits = (c1 & 0x01) ? 1u : 0u;
        s.firmwareVersion = int(c4);
        break;
    case 0x08:
        s.exciterRaw = be16(c1, c2);   // AIN5, drive power
        s.fwdRaw = be16(c3, c4);       // AIN1, PA coupler
        break;
    case 0x10:
        s.revRaw = be16(c1, c2);       // AIN2, PA reverse power
        s.userAdc0Raw = be16(c3, c4);  // AIN3, PA volts (MkII)
        break;
    case 0x18:
        s.userAdc1Raw = be16(c1, c2);  // AIN4, PA amps (MkII)
        s.supplyRaw = be16(c3, c4);    // AIN6, supply volts
        break;
    case 0x20:
        s.overloadBits = ((c1 & 1) ? 1u : 0u) | ((c2 & 1) ? 2u : 0u) | ((c3 & 1) ? 4u : 0u);
        break;
    default:
        break;
    }
    return s;
}

int p1MicSamples(std::span<const std::uint8_t> pkt, int nddc, int decimation, int& phase,
                 std::vector<float>& out) noexcept
{
    if (pkt.size() != hl2::kUsbPacketSize || pkt[0] != 0xEF || pkt[1] != 0xFE || pkt[2] != 0x01
        || pkt[3] != 0x06 || nddc < 1 || nddc > 7)
        return 0;
    decimation = std::max(1, decimation);
    const std::size_t round = hl2::ep6RoundBytes(nddc);
    int n = 0;
    for (std::size_t fs : {std::size_t{8}, std::size_t{8} + hl2::kFrameSize}) {
        const std::uint8_t* frame = pkt.data() + fs;
        if (frame[0] != 0x7F || frame[1] != 0x7F || frame[2] != 0x7F)
            continue;
        const std::uint8_t* payload = frame + 8;
        for (std::size_t k = 0; k + round <= hl2::kFramePayload; k += round) {
            if (++phase < decimation)
                continue;
            phase = 0;
            const std::uint8_t* m = payload + k + static_cast<std::size_t>(nddc) * hl2::kRxIqBytes;
            const auto v = static_cast<std::int16_t>((std::uint16_t(m[0]) << 8) | std::uint16_t(m[1]));
            out.push_back(static_cast<float>(v) / 32768.0f);
            ++n;
        }
    }
    return n;
}

P1TxDecimator::P1TxDecimator()
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double fs = 192000.0, fc = 20000.0;
    double sum = 0.0;
    for (int i = 0; i < kTaps; ++i) {
        const double m = i - (kTaps - 1) / 2.0;
        const double x = 2.0 * fc / fs * m;
        const double sinc = std::abs(m) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1))
                         + 0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
        m_h[static_cast<std::size_t>(i)] = static_cast<float>(sinc * w);
        sum += sinc * w;
    }
    for (auto& h : m_h)
        h = static_cast<float>(h / sum);
}

void P1TxDecimator::reset() noexcept
{
    m_hist.fill({});
    m_pos = 0;
}

void P1TxDecimator::process(const std::complex<float>* in, int n, std::complex<float>* out) noexcept
{
    int o = 0;
    for (int i = 0; i < n; ++i) {
        // Newest sample at m_pos, also mirrored kTaps further on, so the
        // window [m_pos, m_pos + kTaps) is always contiguous.
        m_hist[static_cast<std::size_t>(m_pos)] = in[i];
        m_hist[static_cast<std::size_t>(m_pos + kTaps)] = in[i];
        if ((i % kFactor) == kFactor - 1) {
            std::complex<float> acc{};
            // m_h is symmetric, so newest-to-oldest order does not matter.
            for (int k = 0; k < kTaps; ++k)
                acc += m_hist[static_cast<std::size_t>(m_pos + kTaps - k)] * m_h[static_cast<std::size_t>(k)];
            out[o++] = acc;
        }
        m_pos = (m_pos + 1) % kTaps;
    }
}

void P1PsResampler::configure(int inRateHz)
{
    constexpr double kPi = 3.14159265358979323846;
    m_inRate = inRateHz;
    m_up = inRateHz > 0 && inRateHz < kOutRateHz ? kOutRateHz / inRateHz : 1;
    m_down = inRateHz > kOutRateHz ? inRateHz / kOutRateHz : 1;
    // Prototype at the higher of the two rates; 32 taps per input sample when
    // interpolating, 96 taps for the 2:1 decimation.
    m_taps = m_up > 1 ? 32 * m_up : (m_down > 1 ? 96 : 0);
    m_h.assign(static_cast<std::size_t>(m_taps), 0.0f);
    if (m_taps > 0) {
        const double fs = double(std::max(inRateHz, kOutRateHz));
        const double fc = 0.45 * double(std::min(inRateHz, kOutRateHz));
        double sum = 0.0;
        std::vector<double> h(static_cast<std::size_t>(m_taps));
        for (int i = 0; i < m_taps; ++i) {
            const double m = i - (m_taps - 1) / 2.0;
            const double x = 2.0 * fc / fs * m;
            const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (m_taps - 1))
                             + 0.08 * std::cos(4.0 * kPi * i / (m_taps - 1));
            h[static_cast<std::size_t>(i)] = sinc * w;
            sum += sinc * w;
        }
        // Unity gain at DC: each interpolation phase sums to ~1 (x m_up).
        for (int i = 0; i < m_taps; ++i)
            m_h[static_cast<std::size_t>(i)] = static_cast<float>(h[static_cast<std::size_t>(i)] * m_up / sum);
    }
    m_histLen = m_up > 1 ? m_taps / m_up : m_taps;
    m_hist.assign(static_cast<std::size_t>(2 * std::max(m_histLen, 1)), {});
    reset();
}

void P1PsResampler::reset() noexcept
{
    std::fill(m_hist.begin(), m_hist.end(), std::complex<float>{});
    m_pos = 0;
    m_phase = 0;
}

void P1PsResampler::process(const std::complex<float>* in, int n, std::vector<double>& out)
{
    if (m_taps == 0) {   // 192 ksps: as received
        for (int i = 0; i < n; ++i) {
            out.push_back(in[i].real());
            out.push_back(in[i].imag());
        }
        return;
    }
    const int len = m_histLen;
    for (int i = 0; i < n; ++i) {
        // Newest sample at m_pos and mirrored len further on: the window
        // [m_pos, m_pos + len) is contiguous, newest first going backwards.
        m_pos = (m_pos + len - 1) % len;
        m_hist[static_cast<std::size_t>(m_pos)] = in[i];
        m_hist[static_cast<std::size_t>(m_pos + len)] = in[i];
        const std::complex<float>* x = m_hist.data() + m_pos;   // x[k] = input k samples ago
        if (m_up > 1) {
            // Output phase p of this input sample: sum over k of h[k*up + p] x[k].
            for (int p = 0; p < m_up; ++p) {
                std::complex<float> acc{};
                for (int k = 0; k < len; ++k)
                    acc += x[k] * m_h[static_cast<std::size_t>(k * m_up + p)];
                out.push_back(acc.real());
                out.push_back(acc.imag());
            }
        } else if (++m_phase == m_down) {
            m_phase = 0;
            std::complex<float> acc{};
            for (int k = 0; k < len; ++k)
                acc += x[k] * m_h[static_cast<std::size_t>(k)];
            out.push_back(acc.real());
            out.push_back(acc.imag());
        }
    }
}

namespace {
std::mutex g_p1HostsMutex;
std::set<std::uint32_t> g_p1Hosts;
std::map<std::uint32_t, Protocol1HostInfo> g_p1HostInfo;   // build 212
}  // namespace

void noteProtocol1Host(std::uint32_t ipv4, bool protocol1)
{
    std::lock_guard<std::mutex> lock(g_p1HostsMutex);
    if (protocol1) {
        g_p1Hosts.insert(ipv4);
    } else {
        g_p1Hosts.erase(ipv4);
        g_p1HostInfo.erase(ipv4);
    }
}

void noteProtocol1Host(std::uint32_t ipv4, const Protocol1HostInfo& info)
{
    std::lock_guard<std::mutex> lock(g_p1HostsMutex);
    g_p1Hosts.insert(ipv4);
    g_p1HostInfo[ipv4] = info;
}

std::optional<Protocol1HostInfo> protocol1HostInfo(std::uint32_t ipv4)
{
    std::lock_guard<std::mutex> lock(g_p1HostsMutex);
    const auto it = g_p1HostInfo.find(ipv4);
    if (it == g_p1HostInfo.end())
        return std::nullopt;
    return it->second;
}

bool isProtocol1Host(std::uint32_t ipv4)
{
    std::lock_guard<std::mutex> lock(g_p1HostsMutex);
    return g_p1Hosts.count(ipv4) != 0;
}

}  // namespace AetherSDR::anan
