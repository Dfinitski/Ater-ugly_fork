// Offline tests of libfdv: off-air RADE decode, and TX -> RX loopback in every mode
#include "fdv_api.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

static std::vector<float> readWav(const char *fn, int *rate)
{
    FILE *f = fopen(fn, "rb");
    std::vector<float> out;
    if(!f) return out;
    char hdr[44];
    fread(hdr, 1, 44, f);
    *rate = *(int *)(hdr + 24);
    short s;
    while(fread(&s, 2, 1, f) == 1) out.push_back(s / 32768.0f);
    fclose(f);
    return out;
}

static void writeWav(const char *fn, const std::vector<float> &x, int rate)
{
    FILE *f = fopen(fn, "wb");
    int n = (int)x.size(), b = n * 2;
    int v;
    fwrite("RIFF", 1, 4, f); v = 36 + b; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); short s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f); s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&b, 4, 1, f);
    for(float y : x) { short q = (short)std::max(-32767.0f, std::min(32767.0f, y * 32767.0f)); fwrite(&q, 2, 1, f); }
    fclose(f);
}

static double rms(const std::vector<float> &x, size_t a, size_t b)
{
    double s = 0; b = std::min(b, x.size()); if(b <= a) return 0;
    for(size_t i = a; i < b; i++) s += x[i] * x[i];
    return std::sqrt(s / (b - a));
}

int main(int argc, char **argv)
{
    const int B = 480;      // 10 ms blocks as the audio callback
    // 1. off-air RADE recording (48 kHz), first 60 s
    if(argc < 2 || !strcmp(argv[1], "offair"))
    {
        int rate = 0;
        std::vector<float> in = readWav("/home/claude/fdv/rade_c/FDV_offair.wav", &rate);
        in.resize(std::min(in.size(), (size_t)48000 * 60));
        fdv_t *f = fdv_create(FDV_MODE_RADE);
        std::vector<float> out(in.size()), blk(B);
        int firstSync = -1, syncBlocks = 0; float snrSum = 0;
        int lastSerial = 0;
        for(size_t i = 0; i + B <= in.size(); i += B)
        {
            fdv_rx_write(f, &in[i], B);
            std::this_thread::sleep_for(std::chrono::microseconds(1500));
            fdv_rx_read(f, &out[i], B);
            fdv_status_t st; fdv_status(f, &st);
            if(st.sync) { syncBlocks++; snrSum += st.snrDb; if(firstSync < 0) firstSync = (int)i; }
            if(st.callsignSerial != lastSerial) { lastSerial = st.callsignSerial; printf("  callsign at %.1f s: '%s'\n", i / 48000.0, st.callsign); }
        }
        fdv_status_t st; fdv_status(f, &st);
        printf("offair RADE: first sync %.2f s, sync %.0f %%, SNR %.1f dB, speech rms %.4f, rx underruns %d\n",
               firstSync / 48000.0, 100.0 * syncBlocks / (in.size() / B), syncBlocks ? snrSum / syncBlocks : 0.0, rms(out, 0, out.size()), st.rxUnderruns);
        writeWav("/home/claude/fdv/libfdv/offair_dec.wav", out, 48000);
        fdv_destroy(f);
    }
    // 2. loopback TX -> RX
    if(argc < 2 || !strcmp(argv[1], "loop"))
    {
        int rate = 0;
        std::vector<float> sp16 = readWav("/home/claude/fdv/rade_c/wav/peter.wav", &rate);
        std::vector<float> mic(sp16.size() * 3);
        for(size_t i = 0; i < mic.size(); i++) mic[i] = sp16[i / 3];       // crude x3 (the TX lowpass removes the images)
        const char *names[4] = { "RADE", "1600", "700D", "700E" };
        for(int md = 0; md < 4; md++)
        {
            fdv_t *f = fdv_create(md);
            fdv_set_callsign(f, "N7DDC");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::vector<float> modem, dec, blk(B), o(B);
            fdv_tx_start(f);
            size_t total = mic.size() + 48000 * 3;
            float pk = 0; double e = 0; size_t en = 0;
            bool stopped = false;
            for(size_t i = 0; i < total; i += B)
            {
                if(i < mic.size()) fdv_tx_write(f, &mic[std::min(i, mic.size() - B)], B);
                else if(!stopped) { fdv_tx_stop(f); stopped = true; }
                std::this_thread::sleep_for(std::chrono::microseconds(2000));
                fdv_tx_read(f, o.data(), B);
                for(float v : o) { pk = std::max(pk, std::fabs(v)); e += v * v; en++; }
                modem.insert(modem.end(), o.begin(), o.end());
                fdv_rx_write(f, o.data(), B);
                fdv_rx_read(f, blk.data(), B);
                dec.insert(dec.end(), blk.begin(), blk.end());
            }
            // let RX run out
            std::vector<float> z(B, 0.0f);
            for(int k = 0; k < 200; k++) { fdv_rx_write(f, z.data(), B); std::this_thread::sleep_for(std::chrono::microseconds(2000)); fdv_rx_read(f, blk.data(), B); dec.insert(dec.end(), blk.begin(), blk.end()); }
            fdv_status_t st; fdv_status(f, &st);
            printf("%s loop: modem peak %.2f rms %.3f (PAPR %.1f dB), speech rms in %.4f out %.4f, SNR %.1f, callsign '%s', txActive %d, underruns rx %d tx %d\n",
                   names[md], pk, std::sqrt(e / en), 20 * std::log10(pk / std::sqrt(e / en)), rms(mic, 0, mic.size()), rms(dec, 0, dec.size()),
                   st.snrDb, st.callsign, st.txActive, st.rxUnderruns, st.txUnderruns);
            char fn[128]; sprintf(fn, "/home/claude/fdv/libfdv/loop_%s.wav", names[md]); writeWav(fn, dec, 48000);
            fdv_destroy(f);
        }
    }
    return 0;
}
