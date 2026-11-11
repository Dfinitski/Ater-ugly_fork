# Ater-ugly_fork build 265

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build265-win64-portable.zip` to any folder and run `AetherSDR.exe`.
Upgrading from build 235: copy `AetherSDR.exe` **and the new `fdk-aac.dll`** (DRM audio), or
unpack the whole archive.

## New since build 235

### DRM receiver

- New **DRM** applet (Audio & DSP), decoder Dream. Put a slice on the station, press DRM On:
  the slice goes to DRM mode, and the filter on the panadapter selects the station. Centre it on
  the signal by hand.
- Shows sync lights (IO / Time / Freq / FAC / SDC / MSC), SNR, mode, station labels and up to
  four services, the text message, and the station time. Popped out, it also shows the MOT
  slideshow pictures, Journaline pages and the receiver settings.
- Audio (AAC, HE-AAC, xHE-AAC) goes everywhere the slice audio goes: speaker, TCI, DAX.
- The AAC decoder is FDK-AAC: `fdk-aac.dll` in the Windows archive, built into the Linux
  AppImage. Without it DRM still syncs and shows text, but there is no sound.

### Connection

- Fixed: the program could hang on connect when the radio was still streaming to a previous
  session (for example after the program was closed or crashed and the radio stayed on).
- A radio outside the subnet of the PC adapter it was found on (typically a direct cable to a
  radio with a static IP) is now driven by broadcasts from that adapter. The log advises giving
  the adapter an address in the radio's subnet.
- If the program stops responding for more than 8 s, it writes `aethersdr-crash-*.txt` and
  `.dmp` next to the logs. Please attach them to a report.

### Transmit

- A refused transmit (outside the amateur bands, or in a receive-only mode such as DRM, SAM or
  WFM) now does nothing except show the message at the bottom. Before, the panadapter switched
  to its transmit zoom and the TX indicator lit, although nothing went on air.
- SSB transmit filter: steeper (16384 taps, minimum phase), for better opposite-sideband
  suppression.

### ANAN-100D, Protocol 1

- Setup -> ANAN Hardware: **"Forced RXx Filter control like 7000"**, next to the model. It is
  for 100D radios with modified firmware that switch band-pass filters ahead of both ADCs the way
  the 7000DLE does (as "7000DLE" in Thetis). Shown only for ANAN-100D on Protocol 1; off by
  default.

### Panadapter and waterfall

- **WtrFall Bright** slider (Display -> WATERFALL): brightens or darkens weak and medium signals.
  Black Level and the strongest signals stay where they are. 50 = unchanged.
- The [+] receiver rate button in the panadapter's top right corner no longer stops working
  after one press.
- A click outside an open Band / ANT / Display / Memory / XVTR / DAX panel only closes the panel,
  without also acting on what is underneath. WB is not affected.
- The VFO flag takes the mouse: moving or double-clicking over it no longer reaches the filter
  under it.
- Tuning with the wheel over a frequency digit: the flag no longer jumps with the filter.
- The antenna indicators on the VFO flag follow the ANT panel.

### Window and status bar

- Windows display scaling (120 %, 150 %) is no longer applied: the program always runs at the
  monitor's native resolution. View -> UI Scale still works.
- New-release check every 15 minutes (it was only at start).
- Status bar: the antenna indicator reads "RX: ANTn" over "TX: ANTn". A new **UTC Time** clock.
  Status messages no longer push the station name and everything right of it. The radio
  type/firmware and Network blocks are removed.
- Station name box: smaller, centred, grows with its text. A double click opens Setup -> Radio
  at the station name.
- Title bar: the connection text is gone while connected. The radio tab is one line, with a
  full-width accent bar. The PC Audio button is smaller and redrawn.
- Quick-memory frequency buttons are 20 % lower.
- Torn-off applets keep at least their docked width and keep their × (it closes the applet
  without docking it). The pin is grey when off and red when on.

**Untested on a radio:** DRM on air (tested with recordings only), the broadcast connection for
radios outside the adapter's subnet, the 7000-style filter option (needs the modified 100D), the
new SSB transmit filter on air.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue (and any `aethersdr-crash-*` files). Say which radio, Protocol 1 or 2, and which model is
selected in Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-265`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`).
