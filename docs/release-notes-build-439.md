# Ater-ugly_fork build 439

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build439-win64-portable.zip` to any folder and run `AetherSDR.exe`.
Upgrading from build 413: copying `AetherSDR.exe` is enough. From an older build: unpack the whole
archive (the FTX mode needs `libftx.dll`, `libjs8.dll` and `cty.dat`).

## New since build 413

### Transmit level

- **Full scale for every transmit mode.** WDSP's CIC compensation filter (CFIR) has a passband
  gain of 0.914 (-0.78 dB), so SSB, 2 Tone, FreeDV and the digital modes reached at most -0.9 dBFS
  while TUNE reached full scale. The modulator output is now made up for it: about +20 % peak power
  in those modes at the same drive (measured on a 100D: 2 Tone 42 W -> 58 W). The wire limiter
  (envelope <= 1, x 0.99 for the radio's CORDIC) stays.
- **TX drive peak line** on the transmit panadapter: green within 1 dB of full scale; it now reads
  the samples actually sent.
- **RF Delay** is one transmitter setting: the field in every receiver's ANT menu shows the same
  value, and it is saved in TX Profiles.

### FreeDV

- **Linear-phase transmit filter** for FreeDV, FTX and DIGU / DIGL. The minimum-phase filter spread
  RADE's carriers past its cyclic prefix; an independent test measured RADE SNR 22 dB before,
  **34 dB** after.
- **Transmit level per sub-mode** (RADE, 1600, 700D, 700E): 0.3 dB under the ALC, the ALC idle.
- **Transmit bands** of 1600 / 700D / 700E from codec2's source (1600's outer carriers were cut).
- The Phone applet's TX filter sliders are locked in FreeDV (they did nothing there).
- **FreeDV Reporter:** band buttons filter independently (All clears them), Track / Freq buttons,
  a filter by the time since the last update; the applet's Reporter button opens and closes the
  window.

### Microphone profiles

- A profile holds the receive and transmit equalizers too.

### PureSignal

- **ORION MkII (7000DLE / 8000DLE / G2 / G2E), both protocols:** the feedback source (EXT1 /
  RX Bypass) now switches the input relays as Thetis' "Ext 1 on Tx" / "Rx BYPASS on Tx" do (only
  the bypass-out relay was switched before, so an external coupler never reached the receiver).
  The chosen input is switched on **every transmission, PureSignal on or off** (duplex monitoring of
  the own signal).
- **BPF ByPass on PS** bypasses the first band-pass bank whatever the feedback ADC (Thetis).
- **Protocol 1:** the PureSignal attenuator (and the transmit attenuator) also go in the feedback
  ADC's own attenuator byte while transmitting -- the 7000DLE's Protocol 1 firmware 2.1 does not
  apply the transmit attenuator byte.
- HW peak starts at Thetis' figure for the protocol and board (Saturn too), then follows the
  measurement.
- **Amp delay by hand** (Amp View, Tests: "Amp delay ... Set") and **Previous** (back to the delay
  before the last measurement or change). Measure the delay at low power (5-10 W): at full power the
  measurement can return a wrong value.
- The 2 Tone buttons of the applet and of Amp View stay in step; the applet's lamps are lit only
  while transmitting with PureSignal on.

### Receive

- **ANT menu:** one ADC button for the receiver of that panadapter (it used to set both receivers'
  ADCs), and **Dith: / Rand:** for the ADC selected now, kept per ADC. Protocol 2 sets them per ADC;
  Protocol 1 has one pair for the radio (ADC1's). They are gone from the radio page of Radio Setup:
  set them again in the ANT menu.

### FTX

- The waterfall is one FT8 period (15 s) taller, the same for every sub-mode, in a thin frame.

### Diagnostics

- Protocol 1: one log line per transmission with the board, PureSignal state and the C&C bytes of
  the filters, inputs and attenuators (`ANAN P1 transport: TX C&C ...`).

**Untested on a radio:** the ORION MkII feedback relays and attenuator over Protocol 1 (reported
working by a 7000DLE user for the relays); Dither / Random per ADC; RF Delay in TX Profiles.

**Known:** a peak-reading wattmeter shows 2 Tone about 0.7 dB under TUNE although both reach full
scale on the wire (the 1200 Hz beat is too fast for the detector).

## Linux AppImage

Built from the same source. FTX (FT8 / FT4 / WSPR / JS8) and FreeDV do not work in it yet: their
decoders exist only as Windows DLLs.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue. Say which radio, Protocol 1 or 2, and which model is selected in Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-439`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`).
FTX / JS8 decoders: `libftx.dll` -- WSJT-X's decoders and encoders (K1JT and the WSJT-X team;
WSPR: K1JT, K9AN), `libjs8.dll` -- JS8Call's decoder (W6BAZ), both GPL v3, built by MicronSDR
(N7DDC); `cty.dat` by AD1C.
