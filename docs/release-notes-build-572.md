# Ater-ugly_fork build 572

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build572-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-572-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-572-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 557: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 557

### Fixed: the program stayed running after it was closed (Windows)

- **With a COM port open in Radio Setup → Serial & Controllers, closing the program could
  leave it running without a window.** The hidden process kept the COM port and the MIDI
  controller, so the next start found both busy ("Win32 error 5", "error creating Windows MM
  MIDI input port") until the USB cable was pulled. It happened on ports whose DSR / CTS / DCD
  lines never change — the serial port of an Arduino, an adapter with nothing connected.
  Closing the port no longer waits for a line to change.
- If this happened to you: end the leftover `AetherSDR.exe` in Task Manager (Details) once, or
  replug the cable. A COM port that belongs to an Arduino MIDI panel is not needed in
  Serial & Controllers at all — the panel works over MIDI.
- The start of every log now says whether another copy of the program is already running.

### Noise reduction on the radio's own speaker

- **The radio's speaker and headphones now play the audio after the client receive chain** —
  NR2, NR4, DFNR, RN2 and the rest of AetherRX — the same as the computer's output. They used
  to get the receivers' audio before it.
- What it costs: about 100 ms more delay on the radio's speaker. With the computer's audio
  output disabled the radio's speaker falls back to the audio without noise reduction.
  `AnanCodecAetherRx=False` in the settings returns the previous behaviour.

### RX applet

- **NB and noise reduction buttons** at the right end of the receiver button row. Left click:
  on / off. Right click: which blanker (NB, NB2) or which noise reduction method. The choice
  and the on / off state are kept for each receiver A … D; a sub receiver uses its main
  receiver's. Noise reduction works on the sound of all receivers together and follows the
  selected receiver.
- The filter width is shown in the left group of the second row, after the TX antenna.

### Panadapter

- **A panadapter pulled out of the main window is an independent window:** its own taskbar
  button, minimised on its own, stays on screen when the main window is minimised. Closing it
  returns the panadapter to the main window, as the return button does.
- **Always on top** in the right-click menu of a pulled-out panadapter (off by default).
- **The zoom indicator and the bandwidth buttons** (top right) hide while a receiver's flag
  covers them and come back when it does not.

### S-meter

- **The S-meter detector is Average by default** (Radio Setup); the SmartMTR numbers on the
  flag take the same values as the main S-meter, so the setting acts on both.

### Protection on transmit

- **Full ATT RX1 when TX** (Radio Setup → ANAN Hardware), next to the RX2 option of build 557:
  31 dB on ADC1 while transmitting.

### Status bar

- TX indicator after the antennas; **UTC Time** at the right end. The separate date / time
  block is gone.

### MIDI library

- **The bundled RtMidi is updated to 6.1.0.** A MIDI port that fails to open now reports the
  Windows error code in the log. The update touches the MIDI code of all three systems and
  was not tested on any of them: **if a MIDI controller that worked in build 557 misbehaves
  in this build, please report it.**

## Not tested on a radio

Most of the above was written without a radio at hand. Confirmed by the operator on screen:
the place of the NB / noise reduction buttons, the UTC Time block, hiding of the zoom
indicator and bandwidth buttons under a flag. Not confirmed yet: the fix for the program
staying after exit (found from a user's logs, not reproduced), noise reduction on the radio's
speaker, the independent panadapter window and Always on top, Full ATT RX1 when TX, the
S-meter default, MIDI with the new library — on Linux and macOS the new library has not even
been compiled before this release's builds.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-572`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`).
