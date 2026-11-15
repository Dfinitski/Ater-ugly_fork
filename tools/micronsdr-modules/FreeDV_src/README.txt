FreeDV for MicronSDR (mode FDV): libfdv.dll (+ libcodec2.dll) in win64/, used by FdvUnit.cpp.
Russian version: README_ru.txt

  fdv_api.h        the C API: 48 kHz audio in / out, the work in the library's own thread
  fdv_engine.cpp   RX: USB audio -> 8 kHz -> RADE (features -> FARGAN 16 kHz) or codec2 (8 kHz)
                   TX: microphone -> 16 kHz LPCNet features -> RADE, or 8 kHz -> codec2 -> modem
                   RADE end of over (EOO) with the callsign (rade_text, LDPC as FreeDV-GUI)
                   1600 / 700D / 700E: the text channel - sent: the callsign as FreeDV-GUI (its
                   reliable text) or a message up to 60 characters (plain varicode); received:
                   both (fdv_c2hook.c takes the bits of the channel: codec2 internals)
  build_win.sh     how the DLLs are built (llvm-mingw), with the source versions
  build_linux.sh   the Linux build (below)
  test_fdv.cpp     offline test (Linux build): off-air RADE recording, TX->RX loopback per mode

Results (Linux build, 2026-10-13): FDV_offair.wav of rade_c - sync after 0.5 s, 91 % in sync,
SNR 28 dB, callsigns VK5KVA / VK3TPM decoded; loopback RADE / 1600 / 700D / 700E: speech decoded,
callsign N7DDC received in each mode. CPU about 5 % of one core (RADE receive).

1.10 (2026-10-16): fdv_set_text / fdv_read_text - loopback test (Linux): RADE callsign 'N7DDC/QR'
received; 1600 / 700D / 700E: a 60-character message received complete in 22 s (~3 chars/s).
fdv_set_text_mode: loopback - callsign mode: 'N7DDC' received in 1600 / 700D / 700E, no stray
characters; text mode: the message, no callsign.

Linux (x86_64) -> libfdv.so + libcodec2.so.1.2
  build_linux.sh [deps dir, default ./deps]: fetches the same pinned sources as build_win.sh
  (codec2, rade_c, opus + its model data and the rade_c patches), builds opus (DRED / OSCE),
  codec2, then libfdv.so and test_fdv. Needs git, cmake, make, gcc / g++, wget (or put
  opus_data-<hash>.tar.gz into the deps dir; it is 183 MB).
    ./build_linux.sh ~/fdv_deps
  At the end: "== OK: libfdv.so ... needs glibc X or newer".
  libfdv.so exports only the fdv_* functions (version script); opus, rade_c and the C++ runtime
  are inside, libcodec2.so.1.2 (LGPL) stays separate and is found next to libfdv.so (rpath
  $ORIGIN) - copy libcodec2.so* together with it. fdv_glibc_compat.c + --wrap keep it on
  glibc 2.35 (Ubuntu 22.04, Debian 12) when built on a newer system.
  Test: ./test_fdv offair ~/fdv_deps/rade_c ; ./test_fdv loop ~/fdv_deps/rade_c
  Results (2026-11-15): off-air RADE - sync after 0.55 s, 91 %, SNR 28 dB, VK5KVA / VK3TPM;
  loopback RADE / 1600 / 700D / 700E - speech decoded, RADE callsign N7DDC.
