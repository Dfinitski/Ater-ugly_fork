# Ater-ugly_fork build 190

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2.

## Windows portable

Unpack `AetherSDR-ANAN-build190-win64-portable.zip` to any folder and run `AetherSDR.exe`.
No installer. If Windows reports a missing `VCRUNTIME140.dll` / `MSVCP140.dll`, run the bundled
`vc_redist.x64.exe` once. Settings are kept from build 170.

## New since build 170

### Noise floor and the automatic attenuator

- **The panadapter shows the real noise floor.** The trace is now the power in a 500 Hz
  bandwidth, on the S-meter's calibrated scale, as it reaches the ADC (the attenuator is not
  added back).
  - The floor no longer moves when you change the sample rate, the FFT size, the zoom or the
    averaging.
  - A pixel holding several FFT bins shows their mean power instead of the peak.
  - A dashed line marks the measured floor ("Noise −NNN dBm/500 Hz"). In a 500 Hz CW filter
    with no signal, the S-meter (Average) reads the same within about 1 dB.
  - Signal levels are read on the S-meter. The panadapter is for the noise floor; a carrier
    reads its true level only where a pixel spans about 500 Hz.
- **AUTO ATT holds a margin above the receiver's own noise.** The **NF+** slider in the ANT panel
  sets how many dB the band noise is held above the receiver's own noise (0..+30 dB, default
  +10). The receiver then costs this much sensitivity:

  | NF+ | Sensitivity lost |
  |---|---|
  | +3 dB | 1.8 dB |
  | +6 dB | 1.0 dB |
  | +10 dB | 0.4 dB |
  | +20 dB | 0.04 dB |

  A smaller margin gives more attenuation and more headroom against overload. A larger one
  gains no sensitivity.
- **Capture the receiver's own noise once (required for AUTO ATT).** It cannot be measured on the
  air: on 40 m the antenna noise is far above it even at 31 dB attenuation.
  1. Connect the radio and let it receive.
  2. Disconnect the antenna from the receiver input, or terminate it with a 50 Ω load.
  3. Open **Setup → Receive → Receiver Noise Floor** and press **Capture**. Every receiver goes
     to ATT 31 dB for about a second; the result is stored per ADC with the date.
  4. Reconnect the antenna and switch **AUTO** on in the ANT panel.

  Until a capture exists, AUTO does nothing. Capture again after a firmware or hardware change.
- The same group has "Show the noise floor line on the panadapter" and a test switch for the
  ADC overload warning.
- Auto AGC follows the same noise floor. The squelch line moves with the new scale.

### Receive and diagnostics

- **ADC overload warning:** a large, blinking, half-transparent red "ADC1 OVERLOAD" / "ADC2
  OVERLOAD" across the panadapter of a receiver whose ADC is clipping.
- **S-meter:** Thetis' peak detector (Setup → Receive → Detector: Peak or Average), updated every
  50 ms.
- **HW Diag** (Tools): a new **10 MHz Reference** group shows the lock state, the offset in Hz and
  ppm, the last 5 s and the 122.88 MHz clock against the reference, also with no signal at the
  input.
  - These need the radio's status packets on receive. With the operator's Odyssey firmware they
    come from its current version on.

### Transmit

- **One TX audio chain for both microphones.** The source is **PC** or **CDC** (the radio's codec
  microphone). Both go through the same chain with the same settings: equalizer, gate, compressor,
  mic gain, limiter.
  - Before, the radio microphone bypassed it, so the TX equalizer was not on the air.
  - The TEST source is gone; use 2-Tone in the PureSignal widget.
- **RF Power and Tune sliders follow watts:** 50 % now gives about half the power. Both use the
  per-band PA level calibration.

### Connection and stability

- Fixed: the program could crash when the radio was switched off and on, or stopped and started
  on the Radio tab.
- Stop / start now works like Thetis: the session is started and stopped as a whole, and the
  next start sends the same packets as the first.
- **Crash report:** if the program closes unexpectedly, it leaves `aethersdr-crash-*.txt` and a
  `.dmp` minidump in the log folder. Please attach them to the issue.

## Tested

Only the ANAN-100D has been tested on the air. On other radios, please try receive first,
then transmit at low power.

## Reporting a problem

Attach the log to an issue: **Help → Support & Diagnostics… → Open Log Folder**, or
`%LOCALAPPDATA%\AetherSDR\logs`. Send the newest `aethersdr-YYYYMMDD-HHMMSS.log`, and any
`aethersdr-crash-*` files. Say which radio you have and which model is selected in
Setup → Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-190`. License: GPL v3.
