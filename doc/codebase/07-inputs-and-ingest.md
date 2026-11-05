# Inputs and Ingest

## Input selection

`Engine::prepare` selects the input class from URI count and prefix:

- One ordinary URI: `SingleInput`.
- One URI containing `pls+`: `PlaylistInput`.
- Two URIs: `StandardInput` for redundant/standard input.

The selection is source-backed in `lib/utpl/src/utpl/transcoder/engine.cpp`; application-level validation occurs first in `app/src/application.cpp`.

## `SingleInput`

`SingleInput` builds a source bin and uses `decodebin`/dynamic pads to expose streams. Reachable source construction includes:

| Protocol | Primary elements | Notes |
| --- | --- | --- |
| Local file | `filesrc` | URI/path handling is normalized by UTPL URI parsing. |
| UDP | `udpsrc ! queue` | Transport/caps behavior must be verified per media format. |
| SRT | `srtsrc ! queue` | Mode and latency come from transport parameters. The source queue is capped at 250 ms and the normal three-second live prefill is bypassed because SRT already provides latency-controlled retransmission and jitter recovery. |
| HTTP/HLS | `souphttpsrc`, `hlsdemux` | HLS uses callback logic and may rely on local patch 050. |
| MPEG-DASH (CMAF/fMP4) | `souphttpsrc`, `dashdemux2` | An `http(s)` URI with a `transport.dash` block or a path ending in `.mpd`. See below. |
| NDI | `ndisrc` | Source code is reachable; actual plugin/SDK availability is integration gated. |
| RTMP/RTMPS | `rtmp2src ! flvdemux ! queue ! parse ! mpegtsmux ! queue` | Pull client. Push is served by the bundled relay; see below. |

### RTMP and RTMPS

GStreamer ships an RTMP client (`rtmp2src`) but no RTMP server, so the engine only ever pulls. An
`rtmp://` or `rtmps://` URI is used verbatim (`rtmp://host[:port]/app/stream-key`); there is no
`transport.rtmp` block.

**Push (encoder publishes to the appliance).** The runtime image runs MediaMTX as the Supervisor
program `rtmp_relay` (`docker/utpl-transcoder/mediamtx.yml`, pinned by version and checksum in the
Dockerfile). It listens on `:1935` with every other MediaMTX protocol disabled. Publishing is
allowed from any address to `live/<key>` where the key is 16 or more URL-safe characters; reading is
allowed only from loopback. A push channel's input URI is therefore
`rtmp://127.0.0.1:1935/live/<key>`, and the encoder is given `rtmp://<host>:1935/live` plus the key.
The channel form generates a 24-byte random key and shows the server URL and key separately. RTMPS
ingest needs a mounted `mediamtx.yml` with `rtmpEncryption` and a certificate (port `1936`).

**Pull.** Any other `rtmp(s)://` URI makes the engine connect out to that server.

Examples: `transcoder/config/example/rtmp_push_to_hls_ts.json` (push through the relay to HLS) and
`rtmp_pull_to_srt.json` (pull `rtmps://` to an SRT listener); `config/rtmp_to_hls.json` is the
compose-deployment variant. Their `CHANGE_ME` key is shorter than the relay accepts, so an unedited
file cannot publish.

**Source bin.** FLV cannot be handed to the shared `decodebin`: its header comes once per
connection, so a reconnect would feed decodebin a second header mid-stream. The source bin remuxes
to MPEG-TS instead, and everything downstream treats the input like UDP/SRT:

- `flvdemux` pads feed a 10 s `queue` and `h264parse`/`aacparse`/`mpegaudioparse` into
  `mpegtsmux`. The queues decouple flvdemux's single streaming thread from the muxer, which holds
  audio until the first video keyframe; a publisher joined mid-GOP is up to one keyframe interval
  away from one. Other codecs are logged and not linked.
- Demuxed streams are held until `flvdemux` emits `no-more-pads` (3 s fallback for a single-stream
  publisher), because `mpegtsmux` announces only the streams it has at its first output.
- TS buffers leaving the bin are stamped with their arrival running time, as `udpsrc` and `srtsrc`
  stamp theirs; `tsdemux` needs that to ride over the PCR jump at a reconnect.
