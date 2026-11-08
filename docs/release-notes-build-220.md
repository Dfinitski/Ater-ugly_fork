# Ater-ugly_fork build 220

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1.

## Windows portable

Unpack `AetherSDR-ANAN-build220-win64-portable.zip` to any folder and run `AetherSDR.exe`.
Upgrading from build 211: replacing `AetherSDR.exe` is enough.

## New since build 211

### PureSignal over Protocol 1

- PureSignal now works over Protocol 1 too, on every Protocol 1 board: ANAN-10/100,
  10E/100B, 100D, 200D, 7000DLE/8000DLE. Same controls as on Protocol 2.
- **Untested on a radio.** Try a two-tone at low power first. If the correction does not settle
  or the Feedback level does not move, send the log (see below).

### CAT

- **COM port:** loggers that reported that "the other end did not accept the connection" now
  connect. The program raises DTR and RTS on its COM port, as a real radio does.
- The **Clients** column shows a logger connected over the COM port.
- **Linux:** a CAT row with "PTY only" (no TCP port, no serial device) now starts. In the logger,
  set the PTY path shown in the row, radio Kenwood TS-2000.

### New menu items and buttons

- **Tools -> Log Settings...**: turns on detailed logging per area. For a CAT problem, turn on
  "TCI / CAT / rigctld", restart the program, then send the log.
- **Receiver noise floor reminder:** while the receiver's own noise has not been captured, a
  short window says how to do it. AUTO ATT needs this capture. **Open Setup** goes straight to
  the page; **Don't remind me** turns the reminder off.

### Also

- **The panadapter look is preset once:** build 220 sets the Display panel (FFT AVG 27, Log Avg,
  FFT Floor 10, colours) and the S-meter detector (Average) to the fork's defaults, once. Change
  them afterwards as you like.
- Applet windows (popped out) open at their full size; the width can still be changed.
- Protocol 1 radios that answer only a broadcast discovery now connect ("no IQ" before).

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue. Say which radio, Protocol 1 or 2, and which model is selected in Setup -> Radio.

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-220`. License: GPL v3.
