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
> License: GPL v3, same as upstream (see `LICENSE`). The highlights of upstream AetherSDR v26.10.4 follow.

---

## Upstream AetherSDR highlights

- **GPU-accelerated spectrum & waterfall** — QRhi rendering (OpenGL/Metal/D3D11) with a per-pixel FFT trace at up to **60 fps**, optional FFT Smooth and De-comb trace shaping, an optional **3D stacked-trace** mode, ~71% less CPU than the paint path, GPU-composited slice flags and multi-GPU adapter selection
- **Multi-slice & multi-panadapter** — colour-coded VFO overlays, independent TX assignment, diversity/ESC beamforming, and up to 8 detachable pans with native VITA-49 waterfall tiles and per-flag S-meter / **SmartMTR** views
- **KiwiSDR and Web-888 public-receiver browser** — find and connect to public receivers worldwide through an API-policy-aware directory, with diversity receive and receive-only TX inhibit
- **AetherTX and AetherRX** — the transmit and receive chains, one window each. AetherTX is the channel strip (gate, EQ, compressor, de-esser, tube, AetherVoice exciter, reverb, brickwall limiter) with a preset library and scope; AetherRX puts noise reduction, gate, EQ, compressor, tube, AetherVoice and the output meter on tabs down its left edge
- **Seven client-side noise-reduction engines**, each denoising left and right independently so pans and diversity survive them — NR2 (spectral), RN2 (RNNoise), NR4 (libspecbleach), NNR (WDSP's Neural Noise Reduction), DFNR (DeepFilterNet3), BNR (the NVIDIA Maxine denoiser, in-process on a local RTX/GeForce GPU, Linux + Windows — `docs/nvidia-bnr.md`) and MNR (macOS)
- **DAX virtual audio + IQ** — up to 8 RX audio channels (radio-dependent) plus 1 TX, and 4 channels of raw I/Q at 24–192 kHz for WSJT-X / fldigi / VARA / JS8Call, with a per-slice **WFM demodulator** for satellite data
- **AetherModem packet radio** — KISS-over-TCP TNC, connected-mode AX.25 BBS, a personal mailbox, a WIDE1-1 fill-in digipeater, and an **APRS client** (station table, GPS beacon, messaging) on a Direwolf-derived VHF demodulator
- **AetherSweep** — in-panadapter SWR analyzer with log scale, threshold-band shading and interpolated bandwidth at SWR ≤ 1.5 / 2.0
- **SpotHub** — DX Cluster, RBN, WSJT-X, POTA, FreeDV Reporter, N1MM+/DXLog contest bandmap, the EiBi shortwave schedule and the KiwiSDR DX Community database, with auto-mode switch and per-feed colouring
- **Local voice keyer** — twelve PC-microphone recordings up to 30 seconds each, with preview, import/export and F1–F12 playback; radio DVK remains available separately
- **CW operator suite** — ggmorse plus download-on-demand DeepCW and DeepFist receive decoders where ONNX Runtime is available (ggmorse only on Intel macOS), MIDI/keyboard straight-key and iambic paddles with full QSK, optional Quindar tones
- **Copy Assist (speech-to-text)** — on-device transcription of received voice via whisper.cpp, docked under the waterfall with confidence colour-coding, optionally from the audio before noise reduction. CPU or GPU (Vulkan/Metal, auto-detected), download-on-demand models, optional remote OpenAI-compatible endpoint. Not in the Intel macOS build — see `docs/asr-copy-assist.md`
- **FreeDV RADE** — AI digital-voice codec with a client-side neural encoder/decoder
- **AetherMap (PSK Reporter) overlays** — optional global precipitation (LibreWXR, with NOAA, ECCC and EUMETNET OPERA regional radar backups and a per-provider legend), NOAA/NWS weather radar with observation playback (`docs`) and a NASA/GSFC VIIRS night-lights layer that fades through civil twilight (`docs`), on both the 2D map and 3D globe
- **SmartLink remote, remote over CGNAT, and a TCI v2.0 server** — Auth0/TLS WAN operation; an in-radio Tailscale node that carries AetherSDR to a FLEX-8000 or Aurora radio behind CGNAT; and CAT + audio + IQ + CW + spots + tune and receiver events over a single TCI WebSocket
- **Broad hardware control** — rigctld and virtual-serial CAT, MIDI mapping, the FlexControl knob, serial PTT/CW keying, Multi-Flex operation alongside SmartSDR/Maestro, a CTR2-Max relay over Wi-Fi or USB with negotiated spectrum and CW-text extensions for compatible controllers, and touch gestures (pinch-zoom, press-and-hold, kinetic scrolling) on touch screens
- **Workspace canvas** — place pans and applets freely as resizable, layered items with edge and grid snapping, across several windows if you want them. Named workspaces recall which applets are open as well as where they sit, and bind to radio profiles. Off by default; the Classic shell is unchanged until you enable it
- **A user guide at docs.aethersdr.com** — Stable and Next versions, a printable PDF manual signed with each release, and a Log Analyzer that reads a log or support bundle in your browser without uploading it
- **In-app updates** — a check at launch, at most once a day, that lights an UPDATE badge in the title bar, and an Updates window (File → Update...) with the download built for this computer and the radio's firmware releases
- **Built-in demo mode** — a synthetic backend generating its own RX audio and matching panadapter, with a fault-injection harness, so you can explore the full UI with no radio attached (it cannot transmit)
