! FFTW constants for four2a.f90 (replaces the WSJT-X fftw3mod.f90 that includes fftw3.f03);
! the legacy Fortran calls sfftw_* are in c/fftw_shim.c (to the C API of FFTW)
module FFTW3
  use, intrinsic :: iso_c_binding
  integer(c_int), parameter :: FFTW_FORWARD = -1
  integer(c_int), parameter :: FFTW_BACKWARD = 1
  integer(c_int), parameter :: FFTW_MEASURE = 0
  integer(c_int), parameter :: FFTW_EXHAUSTIVE = 8
  integer(c_int), parameter :: FFTW_PATIENT = 32
  integer(c_int), parameter :: FFTW_ESTIMATE = 64
  integer(c_int), parameter :: FFTW_ESTIMATE_PATIENT = 128
end module FFTW3
