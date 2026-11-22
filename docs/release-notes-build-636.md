# Ater-ugly_fork build 636

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. Windows, Linux and macOS (Apple silicon).

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build636-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-636-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-636-macOS-arm64.dmg` | not notarized: right click → **Open** the first time |

Windows, upgrading from build 572: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive.

## New since build 572

### Receive audio after transmit comes back much sooner

- **The pause after an over is shorter, on the computer and on the radio's own speaker.** The
  end of an over is handled the way Thetis does it: the receivers blank the over's I/Q tail,
  flush their filters and keep their output muted only until the over has left it, and the
  guard ends when the I/Q is clean again rather than after a fixed time.
- **Receive filters** are 2048 taps in every mode (21 ms), 4096 with a notch on. Still linear
  phase.
- Smaller buffers towards the computer's sound card (30 ms) and the radio's speaker (15 ms).
  Several conversions inside the program that added 70 ms each were replaced.
- **Odyssey:** the radio's speaker buffer is held half full (the Odyssey firmware reports its
  level), so the radio's speaker no longer lags the computer by ~40 ms. Other radios do not
  report the level; for them nothing changes.
- **Exclusive mode for the computer's sound output** (Windows): Radio Setup → Audio →
  **Exclusive**. The program then has the sound card to itself (other programs cannot play on
  it); 48 kHz is tried first, then 44.1 kHz. The CW sidetone and Quindar tones are mixed into
  the same output.

### Noise reduction

- **NR2 is WDSP EMNR inside each receiver, as in Thetis** — its sliders in AetherRX now act on
  it, and the **NR GAIN** strip shows it.
- **NR2, NNR and DFNR are each receiver's own** (right click the NR button of the RX applet to
  choose); NR4, RN2, BNR and MNR work on the sound of all receivers together.
- **NNR and DFNR on a receiver delay the sound by ~50 ms** instead of ~190 ms.
- The receivers' noise reduction is frozen while transmitting: no burst of noise at the start
  of receive.
- **NR4 fixed:** the Suppression slider did nothing, and with Adaptive Noise Estimation off NR4
  removed nothing. At the default Suppression NR4 now acts noticeably stronger than before.

### Transmit

- **Guard gap before the relays at the end of an over (SSB, AM, FM, digital, TUNE):** the RF
  falls to zero in 2 ms and the radio is switched to receive **RF Delay** ms later (TX applet,
  25 ms by default). Also when the over is keyed by the radio's own PTT. CW unchanged.
- **Radio Setup → Transmit → TX Panadapter:** whether the panadapter zooms on transmit,
  separately for CW, SSB (and the other modes) and FreeDV.

### Frequency calibration

- **Radio Setup → Calibration:** pick a reference (WWV / WWVH, CHU, RWM or your own frequency)
  and press **Calibrate**. The program tunes there, measures the carrier, applies the
  correction, measures again to check it, and tunes back. The error is shown with its sign.
  Manual entry and Trim are still there.
- **The correction range is ±200 ppm** (was ±50), for radios without a stabilised reference.

### Panadapter

- **Center Lock:** fast tuning in small steps (MIDI) no longer makes the panadapter jerk, and
  larger steps glide smoothly at the pace of the tuning.
- **Fixed: a short dropout of the sound after every retune** that moved the receiver.
- The collapsed menu button at the top left is just the arrow.

### Windows

- No more flashing window or white frame when AetherRX / AetherTX opens, and the top of the
  main window no longer flickers when the focus changes.

### Meters

- **Odyssey-3** model (Setup → Radio, chosen by hand — on the wire it is an ANAN-100D): 50 W
  power scales (60 W full scale) on the cross-needle meter, SmartMTR and TX Controls; the name is
  shown everywhere.
- **Cross-needle meter:** 10 W face with ranges ×1 / ×10 / ×100 / ×1000, chosen automatically
  with hysteresis; the active range stands out in the legend.
- **The VFO flag always shows SmartMTR;** a click opens its settings. Its marks are on top only,
  the receive bar has the S-meter colours, the drawing is centred.
- **S-units** are the unit reached (S1 + 4 dB reads S1) on every meter.

### Applets and windows

- **Torn-off applets** open at their whole content size (at least their size in the panel) and
  can be resized in height again.
- **PureSignal window:** the manual amplifier delay and Previous delay are gone; the delay comes
  from Measure delay only.

### CW, MIDI, CAT

- **CW macros:** Tools → CW Keyer (CWX), F1–F12 in CW, sent by the program's own text keyer
  (5–60 WPM); **PC sidetone for CWX**. TCI `cw_msg` / `cw_macros` work too.
- **Fixed: ANF could only be switched off** from MIDI, a shortcut, CAT or TCI.
- MIDI / shortcuts / CAT / TCI controls that asked the radio now drive the program: NR (the RX
  applet's noise reduction button), NR Cycle, Headphone Volume (the radio's codec volume), CWL
  (switches CW ↔ CWL), CW Speed and Sidetone, Iambic, Iambic Mode, Swap Paddles, Break-In Delay
  (the CW applet). **ATU Start** toggles TUNE. DAX, VOX, AM Carrier and QSK are no longer
  offered in the MIDI list (the ANAN has none of them).

## Not tested on a radio

Most of the above was written without a radio at hand. Confirmed by the operator on the
radio: the shorter pause after an over (by ear), the smooth Center Lock panadapter, the retune
dropout fix, the clean opening of AetherRX / AetherTX and of the main window, exclusive sound
output on his sound card, an automatic calibration on 10 MHz. Not confirmed yet: the RF Delay
guard gap (needs an oscilloscope), the ±200 ppm range, exclusive output on other sound cards,
the load of NNR / DFNR on several receivers at once, the MIDI / CAT changes.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (logs are in `%LOCALAPPDATA%\AetherSDR_ugly_fork\logs`). Say which radio, Protocol 1
or 2, which model is selected in Setup -> Radio, and which system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-636`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C. MIDI: RtMidi 6.1.0 (Gary P. Scavone), MIT-style licence
(`third_party/rtmidi/LICENSE`). Exclusive sound output (Windows): PortAudio, MIT-style licence.
