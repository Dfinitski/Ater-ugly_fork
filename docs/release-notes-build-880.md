# Ater-ugly_fork build 880

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1, and the Hermes-Lite 2. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build880-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Windows 10 / 11 x64 | `AetherSDR-OpenHPSDR-N7DDC-build880-win64-setup.exe` | installer: Start menu, desktop shortcut, Apps entry; settings kept on reinstall |
| Linux x86_64 | `AetherSDR-ANAN-build-880-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-880-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 854: copying `AetherSDR.exe` is enough. From an older build:
unpack the whole archive.

## New since build 854

### Applets

- **BAND applet** (the button where GHE was; Green Heron Everyware is gone): the band buttons
  of the panadapter's Band panel in their groups (160 ... 6 m; WWV, GEN; 2200, 630, XVTR). They
  reflow with the applet's width and keep their groups and order. The button of the active
  receiver's band is lit with a thin frame.
- **View → Appearance → Extra Applets:**
  - **Detach Flag to Applet:** the active receiver's flag leaves the panadapter and lives in an
    applet of its own (in the panel or in its own window, which always fits the flag exactly).
    Closing the applet puts the flag back on the panadapter. Not for the sub-receivers. The DAX
    button is not shown there (no DAX hardware).
  - **Extra Band Applet** (2 × 6: 160 ... 6 m and GEN) and **Extra Mode Applet** (2 × 4, the
    active flag's mode buttons): the bottom bar's buttons in an applet. Popped out, the window
    scales with its buttons and their lettering.

### Panadapter and flag

- **View → Appearance → Panadapter → Zoom on Bottom Left / Zoom on Bottom Right:** the zoom row
  (Zoom ×N, −, sample rate, +) and the indicator strip (WIDE, preamp, RF gain, WNB, K / A / SFI)
  move to a bottom corner, on a semi-transparent backdrop. Both off: top right as before.
- **Frequency digits:** the digit the mouse wheel will change gets a light-blue backdrop under
  the cursor, on the flag and in the RX applet.
- **ADC2 receivers** show the antennas ANT1 ... ANT3 on the flag instead of "RX2" (which could
  also be picked from the antenna list).

### Meters

- **TX power meters** are reset after every transmission and start the next one from zero.
- **Multi Meter:** the PWR scale is named **PO**, centred over SWR.
- **Cross-needle meter:** the RANGE heading is always lit. With the **Uplight** face: the face
  25 % lighter, the lamp light 50 % dimmer.

### Transmit (Odyssey-3)

- **The drive's full scale is 70 on 160, 80, 60, 40, 30 and 20 m**, 130 on the other bands and
  255 inside the 6 m amateur band. The PA Level Calibration is in % of that full scale, so
  **the same calibration gives less power on 160 ... 20 m than in build 854: calibrate these
  bands again.**
- **PA Level Calibration defaults** per band from the operator's radio (160 m 39 %, 80 m 34 %,
  60 m 30 %, 40 m 36 %, 30 m 40 %, 20 m 42 %, 17 m 54 %, 15 m 72 %, 12 m 76 %, 10 m 95 %,
  6 m 99 %) and the **CW key trim default +30 %**. They apply where nothing is saved yet. On
  160 ... 20 m these defaults were set with the full scale of 130.

### MIDI

- New parameters **APF (CW Peak Filter)** (CW / CWL) and **BIN (Binaural Audio)** (receivers 1
  and 2).

## Not tested on a radio

Seen by the operator on screen while they were made: the BAND applet, the detached flag, Zoom on
Bottom Left / Right, the TX power meter at the start of a transmission. Not confirmed yet: the
Extra Band / Mode applets, the frequency digit highlight, the multi meter's PO, the cross-needle
Uplight face, the Odyssey-3 full scale of 70 on 160 ... 20 m, MIDI APF / BIN, the ADC2 antenna
list; PureSignal on the Hermes-Lite 2 (since 744, no HL2 available).

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-880`. License: GPL v3.
Vintage meter photo: "S-Meter (ICOM IC-732 HF Transceiver)" by Chaz360, Wikimedia Commons
(https://commons.wikimedia.org/wiki/File:ICOM_IC-732_S-Meter.jpg), licensed CC BY-SA 4.0
(https://creativecommons.org/licenses/by-sa/4.0/); modified: the needle retouched out (the
program draws it), the light version recoloured; the modified pictures are under the same
licence (`resources/meterfaces/README.md`). ICOM is a trademark of Icom Inc.; the program is not
affiliated with or endorsed by Icom Inc. or the photographer.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`). Exclusive sound output (Windows): PortAudio, MIT-style licence.
