#pragma once

// ANAN-100D port: the WDSP PureSignal (calcc / iqc) and TXA post-generator
// entry points (third_party/wdsp/upstream/calcc.c, gen.c). They live in the
// bundled static WDSP and are thread-safe there (critical sections), so the
// GUI thread may call the control functions while the radio I/O thread
// feeds pscc(). `channel` is the TXA channel id (Hl2TxDsp::wdspChannelId()).

#ifdef __cplusplus
extern "C" {
#endif

void pscc(int channel, int size, double* tx, double* rx);
void SetPSControl(int channel, int reset, int mancal, int automode, int turnon);
void SetPSMox(int channel, int mox);
void GetPSInfo(int channel, int* info);
void SetPSHWPeak(int channel, double peak);
void GetPSHWPeak(int channel, double* peak);
void GetPSMaxTX(int channel, double* maxtx);
void SetPSMoxDelay(int channel, double delay);
double SetPSTXDelay(int channel, double delay);
void SetPSLoopDelay(int channel, double delay);
void SetPSFeedbackRate(int channel, int rate);
#if AETHER_WDSP_PS_CLASSIC
// Thetis classic calcc (third_party/wdsp/classic): cm/cc/cs are 16 x 4 spline
// coefficients each.
void GetPSDisp(int channel, double* x, double* ym, double* yc, double* ys,
               double* cm, double* cc, double* cs);
#else
void GetPSDisp(int channel, double* x, double* ym, double* yc, double* ys,
               double* xm_cor, double* ym_cor, double* xa_cor, double* ya_cor,
               int* nsamps_out, int* cpts_out, double* phs_ref_deg_out);
#endif
#if !AETHER_WDSP_PS_CLASSIC
// AetherSDR WDSP patch: the worst deviation of the last failed scheck --
// curve (0 m, 1 c, 2 s), point k (0..39), x, previous, new, failure count.
void GetPSScheckDiag(int channel, double* diag6);
// AetherSDR WDSP patch 27: switch scheck off (1) / on (0, upstream).
void SetPSScheckOff(int channel, int off);
// Patch 28: 0 upstream, 1 off (logged only), 2 as Thetis classic (compare
// from x = 0.25 up, reject a negative magnitude correction).
void SetPSScheckMode(int channel, int mode);
#endif
void PSSaveCorr(int channel, char* filename);
void PSRestoreCorr(int channel, char* filename);

void SetTXACFIRRun(int channel, int run);
void SetTXAPostGenRun(int channel, int run);
void SetTXAPostGenMode(int channel, int mode);
void SetTXAPostGenTTMag(int channel, double mag1, double mag2);
void SetTXAPostGenTTFreq(int channel, double freq1, double freq2);

#ifdef __cplusplus
}
#endif
