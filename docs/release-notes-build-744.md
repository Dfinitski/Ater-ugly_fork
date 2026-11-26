# Ater-ugly_fork build 744

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1, and the Hermes-Lite 2. Windows, Linux and macOS (Apple silicon).

This build replaces builds 713 and 732: their "ANAN Multi Meter" followed the face design of
another program's meter, whose author asked for it to stay exclusive to that program. It is gone;
the fork has its own multi meter now (below).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build744-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Windows 10 / 11 x64 | `AetherSDR-OpenHPSDR-N7DDC-build744-win64-setup.exe` | installer: Start menu, desktop shortcut, Apps entry; settings kept on reinstall |
| Linux x86_64 | `AetherSDR-ANAN-build-744-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-744-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 713 or 732: copying `AetherSDR.exe` is enough. From an older build:
unpack the whole archive.

## New since build 732

### Multi Meter (the fork's own)

- **Right click the side panel's S-meter → Meter → Multi Meter.** A linear meter with all
  readings at once: **S** (with the reading in dBm), **PWR**, **SWR**, **ALC** and **COMP**, each a
  horizontal scale with a pointer, a bar and the value at the right (the number over its unit).
- **Light** face printed on paper, or **Dark** on the applets' own background (same menu).
- **Shadow TX scales** (same menu, on by default): the transmit scales are dimmed on receive,
  where they rest at their minimum (0.0 W, SWR `_._`, −20 dBFS, 0 dB).
- SWR reads `_._` while the radio cannot measure it, as on the VFO flag.
- ALC and COMP follow the mic applet's gauges: ALC −20 … 0 dBFS, COMP the compression in dB.
- Popped out of the panel it grows with its window up to 640 × 480.

### Transmit

- **The PROC button of the mic applet is now called COMP** (it is the speech compressor; nothing
  else changed).
- **The compression gauge** of the mic applet reads 0 at the left and −30 dB at the right, its bar
  growing to the right as the compression grows; the Multi Meter's COMP scale is the same.

### Hermes-Lite 2

- **PureSignal on the HL2**, after deskHPSDR: feedback on the third receiver, the TX reference on
  the fourth; the PS attenuator (manual or Auto attenuate) works through the HL2's LNA while
  transmitting; the TX buffer latency is at least 40 ms while PureSignal is on. The feedback input
  and ADC choices of the PS applet do not apply to the HL2 and are greyed out.

### Side panel

- **Applet panel docked left:** the scroll bar now runs from the top of the S-meter down beside
  the applets, so the S-meter and the applets line up on either side.

## Not tested on a radio

Confirmed by the operator on screen: the Multi Meter (both faces, all scales, scaling when
popped out), the applet panel's scroll bar, the COMP button. Not confirmed yet: PureSignal on the
Hermes-Lite 2 (no HL2 was available — please report what you see: does the Feedback lamp of the
PS applet reach green, 129..181, and does Auto attenuate hold it), and everything Hermes-Lite 2
from build 732.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-744`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`). Exclusive sound output (Windows): PortAudio, MIT-style licence.
