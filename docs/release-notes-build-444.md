# Ater-ugly_fork build 444

Unofficial fork of AetherSDR v26.10.4 for the Apache Labs ANAN transceivers over openHPSDR
Protocol 2 and Protocol 1. **Now for Windows, Linux and, for the first time, macOS.**

## Downloads

| System | File | Notes |
|---|---|---|
| Windows 10 / 11 x64 | `AetherSDR-ANAN-build444-win64-portable.zip` | unpack anywhere, run `AetherSDR.exe` |
| Linux x86_64 | `AetherSDR-ANAN-build-444-x86_64.AppImage` | `chmod +x`, run; glibc 2.35+ (Ubuntu 22.04+) |
| macOS 14.4+ on Apple silicon (M1 ... M5) | `AetherSDR-ANAN-build-444-macOS-arm64.dmg` | first build for the Mac, see below |

Windows, upgrading from build 439: copying `AetherSDR.exe` is enough. From an older build: unpack
the whole archive (the FTX mode needs `libftx.dll`, `libjs8.dll` and `cty.dat`).

## New since build 439

### Linux: FTX, JS8 and FreeDV work

The AppImage now carries MicronSDR's (N7DDC) Linux modules: `libftx.so` (FT8 / FT4 / WSPR),
`libjs8.so` (JS8) and `libfdv.so` + `libcodec2.so.1.2` (FreeDV RADE / 1600 / 700D / 700E), with
`cty.dat`. Build 439's notes said FTX and FreeDV did not work on Linux; that is no longer so (and
FreeDV already worked in 439's AppImage, which built it from source - the note was wrong).

### macOS: the first build

- Apple silicon only (M1 ... M5), macOS 14.4 or newer (the floor of Qt 6.12). No Intel build.
- FTX / JS8 / FreeDV modules (MicronSDR's macOS builds) and FDK-AAC for DRM audio are inside the
  app.
- **Not notarized** (there is no Apple Developer ID for this fork): the app is signed ad hoc. The
  first time, right click AetherSDR in Applications -> **Open** -> **Open**; if macOS says it is
  damaged or cannot be opened: `xattr -dr com.apple.quarantine /Applications/AetherSDR.app` in
  Terminal, or System Settings -> Privacy & Security -> **Open Anyway**. `READ ME FIRST.txt` in
  the DMG says the same.
- No DAX audio driver package (it needs a Developer ID installer signature).
- Built and started in CI only; **not yet tested with a radio on a Mac** - please report.

### Receive filter

- **DRM** (440): the receive filter is linear-phase in DRM. The minimum-phase filter delayed the
  carriers at the band edges by 5.5 - 7.1 ms, past DRM's guard interval (2.67 / 5.33 ms).
- **FreeDV and DIGU / DIGL** (441): linear phase as well while FreeDV decodes and in the digital
  modes. The minimum-phase filter spread the FreeDV carriers by 6 - 8 ms, past RADE's cyclic
  prefix (4 ms).

### MicronSDR modules

- `libftx` 1.21 on Windows, Linux and macOS (FT8 decoding of a whole period in one call).
- macOS: WSPR decoded nothing in the first Mac build of libftx (macOS' getopt is restarted
  differently); fixed in MicronSDR's source before this release. Windows and Linux are unchanged.

**Untested on a radio:** everything on macOS; the Linux FTX / JS8 / FreeDV modules were checked
by loading them and by MicronSDR's own tests, not on the air.

## Reporting a problem

**Tools -> Open Logs**, close the program, attach the newest `aethersdr-YYYYMMDD-HHMMSS.log` to an
issue. Say which radio, Protocol 1 or 2, which model is selected in Setup -> Radio, and which
system (Windows / Linux / macOS).

Source: https://github.com/Dfinitski/Ater-ugly_fork at tag `build-444`. License: GPL v3.
FDK-AAC: Fraunhofer FDK AAC Codec Library, its own licence (`licenses/fdk-aac/NOTICE`; in the
macOS app `Contents/Resources/libfdk-aac.NOTICE`).
FTX / JS8 / FreeDV modules, built by MicronSDR (N7DDC): `libftx` -- WSJT-X's decoders and encoders
(K1JT and the WSJT-X team; WSPR: K1JT, K9AN), GPL v3; `libjs8` -- JS8Call's decoder (W6BAZ),
GPL v3; `libfdv` -- FreeDV (RADE, codec2: David Rowe and the FreeDV team), BSD / LGPL-2.1 for
codec2. `cty.dat` by AD1C.
