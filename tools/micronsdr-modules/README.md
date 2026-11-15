# MicronSDR decoder modules: sources for the macOS build

Copies of the MicronSDR (N7DDC) module sources, as handed over for the macOS build:

- `FT_src` -> `libftx.dylib` (WSJT-X FT8 / FT4 / WSPR decoders and encoders, GPL-3)
- `JS8_src` -> `libjs8.dylib` (JS8Call decoder, GPL-3)
- `FreeDV_src` -> `libfdv.dylib` + `libcodec2.1.2.dylib` (the wrapper is BSD; codec2 LGPL-2.1,
  rade_c and opus are fetched by the script at fixed commits)

They are built on GitHub Actions by `.github/workflows/macos-modules.yml` on an Apple silicon
runner (`macos-15`) with each folder's own `build_macos.sh`; the libraries and the build logs
are the run's artifact `micronsdr-macos-arm64-modules`. Requirements and checks:
`FT_src/MACOS_MODULES.txt`. The Windows and Linux modules are prebuilt in `third_party/`.
