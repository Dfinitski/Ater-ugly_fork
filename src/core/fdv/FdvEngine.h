#pragma once

// ANAN-100D port: FreeDV digital voice engine for Hl2Backend, ported from
// MicronSDR's FdvUnit. libfdv.dll (+ libcodec2.dll) -- built from MicronSDR's
// FreeDV_src, C API in third_party/libfdv/fdv_api.h -- does all the work in its
// own thread: RADE V1 (rade_c + Opus FARGAN/LPCNet), 1600, 700D, 700E (codec2).
// Its audio is 48 kHz mono float on both sides; Hl2Backend's receive audio and
// microphone are 24 kHz, so this class converts 24 <-> 48 kHz around it.
//
//   RX: demodulated USB audio (24 kHz stereo) -> libfdv -> decoded speech
//   TX: microphone (24 kHz mono) -> libfdv -> modem audio (USB, 0..3 kHz)
//
// All calls from one thread (the backend's). libfdv's own calls only move
// samples through FIFOs, so they are cheap.

#include <QString>

#include <array>
#include <vector>

namespace AetherSDR::fdv {

// 2:1 interpolator / decimator between 24 and 48 kHz (windowed-sinc FIR,
// passband to ~10 kHz, which is far wider than any FreeDV signal).
class HalfbandResampler {
public:
    HalfbandResampler();
    void reset();
    // 24 kHz -> 48 kHz: out gets 2 * in.size() samples.
    void up(const float* in, std::size_t n, std::vector<float>& out);
    // 48 kHz -> 24 kHz: out gets n / 2 samples (n even).
    void down(const float* in, std::size_t n, std::vector<float>& out);

private:
    static constexpr int kTaps = 47;
    std::array<float, kTaps> m_h{};
    std::array<float, kTaps> m_hist{};
    int m_pos = 0;
    float push(float x);   // filter one 48 kHz sample
};

class FdvEngine {
public:
    struct Status {
        int mode = 0;
        int sync = 0;
        float snrDb = 0.0f;
        float freqOffsetHz = 0.0f;
        int rxUnderruns = 0;
        int txUnderruns = 0;
        int txActive = 0;
        char callsign[16] = {};
        int callsignSerial = 0;
    };

    FdvEngine();
    ~FdvEngine();
    FdvEngine(const FdvEngine&) = delete;
    FdvEngine& operator=(const FdvEngine&) = delete;

    // Loads libfdv once; false with error() when it is not there.
    bool load();
    [[nodiscard]] bool loaded() const { return m_obj != nullptr; }
    [[nodiscard]] QString error() const { return m_err; }

    void setLibMode(int libMode);              // FDV_MODE_* (FdvState::fdvLibMode)
    [[nodiscard]] int libMode() const { return m_mode; }
    void setCallsign(const QString& callsign); // sent at the end of a RADE over,
                                               // and on the 1600/700D/700E text channel

    // Receiver running FDV (index into Hl2Backend's receivers), -1 = none.
    void setRxReceiver(int rx);
    [[nodiscard]] int rxReceiver() const { return m_rxReceiver; }
    // Interleaved 24 kHz stereo, replaced in place by the decoded speech.
    void processRx(std::vector<float>& stereo);

    void txStart();
    void txStop();                              // RADE: the end-of-over frame follows
    [[nodiscard]] bool txRunning() const { return m_txRunning; }
    // 24 kHz mono microphone, replaced in place by the modem audio.
    void processTx(std::vector<float>& mono);
    // Polls libfdv; clears txRunning() once the end-of-over has been read out.
    bool status(Status* st);

private:
    struct Api;
    Api* m_api = nullptr;
    void* m_obj = nullptr;
    QString m_err;
    bool m_tried = false;
    int m_mode = 1;
    QString m_callsign;
    int m_rxReceiver = -1;
    bool m_txRunning = false;
    HalfbandResampler m_rxUp, m_rxDown, m_txUp, m_txDown;
    std::vector<float> m_ra, m_rb, m_rc;   // receive scratch
    std::vector<float> m_tb, m_tc;         // transmit scratch
};

}  // namespace AetherSDR::fdv
