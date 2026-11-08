# Ater-ugly_fork build 211

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and, new since build 190, Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build211-win64-portable.zip` to any folder and run `AetherSDR.exe`.
No installer. If Windows reports a missing `VCRUNTIME140.dll` / `MSVCP140.dll`, run the bundled
`vc_redist.x64.exe` once. Settings are kept from earlier builds.

From this build on, the program checks this repository's releases once at start; when a newer
build is out, a "New release: ..." link appears at the bottom right of the window. Click it to
open the release page.

## New since build 190

### openHPSDR Protocol 1 (ANAN boards with Protocol 1 firmware)

The same radios with the older Protocol 1 firmware: ANAN-10/100 (Hermes), 10E/100B (Hermes II),
100D (Angelia), 200D (Orion), 7000DLE/8000DLE (Orion MkII).

- The radio list shows such a radio as "<model> (Protocol 1)". The protocol is chosen
  automatically: whichever the radio answers (Protocol 2 if it answers both).
- **Receive:** RX1 and RX2, sample rates up to 384 kHz (the Protocol 1 limit), frequency,
  attenuator, antennas, Alex filters, open collector outputs, dither/random, ADC overload
  warning, speaker audio to the radio.
  - The DDC plan is Thetis': on the 100D/200D/7000, RX1 is DDC0 and RX2 is DDC2.
  - For the 10E/100B, select that model in Setup -> Radio: Protocol 1 reports them as Hermes.
- **Wideband Monitor** (ADC1 raw spectrum, 0..61 MHz) over Protocol 1 as well.
- **Transmit:** SSB/AM/FM/digital, Tune, host CW, the radio's microphone or the PC's. The
  radio's PTT works. Drive, power sliders, PA calibration, TX antenna and filters as on
  Protocol 2.
- Not yet over Protocol 1: PureSignal, and the radio's internal CW keyer driven from the
  paddles.

### Receive filter

- **Changing the filter no longer breaks the audio.** Dragging a filter edge (panadapter or RX
  Controls) used to stall receive: audio broke up, the panadapter froze, and sometimes the
  audio stopped until a restart. The new filter is now computed on a thread of its own, as
  Thetis does, while the receiver keeps running on the old one.
- **Edges stop at the carrier.** On the panadapter, as in RX Controls: in USB/DIGU the low edge
  stays at or above 0, in LSB/DIGL the high edge at or below 0.
- **AM, SAM, FM:** dragging one edge moves the other symmetrically; neither comes closer than
  100 Hz to the carrier.
- **Mouse wheel** over an edge of the RX Controls filter graphic: up +50 Hz, down -50 Hz.

### Panadapter and waterfall

- **Dragging the panadapter** no longer retunes the radio on every step. The view moves inside
  the band already received; the radio is retuned only when the view reaches the edge, with a
  short audio fade instead of distortion. No more "jelly" trace while dragging.
- **The waterfall** is drawn from the whole received band, so a drag in zoom shows waterfall at
  the sides at once.
- **NB Blank** works: impulse noise rows in the waterfall are lowered to the noise floor, the
  signals stay. The slider is the threshold in dB (default 3 dB).
- **Display panel tidied up:**
  - "Wt Avg" is now **Log Avg**: averaging in dB, noise about 2.5 dB lower and grassier. Off is
    the true power average (matches the noise floor line and the S-meter).
  - Removed: FFT FPS (fixed 25), FFT Smooth, De-comb, NF Range.
  - FFT Floor is always on; the slider is the floor line's height above the bottom.
  - Black Level is always automatic (SW); the slider is the offset from the noise floor.

### CAT

- **Serial CAT port.** Each CAT port row (CAT applet, floated) has **Serial** and **Baud**.
  - Windows: a COM port. For a logger on the same PC, install a virtual null-modem pair
    (com0com or VSPE): choose one end here (e.g. COM11) and the other in the logger (COM12),
    radio type Kenwood TS-2000.
  - Linux: as before, the PTY path shown in the row; or a real serial device (/dev/ttyUSB0...).
  - A port that is missing or busy is retried every 3 s; the dot next to it shows the state.
- **New dialect "Thetis":** Kenwood TS-2000 (ID019) plus the Thetis / PowerSDR ZZ commands, for
  loggers set up for Thetis. The attenuator and preamp ZZ commands (ZZRX, ZZRY, ZZPA) are not
  supported yet.
- On an ANAN, use the **TS-2000** or **Thetis** dialect. The Flex dialect reports a Flex model
  ID that loggers do not know.

### Transmit power

- **PA Power Law** is a setting again (Setup -> Radio, ANAN page): "Drive at slider 50 %" sets
  the mid-scale of the RF Power and Tune sliders. Default 46.7 % (unchanged behaviour).
  - To fit another radio: measure the power at slider 100 % and 50 % with a wattmeter, enter
    both, press **Apply**.

### Other

- **Tools -> Open Logs** opens the folder with the log files.

### Linux

- An Ubuntu x64 AppImage is built for the releases (FreeDV included). Run
  `chmod +x AetherSDR-ANAN-*.AppImage`, then start it; Ubuntu 22.04+ may need `libfuse2`.

## Tested

The ANAN-100D over Protocol 2 has been tested on the air, including everything under "Receive
filter" and "Panadapter and waterfall". Protocol 1, the serial CAT port and the Thetis dialect are
untested on a radio: try receive first, then transmit at low power.

## Reporting a problem

Attach the log to an issue: **Tools -> Open Logs**, or `%LOCALAPPDATA%\AetherSDR\logs`. Close the
program first, then send the newest `aethersdr-YYYYMMDD-HHMMSS.log`, and any `aethersdr-crash-*`
files. Say which radio and firmware (Protocol 1 or 2) you have and which model is selected in
Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-211`. License: GPL v3.
