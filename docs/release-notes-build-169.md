# Ater-ugly_fork build 169 — first release

Unofficial fork of AetherSDR v26.10.4 with support for the Apache Labs ANAN-100D
(Odyssey FPGA firmware) over openHPSDR Protocol 2.

## Windows portable

Unpack `AetherSDR-ANAN-build169-win64-portable.zip` to any folder and run `AetherSDR.exe`.
No installer. If Windows reports a missing `VCRUNTIME140.dll` / `MSVCP140.dll`, run the bundled
`vc_redist.x64.exe` once.

## Features

- ANAN-100D / openHPSDR P2 backend: RX1/RX2, TX, drive, step attenuators, LNA, Alex filters
- PureSignal (WDSP calcc):
  - feedback attenuator auto-ranging inside the Thetis window (128..181) with drive feed-forward
  - manual per-band TX delay measurement with thermal drift compensation
  - Thetis-style solution check (intervals 4..15, negative-correction guard)
- HW Diag, RGMII tune, PS amplifier view and wideband monitor dialogs
- S-meter calibration (Setup → Receive): correction ±50 dB, ATT and LNA compensation
- Auto AGC: the AGC threshold follows the noise floor plus an adjustable NF+ offset
  (VFO flag and RX Control applet, synchronized)

## Known limitations

- One short PureSignal reset may occur at the start of a transmission.
- The built-in updater points to upstream AetherSDR and is not shipped with this build.

Source: this repository at tag `build-169`. License: GPL v3.
