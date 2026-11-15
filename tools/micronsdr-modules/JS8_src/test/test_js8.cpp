// libjs8 test: JS8 frames encoded, modulated (phase continuous 8-FSK, as JS8Call), with noise,
// decoded; or a WAV file (12 kHz, 16 bit mono) decoded: test_js8 [file.wav [submode]]
#include "../js8_api.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <chrono>

static void onDecode(void *, int snr, float dt, float freq, const char *frame, int bits, float q, int sm)
{
    printf("  %3d dB  DT %+.2f  %7.1f Hz  [%s] bits %d  q %.2f  submode %d\n", snr, dt, freq, frame, bits, q, sm);
}

static int synth(int submode, std::vector<short> &out, double snrDb)
{
    const int nsps = submode == JS8_NORMAL ? 1920 : submode == JS8_FAST ? 1200 : submode == JS8_TURBO ? 600 : 3840;
    const double delay = submode == JS8_NORMAL ? 0.5 : submode == JS8_FAST ? 0.2 : submode == JS8_TURBO ? 0.1 : 0.5;
    const int n = js8_period_samples(submode);
    std::vector<double> x(n, 0.0);
    struct { const char *frame; int bits; double f; double amp; } sig[] = {
        { "N7DDCtest+12", 1, 1000.0, 1.0 }, { "Hello-JS8abc", 2, 1500.0, 1.0 }, { "0123456789AZ", 0, 2100.0, 1.0 } };
    for(auto &s : sig)
    {
        int tones[79];
        if(js8_encode(submode, s.bits, s.frame, tones) != 0) { printf("encode failed\n"); return -1; }
        double ph = 0.0;
        const double df = 12000.0 / nsps;
        const int i0 = (int)(delay * 12000);
        for(int k = 0; k < 79 * nsps && i0 + k < n; k++)
        {
            ph += 2 * M_PI * (s.f + tones[k / nsps] * df) / 12000.0;
            x[i0 + k] += s.amp * sin(ph);
        }
    }
    // noise: SNR in 2500 Hz: signal power 0.5, noise power in 2500 Hz = 0.5 / 10^(snr/10)
    std::mt19937 rng(1);
    std::normal_distribution<double> g(0.0, 1.0);
    const double sigma = sqrt(0.5 / pow(10.0, snrDb / 10.0) * 6000.0 / 2500.0);
    out.resize(n);
    for(int i = 0; i < n; i++)
        out[i] = (short)std::max(-32767.0, std::min(32767.0, (x[i] + sigma * g(rng)) * 300.0));
    return n;
}

int main(int argc, char **argv)
{
    printf("libjs8 %d\n", js8_version());
    if(argc > 1)
    {
        FILE *f = fopen(argv[1], "rb");
        if(!f) { printf("no %s\n", argv[1]); return 1; }
        fseek(f, 44, SEEK_SET);
        std::vector<short> a(400000);
        const int n = (int)fread(a.data(), 2, a.size(), f);
        fclose(f);
        const int sm = argc > 2 ? atoi(argv[2]) : JS8_NORMAL;
        auto t = std::chrono::steady_clock::now();
        const int nd = js8_decode(sm, a.data(), n, 200, 3500, 1500, 0, onDecode, 0);
        printf("%d decodes, %.0f ms\n", nd, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
        return 0;
    }
    for(int sm : { JS8_NORMAL, JS8_FAST, JS8_TURBO, JS8_SLOW })
        for(double snr : { 0.0, -12.0, -18.0 })
        {
            std::vector<short> a;
            const int n = synth(sm, a, snr);
            printf("submode %d, SNR %+.0f dB:\n", sm, snr);
            auto t = std::chrono::steady_clock::now();
            const int nd = js8_decode(sm, a.data(), n, 200, 3500, 1500, 0, onDecode, 0);
            printf("  %d decodes, %.0f ms\n", nd, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
        }
    return 0;
}
