# Ater-ugly_fork build 494

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build494-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-494-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-494-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 468: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 468

### Sub receivers (SPLIT)

- **SPLIT on the VFO flag works on the ANAN.** It starts a **sub receiver** — a second VFO on the
  same panadapter — for receiver A and for receiver B, both at once if you like: **A′** and
  **B′**. Receivers C and D have none. A second press on SPLIT ends it.
- The sub receiver hears the same band through its own demodulator. Mode, filter and noise
  reduction are its receiver's; its **frequency, volume, balance, AGC threshold (AGC-T, Auto
  and NF+) and S-meter are its own**. With the two balances the pair makes a stereo pair: main
  left, sub right, or as you set them.
- **Its flag is a small one:** the frequency, TX and the VFO name, with its S-meter. A click on
  the name is **SWAP** (the two frequencies change places). The two flags go to opposite sides
  and never overlap; they collapse and expand together.
- **Transmit:** SPLIT moves transmit to the sub receiver; TX on either flag switches it back and
  forth on every click.
- **Tuning the pair:** a click on the panadapter, a double click on the waterfall, the mouse
  wheel and a drag of the passband tune the VFO of the pair that is nearer to the cursor.
- **RX Controls:** the receiver row reads A, A′, B, B′, C, D. With A′ or B′ selected only what
  the sub receiver has of its own can be changed.
- **Memories:** band buttons, quick memories and station memories store whether the sub
  receiver runs, its frequency, volume, balance and AGC settings. A band button brings the
  parameters back but leaves the sub receiver off until SPLIT is pressed.
- **MIDI:** tune knobs for VFO A′, B′, C and D next to the knobs of A and B.
- "Link Slice" offers the receivers only, not the sub receivers.

### Receivers

- **RX3 / RX4 have their own Auto AGC and NF+** (they changed receiver A's before); their noise
  floor is measured as for RX1 / RX2. The AUTO attenuator stays with RX1 / RX2.
- **Auto AGC and NF+ are stored in the memories** (band buttons, quick and station memories).
- **Protocol 1: the number of DDCs is found from the packet rate.** A radio that sends fewer
  DDCs than its board id promises (home-made transceivers answering as HERMES) showed noise
  only; it is read correctly now, receivers beyond its DDCs stay silent.
- **PureSignal feedback input** (ANAN-7000DLE / 8000DLE): the chosen input reaches the receiver
  on every transmission, with PureSignal on or off (BPF1 is bypassed while transmitting).

### Window

- **About** names the fork's authors and reads "for devices supporting the openHPSDR
  protocols".
- **Sliders:** the knob is grabbed within the same distance either side of its centre.

**Untested on a radio:** two sub receivers at once; the sub receiver over Protocol 1; RX3 / RX4
Auto AGC; the PureSignal input change on a 7000DLE; everything on macOS.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are now in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-494`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C.
