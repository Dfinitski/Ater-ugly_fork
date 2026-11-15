/* Fortran wrapper to seed the C library rand (WSJT-X lib/sgran.c, without init_random_seed.c) */
#include <stdlib.h>
#include <time.h>
void sgran_(void)
{
  srand((unsigned)time(NULL));
}
