/*
 * fdv_api.h - FreeDV digital voice for MicronSDR (libfdv.dll)
 *
 * One object: a receiver and a transmitter of one FreeDV mode at a time,
 *   RADE V1 (rade_c + Opus FARGAN / LPCNet), 1600, 700D, 700E (codec2 freedv_api).
 * Audio on both sides at 48 kHz, float, mono (the program's audio rate):
 *   RX: the USB audio of the receiver in, decoded speech out
 *   TX: microphone in, modem audio (upper sideband audio, 0 .. ~3 kHz) out
 * The work is done in the library's own thread; the write / read calls only move samples
 * through FIFOs (short locks), so they can be called from the audio callback.
 *
 * Text:
 *   RADE: the callsign (up to 8 characters) is sent at the end of each transmission (the EOO
 *         frame, LDPC coded as FreeDV-GUI does); the last callsign received: fdv_status().
 *   1600 / 700D / 700E, sent on the text channel over and over during the transmission, one of:
 *         - the callsign (fdv_set_callsign) as FreeDV-GUI sends it (its reliable text, LDPC):
 *           fdv_set_text_mode(f, 1);
 *         - a text message (up to 60 printable ASCII characters, fdv_set_text; plain varicode
 *           of FreeDV, a CR after each): fdv_set_text_mode(f, 0), the default.
 *         Received: both always - a callsign by fdv_status(), the message characters by
 *         fdv_read_text (1.10 on).
 *
 * License: this wrapper BSD-2; codec2 LGPL-2.1 (separate libcodec2.dll), rade_c BSD-2,
 * Opus BSD-3, rade_text / LDPC from freedv-backend BSD-2.
 */
#ifndef FDV_API_H
#define FDV_API_H

#ifdef _WIN32
#  ifdef FDV_BUILDING
#    define FDV_EXPORT __declspec(dllexport)
#  else
#    define FDV_EXPORT __declspec(dllimport)
#  endif
#else
#  define FDV_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { FDV_MODE_RADE = 0, FDV_MODE_1600 = 1, FDV_MODE_700D = 2, FDV_MODE_700E = 3, FDV_MODE_COUNT = 4 };

typedef struct fdv fdv_t;

typedef struct
{
    int   mode;             /* the mode working now (FDV_MODE_*) */
    int   sync;             /* 1: the receiver is in sync */
    float snrDb;            /* SNR estimate (3 kHz), dB; valid with sync */
    float freqOffsetHz;     /* frequency offset of the received signal (RADE), Hz */
    int   rxUnderruns;      /* speech output had nothing to give (counter) */
    int   txUnderruns;      /* modem output had nothing to give during TX (counter) */
    int   txActive;         /* 1: transmitting (fdv_tx_start .. the end of the EOO) */
    char  callsign[16];     /* the last callsign received ("" none) */
    int   callsignSerial;   /* increments with each callsign received */
} fdv_status_t;

FDV_EXPORT int    fdv_version(void);                       /* 110 = 1.10 */
FDV_EXPORT fdv_t *fdv_create(int mode);
FDV_EXPORT void   fdv_destroy(fdv_t *f);
FDV_EXPORT void   fdv_set_mode(fdv_t *f, int mode);        /* applied by the worker thread */
FDV_EXPORT void   fdv_set_callsign(fdv_t *f, const char *callsign);   /* RADE: sent at the end of each TX */
FDV_EXPORT void   fdv_set_text(fdv_t *f, const char *text);           /* 1600 / 700D / 700E: the message */
FDV_EXPORT void   fdv_set_text_mode(fdv_t *f, int reliableCallsign);  /* 1: the callsign as FreeDV-GUI, 0: the message */
/* the text received since the last call (1600 / 700D / 700E; '\n' between the messages), zero
   terminated; returns its length */
FDV_EXPORT int    fdv_read_text(fdv_t *f, char *buf, int size);

/* receiver (48 kHz) */
FDV_EXPORT void   fdv_rx_write(fdv_t *f, const float *in, int n);
FDV_EXPORT int    fdv_rx_read(fdv_t *f, float *out, int n);         /* always n (silence when empty) */

/* transmitter (48 kHz). start: a new over; stop: the end (RADE: the EOO with the callsign
   follows, fdv_status().txActive stays 1 until it has been read out) */
FDV_EXPORT void   fdv_tx_start(fdv_t *f);
FDV_EXPORT void   fdv_tx_stop(fdv_t *f);
FDV_EXPORT void   fdv_tx_write(fdv_t *f, const float *mic, int n);
FDV_EXPORT int    fdv_tx_read(fdv_t *f, float *out, int n);         /* always n (silence when empty) */

FDV_EXPORT void   fdv_status(fdv_t *f, fdv_status_t *st);

#ifdef __cplusplus
}
#endif

#endif /* FDV_API_H */
