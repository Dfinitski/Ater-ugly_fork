// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
/*
 * FtUnit - FT8 / FT4 / WSPR (and JS8) receiving (decoding only): the receiver IQ -> the decoders
 * of WSJT-X (libftx.dll, FT_src/: FT8, FT4, WSPR - wsprd) and of JS8Call (libjs8.dll).
 *
 * The receiver IQ (48 kHz, centred on the dial frequency: the device tunes RX0 there; right = I,
 * left = Q) is taken from the audio thread into a ring. The worker thread of this class:
 *   - its own USB demodulator: 48 kHz -> 12 kHz complex, a complex band pass 100 .. 4000 Hz
 *     (the lower sideband out), the real part = the audio the decoders want (12 kHz);
 *     no AGC, NR, filter of the main receiver - their settings do not matter;
 *   - the audio into a buffer of the T/R period by UTC (FT8 15 s, FT4 7.5 s, WSPR 2 min from
 *     an even minute);
 *     the time of the samples from the wall clock of the audio blocks, smoothed, minus the
 *     time correction of the settings (the delay of the device and the audio path);
 *   - FT8, the early decode (as WSJT-X): at 11.8 s (nzhsym 41) the decoder runs on the audio so
 *     far - most of the stations are out about 2 s before the period ends (earlyDecoded());
 *     at 14.4 s the full decode (47, 50: the early ones subtracted, only the new ones out);
 *   - at the trigger point of the period the decoder runs (in this thread), the decodes
 *     go out with the signal decoded() (the ring holds 21 s: nothing is lost while a long
 *     WSPR decode runs). WSPR: wsprd wants a directory of its own (%USERPROFILE%/MicronSDR/
 *     wspr: hashtable.txt - the callsigns of the <hashed> messages, ALL_WSPR.TXT - all the
 *     decodes, as WSJT-X).
 * The level: the noise of the period is brought to about 34 dB (int16 rms 50), as WSJT-X
 * wants its input (about 30 dB).
 */
#ifndef FTUNIT_H
#define FTUNIT_H

#include <QThread>
#include <QMutex>
#include <QString>
#include <QVector>
#include <QList>
#include <QPair>
#include <atomic>
#include <vector>

class FtUnit : public QThread
{
    Q_OBJECT
public:
    // FT8 / FT4 / WSPR (libftx), JS8 Normal / Fast / Turbo / Slow (libjs8: JS8Call, 100 + its submode)
    enum { FT8 = 8, FT4 = 4, JS8N = 100, JS8F = 101, JS8T = 102, JS8S = 104, WSPR = 120 };
    static bool isJs8(int mode) { return mode >= JS8N && mode <= JS8S; }
    struct Decode
    {
        int    snr;
        float  dt;
        float  freq;        // audio frequency, Hz
        QString msg;
        int    nap;         // 0, or the a-priori type
        float  qual;        // AP decodes: the quality (below 0.17: doubtful, '?' as WSJT-X)
        int    bits;        // JS8: the 3 frame bits (1 first, 2 last, 4 data); msg = the 12-char frame;
                            // WSPR: the drift (Hz in 2 min)
    };
    struct Settings
    {
        int     mode = FT8;
        int     depth = 3;          // 1 fast, 2 normal, 3 deep
        bool    ap = true;
        int     nfa = 200, nfb = 3500;
        int     nfqso = 1500;       // the RX frequency (Hz)
        QString mycall, hiscall, hisgrid;
        int     timeOffsetMs = 0;   // added to the time of the samples
        bool    autoTime = true;    // the time correction found by itself (the DT of the stations)
        qint64  dialHz = 0;         // WSPR: the dial frequency (the frequencies of the decodes)
        bool    early = true;       // the early decode: FT8 at 11.8 s (as WSJT-X, Normal / Deep), JS8 at 90 % of the signal
    };

    FtUnit(QObject *parent = 0);
    ~FtUnit();

