# Ater-ugly_fork build 336

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build336-win64-portable.zip` to any folder and run `AetherSDR.exe`.
Upgrading from build 302: copying `AetherSDR.exe` is enough, or unpack the whole archive.

## New since build 302

### TCI and CW Skimmer

- **CW Skimmer (TCI) works:** the server announces `protocol:ExpertSDR3,2.0` as Thetis does, and
  I/Q is sent in frames of 2048 pairs as the TCI specification gives.
- **"Center Lock when I/Q"** (TCI applet, under the TX slider): while a TCI client takes I/Q from
  a receiver, that receiver runs like Thetis CTUN off -- the filter stays in the middle of the
  panadapter and the radio's own NCO is retuned, so the skimmer always sees the tuned frequency
  (the CW offset included). Plain Center Lock is unchanged.
- If a client asks for an I/Q rate above the receiver's, the receiver's rate goes up to it.
- The TCI applet shows each client: program and version, I/Q receiver, rate, frames/s and
  kS/s (red when nothing is sent), audio format, last command.

### VFO flag: mode buttons

- Two rows of four mode buttons (default LSB USB CW AM / CWL DIGL DIGU SAM). Right-click a
  button to give it any mode the radio offers; a mode the radio does not offer is greyed.
- A second click on the lit button swaps the sideband: USB<->LSB, DIGU<->DIGL, CW<->CWL.
- **Marker** (1 px / 3 px / Off) and **Filter Edge** moved to the panadapter right-click menu,
  below Center Lock.
- **FreeDV** takes the band's sideband: LSB below 10 MHz (USB on 60 m), USB from 10 MHz, and
  follows band changes.

### Panadapter

- The **CW decode panel** lies over the bottom of the waterfall; opening it or a mode change no
  longer resizes the panadapter.
- While the panadapter is dragged, the **noise floor** (green line, Auto AGC, AUTO ATT, AGC gate)
  ignores the newly drawn edge until its average has built up -- no more jumping peaks.
- A band button retunes only the receiver of its own panadapter.
- Panadapter menu buttons (Band, ANT, Display, Memory, DAX): a click on another button closes the
  open window and opens the new one; a click on the same button closes it; the arrow closes all.
- In **DUP**, the panadapter's automatics (noise floor, auto squelch, Auto Black) also hold
  during transmit.
- Tooltips everywhere: black, blue border, white text.

### Receive audio

- Receiver audio is **mono** by default (the left/right balance no longer moves with USB/LSB).
  **BIN** in the RX applet gives Thetis-style binaural (I left, Q right).
- **DRM:** AGC is off in DRM and the I/Q orientation is right (no Flip Spectrum needed); the DRM
  window no longer changes size as the decoder state changes.

### Transverters

- Setup -> Transverters now works with openHPSDR radios: the list is stored in the program, the
  RF frequency is converted to the radio's IF, and transmit is allowed only in the VHF/UHF/SHF
  amateur bands (not on RX-only transverters).

### MIDI

- Two tuning-knob parameters, **VFO A Tune Knob** and **VFO B Tune Knob**. With one assigned it
  tunes the active VFO as before; with both, each tunes its own VFO.

### CPU and memory

- The 3D waterfall is computed only in 3D; its buffers are allocated only when it is used.
- Lighter spectrum path (ring-buffer FFT window, float log/exp); the overlay is no longer redrawn
  for every small change of the lines.
- Waterfall history is 5 minutes (about a quarter of the memory).

**Untested on a radio:** transverters; "Center Lock when I/Q" with skimmers other than CW
Skimmer; MIDI VFO B knob.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (and any `aethersdr-crash-*` files). Say which radio, Protocol 1 or 2, and which model is
selected in Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-336`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`).
