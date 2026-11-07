# libfdv — FreeDV for AetherSDR (ANAN-100D port)

Taken from MicronSDR (`MicronSDR_64/FreeDV_src`, `MicronSDR_64/win64`), the
same library MicronSDR's FDV mode uses.

* `win64/libfdv.dll`, `win64/libcodec2.dll` — prebuilt (llvm-mingw, msvcrt,
  C API only, so they load into the MSVC build). CMake copies them next to
  `AetherSDR.exe`; `src/core/fdv/FdvEngine.cpp` loads `libfdv` at run time.
* Linux (AppImage): `src/build_linux.sh` builds `libfdv.so` + `libcodec2.so.*`
  natively from the same sources and revisions; the AppImage workflow runs it
  and ships `libfdv.so` next to the executable, `libcodec2` in `usr/lib`.
* `fdv_api.h` — the C API (48 kHz float mono in/out, own worker thread).
* `src/` — the wrapper sources and `build_win.sh` with the exact codec2 /
  rade_c / opus / freedv-backend revisions used to build the DLLs.

Modes: RADE V1, 1600, 700D, 700E. Callsign: RADE end-of-over; 1600/700D/700E
text channel (FreeDV-GUI reliable text). Licenses: wrapper BSD-2, codec2
LGPL-2.1 (separate DLL), rade_c BSD-2, Opus BSD-3, rade_text/LDPC BSD-2.
