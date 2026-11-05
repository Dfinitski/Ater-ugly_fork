FreeDV for MicronSDR (mode FDV): libfdv.dll (+ libcodec2.dll) in win64/, used by FdvUnit.cpp.

  fdv_api.h        the C API: 48 kHz audio in / out, the work in the library's own thread
  fdv_engine.cpp   RX: USB audio -> 8 kHz -> RADE (features -> FARGAN 16 kHz) or codec2 (8 kHz)
                   TX: microphone -> 16 kHz LPCNet features -> RADE, or 8 kHz -> codec2 -> modem
                   RADE end of over (EOO) with the callsign (rade_text, LDPC as FreeDV-GUI)
                   1600 / 700D / 700E: the text channel - sent: the callsign as FreeDV-GUI (its
                   reliable text) or a message up to 60 characters (plain varicode); received:
                   both (fdv_c2hook.c takes the bits of the channel: codec2 internals)
  build_win.sh     how the DLLs are built (llvm-mingw), with the source versions
  test_fdv.cpp     offline test (Linux build): off-air RADE recording, TX->RX loopback per mode

Results (Linux build, 2026-10-13): FDV_offair.wav of rade_c - sync after 0.5 s, 91 % in sync,
SNR 28 dB, callsigns VK5KVA / VK3TPM decoded; loopback RADE / 1600 / 700D / 700E: speech decoded,
callsign N7DDC received in each mode. CPU about 5 % of one core (RADE receive).

1.10 (2026-10-16): fdv_set_text / fdv_read_text - loopback test (Linux): RADE callsign 'N7DDC/QR'
received; 1600 / 700D / 700E: a 60-character message received complete in 22 s (~3 chars/s).
fdv_set_text_mode: loopback - callsign mode: 'N7DDC' received in 1600 / 700D / 700E, no stray
characters; text mode: the message, no callsign.
