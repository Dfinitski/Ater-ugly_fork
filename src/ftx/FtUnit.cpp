// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
#define _USE_MATH_DEFINES   // AetherSDR: M_PI under MSVC
// FT8 / FT4 / WSPR / JS8 receiving: see FtUnit.h
#define _USE_MATH_DEFINES
#include "FtUnit.h"
#include <QLibrary>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {

// windowed sinc (Blackman) low pass, gain 1 at 0 Hz; fc in cycles per sample
std::vector<float> lowpass(int taps, double fc)
{
    std::vector<float> h(taps);
    double sum = 0.0;
    for(int i = 0; i < taps; i++)
    {
        const double m = i - (taps - 1) / 2.0;
        const double s = (m == 0.0) ? 2.0 * fc : sin(2.0 * M_PI * fc * m) / (M_PI * m);
        const double w = 0.42 - 0.5 * cos(2.0 * M_PI * i / (taps - 1)) + 0.08 * cos(4.0 * M_PI * i / (taps - 1));
        h[i] = (float)(s * w);
        sum += s * w;
    }
    for(int i = 0; i < taps; i++)
        h[i] = (float)(h[i] / sum);
    return h;
}

// diagnostics: a 16-bit WAV (12 kHz, 1 or 2 channels) into %USERPROFILE%/MicronSDR, overwritten
void writeWav(const QString &name, const short *x, int frames, int ch)
{
    QFile f(QDir::homePath() + "/AetherSDR-FTX/" + name);
    if(!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    const quint32 rate = 12000, bytes = (quint32)frames * ch * 2;
    auto u32 = [&](quint32 v) { char b[4] = { (char)v, (char)(v >> 8), (char)(v >> 16), (char)(v >> 24) }; f.write(b, 4); };
    auto u16 = [&](quint16 v) { char b[2] = { (char)v, (char)(v >> 8) }; f.write(b, 2); };
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16((quint16)ch); u32(rate); u32(rate * ch * 2); u16((quint16)(ch * 2)); u16(16);
    f.write("data", 4); u32(bytes);
    f.write((const char *)x, bytes);
}

// radix-2 complex FFT in place (n a power of 2)
void fft(std::vector<float> &re, std::vector<float> &im)
{
    const int n = (int)re.size();
    for(int i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;
        for(; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if(i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for(int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / len;
        const float wr = (float)cos(ang), wi = (float)sin(ang);
        for(int i = 0; i < n; i += len)
        {
            float cr = 1.0f, ci = 0.0f;
            for(int k = 0; k < len / 2; k++)
            {
                const int a = i + k, b = a + len / 2;
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                const float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

// the waterfall: the audio (12 kHz) -> lines of the power spectrum (see FtUnit::SpecLine)
struct SpecAcc
{
    static const int N = FtUnit::SPEC_FFT, HOP = N / 2, AVG = 4;
    std::vector<float> x, win, re, im, acc;
    int fill = 0, nAvg = 0;
    SpecAcc() : x(N, 0.0f), win(N), re(N), im(N), acc(N / 2, 0.0f)
    {
        for(int i = 0; i < N; i++)
            win[i] = (float)(0.5 - 0.5 * cos(2.0 * M_PI * i / N));
    }
    void reset() { fill = 0; nAvg = 0; std::fill(acc.begin(), acc.end(), 0.0f); }
    // true: a line is ready in out (dB)
    bool push(float a, QVector<float> &out)
    {
        x[fill++] = a;
        if(fill < N)
            return false;
        for(int i = 0; i < N; i++)
        {
            re[i] = x[i] * win[i];
            im[i] = 0.0f;
        }
        fft(re, im);
        for(int k = 0; k < N / 2; k++)
            acc[k] += re[k] * re[k] + im[k] * im[k];
        std::copy(x.begin() + HOP, x.end(), x.begin());
        fill = N - HOP;
        if(++nAvg < AVG)
            return false;
        out.resize(N / 2);
        for(int k = 0; k < N / 2; k++)
        {
            out[k] = (float)(10.0 * log10(acc[k] / AVG + 1e-20));
            acc[k] = 0.0f;
        }
        nAvg = 0;
        return true;
    }
};

struct DecodeCtx
{
    QVector<FtUnit::Decode> *list;
};

} // namespace

FtUnit::FtUnit(QObject *parent)
    : QThread(parent), fnDecode(0), fnWspr(0), fnJs8(0), active(false), quit(false), setSerial(0),
      ring((size_t)1 << 22), ringMask(((size_t)1 << 22) - 1), wr(0), rd(0), wrDone(0), blkStart(0), blkN(0),
      stampSamples(0), stampMs(0), level(-200.0f), peak(-200.0f), gainDbNow(0)
{
    qRegisterMetaType<FtUnit::Decode>("FtUnit::Decode");
    qRegisterMetaType<QVector<FtUnit::Decode>>("QVector<FtUnit::Decode>");
    qRegisterMetaType<QVector<int>>("QVector<int>");
}

FtUnit::~FtUnit()
{
    active.store(false);
    quit.store(true);
    wait(5000);
}

bool FtUnit::load()
{
    if(fnDecode)
        return true;
    if(!err.isEmpty())
        return false;
    // AetherSDR, build 442: next to the program first (Linux: dlopen does
    // not search the executable's directory), then the search path.
    // Build 444: macOS app bundle - Contents/Frameworks.
    QLibrary lib(QDir(QCoreApplication::applicationDirPath()).filePath("libftx"));
    if(!lib.load())
    {
#ifdef Q_OS_MACOS
        lib.setFileName(QDir(QCoreApplication::applicationDirPath() + "/../Frameworks").filePath("libftx"));
        if(!lib.load())
#endif
        lib.setFileName("libftx");
    }
    if(!lib.isLoaded() && !lib.load())
    {
        err = "libftx (.dll / .so / .dylib) not loaded: " + lib.errorString();
        qWarning() << "FT:" << err;
        return false;
    }
    FtxVersion ver = (FtxVersion)lib.resolve("ftx_version");
    fnDecode = (FtxDecode)lib.resolve("ftx_decode");
    if(!ver || !fnDecode)
    {
        fnDecode = 0;
        err = "libftx: no ftx_decode";
        return false;
    }
    fnWspr = (WsprDecode)lib.resolve("wspr_decode");    // 1.10 on
    fnEncode = (FtxEncode)lib.resolve("ftx_encode");    // 1.20 on
    fnWsprEnc = (WsprEncode)lib.resolve("wspr_encode");
    qDebug() << "FT: libftx" << ver() << "loaded" << (fnWspr ? "(WSPR)" : "(no WSPR: an old one)")
             << (fnEncode ? "(TX)" : "(no TX: older than 1.20)");
    if(!isRunning())
        start(QThread::LowPriority);
    return true;
}

bool FtUnit::loadJs8()
{
    if(fnJs8)
        return true;
    if(!errJs8.isEmpty())
        return false;
    QLibrary lib(QDir(QCoreApplication::applicationDirPath()).filePath("libjs8"));   // AetherSDR, 442
    if(!lib.load())
    {
#ifdef Q_OS_MACOS
        // AetherSDR, 444: macOS app bundle - Contents/Frameworks
        lib.setFileName(QDir(QCoreApplication::applicationDirPath() + "/../Frameworks").filePath("libjs8"));
        if(!lib.load())
#endif
        lib.setFileName("libjs8");
    }
    if(!lib.isLoaded() && !lib.load())
    {
        errJs8 = "libjs8 (.dll / .so / .dylib) not loaded: " + lib.errorString();
        qWarning() << "JS8:" << errJs8;
        return false;
    }
    FtxVersion ver = (FtxVersion)lib.resolve("js8_version");
    fnJs8 = (Js8Decode)lib.resolve("js8_decode");
    if(!ver || !fnJs8)
    {
        fnJs8 = 0;
        errJs8 = "libjs8: no js8_decode";
        return false;
    }
    fnJs8Enc = (Js8Encode)lib.resolve("js8_encode");
    qDebug() << "JS8: libjs8" << ver() << "loaded" << (fnJs8Enc ? "(with the encoder)" : "(no encoder: no TX)");
    if(!isRunning())
        start(QThread::LowPriority);
    return true;
}

void FtUnit::setSettings(const Settings &s)
{
    QMutexLocker l(&mtx);
    set = s;
    setSerial++;
    gapMode.store(s.mode);
    gapOffMs.store(s.timeOffsetMs);
    gapAuto.store(s.autoTime);
}

int FtUnit::phaseMs(qint64 nowMs) const
{
    const int P = periodMs(gapMode.load());
    const qint64 t = nowMs + gapOffMs.load() + (gapAuto.load() ? autoOff.load() : 0);
    return (int)((t % P + P) % P);
}

bool FtUnit::levelGap(qint64 nowMs) const
{
    if(!active.load())
        return true;
    const int m = gapMode.load();
    const int ph = phaseMs(nowMs);
    // after the decoding point (with a margin for the samples still in the ring), up to the
    // next period less the delay of the device (the new level reaches the samples later)
    return ph >= triggerMs(m) + 150 && ph <= periodMs(m) - 250;
}

// the level of the decoder input a[from..to): the noise in 3000 Hz (the median power of the
// bins of 200..3000 Hz over the period, FFT 1024 Hann: the signals left out), the peak; dBFS of
// a sine of amplitude 1
static void levelOf(const std::vector<float> &a, int from, int to, float *noiseDb, float *peakDb)
{
    const int N = 1024;
    float pk = 0.0f;
    for(int i = from; i < to; i++)
        pk = std::max(pk, std::fabs(a[i]));
    *peakDb = pk > 0.0f ? (float)(20.0 * log10(pk)) : -200.0f;
    *noiseDb = -200.0f;
    std::vector<float> w(N), re(N), im(N);
    double sw2 = 0.0;
    for(int i = 0; i < N; i++)
    {
        w[i] = (float)(0.5 - 0.5 * cos(2.0 * M_PI * i / N));
        sw2 += (double)w[i] * w[i];
    }
    const int k0 = 200 * N / 12000, k1 = 3000 * N / 12000;
    std::vector<float> pw;
    for(int pos = from; pos + N <= to; pos += N)
    {
        for(int i = 0; i < N; i++)
        {
            re[i] = a[pos + i] * w[i];
            im[i] = 0.0f;
        }
        fft(re, im);
        for(int k = k0; k <= k1; k++)
            pw.push_back(re[k] * re[k] + im[k] * im[k]);
    }
    if(pw.empty())
        return;
    std::nth_element(pw.begin(), pw.begin() + pw.size() / 2, pw.end());
    // the mean power of a noise bin (the median of an exponential: ln 2 of the mean), one-sided,
    // the window taken out; over the 3000 Hz
    const double binMean = pw[pw.size() / 2] / 0.6931;
    const double perBin = 2.0 * binMean / ((double)N * sw2);
    const double band = perBin * (3000.0 / (12000.0 / N));
    *noiseDb = (float)(10.0 * log10(std::max(1e-20, band)) - 20.0 * log10(0.70710678));
}

FtUnit::Settings FtUnit::settings()
{
    QMutexLocker l(&mtx);
    return set;
}

void FtUnit::setActive(bool on)
{
    if(on && !active.load())
        rd.store(wrDone.load());        // the audio thread does not write now
    active.store(on);
}

void FtUnit::iq(const float *l, const float *r, unsigned long n)
{
    if(!active.load(std::memory_order_acquire))
    {
        blkN = 0;
        return;
    }
    const size_t w = wr.load(std::memory_order_relaxed);
    wrDone.store(w, std::memory_order_release);         // the blocks before are complete
    const size_t space = ring.size() - (w - rd.load(std::memory_order_acquire));
    if(4 * n > space)
        n = (unsigned long)(space / 4);
    for(unsigned long i = 0; i < n; i++)
    {
        ring[(w + 4 * i) & ringMask] = r[i];            // I
        ring[(w + 4 * i + 1) & ringMask] = l[i];        // Q
        ring[(w + 4 * i + 2) & ringMask] = 0.0f;        // the audio (audio() puts it)
    }
    blkStart = w;
    blkN = n;
    wr.store(w + 4 * n, std::memory_order_release);
    stampSamples.store(stampSamples.load(std::memory_order_relaxed) + (long long)n, std::memory_order_relaxed);
    stampMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_release);
}

void FtUnit::audio(const float *a, unsigned long n)
{
    if(!active.load(std::memory_order_acquire) || blkN == 0)
        return;
    n = std::min(n, blkN);
    for(unsigned long i = 0; i < n; i++)
        ring[(blkStart + 4 * i + 2) & ringMask] = a[i];
    blkN = 0;
}

void FtUnit::requestEncode(int mode, const QString &msg)
{
    QMutexLocker l(&encMtx);
    encQ.append(qMakePair(mode, msg));
}

// the worker thread (between the decodes)
void FtUnit::doEncodes()
{
    QList<QPair<int, QString>> q;
    {
        QMutexLocker l(&encMtx);
        q.swap(encQ);
    }
    for(const auto &e : q)
    {
        QVector<int> tones;
        QString sent;
        const QByteArray m = e.second.toLatin1().left(37);
        if(e.first == WSPR)
        {
            unsigned char sym[162];
            if(fnWsprEnc && fnWsprEnc(m.constData(), sym) == 162)
            {
                tones.resize(162);
                for(int i = 0; i < 162; i++)
                    tones[i] = sym[i];
                sent = e.second;
            }
        }
        else if(fnEncode && (e.first == FT8 || e.first == FT4))
        {
            int t[110];
            char ms[40];
            memset(ms, 0, sizeof ms);
            const int n = fnEncode(e.first == FT8 ? 8 : 4, m.constData(), t, ms);
            sent = QString::fromLatin1(ms, 37).trimmed();
            if(n > 0)
            {
                tones.resize(n);
                for(int i = 0; i < n; i++)
                    tones[i] = t[i];
            }
        }
        emit encoded(e.first, e.second, tones, sent);
    }
}

void FtUnit::specLine(const QVector<float> &db, qint64 tMs)
{
    QMutexLocker l(&specMtx);
    if(specQ.size() > 400)              // nobody takes them
        specQ.remove(0, specQ.size() - 400);
    specQ.append(SpecLine { db, tMs });
}

QVector<FtUnit::SpecLine> FtUnit::takeSpectrum()
{
    QMutexLocker l(&specMtx);
    QVector<SpecLine> r;
    r.swap(specQ);
    return r;
}

bool FtUnit::js8Encode(int submode, int bits, const QString &frame, int *tones79) const
{
    if(!fnJs8Enc || frame.size() != 12)
        return false;
    const QByteArray f = frame.toLatin1();
    return fnJs8Enc(submode, bits, f.constData(), tones79) == 0;
}

void FtUnit::onDecodeJs8(void *user, int snr, float dt, float freq, const char *frame, int bits, float quality, int submode)
{
    Q_UNUSED(submode);
    DecodeCtx *c = (DecodeCtx *)user;
    Decode d;
    d.snr = snr;
    d.dt = dt;
    d.freq = freq;
    d.msg = QString::fromLatin1(frame);
    d.nap = 0;
    d.qual = quality;
    d.bits = bits;
    c->list->append(d);
}

void FtUnit::onDecodeWspr(void *user, float snr, float dt, double freqMHz, float audioHz, int drift, const char *msg)
{
    Q_UNUSED(freqMHz);
    DecodeCtx *c = (DecodeCtx *)user;
    Decode d;
    d.snr = (int)lround(snr);
    d.dt = dt;
    d.freq = audioHz;
    d.msg = QString::fromLatin1(msg).simplified();
    d.nap = 0;
    d.qual = 0.0f;
    d.bits = drift;
    c->list->append(d);
}

void FtUnit::onDecode(void *user, int snr, float dt, float freq, const char *msg, int nap, float qual)
{
    DecodeCtx *c = (DecodeCtx *)user;
    Decode d;
    d.snr = snr;
    d.dt = dt;
    d.freq = freq;
    d.msg = QString::fromLatin1(msg).trimmed();
    d.nap = nap;
    d.qual = qual;
    d.bits = 0;
    c->list->append(d);
}

void FtUnit::run()
{
    // stage 1: 48 kHz complex -> 12 kHz complex (flat to about 4.5 kHz)
    const std::vector<float> h1 = lowpass(127, 5600.0 / 48000.0);
    const int n1 = (int)h1.size();
    std::vector<float> x1i(2 * n1, 0.0f), x1q(2 * n1, 0.0f);
    int p1 = 0, ph1 = 0;
    // stage 2: 12 kHz, the complex band pass 100 .. 4000 Hz (the upper sideband only)
    const double fc2 = 2050.0 / 12000.0;
    const std::vector<float> lp2 = lowpass(301, 1950.0 / 12000.0);
    const int n2 = (int)lp2.size();
    std::vector<float> h2r(n2), h2i(n2);
    for(int i = 0; i < n2; i++)
    {
        // y[k] = sum h[i] x[k - i]: the band pass taps h[i] e^{j 2 pi fc i}
        h2r[i] = (float)(lp2[i] * cos(2.0 * M_PI * fc2 * i));
        h2i[i] = (float)(lp2[i] * sin(2.0 * M_PI * fc2 * i));
    }
    std::vector<float> x2i(2 * n2, 0.0f), x2q(2 * n2, 0.0f);
    int p2 = 0;
    // the delay of the two filters (linear phase): the audio sample is this much older
    const double groupDelayMs = (n1 - 1) / 2.0 / 48.0 + (n2 - 1) / 2.0 / 12.0;

    const int NBUF = 1440000;                    // the longest period: WSPR, 2 min
    std::vector<float> pbuf(NBUF, 0.0f);        // the audio of the period (12 kHz)
    std::vector<float> abuf(NBUF, 0.0f);        // check: the receiver audio (DSP output), 12 kHz
    std::vector<float> x1a(2 * n1, 0.0f);
    std::vector<short> buf16(NBUF, 0);
    std::vector<float> cbuf(2 * NBUF, 0.0f);     // diagnostics: the complex baseband (12 kHz) of the period
    std::vector<short> c16(2 * NBUF, 0);
    long long curP = -1;                         // the period index (ms since the epoch / period)
    bool decodedP = true;
    bool earlyDoneP = true;                      // FT8: the early decode of this period done (or not to be)
    QVector<Decode> earlyList;                   // ... its decodes (the time correction takes them too)
    // the start of the last period decoded, ms since the epoch: not again after a time jump back.
    // In ms, not as a period index: the index of another mode is another number (FT4 7.5 s
    // periods: twice the FT8 index - after FT4 every FT8 period was taken as decoded already)
    long long lastDecMs = -1;
    int autoNow = autoOff.load();                // the automatic time correction in use, ms
    int autoBase = autoNow;                      // ... the last one with decodes
    int noDec = 0, searchStep = 0;
    int maxPos = 0, firstPos = -1;
    int filled = 0, resets = 0;                  // diagnostics: samples in the period, clock resets
    double epoch0 = NAN;                         // ms since the epoch of the 48 kHz sample 0
    long long k48 = 0;                           // 48 kHz samples taken from the ring
    long long stamp0 = 0;                        // stampSamples when the ring was emptied
    Settings s;
    int serialSeen = -1;
    bool wasActive = false;
    std::vector<float> chunk;
    SpecAcc spec;
    QVector<float> specOut;

    while(!quit.load())
    {
        msleep(15);
        doEncodes();
        if(!active.load())
        {
            wasActive = false;
            continue;
        }
        if(!wasActive)
        {
            // a new start: the ring from now on
            wasActive = true;
            epoch0 = NAN;
            curP = -1;
            decodedP = true;
            std::fill(x1i.begin(), x1i.end(), 0.0f);
            std::fill(x1q.begin(), x1q.end(), 0.0f);
            std::fill(x1a.begin(), x1a.end(), 0.0f);
            std::fill(x2i.begin(), x2i.end(), 0.0f);
            std::fill(x2q.begin(), x2q.end(), 0.0f);
            k48 = 0;
            stamp0 = -1;
            spec.reset();
        }
        if(setSerial.load() != serialSeen)
        {
            QMutexLocker l(&mtx);
            const int oldMode = s.mode;
            s = set;
            serialSeen = setSerial.load();
            if(s.mode != oldMode)
            {
                curP = -1;
                decodedP = true;
                lastDecMs = -1;
            }
        }
        {
            const int a = autoSet.exchange(AUTO_NONE);
            if(a != AUTO_NONE)
            {
                autoNow = autoBase = a;
                autoOff.store(a);
                autoGood.store(a);
                noDec = searchStep = 0;
                curP = -1;
            }
        }
        const int P = periodMs(s.mode);
        // the samples the decoder takes, the decoding point in the period (the signals end at:
        // FT8 13.1 s, FT4 5.5 s, WSPR 111.6 s; JS8 Normal 13.1 s, Fast 8.1 s, Turbo 4.1 s, Slow 25.8 s)
        int N, trigger;
        switch(s.mode)
        {
            case FT8:  N = 180000; trigger = 172800; break;
            case FT4:  N = 72576;  trigger = 73200;  break;
            case JS8N: N = 180000; trigger = 172800; break;
            case JS8F: N = 120000; trigger = 108000; break;
            case JS8T: N = 72000;  trigger = 57600;  break;
            case JS8S: N = 360000; trigger = 324000; break;
            case WSPR: N = 1368000; trigger = 1368000; break;  // wsprd takes 114 s
            default:   N = 180000; trigger = 172800; break;
        }
        const bool js8 = isJs8(s.mode);
        const bool wspr = (s.mode == WSPR);

        const size_t r0 = rd.load(std::memory_order_relaxed);
        const size_t avail = wrDone.load(std::memory_order_acquire) - r0;
        const int nIq = (int)std::min<size_t>(avail / 4, 16384);
        if(nIq <= 0)
            continue;
        chunk.resize(4 * nIq);
        for(int i = 0; i < 4 * nIq; i++)
            chunk[i] = ring[(r0 + i) & ringMask];
        rd.store(r0 + 4 * nIq, std::memory_order_release);

        // the time of the 48 kHz samples: the wall clock of the last block written, smoothed
        // (a late callback only makes it later: follow down fast, up slowly)
        const long long sS = stampSamples.load(std::memory_order_relaxed);
        const long long sM = stampMs.load(std::memory_order_acquire);
        if(stamp0 < 0)
            stamp0 = sS - (long long)((wr.load() - r0) / 4);     // the sample k48 = 0
        const double est = (double)sM - (double)(sS - stamp0) / 48.0;
        if(std::isnan(epoch0) || fabs(est - epoch0) > 300.0)
        {
            if(!std::isnan(epoch0))
            {
                resets++;
                qDebug() << "FT: clock reset," << (est - epoch0) << "ms";
            }
            epoch0 = est;
        }
        else if(est < epoch0)
            epoch0 += 0.3 * (est - epoch0);
        else
            epoch0 += 0.003 * (est - epoch0);

        for(int i = 0; i < nIq; i++)
        {
            x1i[p1] = x1i[p1 + n1] = chunk[4 * i];
            x1q[p1] = x1q[p1 + n1] = chunk[4 * i + 1];
            x1a[p1] = x1a[p1 + n1] = chunk[4 * i + 2];
            p1 = (p1 + 1) % n1;
            const long long kNow = k48++;
            if(++ph1 < 4)
                continue;
            ph1 = 0;
            float yi = 0.0f, yq = 0.0f, ya = 0.0f;
            const float *ai = &x1i[p1], *aq = &x1q[p1], *aa = &x1a[p1];
            for(int j = 0; j < n1; j++)
            {
                yi += h1[j] * ai[j];
                yq += h1[j] * aq[j];
                ya += h1[j] * aa[j];
            }
            // stage 2: the newest sample last; y = sum h[m] x[k - m]
            x2i[p2] = x2i[p2 + n2] = yi;
            x2q[p2] = x2q[p2 + n2] = yq;
            p2 = (p2 + 1) % n2;
            const float *bi = &x2i[p2], *bq = &x2q[p2];      // oldest .. newest
            float a = 0.0f;
            for(int m = 0; m < n2; m++)
            {
                const float xr = bi[n2 - 1 - m], xi = bq[n2 - 1 - m];
                a += h2r[m] * xr - h2i[m] * xi;            // Re(h x)
            }

            // into the period buffer by its UTC time
            const double t = epoch0 + kNow / 48.0 + s.timeOffsetMs + (s.autoTime ? autoNow : 0) - groupDelayMs;
            if(specOn.load(std::memory_order_relaxed) && spec.push(a, specOut))
                specLine(specOut, (qint64)t);
            const long long p = (long long)floor(t / P);
            if(p != curP)
            {
                curP = p;
                decodedP = (p * P <= lastDecMs);
                earlyDoneP = decodedP;
                earlyList.clear();
                std::fill(pbuf.begin(), pbuf.end(), 0.0f);
                std::fill(cbuf.begin(), cbuf.end(), 0.0f);
                std::fill(abuf.begin(), abuf.end(), 0.0f);
                maxPos = 0;
                firstPos = -1;
                filled = 0;
                emit periodStarted(curP * P, s.mode);
            }
            const int pos = (int)((t - (double)curP * P) * 12.0);
            if(pos >= 0 && pos < N)
            {
                pbuf[pos] = a;
                // the receiver audio: no stage 2, so (n2 - 1) / 2 samples later in the period
                const int pa = pos + (n2 - 1) / 2;
                if(pa < N)
                    abuf[pa] = ya;
                cbuf[2 * pos] = yi;
                cbuf[2 * pos + 1] = yq;
                filled++;
                if(firstPos < 0)
                    firstPos = pos;
                maxPos = std::max(maxPos, pos);
            }
            // the early decode, on the audio so far (zeros after it): FT8 at 11.8 s (as WSJT-X);
            // JS8 at 90 % of its signal (Normal 11.8 s, Fast 7.3 s, Turbo 3.7 s, Slow 23.2 s) - the
            // full decode then brings the same frames again: the JS8 window shows them once
            int EARLY;
            switch(s.mode)
            {
                case FT8: case JS8N: EARLY = 41 * 3456; break;
                case JS8F: EARLY = 87600; break;
                case JS8T: EARLY = 44400; break;
                case JS8S: EARLY = 278400; break;
                default:   EARLY = 0; break;
            }
            if(!decodedP && !earlyDoneP && EARLY > 0 && pos >= EARLY && pos < trigger)
            {
                earlyDoneP = true;
                if(s.early && ((s.mode == FT8 && s.depth >= 2 && fnDecode) || (js8 && fnJs8)))
                {
                    const int from = std::max(0, firstPos), to = std::min(maxPos, N - 1);
                    std::vector<double> blk;
                    for(int b = from; b + 600 <= to; b += 600)
                    {
                        double e = 0.0;
                        for(int q = 0; q < 600; q++)
                            e += (double)pbuf[b + q] * pbuf[b + q];
                        blk.push_back(sqrt(e / 600.0));
                    }
                    if((int)blk.size() >= std::min(100, EARLY / 1200) && firstPos < 12000)  // the period from its start
                    {
                        std::nth_element(blk.begin(), blk.begin() + blk.size() / 2, blk.end());
                        const double g = 50.0 / std::max(1e-12, blk[blk.size() / 2]);
                        for(int q = 0; q < N; q++)
                            buf16[q] = (short)std::max(-32767.0, std::min(32767.0, pbuf[q] * g));
                        const QTime tm = QDateTime::fromMSecsSinceEpoch(curP * P, Qt::UTC).time();
                        const int nutc = tm.hour() * 10000 + tm.minute() * 100 + tm.second();
                        DecodeCtx ctx { &earlyList };
                        QElapsedTimer et;
                        et.start();
                        const QByteArray my = s.mycall.toLatin1(), his = s.hiscall.toLatin1(), hg = s.hisgrid.toLatin1();
                        const int apOn = s.ap && !s.mycall.isEmpty() ? 1 : 0;
                        if(js8)
                            fnJs8(s.mode - JS8N, buf16.data(), N, s.nfa, s.nfb, s.nfqso, nutc, &FtUnit::onDecodeJs8, &ctx);
                        else
                            fnDecode(s.mode, buf16.data(), N, s.nfa, s.nfb, s.nfqso, s.depth, apOn,
                                     0, nutc, 41, my.constData(), his.constData(), hg.constData(), &FtUnit::onDecode, &ctx);
                        qDebug() << "FT:" << modeName(s.mode) << tm.toString("HH:mm:ss") << "early:" << earlyList.size()
                                 << "decodes," << et.elapsed() << "ms";
                        emit earlyDecoded(curP * P, s.mode, earlyList, (int)et.elapsed());
                    }
                }
            }
            if(!decodedP && pos >= trigger && (js8 ? fnJs8 != 0 : wspr ? fnWspr != 0 : fnDecode != 0))
            {
                decodedP = true;
                lastDecMs = curP * P;
                // the level: the median of the rms of 50 ms blocks (the noise, not the
                // strong signals) to int16 rms 50
                const int from = std::max(0, firstPos), to = std::min(maxPos, N - 1);
                std::vector<double> blk;
                for(int b = from; b + 600 <= to; b += 600)
                {
                    double e = 0.0;
                    for(int q = 0; q < 600; q++)
                        e += (double)pbuf[b + q] * pbuf[b + q];
                    blk.push_back(sqrt(e / 600.0));
                }
                const QTime pt = QDateTime::fromMSecsSinceEpoch(curP * P, Qt::UTC).time();
                if(blk.size() < 10 || (wspr && firstPos > 12000))
                {
                    // WSPR: the signals start at 1 s - a period begun later is not decoded
                    // too little of the period (just started)
                    qDebug() << "FT:" << modeName(s.mode) << pt.toString("HH:mm:ss.zzz") << "skipped: audio"
                             << filled / 12 << "ms, from" << firstPos / 12 << "ms";
                    emit decoded(curP * P, s.mode, QVector<Decode>(), -1);
                    continue;
                }
                std::nth_element(blk.begin(), blk.begin() + blk.size() / 2, blk.end());
                const double med = std::max(1e-12, blk[blk.size() / 2]);
                const double g = 50.0 / med;
                {
                    float nd, pd;
                    levelOf(pbuf, from, to, &nd, &pd);
                    level.store(nd);
                    peak.store(pd);
                }
                gainDbNow.store((int)lround(20.0 * log10(g)));
                for(int q = 0; q < N; q++)
                {
                    const double v = pbuf[q] * g;
                    buf16[q] = (short)std::max(-32767.0, std::min(32767.0, v));
                }
                // diagnostics: what the decoder got (mono) and the IQ around the dial (I left, Q right);
                // WSPR: its audio only (ft_wspr.wav, 2 min: test_ftx wspr ft_wspr.wav <dial MHz>)
                if(wspr)
                    writeWav("ft_wspr.wav", buf16.data(), N, 1);
                else
                {
                    writeWav("ft_audio.wav", buf16.data(), N, 1);
                    for(int q = 0; q < 2 * N; q++)
                        c16[q] = (short)std::max(-32767.0, std::min(32767.0, cbuf[q] * g));
                    writeWav("ft_iq.wav", c16.data(), N, 2);
                    {
                        // the receiver audio (what the speaker gets, before the volume)
                        double e = 0.0;
                        for(int q = 0; q < N; q++)
                            e += (double)abuf[q] * abuf[q];
                        const double ga = 1000.0 / std::max(1e-9, sqrt(e / N));
                        for(int q = 0; q < N; q++)
                            c16[q] = (short)std::max(-32767.0, std::min(32767.0, abuf[q] * ga));
                        writeWav("ft_rx.wav", c16.data(), N, 1);
                    }
                }
                const QDateTime dt = QDateTime::fromMSecsSinceEpoch(curP * P, Qt::UTC);
                const QTime tm = dt.time();
                const int nutc = tm.hour() * 10000 + tm.minute() * 100 + tm.second();
                QVector<Decode> list;
                DecodeCtx ctx { &list };
                QElapsedTimer et;
                et.start();
                const QByteArray my = s.mycall.toLatin1(), his = s.hiscall.toLatin1(), hg = s.hisgrid.toLatin1();
                const int apOn = s.ap && !s.mycall.isEmpty() ? 1 : 0;
                if(js8)
                    fnJs8(s.mode - JS8N, buf16.data(), N, s.nfa, s.nfb, s.nfqso, nutc, &FtUnit::onDecodeJs8, &ctx);
                else if(wspr)
                {
                    // wsprd: its directory (the WAV of the period for a moment, the hash table)
                    const QString dir = QDir::homePath() + "/AetherSDR-FTX/wspr";
                    QDir().mkpath(dir);
                    const QByteArray d8 = QDir::toNativeSeparators(dir).toLocal8Bit();
                    const QDate dd = dt.date();
                    const int yymmdd = (dd.year() % 100) * 10000 + dd.month() * 100 + dd.day();
                    const int r = fnWspr(buf16.data(), N, s.dialHz / 1e6, d8.constData(), yymmdd,
                                         tm.hour() * 100 + tm.minute(), s.depth, &FtUnit::onDecodeWspr, &ctx);
                    if(r < 0)
                        qWarning() << "FT: WSPR decoder error" << r << "(the directory" << dir << ")";
                }
                else if(s.mode == FT8)
                {
                    // FT8 (ft8_decode.f90): the audio is taken in at nzhsym 41 / 47 only - the call
                    // at 50 alone decodes the old (empty) buffer. As WSJT-X: 47 first (after the
                    // early decode: it subtracts those signals; without it: just takes the audio),
                    // then the full decode at 50 (the early messages not again).
                    fnDecode(s.mode, buf16.data(), N, s.nfa, s.nfb, s.nfqso, s.depth, apOn,
                             0, nutc, 47, my.constData(), his.constData(), hg.constData(), &FtUnit::onDecode, &ctx);
                }
                if(!js8 && !wspr)
                    fnDecode(s.mode, buf16.data(), N, s.nfa, s.nfb, s.nfqso, s.depth, apOn,
                             0, nutc, 50, my.constData(), his.constData(), hg.constData(), &FtUnit::onDecode, &ctx);
                qDebug() << "FT:" << modeName(s.mode) << pt.toString("HH:mm:ss.zzz") << "audio" << filled / 12
                         << "ms from" << firstPos / 12 << "ms, noise" << QString::number(20.0 * log10(med), 'f', 1)
                         << "dBFS, gain" << gainDbNow.load() << "dB," << list.size() << "decodes (early"
                         << earlyList.size() << "),"
                         << et.elapsed() << "ms, clock resets" << resets;
                emit decoded(curP * P, s.mode, list, (int)et.elapsed());

                // the automatic time correction: the median DT of the decodes to 0; no decodes
                // for 2 periods: a search beyond the DT range of the decoder (the PC clock wrong
                // by seconds), base -1, +1, -2, +2, -3, +3 steps, then the base again. WSPR: no
                // search (wsprd finds signals +-2 s off itself), the DT only when 0.5 s off
                // (the stations themselves are less exact)
                if(s.autoTime)
                {
                    int next = autoNow;
                    int step;
                    switch(s.mode)
                    {
                        case FT8: case JS8N: step = 2000; break;
                        case FT4: case JS8F: step = 1000; break;
                        case JS8S: step = 4000; break;
                        default: step = 2000; break;
                    }
                    if(!list.isEmpty() || !earlyList.isEmpty())
                    {
                        std::vector<float> dts;
                        for(const Decode &d : list)
                            dts.push_back(d.dt);
                        for(const Decode &d : earlyList)
                            dts.push_back(d.dt);
                        std::nth_element(dts.begin(), dts.begin() + dts.size() / 2, dts.end());
                        const double md = dts[dts.size() / 2];
                        const double lim = wspr ? 0.5 : (s.mode == FT8 || s.mode == JS8N || s.mode == JS8S) ? 0.08 : (s.mode == FT4 || s.mode == JS8F) ? 0.04 : 0.02;
                        if(fabs(md) > lim)
                            next = autoNow - (int)lround(md * 1000.0 * (fabs(md) > 0.5 ? 1.0 : 0.5));
                        autoBase = next;
                        autoGood.store(next);   // saved at the end (not a search step)
                        noDec = searchStep = 0;
                        searching.store(false);
                    }
                    else if(!wspr && ++noDec >= 2)
                    {
                        searchStep = (searchStep + 1) % 7;          // 0 = the base
                        if(searchStep == 0 && autoBase != 0)
                        {
                            // a whole search round without decodes: around 0 from now on (an
                            // old correction need not be right any more: the PC clock set)
                            qDebug() << "FT: time correction: no decodes around" << autoBase << "ms, now around 0";
                            autoBase = 0;
                        }
                        const int k = (searchStep + 1) / 2;
                        next = autoBase + ((searchStep & 1) ? -k : k) * step;
                        searching.store(searchStep != 0);
                    }
                    next = qBound(-15000, next, 15000);
                    if(next != autoNow)
                    {
                        qDebug() << "FT: time correction" << autoNow << "->" << next << "ms"
                                 << (list.isEmpty() ? "(search)" : "(DT of the decodes)");
                        autoNow = next;
                        autoOff.store(next);
                        curP = -1;              // the samples to their new periods
                    }
                }
            }
        }
    }
}
