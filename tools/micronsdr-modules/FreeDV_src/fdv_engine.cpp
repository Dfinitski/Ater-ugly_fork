/*
 * fdv_engine.cpp - FreeDV digital voice for MicronSDR, see fdv_api.h
 *
 *  RX: 48 kHz audio -> /6 -> 8 kHz modem -> RADE: features -> FARGAN 16 kHz -> x3 -> 48 kHz
 *                                         codec2: 8 kHz speech -> x6 -> 48 kHz
 *  TX: 48 kHz mic -> /3 -> 16 kHz -> LPCNet features -> RADE -> 8 kHz modem -> x6 -> 48 kHz
 *                 -> /6 -> 8 kHz -> codec2 freedv_tx -> 8 kHz modem -> x6 -> 48 kHz
 *
 * The pipelines follow freedv-backend (RADEReceiveStep / RADETransmitStep) and freedv-gui
 * (FreeDVReceiveStep / FreeDVTransmitStep).
 */
#define FDV_BUILDING 1
#include "fdv_api.h"

#include <atomic>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <io.h>
#define FDV_DEVNULL "NUL"
#else
#include <unistd.h>
#define FDV_DEVNULL "/dev/null"
#endif
#include <fcntl.h>

// rade_open() of rade_c writes info lines to stderr whatever the verbose flag (the program shows
// them in its console / log): stderr is sent to the null device meanwhile
static void *quietStderrBegin()
{
    fflush(stderr);
#ifdef _WIN32
    const int saved = _dup(_fileno(stderr));
    const int nul = _open(FDV_DEVNULL, _O_WRONLY);
    if(saved >= 0 && nul >= 0)
        _dup2(nul, _fileno(stderr));
    if(nul >= 0)
        _close(nul);
#else
    const int saved = dup(fileno(stderr));
    const int nul = open(FDV_DEVNULL, O_WRONLY);
    if(saved >= 0 && nul >= 0)
        dup2(nul, fileno(stderr));
    if(nul >= 0)
        close(nul);
#endif
    return (void *)(intptr_t)(saved + 1);
}

static void quietStderrEnd(void *h)
{
    const int saved = (int)(intptr_t)h - 1;
    fflush(stderr);
    if(saved < 0)
        return;
#ifdef _WIN32
    _dup2(saved, _fileno(stderr));
    _close(saved);
#else
    dup2(saved, fileno(stderr));
    close(saved);
#endif
}

extern "C" {
#include "rade_api.h"
#include "lpcnet.h"
#include "fargan.h"
#include "cpu_support.h"
#include "freedv_api.h"
#include "reliable_text.h"
#include "varicode.h"
void fdvc2_get_txt_callbacks(struct freedv *f, void (**rx)(void *, char), char (**tx)(void *), void **state);
}
#include "rade_text.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// ---- FIFO of floats (the audio thread and the worker; guarded by the engine mutex) ----------
struct Fifo
{
    std::vector<float> b;
    size_t r = 0, w = 0, n = 0;
    explicit Fifo(size_t cap = 0) : b(cap) {}
    size_t used() const { return n; }
    size_t free() const { return b.size() - n; }
    void reset() { r = w = n = 0; }
    size_t write(const float *x, size_t k)
    {
        k = std::min(k, free());
        for(size_t i = 0; i < k; i++)
        {
            b[w] = x ? x[i] : 0.0f;
            w = (w + 1 == b.size()) ? 0 : w + 1;
        }
        n += k;
        return k;
    }
    size_t read(float *x, size_t k)
    {
        k = std::min(k, n);
        for(size_t i = 0; i < k; i++)
        {
            if(x)
                x[i] = b[r];
            r = (r + 1 == b.size()) ? 0 : r + 1;
        }
        n -= k;
        return k;
    }
};

