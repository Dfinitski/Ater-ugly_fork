# Ater-ugly_fork build 713

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build713-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-713-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-713-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 667: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 667

### Windows 11: the missing pixel column is gone

- **A 1-pixel vertical stripe ran across the whole window** (panadapter, buttons, top panel,
  applet panel) and moved when the window was resized; on the rate button it hid the vertical
  stroke of "+", which then read as "−". Windows 11 and Qt disagreed by 6 pixels on the width of
  the main window's frame, and Windows squeezed the picture into the narrower area, dropping
  columns. The main window now tells Windows the same frame Qt counts with. Resizing still does
  not shake (the presentation mode of build 461 is unchanged), and no light strip shows at the
  top or bottom.

### Power meters

- **Radio Setup → ANAN Hardware → Power Meter Calibration → Max Level:** the top of the power
  scales while transmitting, in watts (rounded to tens). It sets the TX applet's gauge, the
  needle S-meter in the side panel, the ANAN Multi Meter's PO scale and the SmartMTR on the VFO
  flag; the red zone starts at Max Level / 1.2. Default 120 W, 60 W for the Odyssey-3 (the scales
  so far); kept per ANAN model.
- **ANAN Multi Meter (from Thetis)** as the other face of the side panel's S-meter: right click
  the S-meter → Meter. Light or dark face; the transmit needle follows the S-meter's TX Select
  (Power, SWR, Level, Compression), and a click on the box under the scale steps to the next.

### VFO flag

- **While transmitting the flag shows forward power and SWR** in place of the S numbers: watts
  to a tenth below 10 W and whole watts above, `SWR 1.5:1` below. The lines are centred and
  never blink off: they hold the last reading, start at `0.0 W`, and show `SWR __:_` while the
  power is too low to measure SWR.

### Panadapter

- **The zoom / sample-rate group at the top right makes room for the flags:** when a flag comes
  over it, the zoom goes to a line above the rate buttons; a flag that would still cover it turns
  to the other side; only a receiver's filter hides the group. The indicators left of it (WIDE,
  RF gain, K / A / SFI ...) simply hide under a flag and come back after.
- **Center Lock, the knob:** each knob step tunes the receiver straight to the step's frequency;
  only the view glides (the frequency no longer runs through the units of Hz).
- **TX panadapter:**
  - **Show TX Panorama when SSB / CW / FreeDV** (Radio Setup → Transmit → TX Panadapter): off
    freezes the receive panadapter and waterfall while transmitting instead of zooming to the
    transmitted signal.
  - **Stop Waterfall when TX** (duplex): the waterfall stops during transmission, the spectrum
    stays live. On by default.
  - A one-bin carrier (TUNE) is drawn about 3 pixels wide.

### Transmit

- **Fixed: TUNE in CW sent Morse:** the radio's own CW keyer stayed on under the carrier. It is
  switched off for the tune, as in Thetis.
- **Full ATT RX2 when TX** is now offered on every radio with two ADCs (ANAN-100D / 200D too).
- The TUNE button returns to the MON / DUP look after a tune.
- **PROC and the Aether TX compressor are one:** PROC on in the applet switches the Aether
  compressor on with the slider's preset (NOR / DX / DX+); moving the slider then writes its
  preset; switching either one off switches both off; switching the Aether compressor on by
  itself leaves PROC off (its own settings). The radio's TXA compressor follows PROC.
- **The graphic EQ and the Aether EQ are kept in step** (on / off and the band gains), receive
  and transmit.

### Wideband Monitor

- **BPF Bypass** check box at the end of the group: bypasses the band-pass filters of the ADC
  shown, Protocol 1 and 2; cleared when the window closes, not remembered.
- With zoom, the view drags with the right mouse button too.

### Settings

- **Settings → Save Settings... / Restore Settings...** (above Reset Settings): every settings
  store of the program in one `.ats` file (default name `aether_settings_<date>`). A restore is
  applied at the next start.

### Fixed

- **No sound after FTX:** band memory recorded the FTX window's state (DIGU, AGC off), so a band
  button recalled it. Bands whose memory was stored while FTX was open (in practice 20 m / 30 m)
  should be set once more by hand.
- The FTX / JS8 window's caption stays grey (not white) when the window is opened again.

## Not tested on a radio

Most of the above was written without a radio at hand. Confirmed by the operator on screen:
the end of the pixel stripe and its fix without shaking, the flag's power / SWR lines, the
zoom / rate group's steps and the flags turning away, the ANAN Multi Meter faces, the BPF Bypass
check box. Not confirmed yet: Max Level on all four meters, PROC / Aether compressor and the EQ
synchronisation, TUNE in CW without the radio's keyer, Full ATT RX2 on the 100D / 200D, Save /
Restore Settings, the band memory fix after FTX, the TX panadapter options.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-713`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`). Exclusive sound output (Windows): PortAudio, MIT-style licence.
ANAN Multi Meter: needle calibration and geometry from Thetis (MeterManager.cs, Richard Samphire
MW0LGE), GPL v2 or later.
