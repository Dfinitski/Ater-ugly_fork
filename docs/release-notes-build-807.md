# Ater-ugly_fork build 807

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1, and the Hermes-Lite 2. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build807-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Windows 10 / 11 x64 | `AetherSDR-OpenHPSDR-N7DDC-build807-win64-setup.exe` | installer: Start menu, desktop shortcut, Apps entry; settings kept on reinstall |
| Linux x86_64 | `AetherSDR-ANAN-build-807-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-807-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 769: copying `AetherSDR.exe` is enough. From an older build:
unpack the whole archive.

## New since build 769

### RIT / XIT

- **RIT and XIT are kept per receiver, band and mode group** (SSB, CW, AM, FM, DIGI): RIT on in
  CW on 40 m does not switch it on in SSB on 40 m; each pair has its own setting, restored when
  you come back to that band and mode.
- **Quick memories and station memories store RIT / XIT** (on / off and offset); a band button
  brings back the band and mode's own RIT / XIT, as above.
- **The offsets can be typed:** double-click the RIT or XIT value on the X/RIT tab (or press
  Enter on it), type the offset in Hz (−9999 … +9999), Enter. The ON / OFF state is left as it
  is. Wheel and arrows still step 10 Hz.
- The flag's **X/RIT tab is lit amber while RIT or XIT is switched on**.
- **MIDI:** new **RIT Knob**, **RIT = 0**, **XIT Knob** and **XIT = 0** (RX group). The knobs
  move the active receiver's offset 10 Hz per detent and work as endless encoders; the
  zero buttons clear the offset. RIT / XIT on / off are not changed by them.
- **The dashed XIT line ("X") is drawn only on the transmitting receiver**, where the carrier
  really goes. Transmitting on a sub receiver (A′ / B′), the line, the TX waterfall and the TX zoom
  sit at the sub receiver's frequency + the main receiver's XIT; the main receiver shows no XIT
  line meanwhile. The RIT line ("R") is unchanged.

### PureSignal

- The **CORR** lamp in the PureSignal applet lights while a correction is in effect, not only
  while transmitting (FEED and FB stay transmit-only).
- Amp View's status explains the check code briefly: **0x1 – Low FB Level**, **0x2 – No max Power
  samples** (unknown bits are still shown in hex).

### Band plan

- **View → Band Plan → Colors…** sets your own colour for each colour group of the active band
  plan (the window lists the segments each colour covers) and the colour of a frequency outside
  the plan. The colours apply at once to the band plan strip on the panadapter and to the quick
  memory buttons, and are kept per band plan. Reset / Reset All bring back the plan's own.

### Themes

- **Buttons follow the theme.** About 170 button styles of the applets, the VFO flags, the
  panadapter menus and panels and the dialogs now take their colours from the theme
  (`color.button.*` tokens), so Midnight and Deep Ocean recolour them too; in Default Dark
  they look exactly as before.
- **Window headers in the theme's colours (Windows 11):** windows with the standard Windows
  frame (message and input boxes, the colour picker, popped-out windows ...) get the program's
  title bar colours as the active theme shows them, also after a theme change.
- The panadapter **Display** panel: the field names are as bright as the values.

### Theme editor (View → Theme Editor…)

- **Theme actions → New theme from colors…:** pick a background, panels (or Auto), text and an
  accent -- or start from a preset (Navy night, Teal ocean, Graphite, Warm paper, Cool slate) --
  and the whole theme is built from Default Dark with them: every grey-blue and cyan-blue colour
  (tokens, palette and the colours written in the code) is placed on your scale by lightness;
  status and band colours stay. A light background with dark text makes a light theme. A live
  preview (its buttons can be clicked) shows the result before **Create**.
- **Palette tab** (next to Tokens): the theme's base colours with how many tokens use each --
  change one and every token that refers to it follows -- and **Colours written in the code**:
  the 12 + 3 colours the theme maps the program's built-in colours onto, each editable, with
  **Regenerate from theme colours** and **Turn off**.
- **Right click a colour token:** **Replace #xxxxxx everywhere…** (every token, gradient, font
  colour and palette entry with that colour), **Show tokens with this colour / with similar
  colours**, **Link to palette color** (the token then follows that palette colour), **Show all
  tokens**.
- **Theme actions → Adjust colors…:** hue, saturation and lightness sliders for the whole theme or
  only the tokens shown in the list, with a live preview; status, band and special colours kept
  unless you untick it; OK keeps, Cancel restores.
- **Only changed vs [theme]** on the Tokens tab: lists only the tokens that differ from the chosen
  theme, with a count.
- **Undo / Redo** (buttons, Ctrl+Z / Ctrl+Y) for every theme edit; a colour-picker drag is one step.
- **Theme actions → Check contrast…:** text / background pairs of the theme with their WCAG
  contrast ratio (4.5 : 1 for main text, 3 : 1 for secondary), worst first; double-click a pair to
  show its two tokens in the list.
- The new windows (wizard, Adjust Colors, Contrast Check) have the program's own title bar in
  frameless mode, like the editor.
- **Fixed:** Save As / Export of Midnight, Deep Ocean (and the light themes) lost the remap of the
  colours written in the code, so the copy looked worse than the original. It is now saved with
  the theme.

### Status bar

- No text is written next to the TX indicator any more.

## Not tested on a radio

Seen by the operator on screen while they were made: the RIT / XIT tab light and typed offsets,
the band plan colours, the themed buttons, window headers, the theme editor's new tools. Not
confirmed on a radio yet: RIT / XIT per band and mode, the XIT line and TX on a sub receiver, the
MIDI RIT / XIT controls; PureSignal on the Hermes-Lite 2 (since 744, no HL2 available).

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-807`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`). Exclusive sound output (Windows): PortAudio, MIT-style licence.
