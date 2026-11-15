# Ater-ugly_fork build 413

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build413-win64-portable.zip` to any folder and run `AetherSDR.exe`.
Upgrading from build 372: copy `AetherSDR.exe` **and the three new files** `libftx.dll`,
`libjs8.dll` and `cty.dat` (the FTX mode needs them), or unpack the whole archive.

## New since build 372

### FTX: FT8, FT4, WSPR and JS8

- **A digital-mode window of its own** (from MicronSDR, N7DDC): FT8, FT4 and WSPR decoders, the
  decodes table with country, distance and azimuth, auto-sequencing, Tx1-Tx6, QRZ, the log
  (ADIF in `%USERPROFILE%\AetherSDR-FTX\`), PSK Reporter.
- **Decoders work on the receiver's I/Q**, not on its audio; the mode's **own transmitter**
  (the waveform is made on the transmit thread). Tune is allowed in FTX; the waterfall holds
  while transmitting; the forward power goes into the log.
- **Highlighting as in JTDX:** calls to me, CQ and 73 with a background for a new DXCC / DXCC on
  the band / new call / call on the band; worked stations struck through (from the ADIF log).
- **Its waterfall under the panadapter**, the RX (and TX) band set with the mouse.
- **Bands:** HF plus 4 m, 2 m, 1.25 m, 70 cm and 23 cm when a transverter of Radio Setup covers
  them.
- **JS8:** the JS8Call sub-mode (Normal / Fast / Turbo / Slow) in its own window -- band activity,
  messages, stations heard, CQ, HB, Reply, SNR, directed messages, AUTO -- with its own waterfall.
  The **JS8** button in the FTX window and **FTX** in the JS8 window switch between them; the last
  one is remembered.
- **Two receivers:** the FTX receiver owns the transmitter, its band filter and its frequency
  (MIDI, TCI and the panadapter cannot retune it); the other receiver keeps working and cannot
  transmit. **+RX** on the FTX bar opens a second receiver.
- A toast "Connection <me> - <him> done" after a QSO is logged.
- Latin-only fields (call, locator) take keys typed in the Russian layout.
- **Auto AGC is frozen** while FTX has the receiver and comes back with the same gain (it used to
  re-learn from a noise floor that was still moving: audible noise after leaving FTX).

### FreeDV

- **FreeDV Reporter:** a **Reporter** button in the FDV applet; an **Enable** button in the
  reporter window (connect / disconnect); a **Visible** button in the applet (this station shown
  on the reporter or not); receive reports (mode, sync, SNR, callsign) from the local FDV.
- "Call:" in the applet is now **Text:** (the text sent).
- **Transmit filter per sub-mode** (shown on the transmit panadapter). RADE: 600..2350 Hz, the
  modem's own band.
- **RADE transmit level 4.5 dB lower:** behind the filter its peaks were 4-5 dB over full scale
  and the ALC rode the signal all the time. Now the ALC is idle (measured on a radio); the average
  power is lower (about 13 W of a 100 W scale).

### Transmit

- **Microphone profiles:** Profile 1-4 under the Mic applet, separate sets for SSB, AM, FM and
  FDV, the last used one applied when the mode group changes. Right click: Save current. A profile
  holds the whole chain from the microphone to the modulator, AetherTX included. The profiles are
  part of a **TX Profile** (Radio Setup -> Transmit).
- **PureSignal:** the receive band-pass filters are bypassed on transmit (ORION MkII boards:
  7000DLE, 8000DLE, G2), always, as Thetis does by default. Not possible on 100D / 200D.
- **Transmit region** in Radio Setup; through a transverter no region limits (as Thetis). The
  refusal message says the real reason.
- **ALC gauge** on ANAN shows WDSP's ALC (it showed 0 dB all the time). A transmit level line
  every 2 s in the log (`TXA levels`).

### Audio and connection

- **Receive audio on its own thread** (the speaker, FreeDV, MON, the computer's transmit audio and
  the radio's loudspeaker in FreeDV): the interface no longer stops the sound.
- Silence instead of no audio when nothing is in the mix.
- Reconnect: no hang (the panadapter was left without a frame limit); the loss of the radio's I/Q
  ends the session and a reconnect starts from scratch.

### Display and windows

- **Waterfall average (WF Average)**, on by default as in Thetis: weak signals stay visible with
  a raised black level.
- **Settings windows** (FreeDV Reporter, Memory, MIDI, MQTT, Profile Manager, Theme Editor and
  others): title bar 1.5x, rounded corners on Windows 11.
- An open VFO flag folds on the first click outside; that click is swallowed only on the
  panadapter / waterfall (where it would retune), elsewhere the control works at once.
- The program's **name to the outside** -- FreeDV Reporter, PSK Reporter, wsprnet, the spot
  comment, JS8 `<MYVERSION>` -- is "Aether ugly fork <build>"; inside, it is AetherSDR as before.

**Untested on a radio:** JS8 transmission and its waterfall; Auto AGC after leaving FTX; the
transmit region through a transverter; the PureSignal filter bypass; microphone profiles inside a
TX Profile.

**Known:** in FreeDV 700D and 700E the ALC still rides the signal (1.3-2.9 dB on average) -- their
transmit filter and level are the next work. 1600 is clean.

## Linux AppImage

Built from the same source. **FTX (FT8 / FT4 / WSPR / JS8) and FreeDV do not work in it yet:**
their decoders exist only as Windows DLLs (`libftx.dll`, `libjs8.dll`, `libfdv.dll`); on Linux
the program starts and reports that the decoder library is not loaded when the mode is switched
on. Everything else is the same as on Windows.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (and any `aethersdr-crash-*` files). Say which radio, Protocol 1 or 2, and which model is
selected in Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-413`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`).
FTX / JS8 decoders: `libftx.dll` -- WSJT-X's decoders and encoders (K1JT and the WSJT-X team;
WSPR: K1JT, K9AN), `libjs8.dll` -- JS8Call's decoder (W6BAZ), both GPL v3, built by MicronSDR
(N7DDC); `cty.dat` by AD1C.
