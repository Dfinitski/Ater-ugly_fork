# Ater-ugly_fork build 235

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build235-win64-portable.zip` to any folder and run `AetherSDR.exe`.
Upgrading from build 211 or 220: replacing `AetherSDR.exe` is enough.

## New since build 220

### Connection

- Radios that Thetis reaches on the same PC but this program did not (several network
  adapters, a radio with a static IP) now connect: the program talks to the radio from the
  adapter the radio was found on. If nothing comes back, the error message says why (the radio
  is not in the adapter's subnet, or a VPN / firewall / another program).
- **Connect by IP** works for Protocol 1 radios too.

### Transmit

- Transmit is allowed only inside the amateur bands of the band plan selected in Display. A
  key-down outside them is refused with a message. The radio's own CW keyer is switched off
  outside the bands as well. Receive is not affected.
- The forward power meter moves smoothly: it holds the peak of the last 250 ms and glides to it.

### Panadapter

- Finer frequency resolution (about 4096 points across the view, up to a 65536-point FFT
  when zoomed in), sharp from the first frame.
- At full zoom the panadapter stops at its limit instead of jerking.
- **FFT Smooth** is back on the Display panel (default 0 = off).

### Tuning and bands

- **Band memory:** each band button brings the band back as you left it: frequency, mode,
  filter, AGC, pan width and position, and the antennas and LNA (RX1) or attenuator (RX2).
  RX1 and RX2 keep their own memory.
- **Wheel over the frequency display** tunes the digit under the cursor (1 Hz ... 10 MHz). The
  panadapter moves and the filter stays where it is.
- **User Out** (Setup -> Receive) follows the band the receiver is tuned to, however it was
  tuned (band button, click, wheel, CAT, TCI). Before, it followed the panadapter centre.

### Receive

- **ANF** (automatic notch) button on the VFO's DSP tab, before NB. Works in LSB, USB, DSB,
  AM and SAM; the DSP slider sets how fast it adapts.
- **TCI I/Q** for skimmers (CW Skimmer, SDC): the I/Q stream now works with the ANAN, at
  24 / 48 / 96 / 192 kHz.

**Untested on a radio:** TCI I/Q with a skimmer, the connection fix on the reporting user's
network, User Out on Protocol 1, PureSignal over Protocol 1 (from build 220).

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue. Say which radio, Protocol 1 or 2, and which model is selected in Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-235`. License: GPL v3.