// ---- windowed-sinc FIR, decimation and interpolation by an integer factor -------------------
static std::vector<float> lowpass(int taps, double fc /* of fs */, double gain)
{
    std::vector<float> h(taps);
    const double m = (taps - 1) / 2.0;
    double sum = 0.0;
    for(int i = 0; i < taps; i++)
    {
        const double x = i - m;
        const double s = (x == 0.0) ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
        // Blackman-Harris window: > 90 dB stop band
        const double t = 2.0 * M_PI * i / (taps - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        h[i] = (float)(s * w);
        sum += h[i];
    }
    for(float &v : h)
        v = (float)(v * gain / sum);
    return h;
}

struct Decim
{
    int D = 1;
    std::vector<float> h, hist;
    int pos = 0, phase = 0;
    void init(int d, double fcOut /* Hz */, double fsIn)
    {
        D = d;
        h = lowpass(D * 48 + 1, fcOut / fsIn, 1.0);
        hist.assign(h.size(), 0.0f);
        pos = phase = 0;
    }
    void reset() { std::fill(hist.begin(), hist.end(), 0.0f); pos = phase = 0; }
    // returns the number of outputs written
    int run(const float *in, int n, float *out)
    {
        int k = 0;
        const int L = (int)h.size();
        for(int i = 0; i < n; i++)
        {
            hist[pos] = in[i];
            pos = (pos + 1 == L) ? 0 : pos + 1;
            if(++phase == D)
            {
                phase = 0;
                double acc = 0.0;
                int p = pos;            // oldest sample
                for(int j = L - 1; j >= 0; j--)
                {
                    acc += h[j] * hist[p];
                    p = (p + 1 == L) ? 0 : p + 1;
                }
                out[k++] = (float)acc;
            }
        }
        return k;
    }
};

struct Interp
{
    int U = 1;
    std::vector<float> h, hist;     // h: U x taps per phase
    int pos = 0, tpp = 0;
    void init(int u, double fcIn /* Hz */, double fsOut)
    {
        U = u;
        tpp = 48;
        std::vector<float> f = lowpass(U * tpp, fcIn / fsOut, (double)U);
        h = f;
        hist.assign(tpp, 0.0f);
        pos = 0;
    }
    void reset() { std::fill(hist.begin(), hist.end(), 0.0f); pos = 0; }
    int run(const float *in, int n, float *out)
    {
        int k = 0;
        for(int i = 0; i < n; i++)
        {
            hist[pos] = in[i];
            pos = (pos + 1 == tpp) ? 0 : pos + 1;
            for(int ph = 0; ph < U; ph++)
            {
                double acc = 0.0;
                int p = (pos == 0) ? tpp - 1 : pos - 1;     // newest
                for(int j = 0; j < tpp; j++)
                {
                    acc += h[j * U + ph] * hist[p];
                    p = (p == 0) ? tpp - 1 : p - 1;
                }
                out[k++] = (float)acc;
            }
        }
        return k;
    }
};

static std::once_flag radeInitOnce;

} // namespace

// ---- the engine ------------------------------------------------------------------------------
struct fdv
{
    // shared with the audio thread
    std::mutex m;
    std::condition_variable cv;
    Fifo rxIn{48000}, rxOut{96000}, txIn{48000}, txOut{96000};
    bool rxOutPlaying = false, txOutPlaying = false;
    std::atomic<int> wantMode{0};
    std::atomic<bool> quit{false};
    std::atomic<bool> txOn{false}, txStopReq{false}, txStartReq{false};
    std::atomic<int> rxUnder{0}, txUnder{0};
    std::string callsign;
    bool callsignNew = false;
    std::string txText;                 // 1600 / 700D / 700E: the text message (sent over and over)
    bool txTextNew = false;
    std::atomic<bool> txReliable{false}; // 1600 / 700D / 700E: send the callsign as FreeDV-GUI (reliable text)
    std::string rxText;                 // the text received, not read yet (fdv_read_text)
    fdv_status_t st;

