// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
#define _USE_MATH_DEFINES   // AetherSDR: M_PI under MSVC
// The transmitter of the FT8 / FT4 / WSPR mode: see FtTx.h
#define _USE_MATH_DEFINES
#include "FtTx.h"
#include <QDateTime>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {

// gfsk_pulse of WSJT-X
double gfskPulse(double b, double t)
{
    const double c = M_PI * sqrt(2.0 / log(2.0));
    return 0.5 * (erf(c * b * (t + 0.5)) - erf(c * b * (t - 0.5)));
}

// the frequency of the GFSK signal (rad per sample), as gen_ft8wave / gen_ft4wave: each symbol
// a pulse 3 symbols long; extend: the first and last symbols extended by one symbol (FT8)
std::vector<double> gfskDphi(const int *tones, int nsym, int nsps, double bt, double f0, bool extend)
{
    const double twopi = 2.0 * M_PI;
    std::vector<double> pulse(3 * nsps);
    for(int i = 0; i < 3 * nsps; i++)
        pulse[i] = gfskPulse(bt, (i + 1 - 1.5 * nsps) / (double)nsps);     // (Fortran i from 1)
    const double peak = twopi / nsps;           // hmod 1
    std::vector<double> dphi((size_t)(nsym + 2) * nsps, 0.0);
    for(int j = 0; j < nsym; j++)
    {
        const size_t ib = (size_t)j * nsps;
        for(int i = 0; i < 3 * nsps; i++)
            dphi[ib + i] += peak * pulse[i] * tones[j];
    }
    if(extend)
    {
        for(int i = 0; i < 2 * nsps; i++)
        {
            dphi[i] += peak * tones[0] * pulse[nsps + i];
            dphi[(size_t)nsym * nsps + i] += peak * tones[nsym - 1] * pulse[i];
        }
    }
    const double df = twopi * f0 / FtTx::RATE;
    for(double &d : dphi)
        d += df;
    return dphi;
}

} // namespace

FtTx::FtTx() : cur(-1), t0(0), on(false), finished(false), resync(false), pos(0)
{
}

std::vector<float> FtTx::ft8Wave(const int *tones, double f0)
{
    const int nsym = 79, nsps = RATE / 1000 * 160;     // 7680: 0.16 s
    const std::vector<double> dphi = gfskDphi(tones, nsym, nsps, 2.0, f0, true);
    const int nwave = nsym * nsps;
    std::vector<float> w(nwave);
    double phi = 0.0;
    for(int k = 0; k < nwave; k++)
    {
        w[k] = (float)sin(phi);
        phi = fmod(phi + dphi[(size_t)nsps + k], 2.0 * M_PI);     // the dummy symbols not sent
    }
    const int nramp = (int)lround(nsps / 8.0);
    for(int i = 0; i < nramp; i++)
    {
        const double a = (1.0 - cos(2.0 * M_PI * i / (2.0 * nramp))) / 2.0;
        w[i] *= (float)a;
        w[nwave - nramp + i] *= (float)(1.0 - a);
    }
    return w;
}

std::vector<float> FtTx::ft4Wave(const int *tones, double f0)
{
    const int nsym = 103, nsps = RATE / 1000 * 48;     // 2304: 48 ms
    const std::vector<double> dphi = gfskDphi(tones, nsym, nsps, 1.0, f0, false);
    const int nwave = (nsym + 2) * nsps;
    std::vector<float> w(nwave);
    double phi = 0.0;
    for(int k = 0; k < nwave; k++)
    {
        w[k] = (float)sin(phi);
        phi = fmod(phi + dphi[k], 2.0 * M_PI);
    }
    // the ramp up and down symbols
    for(int i = 0; i < nsps; i++)
    {
        const double a = (1.0 - cos(2.0 * M_PI * i / (2.0 * nsps))) / 2.0;
        w[i] *= (float)a;
        w[(size_t)(nsym + 1) * nsps + i] *= (float)(1.0 - a);
    }
    return w;
}

