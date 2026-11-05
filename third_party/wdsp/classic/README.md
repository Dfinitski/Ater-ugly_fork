# Classic PureSignal (Thetis) for the ANAN-100D port

`calcc.c/.h` and `iqc.c/.h` are taken unchanged from Thetis 2.10.3.x
(`Project Files/Source/wdsp`, Warren Pratt NR0V, GPL v2 or later) -- the
PureSignal that Thetis ships and that reached about -40 dBc IMD3 on the
operator's ANAN-100D. WDSP 2.10 (upstream/) replaced it with a rewritten
calcc (sample buckets, extrapolation, curve averaging) and a single-buffer iqc.

`TXA.c/.h` are upstream's with only the calcc / iqc parts changed to the
classic interfaces (create_calcc with ints/spi/ptol/pin/map/stbl, iqc.p0/p1),
as in Thetis.

Build switch: CMake cache string `AETHER_WDSP_PS_ENGINE` = `wdsp210` (default)
or `classic`. The build
copies upstream/ to the build tree and lays these six files over it, so the
upstream snapshot stays pristine. OFF builds WDSP 2.10's PureSignal.