    // worker only
    std::thread th;
    int mode = -1;
    struct rade *rade = nullptr;
    FARGANState fargan;
    bool farganReady = false;
    std::vector<float> farganWarm;      // the first 5 feature frames (fargan_cont)
    LPCNetEncState *lpcEnc = nullptr;
    int arch = 0;
    rade_text_t radeText = nullptr;
    struct freedv *c2 = nullptr;
    std::string c2Tx;                   // worker: the text message being sent ("" none), its next char
    size_t c2TxPos = 0;
    // the text channel runs raw bits (varicode code 3, as the reliable text of FreeDV-GUI wants):
    // received - to the reliable text (the callsign) and to a varicode decoder (the plain text),
    // both always; sent - the reliable text bits or the varicode bits of the message
    reliable_text_t c2Rel = nullptr;
    void (*relRx)(void *, char) = nullptr;
    char (*relTx)(void *) = nullptr;
    void *relState = nullptr;
    VARICODE_DEC plainDec;
    // the time: the modem samples received (8 kHz), s - not the wall clock (the audio may come in
    // bursts); the last reliable text seen (its UW)
    double rxT = 0.0, relLast = -1e9;
    bool relSeen = false;
    int ones = 0;                       // one bits in a row (the UW of the reliable text: 16)
    // the plain characters held until the stream is known as plain text (6 s without the UW of
    // a reliable text - its bits decode as random characters), then out as they come
    std::vector<std::pair<double, char> > pendPlain;
    bool plainOk = false;
    int lastSync = 0;
    void plainFlush();
    short txBits[VARICODE_MAX_BITS];
    int txBitN = 0, txBitI = 0;
    Decim rxDec, txDec;
    Interp rxInt, txInt;
    std::vector<float> modemIn;         // 8 kHz modem samples to the demodulator
    double rxPow = 0.01, rxGain = 1.0;  // level control of the modem input
    std::vector<float> speechIn;        // 8 / 16 kHz speech to the encoder
    std::vector<float> features;        // RADE TX features of one modem frame
    int featIdx = 0;
    std::vector<float> pendFeat;        // RADE RX features of one 10 ms frame
    int pendIdx = 0;
    std::atomic<bool> txRunning{false};

    void openMode(int md);
    void closeMode();
    void rxBlock(const float *x48, int n);
    void txBlock(const float *x48, int n);
    void txEnd();
    void pushRx(const float *speech, int n, int rate);
    void pushTx(const float *modem8k, int n);
    void loop();
    static void radeTextRx(rade_text_t, const char *txt, int len, void *state);
    static void c2TxtRx(void *state, char c);
    static char c2TxtTx(void *state);
    static void c2RelRx(reliable_text_t, const char *txt, int len, void *state);
    void c2SetText(const std::string &t);
    void gotCallsign(const char *txt, int len);
};

// ---- modes -------------------------------------------------------------------------------------

void fdv::closeMode()
{
    if(rade) { rade_close(rade); rade = nullptr; }
    if(radeText) { rade_text_destroy(radeText); radeText = nullptr; }
    if(lpcEnc) { lpcnet_encoder_destroy(lpcEnc); lpcEnc = nullptr; }
    if(c2Rel) { reliable_text_unlink_from_freedv(c2Rel); reliable_text_destroy(c2Rel); c2Rel = nullptr; }
    if(c2) { freedv_close(c2); c2 = nullptr; }
    mode = -1;
}

void fdv::openMode(int md)
{
    closeMode();
    mode = md;
    modemIn.clear();
    speechIn.clear();
    featIdx = 0;
    pendIdx = 0;
    farganReady = false;
    farganWarm.clear();
    {
        std::lock_guard<std::mutex> lk(m);
        rxIn.reset(); rxOut.reset(); txIn.reset(); txOut.reset();
        rxOutPlaying = txOutPlaying = false;
        st = fdv_status_t();
        st.mode = md;
        callsignNew = true;
        txTextNew = true;
    }
    // (made without the lock: the audio thread is not held meanwhile)
    if(md == FDV_MODE_RADE)
    {
        std::call_once(radeInitOnce, [] { rade_initialize(); });
        char model[] = "";
        void *q = quietStderrBegin();
        rade = rade_open(model, RADE_USE_C_ENCODER | RADE_USE_C_DECODER | RADE_VERBOSE_0);
        quietStderrEnd(q);
        radeText = rade_text_create();
        rade_text_set_rx_callback(radeText, &fdv::radeTextRx, this);
        fargan_init(&fargan);
        lpcEnc = lpcnet_encoder_create();
        arch = opus_select_arch();
        features.assign(rade_n_features_in_out(rade), 0.0f);
        pendFeat.assign(NB_TOTAL_FEATURES, 0.0f);
        rxDec.init(6, 3600.0, 48000.0);         // 8 kHz modem
        rxInt.init(3, 7600.0, 48000.0);         // 16 kHz speech
        txDec.init(3, 7600.0, 48000.0);         // 16 kHz speech
        txInt.init(6, 3600.0, 48000.0);         // 8 kHz modem
    }
    else
    {
        const int c2mode = (md == FDV_MODE_1600) ? FREEDV_MODE_1600 : (md == FDV_MODE_700D) ? FREEDV_MODE_700D : FREEDV_MODE_700E;
        c2 = freedv_open(c2mode);
        freedv_set_squelch_en(c2, true);
        if(md != FDV_MODE_1600)
        {
            freedv_set_clip(c2, true);          // as FreeDV-GUI: 700D / 700E clipped + BPF
            freedv_set_tx_bpf(c2, 1);
        }
        // the text channel: the reliable text of FreeDV-GUI takes it (varicode code 3: raw bits),
        // then its callbacks are taken over: the bits go to it and to a plain varicode decoder
        c2Rel = reliable_text_create();
        reliable_text_use_with_freedv(c2Rel, c2, &fdv::c2RelRx, this);
        fdvc2_get_txt_callbacks(c2, &relRx, &relTx, &relState);
        varicode_decode_init(&plainDec, 1);
        txBitN = txBitI = 0;
        freedv_set_callback_txt(c2, &fdv::c2TxtRx, &fdv::c2TxtTx, this);
        c2SetText(std::string());       // the message: set in the worker loop (txTextNew)
        rxDec.init(6, 3600.0, 48000.0);
        rxInt.init(6, 3600.0, 48000.0);
        txDec.init(6, 3600.0, 48000.0);
        txInt.init(6, 3600.0, 48000.0);
    }
}

