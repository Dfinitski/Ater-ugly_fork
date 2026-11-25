# Ater-ugly_fork build 732

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1, and now the Hermes-Lite 2 again. Windows, Linux and macOS
(Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build732-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Windows 10 / 11 x64 | `AetherSDR-OpenHPSDR-N7DDC-build732-win64-setup.exe` | installer: Start menu, desktop shortcut, Apps entry; settings kept on reinstall |
| Linux x86_64 | `AetherSDR-ANAN-build-732-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-732-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 713: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 713

### Hermes-Lite 2

- **The Hermes-Lite 2 is supported again** (Protocol 1). It is recognised from the discovery
  answer (board 6, gateware 40 or newer; the Hermes-Lite 1 is not supported) and gets its own
  model, "Hermes-Lite 2" (5 W). Built after Thetis (MI0BOT), deskHPSDR and Quisk; every byte of
  the command rotation was checked against all three.
- **Receive:** up to 4 receivers. The attenuator engine (manual, AUTO, band memory) drives the
  HL2's LNA instead: the ANT menu shows **LNA:** in dB of gain (−12 ... +48 dB).
- **Transmit:** drive as in deskHPSDR (hardware step plus I/Q scaling), its own PA calibration
  (Radio Setup → ANAN Hardware, 100 % = 5 W), forward / reflected power and SWR, PA temperature
  and current.
- **Radio Setup → Hermes Lite settings** (shown for the HL2 only), after Quisk: power amplifier,
  T/R relay in low power, TX buffer latency, PTT hang time, LNA during TX, PWM output, the
  AK4951 codec board (radio microphone and speaker), N2ADR filter / IO board and its receive
  input, CL1 reference clock, AH-4 tuner, watchdog, reset on disconnect. All options are assumed
  fitted by default.
- Not on the HL2 yet: PureSignal, PA bias and IP / MAC settings that write the board's EEPROM.

### Transmit audio

- **The applet's graphic EQ is now its own WDSP equalizer** (bands on the sliders' frequencies),
  separate from the Aether EQ: switching one on switches the other off, receive and transmit.
- **PROC is now its own microphone compressor** with the NOR / DX / DX+ presets, separate from
  the Aether TX compressor: switching one on switches the other off. Both are kept by the Mic
  profile buttons and TX Profiles.

### Panadapter and VFO flag

- **Duplex (DUP), transmit scale:** while transmitting, the panadapter scale is +10 dBm at the
  top by default, and the top and bottom you set during transmission are remembered per mode.
- **VFO flag on transmit:** `SWR 1.5` without ":1", at most 9.9; the power / SWR lines sit three
  spaces from the frequency field.

### Bottom bar

- **Receive on the transmit antenna in one click** (Thetis' "Rx Ant"): a small right-angle arrow
  next to the RX / TX antenna indicator. Dim when the two antennas are the same, normal when it
  can be clicked, bright while the receivers listen on the transmit antenna. Click again to go
  back; the chosen receive antenna is not changed.

### Settings

- **Settings → Autostart Radio with AetherSDR:** starts the last used radio as soon as it is
  found at program start. Off by default.

### Windows installer

- An Inno Setup script for **AetherSDR for OpenHPSDR by N7DDC** (`packaging/windows`): licence and
  user agreement, folder choice, registration in Apps, desktop shortcut; installing again
  replaces the program but keeps the settings. Build it with Inno Setup 7 or 6 and
  `build-installer-anan.cmd`.

### Links

- Documentation, issue reports and update checks in the program lead to this fork's GitHub
  repository only.

## Not tested on a radio

Most of the above was written without a radio at hand. Confirmed by the operator on screen:
the flag's SWR / power lines, the receive-on-transmit-antenna arrow, the Windows installer
(install, run, install again over it). Not confirmed yet: all of the Hermes-Lite 2 support (no
HL2 was available — please report what you see), the WDSP graphic EQ, the PROC compressor, the
DUP transmit scale, Autostart Radio.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-732`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`). Exclusive sound output (Windows): PortAudio, MIT-style licence.
ANAN Multi Meter: needle calibration and geometry from Thetis (MeterManager.cs, Richard Samphire
MW0LGE), GPL v2 or later.
