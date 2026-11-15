libjs8.dll - the JS8 decoder of JS8Call for MicronSDR (the JS8 window: the FT button, its arrow: JS8)

The decoder is the C++ one of JS8Call 2.3+ (JS8.cpp by Allan Bazinet W6BAZ,
https://github.com/js8call/js8call, GPL v3), without its Qt parts, with a plain C interface
(js8_api.h). It needs C++20, boost, Eigen and FFTW: so it is built as a DLL
with MSYS2, like libftx.dll (FT_src), and MicronSDR loads it at run time.

Files
  JS8core.cpp    the decoder (JS8.cpp of JS8Call: the Worker / Decoder classes left out) and
                 the C interface at its end
  js8core.hpp    JS8.hpp without Qt (Costas arrays, the decode events)
  commons.h      constants of JS8Call (the submodes)
  js8_api.h      the interface: js8_decode(), js8_encode() (for the test), js8_period_samples()
  test/test_js8.cpp   the test: frames encoded, modulated, noise added, decoded (all submodes)
  build.sh       the build

Build (the same MSYS2 as for FT_src)
  1. Start "MSYS2 MINGW64" (Start menu).
  2. Once: the compiler, FFTW, boost and Eigen (boost, Eigen: headers only are used):
       pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-fftw mingw-w64-x86_64-boost mingw-w64-x86_64-eigen3
  3. Go to this folder and build:
       cd /c/Users/user/Desktop/ExpertSDR_64/JS8_src
       ./build.sh
     About 1 minute. At the end: "== OK: libjs8.dll ..." and the test: for every submode
     3 frames decoded at 0 and -12 dB ("3 decodes").
  4. Copy libjs8.dll into win64\ (next to libftx.dll), rebuild MicronSDR: it is copied next to
     MicronSDR.exe with the other DLLs of win64\.
If something fails: send build.log.

A WAV file (12 kHz, 16 bit, mono, one period from its start) can be decoded too:
       ./test_js8.exe file.wav 0        (0 Normal, 1 Fast, 2 Turbo, 4 Slow)