void fdv::gotCallsign(const char *txt, int len)
{
    std::lock_guard<std::mutex> lk(m);
    std::string s(txt, std::max(0, std::min(len, 15)));
    while(!s.empty() && (s.back() == ' ' || s.back() == 0))
        s.pop_back();
    std::strncpy(st.callsign, s.c_str(), sizeof(st.callsign) - 1);
    st.callsign[sizeof(st.callsign) - 1] = 0;
    st.callsignSerial++;
}

void fdv::radeTextRx(rade_text_t, const char *txt, int len, void *state)
{
    static_cast<fdv *>(state)->gotCallsign(txt, len);
}

// 1600 / 700D / 700E: a bit of the text channel received (called by freedv_rx in the worker):
// to the reliable text (a callsign of FreeDV-GUI) and to the plain varicode decoder (a message)
void fdv::c2TxtRx(void *state, char bit)
{
    fdv *f = static_cast<fdv *>(state);
    if(f->relRx)
        f->relRx(f->relState, bit);
    const double now = f->rxT;
    f->ones = bit ? f->ones + 1 : 0;
    if(f->ones >= 14)
    {
        // the UW of a reliable text (no varicode has so many ones in a row): not plain text
        f->relSeen = true;
        f->relLast = now;
        f->pendPlain.clear();
        f->plainOk = false;
    }
    short b = bit ? 1 : 0;
    char c = 0;
    if(varicode_decode(&f->plainDec, &c, &b, 1, 1) != 1)
        return;
    const unsigned char u = (unsigned char)c;
    if(u != '\r' && u != '\n' && (u < 32 || u > 126))
        return;                         // only printable ASCII and the line ends
    // a station sending its callsign as reliable text: its bits are no plain text (they would
    // come out as random characters) - none for 30 s after its UW
    if(f->relSeen && now - f->relLast < 30.0)
        return;
    f->pendPlain.push_back(std::make_pair(now, (u == '\r') ? '\n' : (char)u));
    f->plainFlush();
}

// the plain characters out: all once the stream is plain (6 s without a UW), else those older
void fdv::plainFlush()
{
    size_t k = 0;
    while(k < pendPlain.size() && (plainOk || rxT - pendPlain[k].first >= 6.0))
    {
        plainOk = true;
        k++;
    }
    if(!k)
        return;
    std::lock_guard<std::mutex> lk(m);
    for(size_t i = 0; i < k && rxText.size() < 4096; i++)
        rxText += pendPlain[i].second;
    pendPlain.erase(pendPlain.begin(), pendPlain.begin() + k);
}

// a bit of the text channel to send (called by freedv_tx in the worker): the reliable text
// (the callsign, as FreeDV-GUI) or the varicode bits of the message, over and over; nothing:
// zeros (no character)
char fdv::c2TxtTx(void *state)
{
    fdv *f = static_cast<fdv *>(state);
    if(f->txReliable.load())
        return f->relTx ? f->relTx(f->relState) : 0;
    if(f->txBitI >= f->txBitN)
    {
        f->txBitN = f->txBitI = 0;
        if(f->c2Tx.empty())
            return 0;
        char ch = f->c2Tx[f->c2TxPos];
        f->c2TxPos = (f->c2TxPos + 1) % f->c2Tx.size();
        f->txBitN = varicode_encode(f->txBits, &ch, VARICODE_MAX_BITS, 1, 1);
        if(f->txBitN <= 0)
            return 0;
    }
    return (char)(f->txBits[f->txBitI++] ? 1 : 0);
}

