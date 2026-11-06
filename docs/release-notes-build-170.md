# Ater-ugly_fork build 170

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2.

## Windows portable

Unpack `AetherSDR-ANAN-build170-win64-portable.zip` to any folder and run `AetherSDR.exe`.
No installer. If Windows reports a missing `VCRUNTIME140.dll` / `MSVCP140.dll`, run the bundled
`vc_redist.x64.exe` once. Settings are kept from build 169.

## New in build 170

- Every Protocol 2 ANAN is found and driven:
  - ANAN-10/100 and ANAN-10E/100B (Hermes and Hermes II boards);
  - ANAN-100D and ANAN-200D;
  - ANAN-7000DLE, ANAN-8000DLE, Orion MkII and Anvelina Pro3;
  - ANAN-G2 and ANAN-G2-1K;
  - ANAN-G2E.

  Before this build only the 100D and 200D appeared, so G2 and 7000DLE users could not connect.
- ANAN-7000DLE / 8000DLE / G2 / G2-1K / G2E filter scheme: band-pass filters for both ADCs, the
  TX low-pass filter on transmit only, and the RX2 input grounded on TX.
- Setup → Radio: the hardware page is named after the radio. New groups:
  - **Model**: pick your model where one board covers several radios;
  - **Microphone & Connectors**: mic PTT, PTT on tip / mic on ring, mic bias, the G2 balanced
    XLR input, and RX2 grounded on TX.
- Status bar: PA voltage and, while transmitting, PA drain current on the 7000DLE, 8000DLE, G2
  and G2E. No ANAN reports a temperature over Protocol 2.
- Power meter, reflected power and S-meter constants per model as in Thetis. SWR readings
  change slightly, mostly on 6 m.
- PureSignal is not available on the single-ADC boards (Hermes, G2E).
- Fixed: the attenuator's AUTO button did not stay on, so the automatic attenuator never ran.
- Auto AGC: with Auto on, the AGC-T slider shows where the automatic AGC has set the gain.

## Tested

Only the ANAN-100D has been tested on the air. On other radios, please try receive first,
then transmit at low power.

## Reporting a problem

Attach the log to an issue: **Help → Support & Diagnostics… → Open Log Folder**, or
`%LOCALAPPDATA%\AetherSDR\logs`. Send the newest `aethersdr-YYYYMMDD-HHMMSS.log`, and say which
radio you have and which model is selected in Setup → Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-170`. License: GPL v3.
