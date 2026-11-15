/*
 * The part of the single precision FFTW API that the FT8 / FT4 / WSPR decoders use
 * (fftwf_plan_dft_1d, _r2c_1d, _c2r_1d, fftwf_execute, fftwf_destroy_plan, fftwf_malloc / free,
 * the wisdom calls), on pocketfft (c/pocketfft/pocketfft_hdronly.h, BSD-3, M. Reinecke): the
 * Linux build of libftx.so has no FFTW dependency (the Windows DLL links FFTW statically).
 * Any length (mixed radix, Bluestein for large primes); unnormalised, as FFTW.
 * Build with FTX_SYSTEM_FFTW=1 (build_linux.sh) to use the FFTW of the system instead.
 */
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pocketfft/pocketfft_hdronly.h"

#include "fftw/fftw3.h"          // the declarations of FFTW 3.3 (api/fftw3.h)

struct fftwf_plan_s
{
    int kind;               // 0 c2c, 1 r2c, 2 c2r
    size_t n;
    bool forward;
    void *in, *out;
};

extern "C" {

void *fftwf_malloc(size_t n)
{
    void *p = nullptr;
    if(posix_memalign(&p, 64, n ? n : 1) != 0)
        return nullptr;
    return p;
}

void fftwf_free(void *p)
{
    free(p);
}

fftwf_plan fftwf_plan_dft_1d(int n, fftwf_complex *in, fftwf_complex *out, int sign, unsigned)
{
    return new fftwf_plan_s{ 0, (size_t)n, sign < 0, in, out };
}

fftwf_plan fftwf_plan_dft_r2c_1d(int n, float *in, fftwf_complex *out, unsigned)
{
    return new fftwf_plan_s{ 1, (size_t)n, true, in, out };
}

fftwf_plan fftwf_plan_dft_c2r_1d(int n, fftwf_complex *in, float *out, unsigned)
{
    return new fftwf_plan_s{ 2, (size_t)n, false, in, out };
}

void fftwf_execute(const fftwf_plan p)
{
    if(!p || p->n == 0)
        return;
    using namespace pocketfft;
    const shape_t shape{ p->n };
    const shape_t axes{ 0 };
    const stride_t sc{ (ptrdiff_t)sizeof(std::complex<float>) };
    const stride_t sr{ (ptrdiff_t)sizeof(float) };
    switch(p->kind)
    {
    case 0:
        c2c(shape, sc, sc, axes, p->forward, (const std::complex<float> *)p->in,
            (std::complex<float> *)p->out, 1.0f, 1);
        break;
    case 1:
        r2c(shape, sr, sc, 0, true, (const float *)p->in, (std::complex<float> *)p->out, 1.0f, 1);
        break;
    case 2:
        c2r(shape, sc, sr, 0, false, (const std::complex<float> *)p->in, (float *)p->out, 1.0f, 1);
        break;
    }
}

void fftwf_destroy_plan(fftwf_plan p)
{
    delete p;
}

// wisdom: nothing to keep (pocketfft plans are cheap and cached)
int fftwf_import_wisdom_from_file(FILE *)
{
    return 0;
}

void fftwf_export_wisdom_to_file(FILE *)
{
}

} // extern "C"