void fdv::c2RelRx(reliable_text_t rt, const char *txt, int len, void *state)
{
    fdv *f = static_cast<fdv *>(state);
    f->gotCallsign(txt, len);
    f->relLast = f->rxT;
    f->relSeen = true;
    reliable_text_reset(rt);
}

// the message: sent over and over, a CR after each (the receivers start a new line)
void fdv::c2SetText(const std::string &t)
{
    c2Tx = t.empty() ? std::string() : t + "\r";
    c2TxPos = 0;
    txBitN = txBitI = 0;
}

// ---- receiver ----------------------------------------------------------------------------------

void fdv::pushRx(const float *speech, int n, int rate)
{
    std::vector<float> out((size_t)n * (48000 / rate));
    Interp &ip = rxInt;
    const int k = ip.run(speech, n, out.data());
    std::lock_guard<std::mutex> lk(m);
    rxOut.write(out.data(), k);
}

void fdv::rxBlock(const float *x48, int n)
{
    std::vector<float> d8(n / 6 + 2);
    const int k = rxDec.run(x48, n, d8.data());
    // a slow level control of the modem input: the receiver's audio (after its AGC) comes in
    // any level; the demodulators work best around -20 dBFS (near full scale 1600 lost sync)
    for(int i = 0; i < k; i++)
    {
        rxPow += 0.00025 * ((double)d8[i] * d8[i] - rxPow);        // ~0.5 s at 8 kHz
        double g = 0.1 / std::sqrt(rxPow + 1e-12);
        g = std::max(0.05, std::min(1000.0, g));
        rxGain += 0.0005 * (g - rxGain);
        d8[i] = (float)(d8[i] * rxGain);
    }
    modemIn.insert(modemIn.end(), d8.begin(), d8.begin() + k);
    rxT += k / 8000.0;
    if(rade)
    {
        std::vector<RADE_COMP> in(rade_nin_max(rade));
        std::vector<float> fo(rade_n_features_in_out(rade));
        std::vector<float> eoo(std::max(1, rade_n_eoo_bits(rade)));
        for(;;)
        {
            const int nin = rade_nin(rade);
            if((int)modemIn.size() < nin)
                break;
            for(int i = 0; i < nin; i++)
            {
                in[i].real = modemIn[i];
                in[i].imag = 0.0f;
            }
            modemIn.erase(modemIn.begin(), modemIn.begin() + nin);
            int hasEoo = 0;
            const int nout = rade_rx(rade, fo.data(), &hasEoo, eoo.data(), in.data());
            if(hasEoo)
                rade_text_rx(radeText, eoo.data(), rade_n_eoo_bits(rade) / 2);
            else
            {
                for(int i = 0; i < nout; i++)
                {
                    pendFeat[pendIdx++] = fo[i];
                    if(pendIdx < NB_TOTAL_FEATURES)
                        continue;
                    pendIdx = 0;
                    if(!farganReady)
                    {
                        // fargan_cont: 5 frames of NB_FEATURES to start the vocoder
                        farganWarm.insert(farganWarm.end(), pendFeat.begin(), pendFeat.begin() + NB_FEATURES);
                        if((int)farganWarm.size() >= 5 * NB_FEATURES)
                        {
                            float zeros[FARGAN_CONT_SAMPLES] = { 0 };
                            fargan_cont(&fargan, zeros, farganWarm.data());
                            farganReady = true;
                        }
                        continue;
                    }
                    float pcm[LPCNET_FRAME_SIZE];
                    fargan_synthesize(&fargan, pcm, pendFeat.data());
                    pushRx(pcm, LPCNET_FRAME_SIZE, 16000);
                }
            }
            const int sync = rade_sync(rade);
            std::lock_guard<std::mutex> lk(m);
            st.sync = sync;
            if(sync)
            {
                st.snrDb = rade_snrdB_3k_est(rade);
                st.freqOffsetHz = rade_freq_offset(rade);
            }
            else if(farganReady)
            {
                // sync lost: the next over starts the vocoder again
            }
        }
        if(!rade_sync(rade) && farganReady && pendIdx == 0)
        {
            farganReady = false;
            farganWarm.clear();
            fargan_init(&fargan);
        }
    }
    else if(c2)
    {
        std::vector<short> in(freedv_get_n_max_modem_samples(c2));
        std::vector<short> sp(freedv_get_n_max_speech_samples(c2));
        std::vector<float> spf(sp.size());
        for(;;)
        {
            const int nin = freedv_nin(c2);
            if((int)modemIn.size() < nin)
                break;
            for(int i = 0; i < nin; i++)
                in[i] = (short)std::max(-32767.0f, std::min(32767.0f, modemIn[i] * 32767.0f));
            modemIn.erase(modemIn.begin(), modemIn.begin() + nin);
            const int nout = freedv_rx(c2, sp.data(), in.data());
            for(int i = 0; i < nout; i++)
                spf[i] = sp[i] / 32768.0f;
            if(nout > 0)
                pushRx(spf.data(), nout, 8000);
            int sync = 0;
            float snr = 0.0f;
            freedv_get_modem_stats(c2, &sync, &snr);
            if(!sync && lastSync)
            {
                pendPlain.clear();      // the over ended: the next one may be another station
                plainOk = false;
                ones = 0;
            }
            lastSync = sync;
            plainFlush();
            std::lock_guard<std::mutex> lk(m);
            st.sync = sync;
            if(sync && std::isfinite(snr))
                st.snrDb = 0.8f * st.snrDb + 0.2f * snr;
        }
    }
}

