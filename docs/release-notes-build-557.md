# Ater-ugly_fork build 557

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build557-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-557-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-557-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 533: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 533

### Filters

- **Linear phase in every mode, receive and transmit.** Receive used minimum phase in SSB, AM,
  SAM, FM and DSB; transmit was linear only in DIGI, FreeDV and FTX.
- **Shorter receive filters, gentler skirts:** 4096 taps in every mode (was 8192), 2048 in CW
  with a passband of 100 Hz or more (was 4096). The skirt is about 47 Hz wide at 4096 and 94 Hz
  at 2048.
- What it costs: the receive filter delays the audio by 43 ms (21 ms in CW), twice that with
  NR, ANF or SNB on. Transmit: 43 ms for each band-pass filter in the chain (one; up to three
  with the compressor and CESSB) — in the signal and in the monitor. The last 43–128 ms of
  speech before PTT is released are not sent.
- **The narrowest notch is 100 Hz** (was 50 Hz): it follows from the filter length.

### AGC

- **DIGU / DIGL no longer switch AGC off.** The DIGI mode group has its own AGC mode, Med by
  default; what is chosen there is remembered for the group, and the other modes get their own
  AGC back on leaving. The receiver taken by the FTX mode keeps AGC off as before.

### Receivers and session

- **Every receiver (A … D) keeps its state:** saved where a receiver is closed — by the
  operator, by a screen layout change, on disconnect and on exit — and applied when it is
  added again. Everything a quick-memory button stores; the sub receiver is not saved.
- **Protection of the RX2 input on transmit** (Radio Setup → ANAN Hardware, third column):
  **Full ATT RX2 when TX** (31 dB on ADC2 while transmitting) and **Ground RX2 When TX**
  (7000DLE / 8000DLE / G2). Both on by default; switching either off asks for confirmation.

### Flag and panadapter

- **SmartMTR numbers are inside the flag,** left of the frequency (S unit over dBm), instead
  of under it; with Extremes shown, the maximum. On a collapsed flag they are under its
  frequency.
- **Dragging the panadapter:** the picture no longer stops and jumps when the receiver has to
  be retuned at the edge of the captured band, and its average is no longer thrown away on a
  zoomed-in drag (worst at 48 kHz). A short audio dip at each such retune remains; moving the
  filter over a still panadapter has none.

### Spots

- **Lifetime of TCI spots:** a list after **Spots** in the TCI applet (No limit, 5 / 15 /
  30 min, 1 / 3 / 12 hours; 30 min by default). TCI spots used to stay until removed.
- **A repeated spot starts its lifetime anew** and replaces the one on screen — TCI, DX
  cluster, RBN, SpotCollector, POTA, FreeDV. The same station sent again by a TCI client no
  longer piles up as several spots.

### MIDI and serial ports

- **A MIDI device or COM port that is not ready at start is tried again:** five attempts, one
  second apart (USB devices that enumerate late).
- **Progressive tuning starts smoothly:** the step grows along a curve from the threshold up
  instead of switching at it.

### Logs and diagnostics

- **The start of every log lists the module files** next to the program (`libftx`, `libjs8`,
  `libfdv`, …): present or missing, whether Windows marks the file as downloaded, and whether
  it loads. For the case "the mode is not in the list although the files are there".
- Windows: the `aethersdr.log` shortcut in the log folder is gone — it was taken for the
  current log. The current log is the newest `aethersdr-YYYYMMDD-HHMMSS.log`.
- The diagnostic line `RX DSP stall` of builds 526–527 is removed.

## Not tested on a radio

Most of the above was written without a radio at hand. Confirmed by the operator: the
filter-drag fix of builds 525 / 527 and the session restore of receiver A as it was in
build 533. Not confirmed yet: the new filter lengths and linear phase (the delays above are
computed, not measured), AGC in DIGI, the state of receivers B … D, the RX2 protection
options, the panadapter drag, spot lifetimes with live sources, the MIDI / COM start-up
retries.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are now in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-557`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C.
