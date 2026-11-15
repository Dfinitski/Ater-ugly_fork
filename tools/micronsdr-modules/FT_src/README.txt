FT_src - libftx.dll: the FT8 / FT4 / WSPR decoders for MicronSDR (Micron-TRX)
==============================================================================
Russian version: README_ru.txt

What it is
  The decoders of WSJT-X (K1JT and the WSJT-X team, GPL-3) in a separate DLL with a plain C
  interface (ftx_api.h), like libfdv.dll for FreeDV:
    lib/ft8_decode.f90, lib/ft4_decode.f90 and everything they need;
    lib/wsprd/   - WSPR: wsprd of WSJT-X (K1JT, K9AN) as a function (wsprd_main); the changes
                   are marked "Micron" (the spectrum array not on the stack, leaks, the output
                   of the decodes);
    ftx_api.f90  - the DLL interface (ftx_decode, ftx_version);
    c/           - CRC, the FFTW adapter, stubs instead of the Qt shared memory, the noise
                   generator, wspr_api.c - the WSPR interface (wspr_decode);
    test/        - test_ftx.c: decoding a WAV file; wsprgen.c: a WSPR test signal.
  The test signal generators of WSJT-X (ft8sim, ft4sim) are built too.
  Version 1.10: FT2 removed (build.sh deletes its old files at the first run), WSPR instead.
  Version 1.20: the transmitter - ftx_encode (FT8 / FT4: message -> tones, genft8 / genft4 of
  WSJT-X) and wspr_encode (WSPR: 162 symbols); MicronSDR makes the signal from the tones
  (FtTx.cpp).

Building (Windows, once)
  1. MSYS2: https://www.msys2.org -> msys2-x86_64-....exe, install into C:\msys64.
  2. Start -> "MSYS2 MINGW64" (exactly MINGW64).
  3. pacman -Syu        (the window may close - open MINGW64 again and: pacman -Su)
  4. pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-gcc-fortran mingw-w64-x86_64-fftw
  5. cd /c/Users/user/Desktop/ExpertSDR_64/FT_src
     ./build.sh
     At the end: "== OK: libftx.dll ...". An error: "*** FAILED" - send build.log.

Checking
  ./ft8sim.exe "CQ N7DDC CN87" 1500 0.0 0.1 1.0 1 -15  (makes 000000_000001.wav)
  ./test_ftx.exe ft8 000000_000001.wav
  ./ft4sim.exe "CQ N7DDC CN87" 1500 0.0 0.1 1.0 1 -12  -> ./test_ftx.exe ft4 000000_000001.wav
  ./wsprgen.exe "N7DDC CN87 37" -26 1500                (makes wspr_test.wav, 2 min)
  ./test_ftx.exe wspr wspr_test.wav 14.0956 3           (frequency, MHz; depth 1..3)

Put the built libftx.dll into win64\ next to libfdv.dll.

Version 1.21: ftx_decode with nzhsym = 0 - the whole period in one call (FT8: the decoder is run
  at 47 and then at 50 half-symbols, as WSJT-X does; a call at 50 alone, at depth 2..3, decoded
  the old buffer). MicronSDR passes 47 / 50 itself and is not affected; libftx.dll needs a rebuild
  (build.sh) only to have the version number 121.

Building (Linux, x86_64)  -> libftx.so
  Needs gcc, g++, gfortran (any recent, e.g. Ubuntu: apt install gcc g++ gfortran).
  FFTW is not needed: c/fftwf_pocketfft.cpp implements the few FFTW calls of WSJT-X with
  pocketfft (c/pocketfft, header only, BSD-3);.
    cd FT_src
    ./build_linux.sh
  At the end: "== OK: libftx.so ... needs glibc X or newer". Output in this folder:
    libftx.so        - exports only ftx_version, ftx_decode, wspr_decode, ftx_encode, wspr_encode;
                       libgfortran / libquadmath / libstdc++ / libgcc linked in statically,
                       so the target needs nothing but glibc
    test_ftx, ft8sim, ft4sim, wsprgen - the same tests as on Windows (test_ftx finds libftx.so
                       next to itself, rpath $ORIGIN)
  FTX_SYSTEM_FFTW=1 ./build_linux.sh - link the system libfftw3f.a (apt install libfftw3-dev)
  instead of pocketfft.
  glibc: c/glibc_compat.c + -Wl,--wrap=fmod keep the library on glibc 2.35 (Ubuntu 22.04,
  Debian 12) even when built on a newer system. For an older system build on that system.
  Checking: as on Windows, without ".exe" (./ft8sim ...; ./test_ftx ft8 000000_000001.wav).
  Results (2026-11-15, gcc 14.2): FT8 -19 dB decoded at every depth, -22 dB at depth 3
  (the slowest window 0.93 s); FT4 -14 dB; WSPR "N7DDC CN87 37" at -26 dB.

License: GPL-3 (COPYING), the authors of WSJT-X: AUTHORS.
