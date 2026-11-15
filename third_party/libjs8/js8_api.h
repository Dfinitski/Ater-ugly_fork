/*
 * libjs8 - the JS8 decoder of JS8Call as a DLL with a plain C interface (for MicronSDR, which
 * is built with an older compiler: the decoder needs C++20, boost, Eigen, FFTW).
 *
 * Audio: 12000 S/s, 16 bit, the T/R period from its start (Normal 15 s, Fast 10 s, Turbo 6 s,
 * Slow 30 s: 180000 / 120000 / 72000 / 360000 samples; shorter: zeros after it).
 * The decoder keeps its FFT plans between the calls: call from ONE thread only.
 */
#ifndef JS8_API_H
#define JS8_API_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(JS8_BUILD)
#define JS8_API __declspec(dllexport)
#else
#define JS8_API
#endif

/* submodes (JS8Call: Varicode::SubmodeType) */
enum { JS8_NORMAL = 0, JS8_FAST = 1, JS8_TURBO = 2, JS8_SLOW = 4 };

/* frame bits (the 3-bit type of the frame): 1 = the first frame of a message, 2 = the last,
   4 = a data frame (no frame type header) */
typedef void (*js8_callback)(void *user, int snr, float dt, float freq, const char *frame,
                             int bits, float quality, int submode);

JS8_API int js8_version(void);

/* samples per period of the submode, 0: unknown submode */
JS8_API int js8_period_samples(int submode);

/* decode one period; frame: 12 characters of the JS8 alphabet (0-9 A-Z a-z - +), to unpack
   with the varicode of JS8Call. Returns the number of decodes, -1 on an error. */
JS8_API int js8_decode(int submode, const short *audio12k, int nsamples, int nfa, int nfb,
                       int nfqso, int nutc, js8_callback cb, void *user);

/* test: the 79 tones (0..7) of a frame (12 characters, bits as above) */
JS8_API int js8_encode(int submode, int bits, const char *frame, int *tones79);

#ifdef __cplusplus
}
#endif

#endif
