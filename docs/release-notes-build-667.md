# Ater-ugly_fork build 667

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build667-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-667-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-667-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 636: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 636

### Panadapter

- **ZTB (zoom to band), as in Thetis:** the zoom indicator at the top right ("Zoom ×N.N") is now
  a button.
  - **Left click:** the view stored for this receiver's band. With nothing stored: the whole
    band by the region's band edges (the active band plan), centred on the band. A band wider
    than the receive bandwidth gets the whole bandwidth, centred towards the band as far as the
    receiver stays on the screen.
  - **Right click:** stores the current view (centre and width) for this receiver and band.
  - **Every recall that sets a view stores it too:** a band button, a quick memory button, a
    station memory, and the last session restored when the program starts.
  - Under **Center Lock** only the width changes; the centre stays on the receiver.
- **The filter bounces before it reaches the edge of the screen:** when the filter's outer
  edge comes within 5 % of the half screen from the edge (tuning with the knob, the wheel or
  the keys, and dragging the panadapter), the panadapter moves so that the receiver sits at
  70 % of the half screen on the same side. Zoomed in, only the view moves, the bins already
  drawn stay. The filter never leaves the screen.
- **Center Lock, zoomed in:** dragging the panadapter no longer redraws the whole panadapter
  after about a screen; the bins already drawn move with it.
- **Center Lock and the knob:** the MIDI knob, the wheel over the spectrum and FlexControl drive
  the panadapter as a drag with the mouse does: smooth at any speed, and large steps glide
  instead of jumping. Fixed: the receiver no longer jumps away when the knob stops while the
  mouse is moved over the panadapter.
- **Without Center Lock:** when the panadapter follows the VFO at the edge it glides instead of
  jumping.
- The receiver may go to 90 % of the half bandwidth from the centre of the band the radio
  receives (was 80 %) before that band is moved.
- The Center Lock target mark on the frequency line is gone.
- The sample rate text at the top right has even gaps to its buttons; the − / + rate buttons
  at the end of the range stay buttons (a click no longer goes through to the panadapter).

### VFO flag

- **Fixed:** turning the wheel over a digit of the frequency changed the digit next to it.

### Quick memory buttons

- **Group by freq** (the buttons' menu, next to Group by band): from the lowest frequency to the highest, inside
  the band groups when those are on. Without it the buttons are in the order they were added.

### Transmit

- **Transverters: "Disable PA (transmit through the XVTR port)"** (Radio Setup → XVTR, for
  each transverter), as in Thetis: on that transverter the radio's PA is off and the transmit
  signal goes to the XVTR port.
- **The transmit CFIR filter is on with Protocol 2 only**, as in Thetis (Protocol 1 firmware
  has no CIC to compensate). PureSignal's peak values are unchanged.

## Not tested on a radio

Most of the above was written without a radio at hand. Confirmed by the operator on the radio:
the Center Lock knob (with a simulated knob at 100 steps a second: smooth, and the receiver no
longer jumps when it stops), the digit wheel on the flag, the bounce while tuning, the Center
Lock drag when zoomed in. Not confirmed yet: the bounce while dragging, ZTB's recall paths,
transmit through the XVTR port, the CFIR change on Protocol 1, Group by freq.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-667`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`). Exclusive sound output (Windows): PortAudio, MIT-style licence.