// ---- transmitter -------------------------------------------------------------------------------

void fdv::pushTx(const float *modem8k, int n)
{
    std::vector<float> out((size_t)n * 6);
    const int k = txInt.run(modem8k, n, out.data());
    std::lock_guard<std::mutex> lk(m);
    txOut.write(out.data(), k);
}

// output levels: the modem audio peaks near full scale (the ALC of the transmitter holds it)
static const float RADE_TX_GAIN = 0.75f;        // RADE: nominal IQ 1.0 -> 0.75 (PAPR ~ 4 dB)
static const float C2_TX_GAIN   = 1.0f / 16384.0f;

void fdv::txBlock(const float *x48, int n)
{
    if(rade)
    {
        std::vector<float> s16(n / 3 + 2);
        const int k = txDec.run(x48, n, s16.data());
        speechIn.insert(speechIn.end(), s16.begin(), s16.begin() + k);
        const int framesPerModem = (int)features.size() / NB_TOTAL_FEATURES;
        std::vector<RADE_COMP> out(rade_n_tx_out(rade));
        std::vector<float> mod(out.size());
        while((int)speechIn.size() >= LPCNET_FRAME_SIZE)
        {
            opus_int16 pcm[LPCNET_FRAME_SIZE];
            for(int i = 0; i < LPCNET_FRAME_SIZE; i++)
                pcm[i] = (opus_int16)std::lround(std::max(-32767.0f, std::min(32767.0f, speechIn[i] * 32768.0f)));
            speechIn.erase(speechIn.begin(), speechIn.begin() + LPCNET_FRAME_SIZE);
            lpcnet_compute_single_frame_features(lpcEnc, pcm, &features[featIdx * NB_TOTAL_FEATURES], arch);
            if(++featIdx < framesPerModem)
                continue;
            featIdx = 0;
            const int no = rade_tx(rade, out.data(), features.data());
            for(int i = 0; i < no; i++)
                mod[i] = out[i].real * RADE_TX_GAIN;
            pushTx(mod.data(), no);
        }
    }
    else if(c2)
    {
        std::vector<float> s8(n / 6 + 2);
        const int k = txDec.run(x48, n, s8.data());
        speechIn.insert(speechIn.end(), s8.begin(), s8.begin() + k);
        const int ns = freedv_get_n_speech_samples(c2);
        const int nm = freedv_get_n_nom_modem_samples(c2);
        std::vector<short> sp(ns), mo(nm);
        std::vector<float> mf(nm);
        while((int)speechIn.size() >= ns)
        {
            for(int i = 0; i < ns; i++)
                sp[i] = (short)std::lround(std::max(-32767.0f, std::min(32767.0f, speechIn[i] * 32768.0f)));
            speechIn.erase(speechIn.begin(), speechIn.begin() + ns);
            freedv_tx(c2, mo.data(), sp.data());
            for(int i = 0; i < nm; i++)
                mf[i] = mo[i] * C2_TX_GAIN;
            pushTx(mf.data(), nm);
        }
    }
}