    bool load();                    // libftx.dll
    bool loadJs8();                 // libjs8.dll
    QString error() const { return err; }
    QString errorJs8() const { return errJs8; }
    bool hasWspr() const { return fnWspr != 0; }     // libftx.dll 1.10 (with wspr_decode)
    bool hasEncoder() const { return fnEncode != 0; } // libftx.dll 1.20 (ftx_encode, wspr_encode)
    // the transmitter: a message encoded in the worker thread (the library is not thread safe:
    // not while it decodes) -> encoded(); mode FT8 / FT4 / WSPR
    void requestEncode(int mode, const QString &msg);
    // JS8: the 79 tones of a frame (12 characters, the 3 frame bits) - js8_encode of libjs8.dll
    // (no state: any thread); false: no encoder or a bad frame
    bool hasJs8Encoder() const { return fnJs8Enc != 0; }
    bool js8Encode(int submode, int bits, const QString &frame, int *tones79) const;
    void setSettings(const Settings &s);
    Settings settings();
    void setActive(bool on);        // the worker runs, the audio thread fills the ring
    bool isActive() const { return active.load(); }

    // audio thread: the receiver IQ (left = Q, right = I), 48 kHz
    void iq(const float *l, const float *r, unsigned long n);
    // audio thread, after iq() in the same callback: the receiver audio (DSP output, before
    // the volume) of the same samples - decoded too, as a check of the own demodulator
    void audio(const float *a, unsigned long n);

    static int  periodMs(int mode)
    {
        switch(mode)
        {
            case FT8: case JS8N: return 15000;
            case FT4: return 7500;
            case JS8F: return 10000;
            case JS8T: return 6000;
            case JS8S: return 30000;
            case WSPR: return 120000;
            default: return 15000;
        }
    }
    // the decoding point of the period, ms (the decoder takes the samples up to it)
    static int triggerMs(int mode)
    {
        switch(mode)
        {
            case FT8: case JS8N: return 14400;
            case FT4: return 6100;
            case JS8F: return 9000;
            case JS8T: return 4800;
            case JS8S: return 27000;
            case WSPR: return 114000;
            default: return 14400;
        }
    }
    // the time in the period now, ms: the wall clock with the time corrections, as the samples
    // get it (the mode of the settings)
    int  phaseMs(qint64 nowMs) const;
    // a step of the receiver level (ATT) harmless now: the decoder is not running, or the
    // decoding point of the period has passed and the next period has not begun yet
    bool levelGap(qint64 nowMs) const;

    static const char *modeName(int mode)
    {
        switch(mode)
        {
            case FT8: return "FT8";
            case FT4: return "FT4";
            case JS8N: return "JS8";
            case JS8F: return "JS8 Fast";
            case JS8T: return "JS8 Turbo";
            case JS8S: return "JS8 Slow";
            case WSPR: return "WSPR";
            default: return "?";
        }
    }

    // the automatic time correction (ms, added as timeOffsetMs): the PC clock or the delays
    int   autoOffsetMs() const { return autoOff.load(); }
    int   autoGoodMs() const { return autoGood.load(); }    // the last one with decodes (to be saved)
    void  setAutoOffset(int ms) { autoSet.store(ms); }  // the saved one at the start
    bool  autoSearching() const { return searching.load(); }
    // the decoder input of the last period (the receiver IQ, before the level setting): the
    // noise in 3000 Hz (the median of the spectrum: the signals left out) and the peak, dBFS
    float levelDb() const { return level.load(); }
    float peakDb() const { return peak.load(); }
    int   gainDb() const { return gainDbNow.load(); }

