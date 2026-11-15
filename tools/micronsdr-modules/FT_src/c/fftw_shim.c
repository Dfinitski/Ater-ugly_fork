/* The legacy Fortran interface of FFTW (sfftw_*) that four2a.f90 calls, on the C API of
   single precision FFTW (libfftw3f). gfortran: lower case names + one underscore, all by
   reference; a plan is an integer*8 (a pointer). */
#include <fftw3.h>

void sfftw_plan_dft_1d_(fftwf_plan *p, int *n, fftwf_complex *in, fftwf_complex *out, int *sign, int *flags)
{
    *p = fftwf_plan_dft_1d(*n, in, out, *sign, (unsigned)*flags);
}

void sfftw_plan_dft_r2c_1d_(fftwf_plan *p, int *n, float *in, fftwf_complex *out, int *flags)
{
    *p = fftwf_plan_dft_r2c_1d(*n, in, out, (unsigned)*flags);
}

void sfftw_plan_dft_c2r_1d_(fftwf_plan *p, int *n, fftwf_complex *in, float *out, int *flags)
{
    *p = fftwf_plan_dft_c2r_1d(*n, in, out, (unsigned)*flags);
}

void sfftw_execute_(fftwf_plan *p)
{
    fftwf_execute(*p);
}

void sfftw_destroy_plan_(fftwf_plan *p)
{
    fftwf_destroy_plan(*p);
}
