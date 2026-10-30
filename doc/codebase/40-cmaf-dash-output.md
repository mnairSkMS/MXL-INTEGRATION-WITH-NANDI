# CMAF DASH Output

A `dash+file://`, `dash+http://` or `dash+https://` output URI produces a live (or VOD) MPEG-DASH presentation with CMAF segments. Segmenting and the base MPD come from `dashsink2` (gst-plugins-rs); the transcoder uploads every object through the HLS uploader classes, keeps the live window on the origin, merges GROUP members into one MPD and adds SCTE-35 as an MPD `EventStream`.

DASH *input* is a separate feature; see [07](07-inputs-and-ingest.md).

## Graph

```
encodebin (x264enc/x265enc, fdkaacenc) ─► tee ─► queue ─┐            (SingleOutput, per stream)
                                                         ▼
DashOutputSink bin:  ghost pad ─► h264parse | h265parse | aacparse ─► dashsink2.video_%u / audio_%u
                                                                       └ per stream: cmafmux ─► appsink
dashsink2 get-init/segment/manifest-stream ─► UtplDashCaptureStream (in memory, hands bytes over on close)
                                          ─► dash::Presentation ─► hls::FileUploader | hls::HttpUploader
```

- `OutputSink::create` dispatches `Protocol::DASH` (`dash` URI scheme, mapped in `common/types.cpp`) to `OutputSink::createDash`.
- `DashOutputSink` (`output/sink/dash.{h,cpp}`) owns the parsers and `dashsink2`. It returns `needExternalSync() = true`, so a non-live source gets the output's `clocksync`; `dashsink2`'s own `sync` is turned off.
- Parsers are required: `dashsink2` (through `cmafmux`) takes `avc`/`hvc1` H.264/H.265 and raw AAC, not byte-stream.
- `dash::Presentation` (`output/dash/presentation.{h,cpp}`) is shared by every `DashOutputSink` of one output: one for a SINGLE output, one per member of a GROUP output.

## Segmenting and timing

`dashsink2` cuts every stream on one running-time grid of `segmentDuration`:

- For video it requests a key unit from the encoder at each boundary and splits `cmafmux` there. In-source verification: one IDR every 2.000 s, even with the encoder's default GOP.
- Audio is split on the same grid (patch 030), so audio segment N covers the same time as video segment N. Its duration varies by up to one AAC frame (2.005/1.984 s for 2 s at 48 kHz). A `segmentDuration` that is a whole number of video frames and AAC frames (multiples of 640 ms at 25 fps / 48 kHz, e.g. 3200) gives exact audio durations.
- Members of a GROUP output have their own encoders and `dashsink2`, but share the pipeline running time, so their segments line up (verified in the engine: identical keyframe times across 720p and 360p, one IDR per segment).
- The key unit request reaches the encoder through `encodebin`; `TranscodingFlowline` stops it from leaving `encodebin` upstream, where it used to reach the input's parser and cause a second IDR mid-segment in every rendition (see 10 and 27).
- Media time is the pipeline running time. The MPD uses `SegmentTemplate` with `$Number$`, `startNumber=0`, a fixed `duration` and `timescale=1000`; there is no `SegmentTimeline`.
- `availabilityStartTime` is the wall clock when the first segment completes (truncated to the second). Segment N is advertised from AST + (N+1)·duration; in runs it existed about 2 s before that. The presentation pins the first AST for the life of the output (GROUP members each stamp their own).

## Objects and names

| Object | Name (single output, no `baseFileName`) | GROUP member (`baseFileName: live`, modifier `_720p`) |
| --- | --- | --- |
| MPD | `manifest.mpd` | `live.mpd` |
| Init | `video_0_init.cmfi`, `audio_1_init.cmfi` | `live_variant_720p_video_0_init.cmfi` |
| Segments | `video_0_segment_<n>.cmfv`, `audio_1_segment_<n>.cmfa` | `live_variant_720p_audio_1_segment_<n>.cmfa` |

Representation ids get the same prefix. dashsink2 numbers pads across types (`video_0`, then `audio_1`).

- File uploads (`hls::FileUploader`, shared with HLS) are written to `<name>.tmp` and renamed into place, so a web server never serves a truncated MPD or playlist.
- Live (`vodMode: false`): `numSegments` is advertised as `timeShiftBufferDepth`; `numSegments + 2` segments per Representation are kept, older ones are removed through the uploader.
- On stop, dashsink2 rewrites its MPD as static. For a live output that MPD would list removed segments, so the presentation keeps the last dynamic MPD. With `vodMode: true` nothing is removed and the static MPD is published.
- The presentation publishes the merged MPD only when it differs from the last one, and for a GROUP only once every member has reported (or after three segments, so one stalled member cannot block it).

## MPD

The published MPD is dashsink2's, with:

- Representations from every member merged into one `AdaptationSet` per content type, ids and `SegmentTemplate` names prefixed.
- `timeShiftBufferDepth` (live), `suggestedPresentationDelay` (`presentationDelay`, default three segments), `minBufferTime` and `minimumUpdatePeriod` (via dashsink2 properties, defaults two and one segment), `UTCTiming` only when `utcTimingUrl` is set.
- Profile `urn:mpeg:dash:profile:isoff-live:2011` (static: `isoff-on-demand`). Segment brands are `cmf2`, `cmfc` (+ `cfhd` for HD video, `caac` for AAC).

## SCTE-35

With output `scte.mode: passthrough`, each section reaching the output becomes an `Event` in a Period-level `EventStream` (`schemeIdUri="urn:scte:scte35:2014:xml+bin"`, `timescale=1000`): `presentationTime` is the splice running time, `duration` the break duration of a splice_insert out, and the payload is `<Signal xmlns="http://www.scte.org/schemas/35/2016"><Binary>` with the section as base64. A GROUP output publishes each event once. Events that ended before the window are dropped.

Segments are not cut at the splice point (no equivalent of the HLS `split-at-running-time`), and in-band `emsg` is not written. See 27.

## dashsink2 patches

`dashsink2` exists only after the GStreamer 1.24 tags of gst-plugins-rs. The base image builds it from commit `be6a5cfc` with a rustup toolchain (Rust ≥ 1.92 needed), patched from `transcoder/patches/gst-plugins-rs/`:

| Patch | Why |
| --- | --- |
| 010 first handler wins | The `get-*-stream` signals had no accumulator; the default handler (a file under `mpd-root-path`) ran last and replaced the application's stream, which then received no data. |
| 020 final manifest, close streams | The stop-time MPD bypassed the signal and was written to disk. Streams were never closed, so a consumer could not tell when an object was complete. |
| 030 audio grid, `.cmfa`, bandwidth | Audio started with a stub fragment and drifted (1.984 s segments under a 2 s template); audio files used `.cmfv`; bandwidth divided by whole seconds (division by zero below 1 s). Only video pads request key units: from an audio pad the request reached the input's video parsers and produced a second IDR one frame after every boundary. |
| 040 repeated caps | Every caps event on a pad appended another `Representation`. A live RTMP source re-sent video caps 12 times in 85 minutes, so the MPD listed each video Representation 13 times (12 without `bandwidth`) and dash.js offered 39 video qualities. A repeated caps event now updates the existing Representation and keeps its measured bandwidth. Quick check without waiting for the live feed: `concat` a 360p and a 720p `videotestsrc` into `x264enc ! h264parse ! dashsink2`; upstream dashsink2 ends with two `video_0` Representations (stale 360p with bandwidth, 720p without), the patched one with a single 720p Representation that keeps its bandwidth. Rust unit tests in `manifest.rs` cover the same. |

`dashsink2` sets `cmafmux` properties (`manual-split`, `send-force-keyunit`) that fmp4 0.12.4 lacks; with that plugin it panics (`property 'send-force-keyunit' of type 'GstCMAFMux' not found`). The image therefore builds fmp4 from 0.14.5, which builds against GStreamer 1.24 and keeps every `isofmp4mux` property the HLS fMP4 path uses.

## Configuration and validation

See the `dash` transport in [05](05-configuration-reference.md) and `DashOutputTransportParameters` in `transcoder/config/doc/openapi.yaml`. Startup refuses (application.cpp):

- a container other than `fmp4` (GROUP members may omit it), passthrough video, more than one video stream, video other than h264/h265, audio other than aac, caption streams;
- a DASH transport next to another transport on the same output, a URI other than `dash+file`/`dash+http(s)`;
- `segmentDuration` below 500 ms, `numSegments` below 2 (parser).

GROUP outputs accept one HLS or DASH transport.

## Serving

The API serves `/runtime/dash` (`DASH_DIR`) at `/dash` with `no-store` and permissive CORS, like `/hls`. The channel form offers "DASH Live Manifest (CMAF)" (`dash+file:/runtime/dash`, forces fmp4 and AAC).

**Local channel.** `config/rtmp_to_dash_cmaf.json` pulls `rtmp://103.248.72.28:1935/live/Cook_Vook_Out` (H.264 1080p25 + AAC) and publishes a 1080p/720p/360p ladder (5000/3000/800 kbit/s H.264, 128/128/96 kbit/s AAC, GOP 3.2 s) as one GROUP MPD, `live.mpd`, with 3.2 s segments, a 20-segment window and 9.6 s suggested delay. `docker-compose.dash-out.yml` runs it with `docker-compose.playlist.yml` under project `skandha-dash-out`: API and `/dash/live.mpd` on `127.0.0.1:18080`, a read-only LAN server for `runtime/dash` on `:18082` (`hls_lan_server.py`, which also sets the DASH MIME types). Always pass both `-f` files. It shares host port 18080 with `docker-compose.dash.yml`, so run one of the two at a time. It needs an image built from `docker/` with the dashsink2 patches, tagged `skandha-transcoder:dash-out`.