// the end of an over: RADE sends its EOO frame (with the callsign) and 200 ms of silence
void fdv::txEnd()
{
    if(rade)
    {
        std::vector<RADE_COMP> eoo(rade_n_tx_eoo_out(rade));
        const int n = rade_tx_eoo(rade, eoo.data());
        std::vector<float> mod(n + 1600, 0.0f);
        for(int i = 0; i < n; i++)
            mod[i] = eoo[i].real * RADE_TX_GAIN;
        pushTx(mod.data(), (int)mod.size());
    }
    else
    {
        std::vector<float> z(1600, 0.0f);          // 200 ms: the last frame leaves the radio
        pushTx(z.data(), (int)z.size());
    }
}

// ---- worker ------------------------------------------------------------------------------------

void fdv::loop()
{
    std::vector<float> blk(4800);
    while(!quit.load())
    {
        const int wm = wantMode.load();
        if(wm != mode)
            openMode(wm);
        bool work = false;
        // the callsign of the next EOO / text channel
        {
            std::string cs;
            bool newCs = false;
            {
                std::lock_guard<std::mutex> lk(m);
                if(callsignNew)
                {
                    cs = callsign;
                    newCs = true;
                    callsignNew = false;
                }
            }
            if(newCs && c2Rel)
                reliable_text_set_string(c2Rel, cs.c_str(), (int)cs.size());   // the callsign as FreeDV-GUI
            if(newCs)
            {
                if(rade && radeText)
                {
                    const int nb = rade_n_eoo_bits(rade);
                    std::vector<float> syms(nb);
                    rade_text_generate_tx_string(radeText, cs.c_str(), (int)cs.size(), syms.data(), nb);
                    rade_tx_set_eoo_bits(rade, syms.data());
                }
            }
        }
        // the text message of 1600 / 700D / 700E
        {
            std::string t;
            bool newT = false;
            {
                std::lock_guard<std::mutex> lk(m);
                if(txTextNew)
                {
                    t = txText;
                    newT = true;
                    txTextNew = false;
                }
            }
            if(newT && c2)
                c2SetText(t);
        }
        if(txStartReq.exchange(false))
        {
            txRunning = true;
            speechIn.clear();
            featIdx = 0;
            txDec.reset();
            txInt.reset();
            std::lock_guard<std::mutex> lk(m);
            txIn.reset();
            txOut.reset();
            txOutPlaying = false;
            st.txActive = 1;
        }
        // receiver
        {
            int k;
            {
                std::lock_guard<std::mutex> lk(m);
                k = (int)rxIn.read(blk.data(), blk.size());
            }
            if(k > 0)
            {
                rxBlock(blk.data(), k);
                work = true;
            }
        }
        // transmitter
        if(txRunning)
        {
            int k;
            {
                std::lock_guard<std::mutex> lk(m);
                k = (int)txIn.read(blk.data(), blk.size());
            }
            if(k > 0)
            {
                txBlock(blk.data(), k);
                work = true;
            }
            if(txStopReq.exchange(false))
            {
                txEnd();
                txRunning = false;
                work = true;
            }
        }
        else
            txStopReq.store(false);
        if(!work)
        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait_for(lk, std::chrono::milliseconds(20));
        }
    }
    closeMode();
}

// ---- API ----------------------------------------------------------------------------------------