    // the waterfall (FtWaterfall): the spectrum of the decoder audio (12 kHz, FFT 2048, Hann,
    // 4 FFTs half overlapped a line: 5.86 Hz, 0.34 s a line), the power of the bins in dB
    struct SpecLine
    {
        QVector<float> db;      // SPEC_BINS bins, 0 .. 6000 Hz
        qint64 tMs;             // the UTC time of its end (ms since the epoch)
    };
    static const int SPEC_FFT = 2048;
    static const int SPEC_BINS = SPEC_FFT / 2;
    static double specHz() { return 12000.0 / SPEC_FFT; }
    void setSpectrumOn(bool on) { specOn.store(on); }
    QVector<SpecLine> takeSpectrum();       // the lines since the last call (GUI thread)

signals:
    // the decodes of one period (the start of the period: ms since the epoch, UTC)
    void decoded(qint64 periodStartMs, int mode, QVector<FtUnit::Decode> list, int decodeMs);
    // FT8: the early decodes (11.8 s); decoded() of the same period then brings only the new ones
    void earlyDecoded(qint64 periodStartMs, int mode, QVector<FtUnit::Decode> list, int decodeMs);
    void periodStarted(qint64 periodStartMs, int mode);
    // requestEncode(): the channel symbols (FT8 79, FT4 103, WSPR 162; empty: a bad message),
    // sent: the message as it will be decoded
    void encoded(int mode, QString msg, QVector<int> tones, QString sent);

protected:
    void run() override;

private:
    typedef int  (*FtxVersion)(void);
    typedef void (*FtxCallback)(void *, int, float, float, const char *, int, float);
    typedef int  (*FtxDecode)(int, const short *, int, int, int, int, int, int, int, int, int,
                              const char *, const char *, const char *, FtxCallback, void *);
    FtxDecode fnDecode;
    typedef void (*WsprCallback)(void *, float, float, double, float, int, const char *);
    typedef int  (*WsprDecode)(const short *, int, double, const char *, int, int, int, WsprCallback, void *);
    WsprDecode fnWspr;
    typedef int (*FtxEncode)(int, const char *, int *, char *);
    typedef int (*WsprEncode)(const char *, unsigned char *);
    FtxEncode fnEncode = 0;
    WsprEncode fnWsprEnc = 0;
    QMutex encMtx;
    QList<QPair<int, QString>> encQ;
    void doEncodes();
    static void onDecodeWspr(void *user, float snr, float dt, double freqMHz, float audioHz, int drift, const char *msg);
    QString err;
    typedef void (*Js8Callback)(void *, int, float, float, const char *, int, float, int);
    typedef int  (*Js8Decode)(int, const short *, int, int, int, int, int, Js8Callback, void *);
    Js8Decode fnJs8;
    typedef int  (*Js8Encode)(int, int, const char *, int *);
    Js8Encode fnJs8Enc = 0;
    QString errJs8;
    static void onDecodeJs8(void *user, int snr, float dt, float freq, const char *frame, int bits, float quality, int submode);

    QMutex mtx;
    Settings set;
    std::atomic<bool> active, quit;
    std::atomic<int> setSerial;

    // the ring (audio thread -> worker): 4 floats a sample: I, Q, the receiver audio, 0;
    // the worker reads up to wrDone (the blocks the audio of which is in too)
    std::vector<float> ring;
    size_t ringMask;
    std::atomic<size_t> wr, rd, wrDone;
    size_t blkStart;                        // audio thread: the last block of iq()
    unsigned long blkN;
    std::atomic<long long> stampSamples;    // samples written ...
    std::atomic<long long> stampMs;         // ... at this wall clock time (ms since the epoch)

    std::atomic<float> level, peak;
    std::atomic<int> gapMode { FT8 }, gapOffMs { 0 };   // phaseMs: the mode and the time offset ...
    std::atomic<bool> gapAuto { true };                 // ... of the settings
    std::atomic<int> autoOff { 0 }, autoSet { AUTO_NONE }, autoGood { 0 };
    std::atomic<bool> searching { false };
    static const int AUTO_NONE = -1000000;
    std::atomic<int> gainDbNow;

    std::atomic<bool> specOn { false };
    QMutex specMtx;
    QVector<SpecLine> specQ;
    void specLine(const QVector<float> &db, qint64 tMs);

    static void onDecode(void *user, int snr, float dt, float freq, const char *msg, int nap, float qual);
};

Q_DECLARE_METATYPE(FtUnit::Decode)
Q_DECLARE_METATYPE(QVector<FtUnit::Decode>)

#endif // FTUNIT_H
