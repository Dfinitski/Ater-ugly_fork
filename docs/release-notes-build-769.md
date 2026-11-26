# Ater-ugly_fork build 769

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1, and the Hermes-Lite 2. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build769-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Windows 10 / 11 x64 | `AetherSDR-OpenHPSDR-N7DDC-build769-win64-setup.exe` | installer: Start menu, desktop shortcut, Apps entry; settings kept on reinstall |
| Linux x86_64 | `AetherSDR-ANAN-build-769-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-769-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 744: copying `AetherSDR.exe` is enough. From an older build:
unpack the whole archive.

## New since build 744

### Panadapter

- **The panadapter and waterfall no longer stay empty when the network loses packets.** With a
  few per cent of the radio's packets lost, every gap threw the FFT window away and no frame was
  ever completed -- the screen stayed blank while audio and the S-meter worked. Now a lost packet
  only smears the frame slightly. (The loss itself is outside the program: cable, switch, network
  card power saving -- it also causes clicks in the audio.)
- The log gets a line a minute per receiver, `ANAN spectrum diag`: frames computed, frames that
  reached the panadapter, windows discarded and gaps let through.
- The waterfall's **LIVE** chip is dark teal instead of bright red.

### Multi Meter

- **Hide duplicate gauges** (right click the Multi Meter, on by default): while the meter is on
  screen the applets drop the gauges it repeats -- the Mic applet's Compression, ALC Gain and ALC,
  the TX applet's RF Pwr and SWR -- and shrink to suit. Untick it to keep them.
- The light (paper) face has rounded corners, like a cross-needle meter.
- The dark face follows the theme's background, also after a theme change.

### Themes

- **Two new dark blue themes: Midnight** (after Tokyo Night) **and Deep Ocean** (after Solarized
  Dark). The white Default Light theme is gone from the list.
- In Midnight and Deep Ocean the colours written in the program's code are recoloured to the
  theme too, not only those that come from theme tokens.
- Applet gauges (Level, Compression, ALC, RF Pwr, SWR ...) and the RX applet's filter control are
  drawn on the theme's background; slider tracks and gauge frames are 15 % brighter.
- **Theme editor:** a colour token can now hold an **Image** -- a picture drawn stretched wherever
  the token is a background (gradients were already possible: Type → Gradient).
- **Theme editor, Inspect:** an element whose colours are set in the program's code now says so,
  instead of offering its parent panel's tokens (which changed other elements, not the one
  picked).

### Applets and side panel

- **A popped-out applet:** its button in the panel only hides and shows its window; the × in its
  title bar puts it back into the panel (shown). Nothing of this is remembered: closing the
  program with a hidden popped-out applet brings it back in the panel, switched off.
- **Quick memory buttons** are muted: the band plan colour mixed into the panel, a soft gradient
  and a bevel instead of the bright frame.

### Status bar

- **The TX indicator works as MOX:** a click while receiving keys MOX (the TX applet's MOX, with
  its checks); a click while transmitting unkeys, as before.
- The thin cyan line under the status bar indicators (keyboard focus) shows only when they are
  reached with Tab, not after a mouse click.

## Not tested on a radio

Seen by the operator on screen while they were made: the themes, the Multi Meter changes, the
applet gauges, the quick memory buttons. Not confirmed on a radio yet: the panadapter with packet loss
(reported by a user with an ANAN-100 losing ~7 % of packets), the TX indicator as MOX, the image
type of the theme editor; PureSignal on the Hermes-Lite 2 (since 744, no HL2 available).

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-769`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`). Exclusive sound output (Windows): PortAudio, MIT-style licence.