extern "C" {

FDV_EXPORT int fdv_version(void) { return 110; }

FDV_EXPORT void fdv_set_text(fdv_t *f, const char *text)
{
    std::string t;
    for(const char *p = text ? text : ""; *p && t.size() < 60; p++)
        if((unsigned char)*p >= 32 && (unsigned char)*p <= 126)
            t += *p;                    // printable ASCII only
    std::lock_guard<std::mutex> lk(f->m);
    f->txText = t;
    f->txTextNew = true;
}

FDV_EXPORT void fdv_set_text_mode(fdv_t *f, int reliableCallsign)
{
    f->txReliable = reliableCallsign != 0;
}

FDV_EXPORT int fdv_read_text(fdv_t *f, char *buf, int size)
{
    if(!buf || size <= 0)
        return 0;
    std::lock_guard<std::mutex> lk(f->m);
    const int n = std::min((int)f->rxText.size(), size - 1);
    std::memcpy(buf, f->rxText.data(), n);
    buf[n] = 0;
    f->rxText.erase(0, n);
    return n;
}

FDV_EXPORT fdv_t *fdv_create(int mode)
{
    fdv *f = new fdv();
    f->st = fdv_status_t();
    f->wantMode = std::max(0, std::min(FDV_MODE_COUNT - 1, mode));
    f->th = std::thread([f] { f->loop(); });
    return f;
}

FDV_EXPORT void fdv_destroy(fdv_t *f)
{
    if(!f)
        return;
    f->quit = true;
    f->cv.notify_all();
    if(f->th.joinable())
        f->th.join();
    delete f;
}

FDV_EXPORT void fdv_set_mode(fdv_t *f, int mode)
{
    f->wantMode = std::max(0, std::min(FDV_MODE_COUNT - 1, mode));
    f->cv.notify_all();
}

FDV_EXPORT void fdv_set_callsign(fdv_t *f, const char *cs)
{
    std::lock_guard<std::mutex> lk(f->m);
    f->callsign = cs ? cs : "";
    if(f->callsign.size() > 8)
        f->callsign.resize(8);
    f->callsignNew = true;
}

FDV_EXPORT void fdv_rx_write(fdv_t *f, const float *in, int n)
{
    {
        std::lock_guard<std::mutex> lk(f->m);
        f->rxIn.write(in, n);
    }
    f->cv.notify_all();
}

// the decoded speech comes in frames (RADE: 120 ms of features per modem frame): it is played
// from a reserve of 200 ms, and after an underrun only when the reserve is there again
FDV_EXPORT int fdv_rx_read(fdv_t *f, float *out, int n)
{
    std::lock_guard<std::mutex> lk(f->m);
    if(!f->rxOutPlaying && f->rxOut.used() >= 9600)
        f->rxOutPlaying = true;
    size_t k = f->rxOutPlaying ? f->rxOut.read(out, n) : 0;
    if((int)k < n)
    {
        std::memset(out + k, 0, sizeof(float) * (n - k));
        if(f->rxOutPlaying)
        {
            f->rxOutPlaying = false;
            if(k > 0)
                f->rxUnder++;
        }
    }
    return n;
}

FDV_EXPORT void fdv_tx_start(fdv_t *f)
{
    f->txStartReq = true;
    f->txStopReq = false;
    {
        std::lock_guard<std::mutex> lk(f->m);
        f->st.txActive = 1;
    }
    f->cv.notify_all();
}

FDV_EXPORT void fdv_tx_stop(fdv_t *f)
{
    f->txStopReq = true;
    f->cv.notify_all();
}

FDV_EXPORT void fdv_tx_write(fdv_t *f, const float *mic, int n)
{
    {
        std::lock_guard<std::mutex> lk(f->m);
        f->txIn.write(mic, n);
    }
    f->cv.notify_all();
}

// the modem audio: played from a reserve (one RADE modem frame and 60 ms), gaps would splatter
FDV_EXPORT int fdv_tx_read(fdv_t *f, float *out, int n)
{
    std::lock_guard<std::mutex> lk(f->m);
    // the reserve; after the stop the rest (the EOO) goes out as it is
    if(!f->txOutPlaying && (f->txOut.used() >= 9600 || (!f->txRunning && f->txOut.used() > 0)))
        f->txOutPlaying = true;
    size_t k = f->txOutPlaying ? f->txOut.read(out, n) : 0;
    if((int)k < n)
    {
        std::memset(out + k, 0, sizeof(float) * (n - k));
        if(f->txOutPlaying && f->st.txActive && !f->txStopReq && f->txRunning)
            f->txUnder++;
        f->txOutPlaying = false;
        // after the stop: everything (the EOO too) has been read out
        if(!f->txRunning && !f->txStartReq && !f->txStopReq && f->txOut.used() == 0)
            f->st.txActive = 0;
    }
    return n;
}

FDV_EXPORT void fdv_status(fdv_t *f, fdv_status_t *s)
{
    std::lock_guard<std::mutex> lk(f->m);
    f->st.rxUnderruns = f->rxUnder.load();
    f->st.txUnderruns = f->txUnder.load();
    *s = f->st;
}

} // extern "C"
