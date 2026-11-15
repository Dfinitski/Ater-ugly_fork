/* test_ftx: decode a WAV file (12000 S/s, mono, 16-bit) with libftx.dll
   usage: test_ftx ft8|ft4 file.wav [mycall] [depth 1..3]
          test_ftx wspr file.wav [dial MHz] [depth 1..3]   (the first 2 min, from the start)
   The file is cut into windows of one T/R period, every half period (a recording need not
   start at a period boundary); the same message from two windows is shown once. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../ftx_api.h"

#ifdef _WIN32
#include <windows.h>
static double now_ms(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return 1000.0 * c.QuadPart / f.QuadPart; }
#else
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
#endif

static short *readWav(const char *fn, int *n, int *rate, int *ch, int *bits)
{
    FILE *f = fopen(fn, "rb");
    if(!f) return 0;
    unsigned char h[12];
    if(fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return 0; }
    short *d = 0;
    *n = 0;
    for(;;)
    {
        unsigned char c[8];
        if(fread(c, 1, 8, f) != 8) break;
        unsigned len = c[4] | (c[5] << 8) | (c[6] << 16) | ((unsigned)c[7] << 24);
        if(!memcmp(c, "fmt ", 4))
        {
            unsigned char fm[40] = { 0 };
            fread(fm, 1, len < 40 ? len : 40, f);
            if(len > 40) fseek(f, len - 40, SEEK_CUR);
            *ch = fm[2] | (fm[3] << 8);
            *rate = fm[4] | (fm[5] << 8) | (fm[6] << 16) | (fm[7] << 24);
            *bits = fm[14] | (fm[15] << 8);
        }
        else if(!memcmp(c, "data", 4))
        {
            d = (short *)malloc(len + 2);
            *n = (int)fread(d, 1, len, f) / 2;
            break;
        }
        else
            fseek(f, len + (len & 1), SEEK_CUR);
    }
    fclose(f);
    return d;
}

typedef struct { double t0; int count; char seen[400][48]; int nseen; } Ctx;

static void onDecode(void *user, int snr, float dt, float freq, const char *msg, int nap, float qual)
{
    Ctx *c = (Ctx *)user;
    char m[40];
    strncpy(m, msg, 37);
    m[37] = 0;
    for(int i = 36; i >= 0 && m[i] == ' '; i--) m[i] = 0;
    char key[48];
    snprintf(key, sizeof key, "%s", m);
    for(int i = 0; i < c->nseen; i++)
        if(!strcmp(c->seen[i], key)) return;
    if(c->nseen < 400) strcpy(c->seen[c->nseen++], key);
    c->count++;
    if(nap)
        printf("%7.2f s  %4d dB  DT %5.2f  %5.0f Hz  %-37s a%d\n", c->t0, snr, dt, freq, m, nap);
    else
        printf("%7.2f s  %4d dB  DT %5.2f  %5.0f Hz  %s\n", c->t0, snr, dt, freq, m);
    (void)qual;
}

static void onWspr(void *user, float snr, float dt, double freqMHz, float audioHz, int drift, const char *msg)
{
    (void)user;
    printf("%4.0f dB  DT %5.1f  %10.6f MHz  %6.1f Hz  drift %2d  %s\n", snr, dt, freqMHz, audioHz, drift, msg);
}

int main(int argc, char **argv)
{
    if(argc < 3)
    {
        printf("usage: test_ftx ft8|ft4 file.wav [mycall] [depth 1..3]\n"
               "       test_ftx wspr file.wav [dial MHz] [depth 1..3]\n");
        return 1;
    }
    const int wspr = !strcmp(argv[1], "wspr");
    int mode = !strcmp(argv[1], "ft8") ? FTX_FT8 : !strcmp(argv[1], "ft4") ? FTX_FT4 : 0;
    if(!mode && !wspr) { printf("mode: ft8, ft4 or wspr\n"); return 1; }
    const char *mycall = argc > 3 ? argv[3] : "";
    int depth = argc > 4 ? atoi(argv[4]) : 3;
    int n = 0, rate = 0, ch = 0, bits = 0;
    short *x = readWav(argv[2], &n, &rate, &ch, &bits);
    if(!x) { printf("cannot read %s\n", argv[2]); return 1; }
    printf("libftx %d, %s: %d samples, %d S/s, %d channel(s), %d bit\n", ftx_version(), argv[2], n, rate, ch, bits);
    if(rate != 12000 || ch != 1 || bits != 16) { printf("need 12000 S/s mono 16-bit\n"); return 1; }
    if(wspr)
    {
        const double dial = argc > 3 ? atof(argv[3]) : 14.0956;
        double t = now_ms();
        int nd = wspr_decode(x, n, dial, ".", 0, 0, depth, onWspr, 0);
        printf("%d messages, %.0f ms\n", nd, now_ms() - t);
        free(x);
        return nd < 0;
    }
    const double period = mode == FTX_FT8 ? 15.0 : 7.5;
    const int nwin = mode == FTX_FT8 ? 180000 : 72576;
    const int step = (int)(period * 12000 / 2);
    Ctx c;
    memset(&c, 0, sizeof c);
    double worst = 0;
    for(int s = 0; s == 0 || s + step < n; s += step)
    {
        c.t0 = s / 12000.0;
        int len = n - s < nwin ? n - s : nwin;
        double t = now_ms();
        const int sec = s / 12000;                          /* hhmmss of the window start */
        const int hhmmss = (sec / 3600) * 10000 + ((sec / 60) % 60) * 100 + sec % 60;
        ftx_decode(mode, x + s, len, 200, 3500, 1500, depth, mycall[0] ? 1 : 0, 0,
                   hhmmss, 0, mycall, "", "", onDecode, &c);   /* 0: the whole period (FT8: 47 + 50) */
        t = now_ms() - t;
        if(t > worst) worst = t;
    }
    printf("%d messages, the slowest window %.0f ms\n", c.count, worst);
    free(x);
    return 0;
}
