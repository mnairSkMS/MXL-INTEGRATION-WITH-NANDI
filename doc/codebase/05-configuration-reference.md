# Configuration Reference

This page documents the JSON contract consumed by the production executable. The authority order is the parser in `transcoder/app/src/application/config/parser/json/parser.cpp`, application validation in `transcoder/app/src/application.cpp`, parameter defaults in `transcoder/lib/utpl`, and finally the secondary schema and examples. A field appearing only in `transcoder/config/doc/openapi.yaml` is not supported unless the parser reaches it.

## Startup arguments

```text
transcoder -c <config.json> -l <license.json> [-b <events.sqlite>]
```

| Option | Required | Meaning |
| --- | --- | --- |
| `-c`, `--config` | Yes | Existing, non-directory JSON configuration path. |
| `-l`, `--license` | Yes | Existing, non-directory signed license path. Startup validates its signature and validity period. |
| `-b`, `--db` | No | Existing SQLite event database. When omitted, the REST/SQLite scheduled-event path is not attached to the engine. |
| `-h`, `--help` | No | Print command-line help. |
| `--license-info` | No | With `-l` only: print the license as one JSON object and exit 0 (valid), 1 (expired or not yet valid) or 2 (invalid). Needs no config. See [22](22-licensing-crypto-and-security.md#inspecting-a-license). |

An online-only license also requires `application.licenseServer.uri` with an explicit `http` or `https` scheme. The database file must already exist; the C++ application does not create it.

## Control-plane environment variables

These configure the Python API and the `preload_db` startup step, not the C++ engine.

| Variable | Default | Meaning |
| --- | --- | --- |
| `MEDIA_DIR` | `/media/queue` | Root for managed media. All relative paths are containment-checked against it. |
| `MEDIA_MAX_UPLOAD_BYTES` | `10737418240` (10 GiB) | Largest single file `POST /media` accepts. |
| `MEDIA_MAX_UPLOAD_FILES` | `20` | Most files one `POST /media` call accepts; more is `422` before anything is stored. |
| `MEDIA_THUMBNAIL_CONCURRENCY` | `2` | `GET /media/{id}/thumbnail` cuts running at once. |
| `PREVIEW_FIRST_FRAME_WAIT_SECONDS` | `4` | How long `GET /inputs/{id}/preview.jpg` waits for the first keyframe before answering `503`. |
| `PREVIEW_LIVE_MAX_SECONDS` | `1800` | Longest single `GET /inputs/{id}/preview.mjpeg` stream. |
| `UTPL_PREVIEW` (engine) | on | `off` or `0` removes the input preview taps from the media graph; `setInputPreview` then answers `UNAVAILABLE`. |
| `UTPL_PREVIEW_DIR` (engine) | `/dev/shm/utpl-preview` | Where the engine writes preview JPEGs. Must be readable by the API. |
| `DB_PATH` | `./events.sqlite` | SQLite database shared by the API and the engine's `-b` argument. |
| `MANAGED_PLAYLIST_ID` | `main` | Managed playlist and default channel id. |
| `MANAGED_PLAYLIST_FILE` | `/runtime/main.m3u8` | M3U8 materialized on every start; the engine boots from it. |
| `PLAYLIST_SEED_FILE` | `/media/queue/playlist.m3u8` | Legacy one-time M3U8 seed. Used only on a database that has never had an active input and has no applied bootstrap file. |
| `PLAYLIST_BOOTSTRAP_FILES` | unset | Declarative channel files, comma separated, **in the order the transcoder configuration lists its inputs**: the first drives the primary input, the second the secondary, and so on. Each file declares one channel, which is one managed playlist. A runtime with ad insertion lists two (`channel.yml,ads.yml`); a plain channel lists one. See [34](34-scte-ad-insertion.md). |
| `PLAYLIST_BOOTSTRAP_FILE` | unset | Single-file spelling of the above, still supported and treated as a list of one. Declarative channel file. When set, it is the source of truth for the playlist and its graphics and is reconciled into SQLite on every start. Unset leaves the feature inert. Requires `ENABLE_RUNDOWN_SERVICE`. Its **directory** must be bind-mounted read-write; a single-file mount is inode-bound and breaks the atomic rewrite. See [30](30-transactional-rundown-service.md). |
| `PLAYLIST_BOOTSTRAP_WRITEBACK` | `true` | Whether Publish rewrites the channel file so the change survives a restart. `false` makes the file strictly one-way: publishes go on air but the next start reverts them. Publish refuses with `409`, before the queue moves, when writeback is on and the file cannot be written. |
| `PLAYLIST_BOOTSTRAP_STRICT` | `false` | `true` exits `preload_db` non-zero when the channel file is rejected. Either way the database is left untouched and the engine still starts on the previously saved playlist. |
| `ENABLE_RUNDOWN_SERVICE` | `false` in code, `true` in `docker-compose.playlist.yml` | Creates the additive rundown tables (after a `*.before-rundown-v1` backup), exposes `/rundowns/*`, and runs the 250 ms observation loop. Read at import time, so a change requires an API restart. |
| `ENABLE_VIDEO_GRAPHICS` | `false` | Enables `mediaType: video` graphics catalog entries. While off, they are listed as `enabled: false` and rejected by both the API and the channel file. |
| `GRAPHICS_CATALOG_PATH` | packaged `assets/graphics_catalog.json` | Overrides the graphics catalog location. |
| `TRANSCODER_CONFIG_PATH` | `/opt/utpl/etc/config.json` | Engine config read by `GET/PUT /config`; `supervisord.conf` passes the same value to the engine's `-c`. |
| `TRANSCODER_LICENSE_PATH` | `/opt/utpl/etc/transcoder.lic` | Engine license managed by `GET/PUT/DELETE /license`; `supervisord.conf` passes the same value to the engine's `-l`. Must sit in a read-write mounted directory for upload and delete to work. Compose sets `/runtime/license/transcoder.lic`. |
| `TRANSCODER_BINARY` | `/opt/utpl/bin/transcoder` | Engine binary the license routes run with `--license-info`, and the path `GET /stats` identifies the engine process by. |
| `STATS_SAMPLE_INTERVAL_SECONDS` | `2` | How often the `GET /stats` sampler reads the kernel counters; floored at 0.5. |
| `STATS_STORAGE_PATHS` | derived | Comma-separated paths whose filesystems `GET /stats` reports. By default `HLS_DIR`, `MEDIA_DIR` and the directories of `DB_PATH`, `TRANSCODER_CONFIG_PATH` and `TRANSCODER_LICENSE_PATH`. |
| `INIT_DATA` | `/opt/utpl/events.json` | Legacy `events.json` seed for `preload_db`. The file is not shipped in the Compose image, so this branch only logs a warning. |

## Format versions and parser behavior

The parser accepts semantic versions from `1.0.0` through `1.4.2`, inclusive.

| Version range | Input/output layout |
| --- | --- |
| `< 1.1.0` | Input uses `inputs[0].uri`; each output uses `output.uri`. |
| `>= 1.1.0` | Input uses `inputs[0].transport`; outputs use `transports[]`. |
| `>= 1.2.0` | `input.transport.uri` may be a string or an array. |
| `>= 1.4.0` | Parser permits missing output `transports` and `container`, enabling a grouped `single` output to inherit group delivery. Application validation still rejects invalid combinations. |

Important parser characteristics:

- `version` and `transcoder` are effectively required. `transcoder.inputs` must contain at least one entry and `transcoder.outputs` is iterated as an array.
- Only `transcoder.inputs[0]` is consumed. Additional input objects are ignored; redundancy is expressed as two URIs in that first input.
- Unknown object keys are generally ignored because no JSON Schema validation is run at startup.
- Some unsupported enum strings only log an error and leave a default value; other errors throw and abort startup. Treat a clean parse plus meaningful runtime output as the acceptance test.
- Names and enum values are case-sensitive unless a row below explicitly says they are lowercased.
- Timestamps and durations are milliseconds unless a field says seconds or 90 kHz ticks.

## Minimal current-format example

```json
{
  "version": "1.4.2",
  "application": {
    "logger": {
      "transcoder": {
        "level": "log",
        "console": { "enable": true, "colors": false, "stderr": false }
      }
    }
  },
  "transcoder": {
    "inputs": [
      {
        "transport": {
          "uri": "file:///media/input.ts",
          "file": {}
        }
      }
    ],
    "outputs": [
      {
        "name": "main",
        "type": "single",
        "transports": [
          { "uri": "file:///media/output.ts", "file": {} }
        ],
        "container": "mpegts",
        "streams": [
          { "type": "audio", "codec": "passthrough", "audio": {} },
          { "type": "video", "codec": "passthrough", "video": {} }
        ]
      }
    ]
  }
}
```

This example proves the current nesting, not that every input can be passed through. Actual codec/container compatibility must be checked against the input media and installed GStreamer elements.

## Top-level and application settings

| JSON path | Type | Default/required | Production behavior |
| --- | --- | --- | --- |
| `version` | string | Required | SemVer in the accepted range above. |
| `application` | object | Optional | Logger, license-server, and GStreamer feature-rank settings. |
| `application.licenseServer.uri` | string | Empty | Required for a non-offline license; must parse as HTTP(S). |
| `application.logger.transcoder` | logger object | Disabled | Controls UTPL/root application logging. |
| `application.logger.gstreamer` | logger object | Disabled | Controls GStreamer log forwarding unless `GST_DEBUG` is already set. |
| `application.gstreamer.plugins` | array | Empty | Changes ranks of named GStreamer plugin features before graph creation. |
| `transcoder` | object | Required | Media graph, events, and SCTE policy. |

### Logger object

| Field | Accepted values/default | Notes |
| --- | --- | --- |
| `level` | `trace`, `debug`, `log`, `warning`, `error`, `none`; default `none` | Exact lowercase values. The secondary schema advertises `fatal`, but the parser does not accept it. Some examples use `warn`, which is also not accepted. |
| `console.enable` | boolean; default `false` | Enables the console sink. |
| `console.colors` | boolean; default `false` | ANSI colors. |
| `console.stderr` | boolean; default `false` | `true` selects stderr; otherwise stdout. |
| `file.enable` | boolean; default `false` | Enables file output. |
| `file.colors` | boolean; default `false` | Color sequences in the file. |
| `file.path` | string; empty | A non-empty path is required when the file sink is enabled. |

Each configured console/file object creates one output. Omitting an output leaves it absent rather than implicitly enabled.

### GStreamer feature ranks

Each `application.gstreamer.plugins[]` item requires:

| Field | Accepted values |
| --- | --- |
| `name` | GStreamer plugin-feature/factory name. |
| `rank` | `NONE`, `MARGINAL`, `SECONDARY`, or `PRIMARY`. Invalid values abort configuration. |

Use `gst-inspect-1.0 <name>` in the deployed environment before relying on a rank override. A rank does not install a missing plugin.

## Input configuration

Current format:

```json
{
  "transport": {
    "uri": ["srt://primary:9000", "srt://backup:9000"],
    "srt": { "mode": "caller", "latency": 125, "bufferSize": 0 }
  },
  "selector": { "program": 1 }
}
```

| JSON path | Type | Default/required | Behavior |
| --- | --- | --- | --- |
| `transcoder.inputs` | array | At least one entry | Only element zero is parsed. |
| `inputs[0].transport` | object | Required for `>=1.1.0` | Holds URI and protocol parameter objects. |
| `transport.uri` | string or string array | Required/non-empty | One URI selects `SingleInput` or `PlaylistInput`; two select redundant `StandardInput`. More than two are rejected. |
| `inputs[0].selector.program` | integer | Unset | Selects an MPEG-TS program number during discovery. |
| `inputs[0].mxlTap` | object | Absent (no tap) | Copies the decoded source to MXL flows. See [Input MXL tap](#input-mxl-tap) and [41](41-mxl-integration.md). |

### URI schemes and input status

| URI form | Production path | Status/constraints |
| --- | --- | --- |
| path or `file://...` | `filesrc` | One URI only. |
| `udp://host:port` | `udpsrc` | Live UDP; multicast interfaces may be supplied. |
| `srt://host:port` | `srtsrc` | Caller/listener mode may come from JSON transport parameters. |
| `http://...` or `https://...` with a `transport.dash` block or a path ending in `.mpd` | `souphttpsrc` + `dashdemux2` | MPEG-DASH (CMAF/fMP4 or TS segments). See [07](07-inputs-and-ingest.md#mpeg-dash--cmaf). |
| any other `http://...` or `https://...` | HTTP/HLS discovery | Uses HTTP source and HLS-oriented handling where applicable. |
| `hls+file://...`, `hls+http://...` | Explicit HLS with subprotocol | HLS transport parameters apply. |
| `pls+file://...` (or a local path resolved through the file subprotocol) | `PlaylistInput` | The outer playlist document is file-only in current runtime source; exactly one URI; cannot use audio/video passthrough. |
| `ndi://...` | `ndisrc` | Integration gated by NDI SDK/plugin availability. |
| `rtmp://host[:port]/app/key`, `rtmps://...` | `rtmp2src`, remuxed to MPEG-TS | Live. No transport block. `rtmp://127.0.0.1:1935/live/<key>` reads from the bundled push relay. |
| `mxl:///<domain directory>` | one `mxlsrc` per flow | Live. Needs a `transport.mxl` block that names the flows; exactly one URI. See [Input MXL](#input-mxl). |

Two live URIs enable redundancy/fallback and require compatible live protocols. File and playlist redundancy are rejected. The runtime also needs the `fallbackswitch` element.

### Input MXL tap

`inputs[n].mxlTap` copies an input's **decoded** media into [MXL](41-mxl-integration.md) flows that other processes can read. It is **Runtime verified** (fourteen tests, recorded in [41](41-mxl-integration.md#source-tap-step-2)). Parser: `parser.cpp` (the `mxlTap` block, next to `role` and `offAir`); parameters: `MxlTapParameters` in `input.h`; media graph: `stream/input/base.cpp` (`_createMxlTap`, `linkSrcPad`) and `mxltap.cpp`.

```json
"mxlTap": {
  "domain": "/run/mxl",
  "video": { "flowId": "5fbec3b1-1b0f-417d-9059-8b94a47197ed" },
  "audio": { "flowId": "b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10", "channels": 2 }
}
```

| JSON path | Type | Default/required | Behavior |
| --- | --- | --- | --- |
| `mxlTap.domain` | string | Required | MXL domain directory (a tmpfs shared with readers; `docker-compose.yml` mounts one at `/run/mxl`). |
| `mxlTap.video.flowId` | UUID string | Optional | Writes the first decoded video stream as v210 at the source's size and frame rate. Interlaced pictures are reduced to one field per frame. A change of size or rate makes a new flow. |
| `mxlTap.audio.flowId` | UUID string | Optional | Writes the first decoded audio stream as Float32, 48 kHz. |
| `mxlTap.audio.channels` | integer 1–8 | `2` | Channels of the audio flow; the source is mixed to this count. |

At least one of `video` and `audio` is required, the two ids must differ, and ids must be canonical UUIDs; otherwise the configuration is rejected at start. One writer per flow: a second stream asking for a flow already in use is skipped with a warning.

Where the tap sits for each kind of input:

| Input | Tap on | Behaviour |
| --- | --- | --- |
| Single URI | The input's own decoded video and first audio stream | As above. |
| Two redundant URIs | The stream **after the switch** between the two sources | One flow, whichever source is on air. A failover does not restart the tap (test `09`). Neither source is tapped alone. |
| Playlist (`pls+`) | The playlist's own video and audio streams, which carry each item in turn | One flow for the whole playlist, kept through every change of item (test `10`). The items themselves are not tapped. |

Constraints: passthrough streams are not tapped (they are still compressed); only the first audio stream is tapped. A channel with a second input (used only as the alternate source of SCTE ad insertion) was **not tested** with a tap on either input; what a tap does on an input the channel is not using has not been measured.

Timing and faults:

- A flow carries the engine's **running time** of each frame, so it lines up with what the same frame does on air. A source that is held back before it reaches the tap (a playlist item read from a file arrives about 0.7 s early) is handled by a queue of about a second of video in the tap's side pipeline.
- The writer needs strictly increasing frame slots. The tap puts video on an exact frame grid so that jitter or a cut-off source never makes two frames share a slot; a frame that is more than one slot early is moved to the next slot, one that is two or more early is dropped, and a timeline that steps back by over a second restarts the flow.
- A flow whose stream delivers nothing for 3 s is closed (and opened again when data returns), so a flow that is not written never looks alive. A gap of 1.5 s does not close it (test `14`).
- **A flow has one writer.** The engine refuses to start when two writers (taps, or a tap and an `mxl` programme output) in one configuration name the same flow, or when a channel writes a flow its own MXL input reads. If a *process* outside this engine already writes a flow when the tap is created, the tap is switched off with a warning and the channel carries on without it. The check is made once, at creation. See [41](41-mxl-integration.md#flow-registry-step-5-wp4).
- The tap is **fail-open**: it is a leaky side branch with its own pipeline, so a missing `mxlsink`, a full or missing domain, or an unsupported picture stops only the tap, which retries every 5 s. The channel keeps running and its outputs are unchanged.
- By the plugin's source, an MXL reader (`mxlsrc`) returns an error if it reads a slot that was skipped (it is flagged invalid), so a reader should be ready to start again after a gap in the source. This was read, not measured.

### Input MXL

An input whose URI is `mxl:///<domain directory>` reads [MXL](41-mxl-integration.md) flows that another process writes (another channel's programme output or source tap, or any MXL writer on the same host). It is **Runtime verified** (ten tests, recorded in [41](41-mxl-integration.md#mxl-input-step-4)). Parser: `parser.cpp` (`_parseMxlInput`, next to the NDI block); parameters: `MxlInputTransportParameters` in `input.h`; media graph: `SingleInput::_createMxlFlows` in `input/single.cpp`.

```json
"transport": {
  "uri": "mxl:///run/mxl",
  "mxl": {
    "video": { "flowId": "5fbec3b1-1b0f-417d-9059-8b94a47197ed" },
    "audio": { "flowId": "b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10" }
  }
}
```

| JSON path | Type | Default/required | Behavior |
| --- | --- | --- | --- |
| `transport.uri` | `mxl:///<directory>` | Required | The MXL domain: a directory (a tmpfs the writers share; `docker-compose.yml` mounts one at `/run/mxl`). Exactly one URI. A name after `mxl://` (`mxl://studio-a/...`) means a domain on another host, which needs MXL Fabrics: refused. |
| `transport.mxl.video.flowId` | UUID string | Optional | The video flow (v210). The channel gets the picture at the flow's size and frame rate, as I420. |
| `transport.mxl.audio.flowId` | UUID string | Optional | The audio flow (Float32, 48 kHz). The channel gets the flow's channels. |
| `transport.mxl.delayMs` | integer 0–2000 | Optional, 500 | Milliseconds the channel plays the flows after they arrive. It must cover how far the writer's lag moves and the difference between its picture and its sound (160 to 180 ms for a Nandi programme output); a buffer that arrives later than it allows is late and lost, and the engine warns and names this setting. The input learns the constant difference between the flows at start and relearns when buffers stay late for 3 s, so the delay only has to cover the movement (measured 4 to 52 ms at 720p, 669 ms once at 1080p). Lower it to cut latency, raise it if the warning appears. It adds to the input's end-to-end delay. See [41](41-mxl-integration.md#the-delay-of-an-mxl-input). |

At least one flow is required, the two ids must differ, and ids must be canonical UUIDs; a wrong block is refused when the engine starts, with a message that names the key. An `mxlTap` on the same input must not write a flow that the input reads. A playlist cannot hold MXL items (an item has no `mxl` block), and a redundant (two-URI) MXL input is refused.

How it behaves:

- **Live, and not prebuffered.** The flows are written in real time, so the input does not wait for a buffer to fill (the 3 s live prebuffer of UDP inputs is not used). A queue of about a second of media (3 frames of video, 1 s of audio) decouples the readers from the channel.
- **Waits for its flows.** Started before the writer, the channel waits (as it does for a UDP source that is not sending yet) and starts when the flows appear (test `04`).
- **A writer that goes away is noticed by the input, not by the reader.** An `mxlsrc` neither fails nor ends when its writer dies, and it never attaches to a new writer by itself ([41](41-mxl-integration.md#phase-1-results), test 07). So a flow that delivers nothing for 3 s has its reader stopped and started again; this repeats every few seconds until data returns, and logs once at info level (`no data on the video flow ... reattaching`). While nothing arrives, the channel behaves as for any live input that has stopped (its fallback picture and silence). A writer that comes back with another picture size is followed after the reattach (test `08`).
- **Time.** A flow's buffers carry the time at which the writer made them, which is in the past by whatever the writer's own pipeline held them for (a programme taken behind a second of processing arrives a second old). The first buffer to arrive sets one shift that puts the flows on the channel's live time plus `delayMs`, the same for every flow of the input so that sound and picture stay as the writer had them. The shift is set again after the writer has been gone and comes back. Lip-sync through a loopback: [41](41-mxl-integration.md#mxl-input-step-4).
- **Picture.** v210 is converted to I420 once, at the input (about 1.4 ms per 1080p frame for the whole route to the canvas, against 7.7 ms for v210 straight to BGRA). The conversion halves the chroma height of the 4:2:2 flow and keeps 8 of its 10 bits, as every decoded H.264 source is.
- **Not supported:** interlaced flows (MXL flows are progressive), data (ancillary) flows, more than one audio flow, a redundant or playlist MXL input, an MXL stream in passthrough (there is nothing compressed to pass), SCTE-35 (it does not travel in a flow; decision Q7 is open).

### Input transport objects

| Object/field | Type | Default | Notes |
| --- | --- | --- | --- |
| `transport.srt.mode` | `caller` or `listener` | `UNKNOWN` | Value is lowercased. An unsupported string logs and retains the default. |
| `transport.srt.latency` | integer ms | `125` | Used as SRT receive latency. |
| `transport.srt.bufferSize` | non-negative integer bytes | `0` | Used for the receive socket buffer when nonzero. |
| `transport.udp.multicast.interfaces` | string array | Empty | Interfaces on which to join the multicast group. A wrong node type logs and is ignored. |
| `transport.hls.bandwith` | integer bits/s | `4294967000` | The live parser key is intentionally misspelled `bandwith`. Parameter bounds are 1,000–4,294,967,000, but startup does not explicitly enforce them. |
| `transport.hls.credentials.username` | string | Empty | HTTP credentials. |
| `transport.hls.credentials.password` | string | Empty | Secret; avoid logging or committing it. |
| `transport.hls.scte.source` | `segments` or `manifest` | `segments` | Value is lowercased. Selects in-segment versus manifest SCTE acquisition. |
| `transport.dash` | object | Absent | Present (even empty) forces DASH for an `http(s)` URI whose path does not end in `.mpd`. |
| `transport.dash.bandwidth` | integer bits/s | `4294967295` | The highest representation not above it is used for the whole session; no throughput-based switching. Outside 1,000–4,294,967,295 is rejected at parse time. |
| `transport.dash.presentationDelay` | integer ms | Absent (10 s) | Distance behind the live edge, used when the MPD has no `suggestedPresentationDelay`. |
| `transport.dash.scte.source` | `segments` | `segments` | In-band SCTE 214-1 `emsg` boxes. Any other value, including `manifest`, is rejected at parse time: MPD `EventStream` is not ingested. |
| `transport.file` | object | Absent | Marker object registering file transport parameters. |
| `transport.playlist` | object | Absent | Marker object for playlist input. |
| `transport.ndi.ndiName` | string | Empty | NDI source name. Integration gated. |
| `transport.ndi.urlAddress` | string | Empty | NDI URL/address. Integration gated. |

The secondary schema spells HLS `bandwidth`, omits playlist/NDI, and therefore does not represent the live parser exactly.

`transport.srt.latency` remains the SRT retransmission/jitter-recovery budget. The runtime does not
add the generic three-second live-input prefill to SRT: it uses a 250 ms source queue and an
eight-buffer per-stream multiqueue for graph decoupling, neither of which is prefilled before start.

### Runtime-verified playlist input

The verified playlist is an M3U8 document selected by a JSON input object; the playlist file itself is not JSON:

```json
{
  "transport": {
    "uri": "pls+file:///media/playlist.m3u8",
    "playlist": {}
  }
}
```

```m3u8
#EXTM3U
#EXTINF:-1,
file:///media/input-a.ts
#EXTINF:-1,
file:///media/input-b.ts
```

This shape loads the local H.264/MPEG-TS items. `GET /inputs` returns one logical input whose `streams` array has one entry per M3U8 item, and selecting a stream UUID changes decoded output. Audio/video passthrough is invalid with `PlaylistInput`, so the output must name actual transcode codecs/encoders.

Each `transcoder.inputs` entry becomes an engine input, in order. The first is the programme source; a second entry is used only as the alternate source of SCTE ad insertion (`role: secondary`, see [34](34-scte-ad-insertion.md)), and at most one input may be live. Do not add a second entry to extend a playlist: configure further playlist items in the M3U8 file.

The handwritten `/config/set-input*` routes are not a working deployment path in this revision. Set the JSON before startup (or replace it through a controlled external deployment process), validate it, and then start/restart the transcoder. See [17](17-rest-grpc-and-sqlite-control-plane.md) and [28](28-playlist-input-api-runtime-verification.md).

## Output configuration

Every output has a logical type, optional unique name, delivery transports, container, SCTE behavior, and streams.

| Field | Type/default | Behavior |
| --- | --- | --- |
| `name` | string; empty | Non-empty names must be unique. Required for `group` and for any single output referenced by a link. |
| `type` | `single`, `group` or `mxl`; parser default `single` | Value is lowercased. Unknown values eventually fail application validation. `mxl` writes the programme to MXL flows; see [Output MXL](#output-mxl). |
| `transports` | array | Required for standalone single and group outputs. A grouped single may omit it in format `>=1.4.0`. |
| `container` | `mpegts`, `es`, or `fmp4` | Required for a standalone single. Group inheritance is available in `>=1.4.0`; actual fMP4 use is HLS-specific. Values are case-sensitive. |
| `streams` | non-empty array | Always required by the parser. |
| `scte` | object | Optional SCTE PID/mode/null injection settings. |

### Output transports

Each transport requires a non-empty `uri`. Sink selection is driven by the URI scheme; adding a parameter object alone does not change the sink.

| URI/object | Fields and defaults | Behavior |
| --- | --- | --- |
| path, `file://`; `file: {}` | No fields | File sink. |
| `udp://`; `udp.multicast.interface` | string, empty | Outgoing multicast interface. |
| `udp.multicast.ttl` | integer, default `1` | Zero logs and resets to 1; parsed as `uint8_t`. |
| `udp.unicast.ttl` | integer, default `64` | Zero logs and resets to 64; parsed as `uint8_t`. |
| `srt://`; `srt.mode` | `caller`/`listener`, default unknown | Used by SRT sink; value lowercased. |
| `srt.latency` | integer ms, default `0` in the constructed output object | SRT peer latency. Note the class constant says 125 but the default constructor initializes 0. |
| `srt.bufferSize` | non-negative integer bytes, default `0` | Send socket buffer. |
| `s3://`; `s3.bucket` | string | S3 sink path; integration gated. |
| `s3.accessKey`, `s3.secretAccessKey`, `s3.region`, `s3.keyPrefix` | strings | Credentials and object prefix. Keep secrets out of logs and source control. |
| `http(s)://` or `hls+...`; `hls` | object | Internal HLS segmenter/uploader. See below. |
| `dash+file://`, `dash+http(s)://`; `dash` | object | CMAF DASH output (dashsink2). Defaults apply without the object. See below and [40](40-cmaf-dash-output.md). |

The parser creates HLS parameters only when an `hls` object exists. For a plain HTTP URI without that object, the HLS sink is still selected but no HLS parameter block is attached; this pairing requires runtime verification.

The output SRT peer latency is separate from the sink's 250 ms downstream-leaky graph queue.
Listener mode drops media while no client is connected, so a late monitor starts near live instead
of pinning the first old buffer. Caller mode retains the existing wait-for-connection behavior.

### HLS output fields

| `hls` field | Type | Default | Behavior |
| --- | --- | --- | --- |
| `numSegments` | integer | `10` | Manifest window; application minimum 1. |
| `segmentLength` | integer seconds | `10` | Converted to milliseconds; application minimum 1 second. |
| `singleDirectory` | boolean | `true` | Keep playlists and segments in one directory when supported by uploader. |
| `vodMode` | boolean | `false` | Enables VOD-oriented behavior in the HLS sink/streamer path. |
| `transferMode` | `http_put` or `http_dav` | `http_put` | Parser does not accept schema-advertised `file`; file behavior is selected by URI/subprotocol. |
| `transferFraming` | `chunked` or `fixed` | `fixed` | HTTP upload framing. |
| `credentials.username/password` | strings | Empty | HTTP credentials. |
| `timeout` | integer ms | `2000` | Upload timeout. |
| `retry.count` | integer-like size | `0` | Retry count; uploader defines zero as none. The schema mentions `-1` for unlimited, but parser conversion to an unsigned size makes this unsafe/unproven. |
| `retry.interval` | integer ms | `1000` | Delay between retries. |
| `adMarkers` | string array | Empty | `elemental` and `enhanced` are compiled in; `adobe` is not implemented and warns as unsupported. `enhanced` adds `EXT-OATCLS-SCTE35` carrying the section as base64, plus `SCTE35=` on `EXT-X-CUE-OUT-CONT`. A break with `auto_return` false is signalled with a bare `EXT-X-CUE-OUT` and continuations without `Duration`; `elemental` emits no continuation for one. |
| `authentication` | `basic` or `digest` | `basic` | Exact lowercase. Other strings silently retain the current default. |
| `baseFileName` | string | Empty | Overrides master/variant/segment base naming. |

HLS output rejects passthrough video. MPEG-TS and fMP4 HLS paths are present, but transfer mode, endpoint capabilities, patched GStreamer elements, and resulting manifests must be runtime validated.

### DASH output fields

The output `dash` object is unrelated to the input `transport.dash` block. A `dash+...` URI gets these defaults even without the object.

| `dash` field | Type | Default | Behavior |
| --- | --- | --- | --- |
| `segmentDuration` | integer ms | `2000` | Segment grid for every stream; parser minimum 500. Use a whole number of video and AAC frames (multiples of 640 ms at 25 fps / 48 kHz) for exact audio durations. |
| `numSegments` | integer | `30` | Live window, written as `timeShiftBufferDepth`; `numSegments + 2` kept on the origin. Parser minimum 2. |
| `vodMode` | boolean | `false` | Keep all segments; publish the static MPD on stop. |
| `presentationDelay` | integer ms | `0` (three segments) | `suggestedPresentationDelay`. |
| `minBufferTime` | integer ms | `0` (two segments) | MPD `minBufferTime`. |
| `minimumUpdatePeriod` | integer ms | `0` (one segment) | MPD `minimumUpdatePeriod`. |
| `utcTimingUrl` | string | Empty | `UTCTiming` (http-xsdate); omitted when empty. |
| `baseFileName` | string | Empty | MPD `<base>.mpd` (else `manifest.mpd`); object and Representation-id prefix. |
| `transferMode`, `transferFraming`, `credentials`, `authentication`, `timeout`, `retry.count`, `retry.interval` | as HLS | as HLS | Same uploader classes as HLS. Unknown `transferMode`/`transferFraming`/`authentication` values are refused (HLS only logs them). |

Application validation: container `fmp4` (GROUP members may omit it), at most one video stream (h264/h265), aac audio, no caption streams, no passthrough video, DASH as the output's only transport. A GROUP output accepts one HLS or DASH transport.

## Stream definitions

### Audio/video streams

| Field | Values/default | Notes |
| --- | --- | --- |
| `type` | `audio` or `video` | Lowercased; required. `caption` is not accepted by this JSON parser even though internal caption paths exist. |
| `codec` | `passthrough`, `es`, `mp1`, `mp2`, `mp3`, `aac`, `ac3`, `eac3`, `h264`, `h265` | Exact lowercase; required for audio/video. The parser does not restrict audio-only codecs to audio at parse time, so nonsensical combinations may fail later. |
| `pid` | integer 16–8190 (0x10–0x1FFE); default unset | MPEG-TS PID of this audio/video stream. Unset lets `mpegtsmux` number it (0x41 upward). Configured PIDs are requested before muxer-numbered ones, so a fixed PID never collides with an automatic one. Applies to the `mpegts` container on file/UDP/SRT outputs and inside HLS segments (via `splitmuxsink muxer-pad-map`); ignored with a warning for `es`/`fmp4`. Rejected at parse time when out of range, used twice in one output, equal to the output's `scte.pid`, or set on a `link` stream. |
| `audio.channels` | integer, default `0` | Zero means inherit/derive from input. |
| `audio.rate` | integer Hz, default `0` | Zero means inherit/derive from input. |
| `audio.inputLayout` | string `metadata` (default), `auto`, `stereo` | Dolby PAT path only. How the source's layout is determined. `metadata`: the declared layout (`dlbpatmx in-channels=source`). `auto`: from the audio itself - the output mixer runs as `dlbpatmx in-channels=source-auto`, PADMX upmix mode with automatic detection, so stereo carried in a 5.1 header (silent C/LFE/Ls/Rs) is upmixed and real 5.1 passes through, switching where the content changes; needs `channels` 6 or 10, otherwise a warning is logged and `metadata` is used. `stereo`: as `audio.stereoSource: true`. An unknown value is a configuration error. |
| `audio.stereoSource` | boolean, default `false`, deprecated | Older spelling of `inputLayout: stereo`. Dolby PAT path only, when `audio.channels` > 2. The source is treated as 2.0 regardless of its declared layout: the output mixer runs as `dlbpatmx in-channels=2 mode=upmix`, taking only front left/right and rendering `channels`. Any real centre/surround content in the source is dropped. Setting it together with a contradicting `inputLayout` is a configuration error. |
| `audio.bitrate` | integer | Parsed into `AudioParameters` and never used, so it has no effect. Set the bitrate as the codec block's encoder `bitrate` property: bits/s for `fdkaacenc`, `avenc_mp2`, `avenc_ac3`; an enum nick such as `"192k"` for `dlbpatenc`. |
| `video.width`, `video.height` | integer pixels, default `0` | Zero means inherit/derive. |
| `video.rate` | integer frames/s, default 0/1 | Only integer rates are expressible in JSON. For interleaved output it is the frame rate: `25` is 1080i25, 50 fields/s. |
| `video.format` | `I420`, `YV12`, `I422`, `I444`, `UYVY`, `YUY2`, `YVYU`, `NV12`, `NV21`, or `BGRA` | Exact uppercase mapping. Default `I420`; unknown values log and retain it. The schema omits `BGRA`. |
| `video.interlace` | `progressive` or `interleaved`, default unset | Unset or `progressive` gives progressive output; the scan type is never inherited from the input, which is always deinterlaced. `interleaved` requires `video.rate` and codec `h264`, otherwise the engine refuses to start. Other values are refused. |
| `video.fieldOrder` | `tff` or `bff`, default `tff` | Field order of interleaved output. Other values are refused. |

For codec-specific encoder selection, add an object whose key exactly matches `codec`:

```json
{
  "type": "video",
  "codec": "h264",
  "video": { "width": 1920, "height": 1080, "rate": 25, "format": "I420" },
  "h264": {
    "encoder": "x264enc",
    "properties": { "bitrate": "4000", "key-int-max": "50" },
    "caps": { "profile": "high" }
  }
}
```

| Encoder field | Meaning |
| --- | --- |
| `encoder` | Exact installed GStreamer factory name. |
| `properties` | Arbitrary property map. Non-string JSON values are warned about and converted through the JSON/string path. Valid names/types are factory-specific. |
| `caps` | Output-capability map, such as H.264 `profile`. A field applies only if the encoder's src template already has it; otherwise it is warned about and ignored. |

No default encoder factory is guaranteed by this contract. Inspect the deployed registry and generated graph. `properties` and `caps` are applied only when `encoder` is set, and an unknown property name is only warned about. Rate control, B-frames and audio profile behaviour per encoder are listed in [configuration-reference.md](../configuration-reference.md).

### Group link streams

A group output accepts only link streams:

```json
{
  "type": "link",
  "link": {
    "type": "output",
    "output": { "name": "rendition-hd", "modifier": "_hd" }
  }
}
```

Rules:

- `link.type` is lowercased and only `output` is supported.
- `link.output.name` must name an existing `single` output.
- A group must have a unique non-empty name, exactly one transport, and that transport must be HLS/HTTP.
- A group's streams must all be links. Non-empty modifiers must be unique within the group.
- Group-to-group links are rejected.
- Linking a single output marks it as belonging to the group; such a child may omit transport/container where inheritance applies.

### Output MXL

An output of `"type": "mxl"` publishes the **finished programme** (the picture and sound the channel puts on air, after its compositor, graphics and audio processing) as [MXL](41-mxl-integration.md) flows. It is **Runtime verified** (six tests, recorded in [41](41-mxl-integration.md#programme-tap-step-3)). It replaces the encoder, muxer and sink of an ordinary output and has no `transports`, `container` or `streams`: the `mxl` block says what to write. Parser: `parser.cpp` (the `type: "mxl"` branch of the output loop, helpers `_parseMxlFlows` and `_parseMxlVideo`); parameters: `OutputType::MXL` and `OutputParameters::getMxl()`; media graph: `output/mxl.cpp` and `channel/processor/transcoder/mxlflowline.cpp`.

```json
{
  "type": "mxl",
  "name": "programme",
  "mxl": {
    "domain": "/run/mxl",
    "video": { "flowId": "5fbec3b1-1b0f-417d-9059-8b94a47197ed", "width": 1920, "height": 1080, "rate": "30000/1001" },
    "audio": { "flowId": "b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10", "channels": 2 }
  }
}
```

| JSON path | Type | Default/required | Behavior |
| --- | --- | --- | --- |
| `mxl.domain` | string | Required | MXL domain directory (a tmpfs shared with readers). |
| `mxl.video.flowId` | UUID string | Optional | Writes the programme picture as v210. |
| `mxl.video.width`, `height` | positive integers | The input's | Size of the programme flow. |
| `mxl.video.rate` | integer or fraction string (`"30000/1001"`) | The input's | Frame rate of the programme flow. The normal stream `rate` takes whole numbers only; a fraction is accepted here so that 29.97 and 59.94 can be written. |
| `mxl.audio.flowId` | UUID string | Optional | Writes the programme audio as Float32, 48 kHz. |
| `mxl.audio.channels` | integer 1–8 | `2` | Channels of the audio flow; the channel's audio processor mixes to this count. |

At least one of `video` and `audio` is required, the two ids must differ, and ids must be canonical UUIDs; otherwise the configuration is rejected at start with a message naming the key (`transcoder.outputs[n].mxl…`).

How it fits the channel:

- The output joins the channel's **video and audio processors** like an ordinary output. An MXL video of the same size and rate as an on-air output shares its canvas (the cost is the conversion to v210 and one extra branch); any other size or rate makes a canvas of its own, with the compositor and scaler that implies (test `04`, 720p in and 1920×1080 out). Audio of the same channel count and rate as another output shares its processor.
- A flow carries the processors' output in running time, so it lines up with the frames the encoders get. It is taken before the encoders: it does not carry the encoders' latency or the muxer's offsets, and it is in sync with the source (test `03`).
- **It keeps running when the source stops.** The programme is the channel's output, so the flows keep being written while the channel holds or fills the picture (test `05`).
- The tap is **fail-open**, as the source tap is: it is a leaky branch with a side pipeline of its own, so a missing `mxlsink`, a full or unusable domain, or a flow that another stream already writes stops only the tap (the media is discarded with a warning) and the on-air outputs are unchanged.
- SCTE-35 does not travel in the flow (decision Q7 of the plan is still open). Only the first video and first audio stream have flows.
- The UI's channel form has no fields for it: a config with an MXL output opens in the UI, the output is not shown, and a save keeps it exactly as it is.

## Output SCTE-35 settings

| Field | Default | Behavior |
| --- | --- | --- |
| `output.scte.pid` | `0x1fff` (null PID/disabled) | Parsed as signed 16-bit. The secondary schema's 0–8192 range is not enforced by the parser. |
| `output.scte.mode` | `removal` | Exact `passthrough` selects passthrough; every other supplied string selects removal. |
| `output.scte.nullInterval` | `0` ms | Interval for SCTE-35 null injection; zero disables it. |

A stream `pid` equal to the output's own `scte.pid` is refused. The check does not see an SCTE PID that a grouped output inherits from its group.

SCTE output behavior depends on MPEG-TS/HLS mux paths and local GStreamer patches. Validate PID, PMT registration, sections, and HLS tags with TSDuck and manifest inspection.

## Predefined squeeze configurations

`transcoder.squeeze` is an array. Only object entries with a non-null, non-empty `name` are retained; duplicate names overwrite earlier entries.

```json
{
  "name": "l-band",
  "windows": {
    "input":  { "x": 0, "y": 0, "width": -1, "height": -1 },
    "output": { "x": 0.166667, "y": 0, "width": 0.833333, "height": 0.833333 }
  }
}
```

Both `windows.input` and `windows.output` accept nullable `x`, `y`, `width`, and `height` numbers, expressed as fractions of the output frame (0..1) and resolved by the engine against the real compositor raster. Omitted/null members retain the original-window sentinel `-1`. Scheduled squeeze events may reference the name or supply both windows.

## Global SCTE-35 and POIS

| Field | Default | Production behavior |
| --- | --- | --- |
| `transcoder.scte.pois.enable` | `false` | Enables external POIS/ESAM conditioning. |
| `transcoder.scte.pois.server.uri` | Empty | Required when POIS is enabled. |
| `transcoder.scte.pois.server.acquisitionPointId` | Empty | Required when POIS is enabled. |
| `transcoder.scte.pois.timeout` | `200` ms | This is the live parser location, but it is only read when a `server` object exists. The secondary schema incorrectly nests it under `server`. |
| `transcoder.scte.prerollTime` | Class default `4000` ms | Advertised by the schema but not parsed; the runtime always uses the class default. |

POIS is integration gated by the external endpoint. A valid parse does not prove NOOP/DELETE/REPLACE interoperability.

## Blanking and blackout policy

`transcoder.blanking` and `transcoder.blackout` have the same shape and feed separate engine policies.

```json
{
  "enable": true,
  "restriction": { "ignore": "NONE" },
  "conditions": {
    "timeSignal": {
      "blankWhenNoSegmentationDescriptors": false,
      "segments": [
        { "name": "provider-ad", "start": "0x30", "stop": "0x31", "priority": 1 }
      ]
    },
    "spliceInsert": {
      "blankWhenNoSegmentationDescriptors": true,
      "segments": []
    }
  }
}
```

| Field | Default/values | Notes |
| --- | --- | --- |
| `enable` | `false` | Enables the relevant policy. |
| `restriction.ignore` | `NONE`, `NO_REGIONAL_BLACKOUT`, `WEB_DELIVERY_ALLOWED`; default `NONE` | Exact uppercase; unsupported strings log and keep the default. |
| `conditions.timeSignal` | condition object | Both condition keys must exist if `conditions` is present because the parser indexes them directly. |
| `conditions.spliceInsert` | condition object | Same shape. |
| `blankWhenNoSegmentationDescriptors` | `false` | Controls behavior when a command has no segmentation descriptors. |
| `segments[].name` | string | Diagnostic name. |
| `segments[].start`, `segments[].stop` | numeric string | Parsed with base autodetection, so decimal, octal, and `0x` hexadecimal forms work. |
| `segments[].priority` | unsigned 8-bit; class default 16 | Intended range is 1 (highest) through 16 (lowest), but parser-side range checks are absent. |

Descriptor matching and restriction semantics have unsupported branches; verify each policy against real SCTE-35 sections and output media.

## Application validation summary

Startup rejects at least the following:

- Missing/invalid config or license paths, unsupported config version, or malformed JSON/type conversions.
- Enabled file logger without a path.
- Nonexistent event database path.
- Online license without a valid HTTP(S) license server URI.
- Zero input URIs, more than two URIs, empty URIs, redundant file/playlist input, or playlist plus passthrough audio/video.
- Duplicate non-empty output names or empty transport URIs.
- HLS segment duration/window below one second/one segment or HLS with passthrough video.
- Standalone single output without transport, unknown container, empty stream list, link streams on a single output, or more than one stream in ES.
- Group without a name/transport, more than one group transport, non-HLS group transport, non-link group streams, duplicate modifiers, missing link target, or links to another group.
- Audio/video stream with unknown codec.
- Enabled POIS with missing endpoint or acquisition point ID.

Application validation does not replace media negotiation. Encoder properties, plugin availability, caps, credentials, network reachability, and output correctness remain runtime concerns.

## Schema and example discrepancies

The following are known contract differences, not aliases:

| Secondary material | Live production behavior |
| --- | --- |
| HLS input `bandwidth` | Parser reads `bandwith`. |
| UDP output schema `mulitcast` | Parser reads `multicast`. |
| Logger `fatal` | Not accepted; parser accepts `none`, which schema omits. |
| HLS transfer mode `file` | Not accepted as `transferMode`; file selection is URI-driven. |
| `scte.prerollTime` | Not parsed; fixed parameter default remains. |
| POIS timeout under `pois.server.timeout` | Parser reads `pois.timeout` while inside the server-present branch. |
| Schema omits NDI, playlist, and S3 parameter objects | Parser reaches all three; runtime integrations may still be gated. |
| Schema HLS input minimum/maximum and PID/priority ranges | Parameter documentation only; parser does not enforce all ranges. |
| Some examples use `warn` | Parser accepts only `warning`. |

See [known gaps and remediation](27-known-gaps-contradictions-and-remediation.md) before changing a schema or example. Preserve parser, parameter defaults, schema, examples, API config models, and tests as one contract.
