# Ater-ugly_fork build 854

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1, and the Hermes-Lite 2. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build854-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Windows 10 / 11 x64 | `AetherSDR-OpenHPSDR-N7DDC-build854-win64-setup.exe` | installer: Start menu, desktop shortcut, Apps entry; settings kept on reinstall |
| Linux x86_64 | `AetherSDR-ANAN-build-854-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-854-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 807: copying `AetherSDR.exe` is enough. From an older build:
unpack the whole archive.

## New since build 807

### View → Appearance…

A new window that collects the look-and-layout settings.

- **Meters → Use Icom Vintage Meter** (off by default): the S-meter becomes a photographed
  vintage Icom S / RF output meter with a drawn needle, in a **Light** or **Dark** version (the
  light one is the photo recoloured on old paper, with a lit lamp window). On transmit it always
  shows the RF output in % of the rated power; its right-click menu has no TX Select then.
- **Bottom Panel** -- each element of the bottom bar can be switched on or off, and an element
  you switch on joins the end of its group (the order is remembered):
  - left group: **Show Antenna**, **Show TX Button**, **Show Attenuators** (ATT1 / ATT2 in dB, as
    last sent to the radio), **Fast AeDSP Buttons** (AeRX / AeTX open and close AetherRX /
    AetherTX), **Show Dith and Rand buttons** (A1 / A2 = Dith and Rand of ADC1), **Show HAM
    Bands** (small buttons 160 ... 6 m and GEN in two rows, the active band lit), **Show Modes**
    (the active flag's mode buttons, small, two rows of four);
  - centre: **Show Station Name** -- the name sits in the middle of the whole bar; left elements
    that do not fit continue just right of it; the window cannot be made narrower than the bar
    needs;
  - right group: **Show CPU Usage**, **Show UTC Time**.
  - Every group and element is framed by separators; only elements really shown are counted.
- **Panadapter → Jump From Edge 0 % / 25 % / 50 %** with **Jump From The Display** or **Jump
  From The End**: when tuning reaches the edge of the display (or the end of the receiver's
  usable span), the panadapter jumps; 0 % keeps the filter edge where it was, 25 % / 50 % land
  the VFO that far from the edge (50 % = centre). It applies to any kind of tuning.
- **View → Pan Follows VFO** is gone: the Jump From Edge settings above do its job.

### Bottom bar and indicators

- **The TX button is crossed in red while transmitting is not possible** (its tooltip says why).
- CPU / UTC left-aligned with tight widths.

### Transmit

- **Odyssey-3:** the drive's full scale is 130; 255 only inside the 6 m amateur band.
- **Setup → Radio → PA Level Calibration:** new **CW** slider (−30 ... +30 %) -- a drive trim for
  the CW key carrier only (not TUNE), so the key and TUNE give the same power.

### CW keying from the PC (ANAN)

Done the way Thetis does it: with the radio's CW keyer on (mode CW, not TUNE, inside an amateur
band) the program no longer makes the CW carrier itself; it keys the radio's keyer, which shapes
the carrier and applies its own RF delay, hang time and sidetone.

- **Macros / CWX text** go out as the keyer's CWX line; the program's text keyer takes its base
  speed from the **CW applet** (+ / − modifiers step from it), up to 63 WPM.
- **COM port** (a real one, or a virtual one a logging / contest program keys): the **paddles**
  go straight onto the keyer's **Dot / Dash** lines -- speed, iambic A / B, weight and paddle swap
  are the CW applet's; the **key line** is the **Dash** line (the radio's straight key; set the
  CW applet to Straight for a straight key) and the **PTT line (foot switch)** is the **Dot**
  line (the radio's PTT). Outside CW the PTT line still keys MOX.
- MIDI, keyboard and CAT / TCI keying also go through the keyer's CWX line.
- With break-in off in the program and no MOX, a key press does not transmit (as before).
- **Hermes-Lite 2:** unchanged -- it has no keyer of its own, so CW there is still made by the
  program.

### PureSignal

- **The correction is kept per band (and feedback ADC) across restarts** -- no new learning
  after every start.
- With **Full ATT when TX**, TUNE starts at 31 dB attenuation (no ADC overload at the start);
  voice keeps its learnt attenuator. The attenuator follows the drive across pauses.
- **RF Delay is applied to TUNE** too (no short dip at its start).

### CAT, TCI, MIDI

- **TCI:** `rx_smeter` is sent as `trx,channel,level` (5M Contest: "RX_METER:0 is not valid
  integer value" fixed).
- **CAT:** MD7 = CW-R (CWL; plain CW on a radio without CWL); **ZZLA** added; ZZLA / ZZLE in the
  Thetis dialect.
- **MIDI:** **Panadapter Zoom Knob** works as an endless knob in both directions (it went only to
  the extremes).

### Other

- **View → VFO Marker Size** survives a restart (the per-slice widths no longer override it).
- **Multi Meter** values 25 % larger.
- **Popped-out applets** reopen at their saved size after a restart.
- The release check asks GitHub with an ETag (unchanged answers cost nothing), 5 releases.
- About credits the meter photo.

## Not tested on a radio

Seen by the operator on screen while they were made: the Appearance window, the bottom panel
elements, the vintage meter, the TX cross, the Jump From Edge settings. Not confirmed on a radio
yet: the CW keying through the radio's keyer (CWX, Dot / Dash, the RF delay before the first
element, the last element not cut on release), the CW drive trim, the Odyssey-3 drive scale,
PureSignal per band across restarts and TUNE at 31 dB; PureSignal on the Hermes-Lite 2 (since
744, no HL2 available).

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-854`. License: GPL v3.
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
