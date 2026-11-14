/*
 * ftx_api.h - libftx.dll: the FT8 / FT4 / WSPR decoders (and, 1.20 on, encoders) of WSJT-X for MicronSDR
 *
 * Built from WSJT-X (K1JT and the WSJT-X team, GPL-3): lib/ft8_decode.f90, ft4_decode.f90 and
 * what they need (ftx_api.f90 is the interface); WSPR: wsprd (lib/wsprd/, K1JT, K9AN) as a
 * function (c/wspr_api.c).
 *
 * Audio: 12000 S/s, 16-bit, the T/R period from its start (FT8 15 s = 180000 samples,
 * FT4 7.5 s = 72576 used, WSPR 2 min: 114 s = 1368000 used); fewer samples: zeros after them.
 * NOT thread safe (the decoders keep state between the periods, FFTW plans): one decode at a
 * time.
 */
#ifndef FTX_API_H
#define FTX_API_H

#ifdef __cplusplus
extern "C" {
#endif

enum { FTX_FT8 = 8, FTX_FT4 = 4 };

/* one decode: SNR (dB, 2500 Hz), DT (s), audio frequency (Hz), the message (37 characters,
   blank padded, zero terminated), nap: 0 = normal, else the a-priori type (a1..a8) */
typedef void (*ftx_callback)(void *user, int snr, float dt, float freq, const char *msg, int nap, float qual);

int ftx_version(void);      /* 120 = 1.20 (the encoders); 110: no encoders; 100: FT2 instead of WSPR */

/* mode: FTX_FT8 / FTX_FT4
   nfa, nfb: the audio frequency range searched (Hz, e.g. 200 .. 3000)
   nfqso: the RX frequency (Hz): decoded first, with a priori information
   ndepth: 1 fast, 2 normal, 3 deep
   ap: 1 = a priori decoding (needs mycall; hiscall for the QSO)
   nqso: QSO progress 0..5 (0 = CQ / calling)
   nutc: hhmmss of the period start
   nzhsym: FT8 only: 50 = the full period (0 = 50); 41 / 47 = the early decodes of WSJT-X
   mycall, hiscall: up to 12 characters ("" = none); hisgrid: up to 6
   Returns the number of decodes (the callback was called that often), -1: bad mode */
int ftx_decode(int mode, const short *audio12k, int nsamples, int nfa, int nfb, int nfqso,
               int ndepth, int ap, int nqso, int nutc, int nzhsym,
               const char *mycall, const char *hiscall, const char *hisgrid,
               ftx_callback cb, void *user);

/* one WSPR decode: SNR (dB, 2500 Hz), DT (s), the frequency (MHz, dial + audio), the audio
   frequency (Hz), the drift (Hz per 2 min), the message ("K1ABC FN42 37", "<K1ABC> FN42AB 37",
   "K1ABC/P 37"; zero terminated) */
typedef void (*wspr_callback)(void *user, float snr, float dt, double freqMHz, float audioHz, int drift, const char *msg);

/* WSPR-2 (wsprd of WSJT-X): the audio of the 2 min period from its start (an even minute),
   114 s used (1368000 samples; fewer: zeros after them); dialMHz: the dial frequency (USB);
   dataDir: a writable directory (the WAV of the period for a moment, hashtable.txt,
   ALL_WSPR.TXT, wspr_spots.txt); yymmdd, hhmm: the date and time of the period start (UTC);
   depth: 1 fast, 2 normal, 3 deep.
   Returns the number of decodes; < 0: an error (-2: the data directory not writable). */
int wspr_decode(const short *audio12k, int nsamples, double dialMHz, const char *dataDir,
                int yymmdd, int hhmm, int depth, wspr_callback cb, void *user);

/* the transmitter (1.20): a message -> the channel symbols of WSJT-X (genft8 / genft4):
   mode FTX_FT8: 79 tones 0..7, FTX_FT4: 103 tones 0..3 (tones: room for 103); msgsent: 38 chars,
   the message as it will be decoded. Returns the number of tones, 0: a bad message, -1: a bad mode.
   The waveform (GFSK: FT8 BT 2, 6.25 baud; FT4 BT 1, 20.833 baud, the ramp symbols) is made
   by the caller. */
int ftx_encode(int mode, const char *msg, int *tones, char *msgsent);

/* WSPR-2: a message -> 162 symbols 0..3 (4-FSK, 1.4648 baud and Hz). Returns 162, or -1. */
int wspr_encode(const char *msg, unsigned char *symbols);

#ifdef __cplusplus
}
#endif

#endif /* FTX_API_H */
