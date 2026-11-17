# Ater-ugly_fork build 468

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build468-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-468-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-468-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 444: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 444

### Four receivers

- **RX3 and RX4** on the boards with four or more DDCs (ANAN-100D, 200D, 7000DLE / 8000DLE,
  G2 and ANAN-10 / 100), over Protocol 2 and Protocol 1. Add them as panadapters as usual.
- On the PureSignal boards two of these DDCs are PureSignal's feedback pair. They receive
  normally; **while transmitting with PureSignal on** PureSignal takes them back: the panadapter
  and waterfall of the receiver(s) on them stand still and their audio is silent, the other
  receivers carry on. They come back 60 ms after the transmission. Without PureSignal they keep
  receiving during transmit (full duplex), as RX1 / RX2 do.
  - Protocol 2: RX1 / RX2 on DDC2 / DDC3 as before, RX3 / RX4 on DDC0 / DDC1 (both held by
    PureSignal).
  - Protocol 1 (ANAN-100D / 200D / 7000): RX3 on DDC1 (diversity is not used), RX4 on DDC3
    (held by PureSignal). ANAN-10 / 100: RX3 / RX4 on DDC2 / DDC3.
- Receive filters: as before, if any running receiver is on another band, the bank goes wide.
- The program no longer limits receivers or span by a network budget (that was the
  Hermes-Lite 2's 100 Mbit/s link); the board's DDC count is the only limit. Four receivers at a
  wide span load the network accordingly.

### Running next to the original AetherSDR

- The fork calls itself **AetherSDR_ugly_fork** and keeps its own settings and logs in
  `%LOCALAPPDATA%\AetherSDR_ugly_fork` (Linux / macOS: the matching config folder). On the
  first start the settings of the original AetherSDR are copied once, so nothing is lost.
- It does not take over a radio that another program is already running.

### Memories and modes

- **Tuning step per mode group** (SSB, CW, AM / SAM / DRM, FM, DIGI), kept per radio.
- **Drive and TUNE power per mode group** instead of per band.
- **Band buttons, quick memories and station memories** now store and recall the same set of
  receiver settings (a station memory keeps its own frequency and name). The tuning step is not
  part of it: it belongs to the mode group.
- Mode buttons: **DIGI** stands for DIGU / DIGL (click again to switch, like SSB); DSB is no
  longer offered. A new default layout of the mode buttons, with FTX on it.

### Window and panels

- **Minimal mode redone:** the panadapters only, in a small window — the applet panel goes
  away, the VFO flags collapse to their badges, the title bar and status bar stay. A new
  title-bar button (diagonal arrow, before "minimize") switches it on and off, as does
  Ctrl+Shift+M. Everything comes back as it was.
- **No more shaking while resizing the window** (Windows): the contents follow the frame
  (`QT_D3D_NO_FLIP=1`, set by the program unless you set it yourself), and the panadapter and
  waterfall no longer jump vertically during a resize.
- A short click on the title bar no longer leaves the hand cursor stuck.
- Title bar: the right-hand buttons on an even pitch; the "+" (add a radio) is hidden while a
  radio runs.
- VFO flag: the speaker, DSP, mode and X/RIT tabs look like buttons (lighter, underlined on
  hover, tooltips); the FTX entry is easier to find.
- Smaller station name in the status bar; narrower applet panel scroll handle.

### Modules

- At start the program checks that the FTX / JS8 / FreeDV modules and `cty.dat` are next to it
  and says exactly what is missing or refused (e.g. an antivirus quarantine), instead of a bare
  "cannot load" later.

**Untested on a radio:** RX3 / RX4 over Protocol 1; everything on macOS.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are now in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-468`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C.
