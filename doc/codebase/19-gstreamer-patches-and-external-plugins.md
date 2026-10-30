# GStreamer Patches and External Plugins

The Docker base image rebuilds `gst-plugins-bad` 1.24.2 and applies these patches in lexical order:

| Patch | Intended implementation area | Evidence to collect |
| --- | --- | --- |
| `010-dvb-ac3-eac3.patch` | DVB metadata/stream type treatment for AC-3/E-AC-3. | TS PMT and decoder compatibility tests. |
| `020-h26x-over-srt.patch` | H.26x behavior over SRT. | SRT H.264/H.265 protocol tests. |
| `030-mpegtsmux-allow-to-disable-SCTE-NULL-by-setting-inte.patch` | Control of SCTE null insertion. | Muxer property + SCTE section inspection. |
| `040-mpegtsmux-concurrent-scte.patch` | Concurrent SCTE handling in mpegtsmux. | Simultaneous event output tests. |
| `050-hlsdemux-add-playlist-update-signal.patch` | Playlist-update signal from HLS demux. | HLS input update/switch tests and callbacks. |

Read each diff before asserting its exact externally visible behavior. The table gives the source intent, not a tested compatibility promise.

## Rust plugins

The base image builds `gst-plugins-rs` components for `fallbackswitch`, fMP4, AWS, NDI and `dashsink2`. The Dockerfile creates one Debian package, nominally `fallbackswitch`, whose assets are every `.so` in its `target/release`, so all of them are installed by it. Relevant source factories include `fallbackswitch`, `isofmp4mux`, `cmafmux`, `dashsink2`, `awss3hlssink`, and `ndisrc`.

fallbackswitch, AWS and NDI are built with Ubuntu's cargo from the `gstreamer-<version>` tag of the installed GStreamer (1.24.2). The CMAF DASH output needs two exceptions, built with a pinned rustup toolchain (`DASH_RUST_TOOLCHAIN`):

- `dashsink2` from commit `DASHSINK2_COMMIT` (it is not in any 1.24 tag; it needs Rust ≥ 1.92), with the patches in `transcoder/patches/gst-plugins-rs/` applied in name order. See [40](40-cmaf-dash-output.md) for what each patch fixes.
- fmp4 from `FMP4_PLUGIN_TAG` (0.14.5) instead of the GStreamer tag (0.12.4): dashsink2 sets `cmafmux` `manual-split` and `send-force-keyunit`, which 0.12.4 lacks (dashsink2 panics). 0.14.5 is the newest release that builds against GStreamer 1.24 (later ones need 1.26 audio API); it keeps every `isofmp4mux` property the HLS fMP4 path sets. Current gst-plugins-rs as a whole does not build against 1.24.

## MXL plugins

`libgstmxl.so` (`mxlsink`, `mxlsrc`, from `gst-mxl-rs`) and `libgstavsynctest.so` come from the MXL v1.1.0 repository, built by the `mxl-builder` stage of `docker/utpl-base/Dockerfile` with a rustup toolchain (1.90.0; MXL needs ≥ 1.87) and installed in the GStreamer plugin directory. The plugin loads `libmxl.so` at run time from a path recorded at build time, so the image keeps a symlink at `/opt/mxl-src/build/Linux-Clang-Release/lib/libmxl.so`. Status: **Runtime verified** against GStreamer 1.24.2 by the SDK tests; no Nandi code uses these elements yet. Caps: `mxlsink` accepts `video/x-raw,format=v210`, `audio/x-raw,format=F32LE` (1–8 channels, interleaved, with a channel mask) and `meta/x-st-2038`; `mxlsrc` outputs the same. Details, findings and costs: [41](41-mxl-integration.md#phase-1-results).

## Vendor and hardware plugins

The source accepts encoder factory names in JSON. MainConcept, Visionular, VA-API, and GPU/NDI paths are selected at runtime through GStreamer discovery. `doc/misc/encoder-integration.md` describes candidate deployment steps for both external software encoders and GPU hardware acceleration, but cannot prove installed factories.

For every external plugin, record: package/SDK version, library/plugin path, environment variables, `gst-inspect` factory and caps, selected config, negotiated runtime caps, output media validation, and fallback behavior when unavailable.