std::vector<float> FtTx::wsprWave(const unsigned char *sym, double fc)
{
    // 8192 samples a symbol at 12 kHz = 32768 at 48 kHz; the tone spacing 12000 / 8192 Hz
    const int nsps = 32768, nsym = 162;
    const double dfTone = 12000.0 / 8192.0;
    std::vector<float> w((size_t)nsym * nsps);
    double phi = 0.0;
    for(int j = 0; j < nsym; j++)
    {
        const double dp = 2.0 * M_PI * (fc + (sym[j] - 1.5) * dfTone) / RATE;
        for(int i = 0; i < nsps; i++)
        {
            w[(size_t)j * nsps + i] = (float)sin(phi);
            phi = fmod(phi + dp, 2.0 * M_PI);
        }
    }
    // 5 ms ramps (no clicks)
    const int nr = RATE / 200;
    for(int i = 0; i < nr; i++)
    {
        const double a = (1.0 - cos(M_PI * i / nr)) / 2.0;
        w[i] *= (float)a;
        w[w.size() - 1 - i] *= (float)a;
    }
    return w;
}

std::vector<float> FtTx::js8Wave(const int *tones, double f0, int nsps12)
{
    // the Modulator of JS8Call: 4 samples at 48 kHz for each one at 12 kHz, the phase continuous,
    // the frequency stepped at the symbol edges
    const int nsym = 79, nsps = 4 * nsps12;
    const double df = 12000.0 / nsps12;
    std::vector<float> w((size_t)nsym * nsps);
    double phi = 0.0;
    for(int j = 0; j < nsym; j++)
    {
        const double dp = 2.0 * M_PI * (f0 + tones[j] * df) / RATE;
        for(int i = 0; i < nsps; i++)
        {
            w[(size_t)j * nsps + i] = (float)sin(phi);
            phi = fmod(phi + dp, 2.0 * M_PI);
        }
    }
    // the ramps: 1/8 symbol (no clicks; JS8Call: a fade out of the last 2 % of a symbol)
    const int nr = nsps / 8;
    const size_t n = w.size();
    for(int i = 0; i < nr; i++)
    {
        const double a = (1.0 - cos(M_PI * i / nr)) / 2.0;
        w[i] *= (float)a;
        w[n - 1 - i] *= (float)a;
    }
    return w;
}

void FtTx::arm(std::vector<float> &&wave, qint64 startMs)
{
    // the buffer not in use (the audio thread reads cur once a block; arm is seconds apart)
    const int c = cur.load();
    const int b = (c == 0) ? 1 : 0;
    buf[b] = std::move(wave);
    if(startMs != t0.load())
        resync.store(true);             // another start: the position from the clock again
    t0.store(startMs);
    finished.store(false);
    cur.store(b, std::memory_order_release);
}

void FtTx::start()
{
    finished.store(false);
    resync.store(true);
    on.store(true, std::memory_order_release);
}

void FtTx::stop()
{
    on.store(false, std::memory_order_release);
    cur.store(-1);
}

int FtTx::lengthMs() const
{
    const int c = cur.load();
    return c < 0 ? 0 : (int)(buf[c].size() * 1000 / RATE);
}

void FtTx::audio(float *l, float *r, unsigned long n)
{
    if(!on.load(std::memory_order_acquire))
        return;
    const int c = cur.load(std::memory_order_acquire);
    if(c < 0)
    {
        memset(l, 0, sizeof(float) * n);
        memset(r, 0, sizeof(float) * n);
        return;
    }
    if(resync.exchange(false))
    {
        // the position from the clock (once; then by the samples: no jitter)
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        pos = (now - t0.load()) * (RATE / 1000);
    }
    const std::vector<float> &w = buf[c];
    const long long len = (long long)w.size();
    for(unsigned long i = 0; i < n; i++, pos++)
        l[i] = (pos >= 0 && pos < len) ? w[(size_t)pos] : 0.0f;
    memcpy(r, l, sizeof(float) * n);
    if(pos >= len)
        finished.store(true, std::memory_order_release);
}