- The bin answers latency queries as live. `rtmp2src` reports itself non-live, and the live
  `fallbackswitch`/`compositor` in the video connector count no latency from a non-live branch
  (`tsdemux`'s 700 ms included). Without the live flag every decoded frame arrives after its
  deadline, `fallbackswitch` stays on its black fallback, and only passthrough audio reaches the
  output. Check picture content (for example `signalstats` YAVG), not only decode errors, when
  changing this path.

**Reconnect.** `rtmp2src` has its own bus. Its errors never reach the pipeline: a child ERROR during
the input bin's asynchronous READY→PAUSED (decodebin not yet prerolled) makes GstBin refuse to
commit the state, so the input would never unblock its streams. Errors and a clean unpublish (EOS)
instead schedule a reconnect that restarts only `rtmp2src` and `flvdemux` after the input's backoff,
capped at 5 s for RTMP (`RTMP_MAX_BACKOFF_TIME`). `mpegtsmux` keeps running, so the programme stays
one continuous transport stream. Each session is shifted onto the pipeline clock by one offset,
shared by its streams to keep A/V sync, so its first buffer lands at the current running time: an
outage is a gap, never a rewind. `idle-timeout` (5 s) turns a stalled publisher into a reconnect.

The channel starts whichever of engine and encoder comes up first, and resumes after abrupt drops,
clean unpublishes and outages longer than a minute. Only H.264 video with AAC or MP3 audio is
supported; see [27](27-known-gaps-contradictions-and-remediation.md).

## MPEG-DASH / CMAF

An `http(s)://` input is DASH when the transport has a `dash` block or the URI path ends in `.mpd`
(case-insensitive); anything else stays on the HLS branch. An MPD served under another name needs
the block, even empty. Fields: `bandwidth` (bits/s), `presentationDelay` (ms) and `scte.source`
(`segments` only); see [05](05-configuration-reference.md).

**Why `dashdemux2`.** Both DASH demuxers ship in the 1.24 image: legacy `dashdemux`
(`libgstdash.so`, plugins-bad) and `dashdemux2` (`libgstadaptivedemux2.so`, plugins-good). Against
the DASH-IF live simulator (`livesim2/periods_60`) the legacy element requested fragments too early
(HTTP 425) and produced no frames in 85 s, while `dashdemux2` crossed period boundaries with
continuous timestamps. DASH server-side ad insertion is built on multiple periods, so the legacy
element was rejected.

**Source bin.** `souphttpsrc` fetches the MPD and `dashdemux2` fetches segments itself. The
demuxer refuses to start ("Element requires a streams-aware context") unless its parent declares
it handles stream collections, so the input's source bin sets `GST_BIN_FLAG_STREAMS_AWARE`. No
`select-streams` event is sent, so the demuxer's default selection applies: one video and one audio
track, the MPD's main/default ones where marked. A subtitle track it selects is terminated on a
`fakesink`, because no subtitle stream type exists downstream. `dashdemux2` outputs parsed
elementary streams, which go through the same per-pad demuxer `decodebin` as HLS
(`SingleInput::_linkAdaptiveDemuxer`), so passthrough output works (verified with H.264).

**Pad discovery.** `dashdemux2` never emits `no-more-pads`. Once it has exposed the selected tracks
it posts `streams-selected`; the input counts its audio/video entries and treats the source as
complete when that many pads exist. Because the message arrives through the bus while the demuxer
may already be pushing, each DASH pad's demuxer is started as soon as the pad appears, not at
completion.

**Representation.** A fixed `connection-bitrate` (with `bandwidth-target-ratio` 1.0) replaces the
throughput estimate, so the highest representation within `bandwidth` is chosen once and never
switched; a switch would change resolution or codec under a passthrough output. With no
`bandwidth` the highest representation is used. Verified on a two-representation ladder: default
1920x1080, `bandwidth: 1000000` 640x360.

**Buffering.** Like SRT, DASH skips the generic three-second live prefill: `dashdemux2` already
holds the presentation delay (10 s unless `presentationDelay` or the MPD's
`suggestedPresentationDelay` says otherwise) and delivers whole segments in bursts, which made the
three-second level flap between underrun and overrun once per segment.

**SCTE-35.** The fMP4 demuxer inside `dashdemux2` discards `emsg` boxes ("Unknown fourcc while
parsing header : emsg"), and neither GStreamer DASH demuxer exposes an MPD `EventStream`. The input
therefore hooks `deep-element-added` for each track's internal `qtdemux` and scans the segment bytes
on its sink pad (`input/dash/emsg.{h,cpp}`): top-level boxes only, reset at each download (offset
0), `mdat` skipped without buffering. Boxes with a SCTE 214-1 binary scheme
(`urn:scte:scte35:2013:bin` and later `:bin` revisions) carry a complete `splice_info_section`.

- The `emsg` time is on the timeline `dashdemux2` stamps its output with (version 1:
  `presentation_time / timescale`; version 0: the segment's start plus the delta). Measured against
  the live simulator, `presentation_time / timescale` equals the output PTS of the splice frame.
  The demuxer output segment converts it to running time.
- The section's own splice time is on the source timeline (seconds since `availabilityStartTime`
  for live). It is shifted wholesale onto running time with `scheduler::sectionFromBase64`, as an
  operator-supplied section is; the muxer then adds its own PTS offset. Verified with TSDuck: an
  event for running time 40 s reached the output at PTS 3640.000 s (40 s + the 3600 s mux offset).
- An event repeated across segments is injected once (last 64 ids remembered). Only
  `splice_insert` and `time_signal` are accepted.

The `emsg` parser and scanner have a CTest suite, `utpl_dash_emsg_test`
(`lib/utpl/test/src/dashEmsg.cpp`), built on a box captured from the DASH-IF simulator: arbitrary
chunking, 64-bit box sizes, an `mdat` full of the bytes `emsg`, size-0 and oversized boxes, and
GStreamer accepting the payload as a `splice_insert`. A green run is not evidence for the media path;
verify output with TSDuck as above.

**Not supported.** MPD `EventStream` SCTE-35, encrypted (DRM/CENC) content, credentials,
low-latency chunked CMAF at low latency (a chunked stream plays, as whole segments), more than one
audio track (the channel uses the first audio and video stream of an input anyway; see 09).
A `.mpd` entry in a playlist reaches the same `SingleInput` branch but is untested. Example:
`transcoder/config/example/dash_to_ts.json`.

**Local test channel.** `config/dash_to_hls_srt.json` pulls `http://host.docker.internal:8090/live.mpd`
and writes HLS plus an SRT listener from one 720p24 encode. `docker-compose.dash.yml` runs it beside
the stack from `docker-compose.playlist.yml` under its own project name (`skandha-dash`), container
names and ports (API/operator `127.0.0.1:18080`, HLS `:18081`, SRT `:19092/udp`); always start it with
both `-f` files, because the base file's project name equals the main stack's. It needs
`skandha-transcoder:dash-dev` (the runtime image plus the DASH binaries) and an existing
`runtime/hls` directory (the HLS sink does not create it; see 27). A feed for it is any live DASH
publisher on host port 8090: ffmpeg looping an mp4 into `-f dash ... -use_timeline 0`. With the
default `SegmentTimeline` manifest ffmpeg writes, `dashdemux2` requested no segment in 40 s
(it scheduled the first one minutes ahead of the clock), so timeline-based publishers need testing
before relying on them.

## Setup-time input probing

`GET /inputs/probe` answers the channel creation screen's "Input Data" readout - resolution,
frame rate, bitrate, scan type, audio tracks and MPEG-TS PIDs - for a source the operator has
typed in but not yet committed. It does **not** go through the engine: `Input::probe()` reports
only a bool, the gRPC `InputStream` message carries id/uri/state, and neither bitrate nor PID
exists anywhere in the media graph. The API shells out to `ffprobe`
(`api/src/transcoder_api/services/probe.py`), which is installed in the runtime image.

The route opens the source itself, so it is for a channel that is **not yet running**. Probing a
single-subscriber source - an SRT caller target, or a UDP port the engine already holds - while
that channel is on air can steal or duplicate the subscription.

The client supplies both the URI and its `transport`, and the transport pins the URI scheme that
will be accepted: a mismatch is rejected rather than reinterpreted. That is the primary control
against being asked to dial arbitrary hosts or read arbitrary files.

| transport | accepted URI | handed to ffprobe | `-protocol_whitelist` |
| --- | --- | --- | --- |
| `file` | `file:///abs`, `/abs`, MEDIA_DIR-relative | bare path, confined to MEDIA_DIR | `file` |
| `playlist` | `pls+file://`, `file://`, path | **the first media entry**, not the M3U8 | `file` |
| `udp` | `udp://host:port` | `+ overrun_nonfatal`, `timeout`, optional `localaddr` | `udp` |
| `srt` | `srt://host:port?mode=` | normalized query, `transtype=live` | `srt` |
| `hls` | `http(s)://` | unchanged, userinfo rejected | `http,https,tcp,tls,crypto,hls` |
| `dash` | `http(s)://` | unchanged, userinfo rejected | `http,https,tcp,tls` |
| `ndi` | - | rejected: ffmpeg has no NDI input protocol | - |
| `rtmp` | `rtmp://` or `rtmps://` with a host and a path | unchanged, userinfo rejected; the stream key is redacted from error text | `rtmp,rtmps,tcp,tls` |

The HLS and DASH whitelists deliberately omit `file`. Without that, a hostile manifest can list
`file:///etc/passwd` as a segment and have the API read it back.

**Known limit: live DASH may time out.** ffmpeg's DASH demuxer requests the live-edge segment
without a presentation delay. A server that refuses segments before their availability time (the
DASH-IF live simulator answers HTTP 425) makes the probe retry until its timeout. VOD MPDs probe
normally, language tags included.

Three details are easy to get wrong and are covered by tests:

* **libsrt disagrees with itself about units.** `-connect_timeout` is milliseconds while
  `-listen_timeout` is microseconds. Separately, `transport.srt.latency` in the engine
  configuration is milliseconds while libsrt's `latency` option is microseconds, so the route
  reads the caller's `latency`/`rcvlatency`/`peerlatency` as milliseconds - matching the
  configuration vocabulary - and converts. The effective URI comes back as `probedUri`.
* **No `-f`.** Forcing `mpegts` makes a dead UDP port exit 0 with an empty stream list after
  about fourteen seconds, ignoring `-timeout`. Autodetection fails properly instead, and a
  successful exit carrying no streams is treated as a failure anyway.
* **PIDs are decoded only for MPEG-TS.** MP4 reports an `id` too, but those are track numbers.

`playlist` cannot be handed to ffprobe directly: the engine's playlist is a UTPL M3U8 - an
`#EXTINF:-1` schedule of `file://` entries - and ffprobe reports `Invalid data found` on it. The
route parses it and probes its first entry.

**Known limit: no bitrate for a live source.** ffmpeg derives `format.bit_rate` from size over
duration, and a live socket has neither, so the field is absent rather than zero for `udp`,
`srt` and `hls`. Measuring it would need a second pass with `-read_intervals`, which truncates
stream analysis badly enough to lose `width` and `field_order` - the fields an operator needs
more than bitrate. The response reports a null `bitrateBps` and says why in `warnings`.

Each decoded or passthrough stream has tee-based fan-out through `BaseInputStream`. Decoder autoplug sorting includes Dolby-related logic, so encoder/decoder selection should be tested with the actual installed plugin registry.

A live TS whose PMT changes a stream's type on the same PID (AAC ↔ E-AC-3) makes the demuxer `decodebin` drain the old pad group and expose new pads. The `onPadAdded` handler matches the new pad to the existing `SingleInputStream` by stream-id (upstream id plus PID) and calls `replacePads`. This rebinds the passthrough pads and rebuilds the inner decoder, and once the input is streaming it does not re-block the pads. Downstream connectors keep their links; the Dolby connector rebuilds its own decoder on the new caps ([11](11-audio-and-dolby-processing.md)). The demuxer `decodebin` queues are bounded at 2 s / 64 MB / 1000 buffers (`DEMUXER_QUEUE_MAX_*`) so that coarsely interleaved MP4 files do not stall one stream behind the other.

Live UDP and HTTP/HLS inputs retain the generic three-second startup/rebuffer threshold. SRT uses
the same per-stream multiqueue for thread and A/V decoupling, bounded to eight buffers, but does not
wait for that queue to fill before starting. This makes SRT startup/recovery follow the configured
`transport.srt.latency` rather than adding a fixed three seconds on top.

## `StandardInput`

`StandardInput` represents two input URIs and builds selector/fallback paths. The source uses `input-selector`, `clocksync`, and the Rust `fallbackswitch` element. It probes inputs and creates dummy audio/video streams when needed. This is not simply two `SingleInput` instances: it has its own state, switching, pad, and timing logic.

Runtime verification needs two independently controllable live sources, controlled loss/return, and output timestamp analysis. Until then, behavior such as precise failback timing is statically implemented rather than runtime verified.

## Discovery and error behavior

Discovery is callback-driven. `decodebin` pads, stream collection, and caps determine media type and downstream linkage. Input EOS and bus errors ultimately reach `Engine`, which stops the graph; source-specific reconnection behavior depends on element properties and must not be generalized across UDP/SRT/HLS.

## Input preview

Every input video stream carries a JPEG preview tap (`utpl/transcoder/preview.{h,cpp}`), so an operator can see a live feed without opening a second connection to the source. Many sources refuse one: the Jio SRT sender rejected reconnects during testing.

**In the media graph.** `BaseInputStream::linkSrcPad` adds the tap as one more branch on the stream's tee:
- It hangs off the decoded pad when the on-air path decodes the input (transcoded output), otherwise off the passthrough pad.
- The branch is `valve` (closed by default) → `identity` (keyframe filter) → leaky `queue` (120 buffers encoded, 3 raw) → `appsink`.
- These elements are simple and synchronous, so the branch can never stall the tee or post errors on the channel's bus. A tee link that fails is logged (`Input preview disabled for stream …`) and the stream carries on without a tap.

**Outside it.** When someone watches, the tap starts a separate small pipeline: `appsrc` → `decodebin` (encoded streams only, decoder threads capped at 2) → linear deinterlace → `videorate` (encoded only) → scale to 640 px wide → `jpegenc`.
- Buffers cross by reference, not copied.
- Decoded streams are thinned in the feed callback before the renderer touches them.
- The renderer writes `<UTPL_PREVIEW_DIR>/<input id>.jpg` atomically (default `/dev/shm/utpl-preview`, RAM).
- It is torn down when demand stops. Its own bus only logs errors, so a stream the preview cannot decode never affects on-air.

**Demand.** `PreviewHub` joins demand, keyed by input id, to taps, keyed by stream.
- gRPC `setInputPreview(inputId, mode, ttlMs)` renews demand (TTL clamped to 1–120 s, default 30 s) and binds the input's current video streams.
- `STILL` passes keyframes only, at most 2 frames/s. `LIVE` passes every frame, at most 10 frames/s.
- A tap closes itself on the first frame after its demand expires.
- Streams created later, such as the next playlist item, are bound by the next renewal, so the first request after an item change can miss.
- `UTPL_PREVIEW=off` removes the taps entirely; the media graph is then what it was before this feature.

**Measured (scratch build, 1080i25 H.264 Jio capture, 2026-10-24):**

| On-air video | No viewer | Stills polled every 2 s | One live viewer | Live rate |
|---|---|---|---|---|
| Passthrough (preview decodes) | +0 | no measurable change | +13–28% of a core, mostly H.264 decode | 11–14 fps |
| Transcoded (frames reused) | +0 | no measurable change | +6–24% of a core | 7–9 fps |

The first still after a quiet spell takes about 0.1–0.3 s, waiting for a keyframe. The on-air output (H.264, E-AC-3 5.1, SCTE-35) was unchanged with `ffprobe` during every preview mode, for an SRT/UDP live input and a two-item playlist. The REST endpoints are in [17](17-rest-grpc-and-sqlite-control-plane.md#input-preview-and-media-thumbnails).

## MXL source tap

`inputs[n].mxlTap` copies an input's **decoded** video and first audio stream into MXL flows (v210 and
Float32) for other processes. The branch sits on the same decoded-stream `tee` as the input preview
(`BaseInputStream::linkSrcPad`, `stream/input/base.cpp`), made only of a leaky `queue` and an `appsink`;
the conversion and `mxlsink` run in a side pipeline of their own (`MxlTap`, `mxltap.cpp`) that shares
the channel's clock and base time. A fault there (a missing plugin, a full or unusable domain, an
unsupported picture) stops only the tap, which retries every 5 s; the channel and its outputs are
unchanged (measured: same streams, no decode errors, same frame count). Single, redundant and playlist
inputs are tapped (for the last two the tap sits on the combined stream, so a failover or a change of
item does not break the flow); passthrough streams are not tapped. Settings: [05](05-configuration-reference.md#input-mxl-tap); design, results
and limits: [41](41-mxl-integration.md#source-tap-step-2).

## MXL input

An input whose URI is `mxl:///<domain directory>` reads MXL flows written by another process (another channel's
programme output or source tap, or any MXL writer on the host). `SingleInput::prepare` builds, for each flow named
in `transport.mxl`, an `mxlsrc`, a leaky `queue`, a conversion (video: `videoconvert` to I420; audio:
`audiobuffersplit` to 20 ms buffers) and a `decodebin` demuxer of its own, which exposes the raw pad; from there it is an
ordinary live input (`SingleInputStream`, tee, processors, `StandardInput` selection). It differs from the network
inputs in four ways: it is not prebuffered (the 3 s live prebuffer is skipped, as for SRT and DASH); a watchdog stops and
restarts a reader that has had no data for 3 s, because `mxlsrc` neither fails nor attaches to a new writer by itself;
one shift, the same for all flows of the input, puts the writer's timestamps on the channel's live time plus 300 ms; and
the reader's queue and a dropped ALLOCATION query keep `mxlsrc` from deadlocking a channel whose pads are blocked until it
plays. A flow that is not there yet is waited for. Settings: [05](05-configuration-reference.md#input-mxl); design,
results and limits: [41](41-mxl-integration.md#mxl-input-step-4).

## Constraints

The application rejects empty URI sets, more than two URIs, redundant file/playlist input, and playlist with passthrough. See [configuration](05-configuration-reference.md) and [playlist switching](08-playlists-buffering-and-switching.md).
