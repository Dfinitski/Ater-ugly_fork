/* wspr_api.c - libftx.dll: the WSPR decoder (wsprd of WSJT-X, lib/wsprd/) as a function, see
   ftx_api.h. wsprd reads a WAV file named YYMMDD_HHMM.wav (the date and the time are taken from
   the name): the audio is written there (the data directory), decoded and deleted. The data
   directory also keeps hashtable.txt (the callsigns of the hashed messages <CALL>),
   ALL_WSPR.TXT (all decodes, as WSJT-X) and wspr_spots.txt (the last period). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ftx_api.h"

int wsprd_main(int argc, char *argv[]);
extern void (*wsprd_spot_hook)(void *user, float snr, float dt, double freq, int drift, const char *msg);
extern void *wsprd_spot_user;
extern int optind;

typedef struct { wspr_callback cb; void *user; double dial; int count; } Ctx;

static void onSpot(void *user, float snr, float dt, double freq, int drift, const char *msg)
{
    Ctx *c = (Ctx *)user;
    c->count++;
    if(c->cb)
    {
        char m[24];
        int n;
        strncpy(m, msg, 23);
        m[23] = 0;
        for(n = (int)strlen(m) - 1; n >= 0 && m[n] == ' '; n--)
            m[n] = 0;
        c->cb(c->user, snr, dt, freq, (float)((freq - c->dial) * 1e6), drift, m);
    }
}

static void put32(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

int wspr_decode(const short *audio12k, int nsamples, double dialMHz, const char *dataDir,
                int yymmdd, int hhmm, int depth, wspr_callback cb, void *user)
{
    enum { NPTS = 114 * 12000 };
    char wav[1024], dial[32], cycles[16];
    unsigned char h[44];
    short *z;
    FILE *f;
    int rc, argc = 0, n;
    char *argv[16];
    Ctx c;

    if(!dataDir || !dataDir[0] || nsamples <= 0)
        return -1;
    snprintf(wav, sizeof wav, "%s/%06d_%04d.wav", dataDir, yymmdd % 1000000, hhmm % 10000);
    f = fopen(wav, "wb");
    if(!f)
        return -2;
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + 2 * NPTS); memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 1); put32(h + 24, 12000); put32(h + 28, 24000);
    put16(h + 32, 2); put16(h + 34, 16); memcpy(h + 36, "data", 4); put32(h + 40, 2 * NPTS);
    fwrite(h, 1, 44, f);
    n = nsamples < NPTS ? nsamples : NPTS;
    fwrite(audio12k, 2, n, f);
    if(n < NPTS)
    {
        z = (short *)calloc(NPTS - n, 2);
        if(z)
        {
            fwrite(z, 2, NPTS - n, f);
            free(z);
        }
    }
    fclose(f);

    /* as WSJT-X: -C 500 (the Fano decoder limit) -o 4 (the ordered statistics decoder);
       depth 1 (Fast): no OSD, no time jitter (-q); 3 (Deep): more candidates (-d) */
    snprintf(dial, sizeof dial, "%.6f", dialMHz);
    snprintf(cycles, sizeof cycles, "%d", 500);
    argv[argc++] = "wsprd";
    argv[argc++] = "-a"; argv[argc++] = (char *)dataDir;
    argv[argc++] = "-f"; argv[argc++] = dial;
    argv[argc++] = "-C"; argv[argc++] = cycles;
    if(depth <= 1)
        argv[argc++] = "-q";
    if(depth >= 2)
    {
        argv[argc++] = "-o";
        argv[argc++] = "4";
    }
    if(depth >= 3)
        argv[argc++] = "-d";
    argv[argc++] = wav;
    argv[argc] = 0;

    c.cb = cb;
    c.user = user;
    c.dial = dialMHz;
    c.count = 0;
    wsprd_spot_hook = onSpot;
    wsprd_spot_user = &c;
    optind = 0;                     /* getopt from the start again (glibc, mingw-w64) */
    rc = wsprd_main(argc, argv);
    wsprd_spot_hook = 0;
    wsprd_spot_user = 0;
    remove(wav);
    return rc == 0 ? c.count : -3;
}

/* the transmitter (1.20): a WSPR message ("K1ABC FN42 37", "<PJ4/K1ABC> FK52UD 37",
   "PJ4/K1ABC 37") -> its 162 channel symbols 0..3 (wsprsim of WSJT-X). Returns 162, or -1. */
int get_wspr_channel_symbols(char *message, char *hashtab, char *loctab, unsigned char *symbols);

int wspr_encode(const char *msg, unsigned char *symbols)
{
    static char *ht = 0, *lt = 0;
    char m[23];
    if(!msg || !symbols)
        return -1;
    if(!ht)
    {
        ht = (char *)calloc(32768 * 13, 1);
        lt = (char *)calloc(32768 * 5, 1);
        if(!ht || !lt)
            return -1;
    }
    strncpy(m, msg, 22);
    m[22] = 0;
    return get_wspr_channel_symbols(m, ht, lt, symbols) ? 162 : -1;
}
