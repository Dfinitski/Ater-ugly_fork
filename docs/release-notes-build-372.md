# Ater-ugly_fork build 372

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build372-win64-portable.zip` to any folder and run `AetherSDR.exe`.
Upgrading from build 336: copying `AetherSDR.exe` is enough, or unpack the whole archive.

## New since build 336

### FreeDV

- **FDV is a mode of its own** on the VFO flag (it used to look like SSB, so the SSB button
  could not leave it). Default second-row button.
- **The filter on the panadapter is the modem's:** no carrier line, centred on the signal
  (1500 Hz), as wide as the sub-mode needs; its edges cannot be dragged. The transmit filter
  follows the sub-mode as well.
- **Received text** in a strip under the flag; **SNR and frequency offset** beside the
  frequency.
- **Transmit chain put right:** microphone -> MIC gain -> speech processing -> encoder ->
  modulator (the modem signal no longer passes through the microphone processing).
- The ALC gauge becomes **MOD** in FreeDV: the microphone level at the encoder's input.

### Transmit panadapter

- **Full resolution** on transmit, the same width on Protocol 1 and Protocol 2, with the
  receiver's FFT average applied.
- **Fixed scale 0 ... -80 dB**, 0 dB = full scale of the signal sent to the transmitter, and a
  **TX drive peak** line: how hard the transmitter is driven.
- The side menu and open flag options fold away while transmitting.
- **Drive ceiling 0.99 of full scale** on the wire: the radio's standard firmware wraps the
  signal just above that, so a full-scale peak can no longer distort.

### VFO flag and windows

- Mode list for the buttons: one **SSB** entry (takes the band's sideband; a second click swaps
  it) instead of USB and LSB; CWL is CW's second click. New default buttons: SSB, AM, SAM, FDV /
  CWL, SAM, DIGU, FM (saved assignments are kept).
- A click outside an opened flag folds it; the flag keeps the right height when the mode tab
  changes.
- **Floating applet windows** switched off with their tray button stay off after a restart.
- The Window menu is gone from the menu bar (its shortcuts still work).

### Panadapter

- **No level jump after a pan drag:** the part of the band drawn in takes real data at once, and
  neither the noise line, the AUTO attenuator nor the display's Auto Floor counts bins that are
  not averaged yet.
- **AUTO attenuator** measures the frame before averaging, so a long FFT average no longer makes
  it hunt or stick.
- **Zoom** no longer retunes the receiver on every step (clicks in the audio).
- **Center Lock:** a zoom changes the width only, the filter stays in the middle; **dragging the
  panadapter tunes the locked receiver** -- the filter stays in the middle and the band moves
  under it, rounded to the tuning step on release.
- Band buttons show their whole label ("2200", "XVTR"); a transverter's name may be 6 characters.

### Transverters

- Setup -> Transverters: the **LO can be typed** (RF = LO + IF), frequencies to 1 Hz, **LO Error
  in Hz**, **Valid / Invalid** follows every entry and says why, with the RF range served.
- **RX Gain** now works: the S-meter, the panadapter and the noise / AGC lines read the level at
  the transverter's antenna socket.
- **Max Power (%)** now works: the ceiling of the RF Power and Tune sliders through that
  transverter (100 = no limit).
- PA Level Calibration and the PureSignal delay follow the **IF band** (they followed 6 m).
- Both band buttons of a transverter select USB.

### TCI

- An I/Q rate above the receiver's is raised again whenever the client asks or restarts I/Q
  (rates above 96 kHz).

**Untested on a radio:** transverters (RX Gain, Max Power, PA calibration by the IF); TCI I/Q
above 96 kHz; the last Center Lock change (build 372: the panadapter following a drag while it
lasts).

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (and any `aethersdr-crash-*` files). Say which radio, Protocol 1 or 2, and which model is
selected in Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-372`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`).
