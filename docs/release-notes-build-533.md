# Ater-ugly_fork build 533

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build533-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-533-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-533-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 494: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 494

### Sub receivers (A′, B′)

- **Independent volume and balance.** The sub receiver is now its own audio stream: its volume
  and balance no longer depend on the main receiver's (turning the main receiver down used to
  silence both). Mute is still the pair's.
- **The active VFO is the one controlled.** Click, wheel and passband drag tune the active VFO
  of the pair, not the one nearer to the cursor.
- **SPLIT on the flag is an indicator:** lit while transmit is on any receiver other than A; a
  click on the lit text returns transmit to A. Receiver A is the base receiver and cannot be
  closed.
- **SubRX** (panadapter side menu, was "+RX") switches the sub receiver of that panadapter's
  receiver on and off. **+RX** in RX Controls adds the next independent receiver.
- A sub receiver takes its filter from its main receiver: the filter buttons, the passband
  picture in RX Controls and the passband edges on the panadapter are locked for it. Its NF+
  is its own and is shown as active.
- Noise and AGC lines on the panadapters of RX3 / RX4.

### Filter, audio and panadapter

- **Dragging a filter edge no longer stalls the audio and the panadapter** when a sub receiver
  and Auto AGC are on (two causes found from operator logs and fixed in builds 525 and 527).
- **Phantom spike at the panadapter centre** next to a very strong carrier is gone (it was an
  artefact of the display only, not of the receiver).
- The cropped edge of the panadapter is 5 % a side instead of 10 %.
- **Codec Level Compensation** (Radio Setup → ANAN Hardware → Codec): −40…0 dB for everything
  sent to the radio's codec.

### Transmit options (Radio Setup → ANAN Hardware)

- **Bypass BPF1 When TX**, **Bypass BPF2 When TX**, **PS Coupler Always when TX** — off by
  default; with PureSignal on they apply as before. For the 7000DLE / 8000DLE / G2.
- **Mic +20 dB** is a device setting now (on by default) instead of the 20dB button of the Mic
  applet, and is no longer stored in profiles.
- "Microphone & Connectors" is laid out in two columns; "PA Power Law" is "Power Slider Law",
  one setting per row. The window widens itself so that no page needs horizontal scrolling.

### Mic applet

- **Separate mic level for the computer microphone (PC) and the codec microphone (CDC)**, kept
  apart in the mic profiles as well.
- Mic and monitor levels are shown in percent; the two sliders end at the same place.

### Memories and session

- **The last session is restored:** on exit every receiver's state is saved — everything a
  quick-memory button stores, the panadapter zoom included — and put back after the radio
  starts.
- Quick memories and station memories store and recall the **tuning step**.
- **Station memories can be removed** by a right click in the memory list of the panadapter
  side menu.

### TCI, spots

- **TCI I/Q up to 192 kHz** for skimmers (the advertised I/Q limits were ±48 kHz).
- TCI applet: the server button reads **Enable** and lights when running; a **Spots** checkbox
  shows or removes the spots sent by TCI clients.
- Spots of a spot source are removed from the panadapter when that source is switched off by
  hand (FreeDV Reporter included).

### MIDI

- **Progressive tuning for VFO knobs**, per knob: column **Progressive** in MIDI Controller
  Mapping, with the turning rate it starts from (**From, pulses/s**; about one turn a second:
  30 for a 24-detent knob, 100 or 400 for 100- / 400-pulse encoders). Slow turning stays one
  step per pulse; faster turning multiplies the step, up to ×20.
- Opening the MIDI window no longer freezes the program while Windows looks for MIDI inputs.
- The manual binding editor matches the dark theme and is dragged by any free place; the row
  buttons are **Edit** and **Del**.

### Window

- **The settings menu tree stays open** after a choice (Windows): it closes on a click outside
  it (that click does nothing else), on a second click on the menu button, or when a window is
  dragged.
- A click on the station name in the bottom bar opens the device settings (with a radio
  connected).
- Every log starts with the fork build number.

## Not tested on a radio

Most of the above was written without a radio at hand and checked by the operator only in
part. Not confirmed yet: the filter-drag fix of build 527, the last-session restore, the
progressive MIDI tuning thresholds, the TX bypass options on Protocol 1, the phantom-spike fix.
Builds 526–527 leave a diagnostic line `RX DSP stall` in the log when the receive DSP thread is
late with a block.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are now in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-533`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C.
