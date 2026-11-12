# Ater-ugly_fork build 302

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build302-win64-portable.zip` to any folder and run `AetherSDR.exe`.
Upgrading from build 265: copying `AetherSDR.exe` is enough, or unpack the whole archive.

## New since build 265

### Start-up

- Every volume control (PC master, radio codec, headphones, each receiver) now defaults to 1, so
  a new installation no longer starts at full volume. Saved levels are kept.
- The program finds the radio at start but does not start it: the title bar and the panadapter
  say "<radio> found — click to start". Click the radio's tab or that text. Reconnects after a
  dropped link stay automatic.
- Before a radio is connected, the window shows the openHPSDR controls, not FlexRadio ones.

### Radio type

- Setup -> Radio, Model: **"Forcedly assign the radio"** (it replaces build 245's "Forced RXx
  Filter control like 7000"). Off: the model is the one the radio reports. On (after a warning):
  any type the protocol drives can be chosen, and the session runs with that board's profile
  (DDCs, ADCs, filters, PureSignal). Takes effect from the next connection. A modified
  ANAN-100D that should switch filters like a 7000DLE: assign ANAN-7000DLE.

### Band filters and ADCs

- The band filters are chosen per ADC, as they are wired in the radio: any receiver can use
  either ADC, but each filter bank sits in front of one ADC. When receivers on the same ADC are
  on different bands, that ADC's bank goes to bypass and the panadapter shows **WIDE** (styled
  like the span buttons, left of them). The 7000/8000 filter control is handled separately.
- Radios with one ADC no longer offer ADC2.

### AUTO attenuator and AGC

- **AUTO ATT** works like MicronSDR: it holds the noise floor at the ADC at an absolute target
  (Setup -> Receive, default -120 dBm / 500 Hz on the S-meter scale), in steps of up to 6 dB with
  3 dB hysteresis. It detects when more attenuation no longer lowers the floor (the receiver's
  own noise) and takes that step back. The own-noise capture is removed.
- **Noise floor**: the 20th percentile of the visible spectrum, as MicronSDR computes it. Green
  dashed line on the panadapter, "Show the signal noise floor at the ADC input".
- **AGC gate**: yellow dashed line, "AGC Gate -NN dBm/500 Hz" — the level above which the AGC
  levels signals. With Auto AGC it is set from the noise floor + NF+ (marked "(Auto)", shown
  only; set NF+ on its slider). With Auto off, drag the line: it is the AGC-T slider.
  Both lines are on by default ("Show the AGC gate" in Setup -> Receive).
  Note: the noise in the pauses drops only once the gate is about 12-15 dB above the green line;
  the AGC follows the peaks of the noise, not its average.

### TCI

- I/Q streams for skimmers: the spectrum is no longer mirrored about the centre frequency, and
  384 kHz is accepted.
- RX audio in int16/int24/int32/float32, with the packet size, TX buffering and TX audio format
  the client asks for (as the Thetis TCI server does).

### Panadapter and waterfall

- **3D waterfall rebuilt.** Each row is the waterfall's own data at full resolution, one bar
  per bin, with perspective from a centre vanishing point and the level's own colours. Rows
  already drawn no longer change when the panadapter is panned or zoomed. Its own sliders:
  3D Floor, 3D Gain, 3D Height.
- The waterfall's black level no longer changes with zoom.
- Quick-memory buttons: also store the sample rate, the pan centre and the zoom, so a press
  restores the whole view. Buttons a little lower, text 1 pt larger.
- A right click on the frequency scale no longer opens a stray popup.
- A receiver's filter that was off screen now appears while the panadapter is being dragged.
- No gaps between the panadapter menu buttons (+RX, WB, ...).

**Untested on a radio:** TCI I/Q with a skimmer and the new TCI audio formats; "Forcedly assign
the radio" on boards other than the operator's; the per-ADC filters on 7000/8000 and on
two-filter boards.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (and any `aethersdr-crash-*` files). Say which radio, Protocol 1 or 2, and which model is
selected in Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-302`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`).
