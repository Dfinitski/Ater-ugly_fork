# libfdv — FreeDV for AetherSDR (ANAN-100D port)

Taken from MicronSDR (`MicronSDR_64/FreeDV_src`, `MicronSDR_64/win64`), the
same library MicronSDR's FDV mode uses.

* `win64/libfdv.dll`, `win64/libcodec2.dll` — prebuilt (llvm-mingw, msvcrt,
  C API only, so they load into the MSVC build). CMake copies them next to
  `AetherSDR.exe`; `src/core/fdv/FdvEngine.cpp` loads `libfdv` at run time.
* `linux64/libfdv.so`, `linux64/libcodec2.so.1.2` — MicronSDR's Linux build
  (FreeDV_src `build_linux.sh`, glibc 2.35+, opus and rade_c linked in; libfdv
  finds codec2 next to itself, RUNPATH `$ORIGIN`). The AppImage workflow ships
  both next to the executable (and codec2 in `usr/lib` too). Notes from its
  builder: `linux64/LINUX_MODULES.txt`. `src/build_linux.sh` is the older
  in-repo recipe, no longer run by CI.
* `fdv_api.h` — the C API (48 kHz float mono in/out, own worker thread).
* `src/` — the wrapper sources and `build_win.sh` with the exact codec2 /
  rade_c / opus / freedv-backend revisions used to build the DLLs.

Modes: RADE V1, 1600, 700D, 700E. Callsign: RADE end-of-over; 1600/700D/700E
text channel (FreeDV-GUI reliable text). Licenses: wrapper BSD-2, codec2
LGPL-2.1 (separate DLL), rade_c BSD-2, Opus BSD-3, rade_text/LDPC BSD-2.
