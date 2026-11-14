third_party/libftx -- the FT8 / FT4 / WSPR decoders and encoders for the FTX mode
=================================================================================

win64/libftx.dll  The decoders and encoders of WSJT-X (K1JT and the WSJT-X team,
                  GPL-3; WSPR: wsprd, K1JT and K9AN) in one DLL with a plain C
                  interface (ftx_api.h). Built by MicronSDR's FT_src (N7DDC) with
                  MSYS2 MinGW gfortran; the gfortran runtime and FFTW are linked
                  statically inside, so it needs no other DLL. Loaded at run time
                  by src/ftx/FtUnit.cpp (QLibrary) from the program's directory.
win64/cty.dat     The country file (AD1C), read by src/ftx/FtPanel.cpp.

The source of the DLL and how to build it: MicronSDR, folder FT_src (build.sh,
README.txt). License: GPL-3 (COPYING); the authors of WSJT-X: AUTHORS.