## Verification

Evidence from 2026-10-27 (scratch build in `utpl/devel`, patched dashsink2 and fmp4 0.14.5 on the plugin path, live UDP MPEG-TS source):

- Unit: `utpl_dash_presentation_test` — manifest merge/prefixing, pinned AST, live attributes, EventStream, VOD, window removal, final static MPD ignored, GROUP publish gating, no `.tmp` left.
- SINGLE, 75 s, `dash+file`: 7 segments per Representation on disk (window 5 + 2), dynamic MPD with `timeShiftBufferDepth=PT10S`, `suggestedPresentationDelay=PT6S`, `UTCTiming`; video segments 2.000 s, audio on the same grid; one IDR per segment; 0 ffmpeg decode errors; engine exit 0.
- GROUP 720p + 360p, served over HTTP: one MPD with two video and two audio Representations; dash.js 4.7.4 in Firefox played live with no errors, switched 360p → 720p → 360p on request, live latency 6.2–6.3 s (two runs). `dashdemux2` (`uridecodebin3`) played it for 40 s: 938 frames at 25 fps, 0 dropped. Before file uploads became atomic, `dashdemux2` aborted on an empty MPD read mid-rewrite.
- H.265 (`x265enc`), `segmentDuration: 3200`: `codecs="hvc1.1.6.L93.90"`, brand `chhd`, video and audio segments exactly 3.200 s, 0 decode errors.
- Startup refuses `container: mpegts`, `ac3` audio, `segmentDuration: 100` and two video streams with the messages above.
- `transcoder/config/example/udp_ts_to_dash_cmaf.json` (GROUP, 3.2 s) runs and publishes four Representations.
- `vodMode: true`, 24 s run: every segment kept (10 per Representation) and a static `isoff-on-demand` MPD with `mediaPresentationDuration="PT20S"`. A stop cancels uploads still queued in `BaseUploader`, so the last segment or MPD update can be lost on a busy origin.
- Regression with fmp4 0.14.5: HLS MPEG-TS GROUP and HLS fMP4 outputs run and decode; HLS fMP4 segment lengths differ from 0.12.4 (both wrong, see 27).
- ffmpeg's DASH demuxer is not usable as a live check (27).

- HTTP upload: GROUP 720p + 360p to `dash+http://` with `transferMode: http_put`, `transferFraming: chunked` and Basic credentials, 40 s against a local PUT/DELETE origin: 146 PUTs (all chunked, authenticated, `application/dash+xml`, `video/mp4` or `audio/mp4`, none empty) and 44 DELETEs, leaving 7 segments per Representation; the uploaded segments decode with 0 errors and one IDR per segment at the same times in both renditions.
- SCTE-35 through the engine: a file input carrying splice_insert out (12 s), splice_insert in (20 s) and a time_signal with a segmentation descriptor (26 s), inserted with TSDuck `spliceinject` on PID 765, output `scte.mode: passthrough`. The MPD `EventStream` held three `Event`s at `presentationTime` 12000, 20000 and 26000 with the original sections (table `0xFC`, commands `0x05`, `0x05`, `0x06`). splice_null heartbeats are dropped silently (debug trace only).

- Runtime image: `docker/utpl-base`, `utpl-devel` and `utpl-transcoder` built in sequence (tag `dash-out`, 2026-10-27). The image carries `libgstdashsink2.so` and fmp4 0.14.5 in the system plugin directory, and its `/channel` bundle has the DASH option. With only packaged plugins, the SCTE file source run above gave 0 decode errors, three EventStream events and exit 0.

- Local channel (`config/rtmp_to_dash_cmaf.json` under `docker-compose.dash-out.yml`, live RTMP 1080p25 source, image with patches 010-040): MPD served with `application/dash+xml`, `no-store` and CORS by the API (`/dash/live.mpd`) and the LAN server (`:18082`); 6 Representations with measured bandwidths 4.76/2.86/0.77 Mbit/s video and 128/128/96 kbit/s audio; 22 segments per Representation; 0 decode errors; segment starts on the 3.2 s grid identical in all renditions (x264 adds scene-cut keyframes inside segments). dash.js 4.7.4 in Firefox: live, three video qualities, switched 360p to 1080p, latency 9.7 s, no errors. Before patch 040 the same channel listed 39 video Representations after 85 minutes; before the encoder-equality fix its three audio tracks were byte-identical 128 kbit/s.

Not verified: WebDAV (`http_dav`) and fixed-length framing, long soak, restart behaviour beyond the observation in 27, dash.js event callbacks for the EventStream, the DASH-IF conformance tool. A `file://` input that ends stops the engine with an x264 error (exit 10); that predates the DASH output and hits every output type (27).
