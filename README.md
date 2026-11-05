# AetherSDR — ANAN-100D fork (Ater-ugly_fork)

> **This is an unofficial fork of [AetherSDR](https://github.com/aethersdr/AetherSDR) v26.10.4**
> that adds support for the **Apache Labs ANAN-100D** (Odyssey FPGA firmware) over the
> **openHPSDR Protocol 2**. It is not affiliated with or supported by the AetherSDR project.
> Please report problems with this fork here, not upstream.
>
> **What the fork adds**
> - ANAN-100D / openHPSDR P2 backend: RX1/RX2, TX, drive, attenuators, LNA, Alex filters
> - PureSignal (WDSP calcc) with feedback attenuator auto-ranging, drive feed-forward,
>   manual per-band TX delay measurement with thermal drift compensation, Thetis-style scheck
> - HW Diag, RGMII tune, PS amplifier view and wideband monitor dialogs
> - S-meter calibration (correction, ATT and LNA compensation)
> - Thetis-style Auto AGC: AGC threshold follows the noise floor plus an adjustable offset (NF+)
>
> **Windows portable build:** download the ZIP from the
> [Releases](https://github.com/Dfinitski/Ater-ugly_fork/releases) page, unpack it to any folder
> and run `AetherSDR.exe`. No installer is needed. If the program does not start, install the
> Microsoft Visual C++ 2015–2022 x64 runtime (`vc_redist.x64.exe`, included in the ZIP).
>
> The fork version is identified by its build number (`AETHER_FORK_BUILD` in
> `src/core/ForkBuild.h`); releases are tagged `build-NNN`.
>
> License: GPL v3, same as upstream (see `LICENSE`). Features inherited from AetherSDR v26.10.4 are listed below.

---

![AetherSDR Screenshot](docs/assets/screenshot-3dstackedtrace.JPG)

## Features inherited from AetherSDR

- **GPU-accelerated spectrum & waterfall** — QRhi rendering (OpenGL/Metal/D3D11) with a per-pixel FFT trace at up to **60 fps**, optional FFT Smooth and De-comb trace shaping, an optional **3D stacked-trace** mode, ~71% less CPU than the paint path, GPU-composited slice flags and multi-GPU adapter selection
- **AetherTX and AetherRX** — the transmit and receive chains, one window each. AetherTX is the channel strip (gate, EQ, compressor, de-esser, tube, AetherVoice exciter, reverb, brickwall limiter) with a preset library and scope; AetherRX puts noise reduction, gate, EQ, compressor, tube, AetherVoice and the output meter on tabs down its left edge
- **Seven client-side noise-reduction engines**, each denoising left and right independently so pans and diversity survive them — NR2 (spectral), RN2 (RNNoise), NR4 (libspecbleach), NNR (WDSP's Neural Noise Reduction), DFNR (DeepFilterNet3), BNR (the NVIDIA Maxine denoiser, in-process on a local RTX/GeForce GPU, Linux + Windows — `docs/nvidia-bnr.md`) and MNR (macOS)
- **AetherModem packet radio** — KISS-over-TCP TNC, connected-mode AX.25 BBS, a personal mailbox, a WIDE1-1 fill-in digipeater, and an **APRS client** (station table, GPS beacon, messaging) on a Direwolf-derived VHF demodulator
- **SpotHub** — DX Cluster, RBN, WSJT-X, POTA, FreeDV Reporter, N1MM+/DXLog contest bandmap, the EiBi shortwave schedule and the KiwiSDR DX Community database, with auto-mode switch and per-feed colouring
- **Local voice keyer** — twelve PC-microphone recordings up to 30 seconds each, with preview, import/export and F1–F12 playback
- **CW operator suite** — ggmorse plus download-on-demand DeepCW and DeepFist receive decoders where ONNX Runtime is available (ggmorse only on Intel macOS), MIDI/keyboard straight-key and iambic paddles with full QSK, optional Quindar tones
- **Copy Assist (speech-to-text)** — on-device transcription of received voice via whisper.cpp, docked under the waterfall with confidence colour-coding, optionally from the audio before noise reduction. CPU or GPU (Vulkan/Metal, auto-detected), download-on-demand models, optional remote OpenAI-compatible endpoint. Not in the Intel macOS build — see `docs/asr-copy-assist.md`
- **FreeDV RADE** — AI digital-voice codec with a client-side neural encoder/decoder
- **AetherMap (PSK Reporter) overlays** — optional global precipitation (LibreWXR, with NOAA, ECCC and EUMETNET OPERA regional radar backups and a per-provider legend), NOAA/NWS weather radar with observation playback (`docs`) and a NASA/GSFC VIIRS night-lights layer that fades through civil twilight (`docs`), on both the 2D map and 3D globe
- **Workspace canvas** — place pans and applets freely as resizable, layered items with edge and grid snapping, across several windows if you want them. Named workspaces recall which applets are open as well as where they sit, and bind to radio profiles. Off by default; the Classic shell is unchanged until you enable it
- **Built-in demo mode** — a synthetic backend generating its own RX audio and matching panadapter, with a fault-injection harness, so you can explore the full UI with no radio attached (it cannot transmit)
