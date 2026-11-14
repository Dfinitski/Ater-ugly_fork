#pragma once

// ANAN fork build 391: the FTX mode's two real-time connections to the radio
// threads, as MicronSDR has them in its audio callback:
//
//   receive : the receiver's own I/Q (dial at 0 Hz, 48 kHz) -> FtUnit::iq().
//             The decoders demodulate it themselves (USB, 100 .. 4000 Hz):
//             no receive filter, no AGC, no audio path of this program in
//             between. Called on the I/O thread (Hl2RxDsp::processIqBlock).
//   transmit: the mode's waveform (FtTx, 48 kHz) instead of the microphone.
//             Called on the transmit thread (AnanTxEngine::onMicPacket), so it
//             is clocked by the radio's own microphone packets -- no GUI
//             thread, no computer sound card on the way.
//
// The GUI sets and clears the two functions; a call in flight finishes before
// set / clear returns (one mutex, held for the call). No Qt, no other header
// of the program: the DSP classes include this alone.

#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

namespace AetherSDR::ftx {

class FtxBridge {
public:
    static FtxBridge& instance()
    {
        static FtxBridge b;
        return b;
    }

    using IqSink = std::function<void(const float* q, const float* i, unsigned long n)>;
    using TxSource = std::function<void(float* mono48k, unsigned long n)>;

    // ---- GUI thread ----
    void setIqSink(IqSink f)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_iq = std::move(f);
    }
    void setTxSource(TxSource f)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tx = std::move(f);
        m_hasTx.store(static_cast<bool>(m_tx), std::memory_order_release);
    }
    // The receiver DSP object whose I/Q is taken (nullptr = none). The backend
    // refreshes it; the DSP compares it with itself.
    std::atomic<const void*> iqOwner{nullptr};
    // The mode's own transmission is on the air (between its PTT on and off).
    std::atomic<bool> txOnAir{false};

    // ---- I/O thread ----
    void feedIq(const float* q, const float* i, unsigned long n)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_iq)
            m_iq(q, i, n);
    }
    // ---- transmit thread ----
    [[nodiscard]] bool hasTx() const { return m_hasTx.load(std::memory_order_acquire); }
    bool fillTx(float* mono48k, unsigned long n)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_tx)
            return false;
        m_tx(mono48k, n);
        return true;
    }

private:
    std::mutex m_mutex;
    IqSink m_iq;
    TxSource m_tx;
    std::atomic<bool> m_hasTx{false};
};

// The receiver's I/Q at the DDC rate (signals above the NCO at positive
// frequency) -> the slice's dial at 0 Hz, 48 kHz. Mixer, then halfband
// decimators by two: 23 taps, 86 dB down where a stage folds onto 0 .. 4.6 kHz
// (the band the decoders use), flat within 0.001 dB there.
class FtxIqTap {
public:
    // false: this rate is not 48 kHz times a power of two -- nothing done.
    bool process(const std::vector<std::complex<float>>& iq, double shiftHz, int rateHz)
    {
        int stages = 0;
        for (int r = rateHz; r > 48000; r /= 2) {
            if (r % 2 != 0)
                return false;
            ++stages;
        }
        if ((48000 << stages) != rateHz || stages > kMaxStages)
            return false;
        if (rateHz != m_rate) {
            m_rate = rateHz;
            for (auto& st : m_stage)
                st = Stage{};
            m_phase = 0.0;
        }
        const double step = -2.0 * 3.14159265358979323846 * shiftHz / static_cast<double>(rateHz);
        m_a.resize(iq.size());
        for (std::size_t n = 0; n < iq.size(); ++n) {
            const std::complex<float> lo(static_cast<float>(std::cos(m_phase)), static_cast<float>(std::sin(m_phase)));
            m_a[n] = iq[n] * lo;
            m_phase += step;
            if (m_phase > 6.283185307179586)
                m_phase -= 6.283185307179586;
            else if (m_phase < -6.283185307179586)
                m_phase += 6.283185307179586;
        }
        for (int s = 0; s < stages; ++s) {
            m_b.clear();
            Stage& st = m_stage[s];
            for (const std::complex<float>& x : m_a) {
                st.h[st.pos] = x;
                st.pos = (st.pos + 1) % kTaps;
                st.odd = !st.odd;
                if (st.odd)
                    continue;
                // The newest sample is at pos-1; tap k looks k samples back.
                const auto at = [&st](int back) { return st.h[(st.pos - 1 - back + 2 * kTaps) % kTaps]; };
                std::complex<float> acc = kCentre * at(kHalf);
                for (int k = 0; k < 6; ++k)
                    acc += kOdd[k] * (at(kHalf - 1 - 2 * k) + at(kHalf + 1 + 2 * k));
                m_b.push_back(acc);
            }
            m_a.swap(m_b);
        }
        m_i.resize(m_a.size());
        m_q.resize(m_a.size());
        for (std::size_t n = 0; n < m_a.size(); ++n) {
            m_i[n] = m_a[n].real();
            m_q[n] = m_a[n].imag();
        }
        return true;
    }
    [[nodiscard]] const std::vector<float>& i() const { return m_i; }
    [[nodiscard]] const std::vector<float>& q() const { return m_q; }

private:
    static constexpr int kTaps = 23;
    static constexpr int kHalf = 11;
    static constexpr int kMaxStages = 6;   // 3072 kHz
    static constexpr float kCentre = 0.4999765f;
    // Taps 1, 3, 5, 7, 9, 11 from the centre (Kaiser 8.0 windowed sinc).
    static constexpr float kOdd[6] = {0.308586444f, -0.079928822f, 0.02820326f,
                                      -0.008360258f, 0.001578801f, -6.7676e-05f};
    struct Stage {
        std::complex<float> h[kTaps] = {};
        int pos = 0;
        bool odd = false;
    };
    Stage m_stage[kMaxStages];
    int m_rate = 0;
    double m_phase = 0.0;
    std::vector<std::complex<float>> m_a, m_b;
    std::vector<float> m_i, m_q;
};

}  // namespace AetherSDR::ftx
