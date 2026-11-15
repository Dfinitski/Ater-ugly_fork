/* glibc compatibility of libftx.so (x86_64): glibc 2.38 gave fmod a new symbol version
   (fmod@GLIBC_2.38); a library linked on such a system would not load on an older one. The
   library calls fmod through this wrapper (ld --wrap=fmod), bound to the base version
   fmod@GLIBC_2.2.5 that every x86_64 glibc has. Same result: fmod is exact either way. */
#if defined(__linux__) && defined(__x86_64__)
__asm__(".symver fmod,fmod@GLIBC_2.2.5");
double fmod(double, double);
double __wrap_fmod(double x, double y)
{
    return fmod(x, y);
}
#endif
