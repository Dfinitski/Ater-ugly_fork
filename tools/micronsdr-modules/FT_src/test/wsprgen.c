/* wsprgen: a WSPR-2 test signal as a WAV file (12000 S/s, mono, 16-bit, 2 min)
   usage: wsprgen "K1ABC FN42 37" [snr dB, 2500 Hz] [audio Hz] [file.wav]
   (defaults: -20 dB, 1500 Hz, wspr_test.wav); the signal starts 1 s after the start, with
   white noise; then: test_ftx wspr wspr_test.wav */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int get_wspr_channel_symbols(char *message, char *hashtab, char *loctab, unsigned char *symbols);
int printdata = 0;                 /* wsprsim_utils.c wants it (wsprd.c / wsprsim.c have it) */

static void put32(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        printf("usage: wsprgen \"K1ABC FN42 37\" [snr dB] [audio Hz] [file.wav]\n");
        return 1;
    }
    const double snr = argc > 2 ? atof(argv[2]) : -20.0;
    const double f0 = argc > 3 ? atof(argv[3]) : 1500.0;
    const char *fn = argc > 4 ? argv[4] : "wspr_test.wav";
    enum { N = 120 * 12000, NS = 8192 };
    unsigned char sym[162];
    char msg[32];
    char *ht = calloc(32768 * 13, 1), *lt = calloc(32768 * 5, 1);
    strncpy(msg, argv[1], 31);
    msg[31] = 0;
    if(!get_wspr_channel_symbols(msg, ht, lt, sym))
    {
        printf("bad message: %s\n", argv[1]);
        return 1;
    }
    float *x = calloc(N, sizeof(float));
    srand(12345);
    for(int i = 0; i < N; i++)
    {
        const double u1 = (rand() + 1.0) / (RAND_MAX + 2.0), u2 = rand() / (double)RAND_MAX;
        x[i] = (float)(sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2));    /* noise: 1 in 6 kHz */
    }
    const double a = sqrt(2.0 * pow(10.0, snr / 10.0) * 2500.0 / 6000.0);
    double ph = 0.0;
    for(int i = 0; i < 162 * NS; i++)
    {
        /* the centre of the 4 tones at f0 */
        ph += 2.0 * M_PI * (f0 + (sym[i / NS] - 1.5) * 12000.0 / NS) / 12000.0;
        x[12000 + i] += (float)(a * sin(ph));
    }
    FILE *f = fopen(fn, "wb");
    if(!f)
    {
        printf("cannot write %s\n", fn);
        return 1;
    }
    unsigned char h[44];
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + 2 * N); memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 1); put32(h + 24, 12000); put32(h + 28, 24000);
    put16(h + 32, 2); put16(h + 34, 16); memcpy(h + 36, "data", 4); put32(h + 40, 2 * N);
    fwrite(h, 1, 44, f);
    for(int i = 0; i < N; i++)
    {
        const short s = (short)lrint(fmax(-32767.0, fmin(32767.0, x[i] * 50.0)));
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    printf("%s: \"%s\", %.0f dB, %.0f Hz\n", fn, argv[1], snr, f0);
    return 0;
}
