# Single-instance wireframe → API gap analysis

Scope: **one playout instance.** Everything the operator needs to define a channel, start it, watch
it and act on it while it runs. Multi-channel concerns — the channel list, fleet status, cloning,
archiving, per-channel routing — are deliberately excluded; a separate codebase will own instance
orchestration.

## Method, and why this supersedes the earlier report

Every screen below was **rendered as an image and read visually**. The previous analysis
(`2026-09-08-wireframe-api-gap-analysis.md`) read Figma *layer names* from file metadata, which is
unreliable: the licence screens were duplicated from the encoder screens, and Figma kept the old
layer names while the visible text was replaced. That produced a wholly incorrect finding about
per-feature licensing, since corrected in §12 of that report. Where the two documents disagree, this
one is correct.

Frames read: `Create new channel` (456:866, 699:568), `- Playlist` (699:146), `- SRT` (699:766),
`- UDP` (733:16), `- HLS` (733:253), `- backup` (699:357), `Channel Create - SRT` (815:1238),
`- HLS` (814:753, 839:39), `Channel Control - Show` (449:623), `Channel Status` (791:737).

Config paths are quoted against the parser (`transcoder/app/src/application/config/parser/json/parser.cpp`)
and a real shipped config (`config/srt_to_hls_srt.json`), not against `openapi.yaml`, which diverges
from what the parser actually reads.

## Legend

| | Meaning |
| --- | --- |
| **A** | Available — a config key or route exists and does what the control implies |
| **P** | Partial — reachable, but through encoder passthrough, a narrower type, or a different shape |
| **M** | Missing — no config key and no route; needs new work |
| **E** | Elsewhere — belongs to a different service, not this API |

---

## 1. Channel identity and inputs

| Control | | Backing |
| --- | --- | --- |
| `Name` (channel) | **M** | The config has no channel name. `outputs[].name` names an *output*, not the channel. |
| `Import Channel` | **A** | `PUT /config` takes the whole document; import is a client-side file read. |
| `Input 1` type (SRT / HLS / UDP / Playlist) | **A** | `transcoder.inputs[0].transport.{srt,udp,hls,playlist}` |
| `Input 1 URL` | **A** | `inputs[0].transport.uri` — string, or an array for source redundancy |
| `Caller` / `Listener` | **A** | `inputs[0].transport.srt.mode` |
| SRT latency | **A** | `inputs[0].transport.srt.latency` |
| `Input Name` | **M** | Not parsed. Inputs carry no name. |
| `Add Input` — multiple sources | **A, wrong shape** | The engine fully supports multiple sources with automatic failover and manual switching — but they come from **`inputs[0].transport.uri` as an array**, not from multiple `inputs[]` entries. See below. |
| Automatic failover between sources | **A** | `StandardInput` builds one `SingleInput` per URI, probes them all, and switches on underflow or error (`input/standard.cpp:90-108, 150-175`) |
| Manual source switching | **A** | `POST /inputs/{id}` with `currentStreamId` → gRPC `setInputStream` → `input->setCurrentStream()` (`transcoder/api/grpc.cpp:42-75`) |
| Per-source transport settings | **P** | All URIs share one transport parameter block (`standard.cpp:95-105` copies it per URI). An SRT primary and a UDP backup with different latency/mode is not expressible. |
| `Preview Input / Probe Input` | **M** | No route. The newest frame (839:39) annotates this "MIGHT NEED TO REMOVE". |
| `Input Data:` Resolution / FPS / Bitrate / Scan Type / Audio / PID | **M** | `GET /inputs` returns `id`, `streams[].uri`, `streams[].state` and `currentStreamId` only. No codec, geometry or bitrate anywhere in the model. |
| `Audio Selector` / `Select PID` | **P** | `inputs[0].selector.program` selects a **program number**, not an audio PID. |
| `Advanced` | **?** | Contents not drawn. |

### Multiple inputs: supported, but the config shape is a trap

The engine ingests several sources and switches between them, automatically and on demand. What it
does **not** accept is more than one entry in the `inputs[]` array — the parser reads `inputs[0]`
and never looks at the rest (parser.cpp:289, 294).

```jsonc
// Works — three redundant sources, one input
"inputs": [ { "transport": { "uri": ["srt://a:9000", "srt://b:9000", "srt://c:9000"] } } ]

// Silently broken — entries 2 and 3 are dropped, HTTP 200, engine runs on the first
"inputs": [ { "transport": { "uri": "srt://a:9000" } },
            { "transport": { "uri": "srt://b:9000" } },
            { "transport": { "uri": "srt://c:9000" } } ]
```

Naming differs between layers and is worth knowing before building the form: what the wireframe
calls "Input 1, Input 2" the API models as **one Input with several `streams[]`**, switched by
`streams[].id`. The UI's `Add Input` button is implementable today — it must append to the URI
array, not to `inputs[]`.

The failure mode is the dangerous part: an over-long `inputs[]` array is accepted, written to disk
and reported as success, and the operator finds out only when failover doesn't happen.

## 2. Outputs

| Control | | Backing |
| --- | --- | --- |
| Output group tabs (`Archive`, `UDP/TS`) + `Add` | **A** | `transcoder.outputs[]` is a genuine array and loops (parser.cpp:471) |
| `New Output` → `Name` | **A** | `outputs[].name` |
| `Output 1 Select Type` (TS) | **A** | `outputs[].container` — `mpegts` \| `fmp4` \| `es` |
| `Caller` / `Listener` (output) | **A** | `outputs[].transports[].srt.mode` |
| `Add output` | **A** | Append to `outputs[]` |
| **Archive Settings** → `Destination` | **P** | Path comes from `transports[].uri`; the `file` transport object itself takes **no parameters** (parser.cpp:776-780). |
| **Archive Settings** → `Rollover interval` | **M** | No key anywhere. The archive writes one continuous file. |
| **HLS** `Container` | **A** | `outputs[].container` |
| **HLS** `NumSegments`, `SegmentLength`, `SingleDirectory`, `TransferMode`, `BaseFileName` | **A** | `transports[].hls.*` |
| **HLS** `AdMarkers` (`Elemental`) | **A** | `transports[].hls.adMarkers[]` — `elemental` \| `enhanced`. `adobe` exists in the parser but is `#if 0`. |
| **HLS** `Authentication` (Digest) + `Username` / `Password` | **A** | `transports[].hls.credentials.{username,password}` |
| **HLS** `Transport.url` = "HLS MediaPackage endpoint" | **M** | No MediaPackage concept in the parser. The field is a plain URI. |
| **SCTE** `SCTE.mode` (Passthrough) | **A** | `outputs[].scte.mode` — `passthrough` \| `removal` |
| **SCTE** `SCTE.pid` | **A** | `outputs[].scte.pid` |
| **UDP output settings** (address, port, TTL, interface) | **M on screen** | The parser supports `transports[].udp.{multicast,unicast,ttl,interface}`, but **the UDP screen draws no output-settings block at all** — it jumps from output type straight to video parameters. The API is ahead of the design here. |

## 3. Video parameters

The whole encoder section reaches the engine through **passthrough**: `streams[].h264.encoder` names
a GStreamer element, and `properties` / `caps` are free-form string maps handed to it unchecked
(parser.cpp:892-915). Nothing validates them. A typo is accepted by the API, written to disk, and
surfaces only as a failed engine start.

| Control | | Backing |
| --- | --- | --- |
| `Codec` (H264AVC) | **A** | `streams[].codec` — `h264` \| `h265` \| `aac` \| `ac3` \| `eac3` \| `mp1/2/3` |
| `Resolution` (720x480) | **A** | `streams[].video.width` / `.height` |
| `Encoding engine` (S/W codec) | **P** | Maps only indirectly to `h264.encoder` (`x264enc`, `nvh264enc`, …). The older HLS frame (733:253) labels this field **`Encoder` with the value `x264enc`**, which is the config's own vocabulary — that naming is the one to keep. |
| `Bitrate` | **P** | `h264.properties.bitrate` — a passthrough string, units depend on the element |
| `Rate control` (CBR) | **P** | Passthrough property, element-specific (`pass`, `rc-mode`, …) |
| `H264 Profile` / `Level` | **P** | `h264.caps.profile` / `.level` |
| `Symbol mode` (CABAC) | **P** | Passthrough property |
| `Bit depth` (8b) | **P** | Passthrough caps |
| `No of B frames` | **P** | Passthrough property (`b-frames` / `bframes`, element-specific) |
| `Frame rate` (25 fps) | **P** | `streams[].video.rate` is parsed as `Fraction(int, 1)` (parser.cpp:961). **29.97 and 59.94 cannot be expressed.** |
| `Aspect ratio` (Default) | **M** | No key. The engine hardcodes pixel-aspect-ratio 1:1 on both input and output, and scales with `add-borders=false` — it always stretches. There is nothing to configure. |
| `Frame format` (Progressive) | **M** | `video.interlace` exists in the parser but is wrapped in `#if 0` (parser.cpp:974-985). **The dropdown offers a choice that cannot take effect.** |
| `Key-Int-Max`, `Tune`, `Speed Preset` (733:253 only) | **P** | Passthrough properties — these are honest, they name real x264 properties |

## 4. Audio parameters

| Control | | Backing |
| --- | --- | --- |
| `Codec` (AAC) | **A** | `streams[].codec` |
| `Sampling Rate (Hz)` | **A** | `streams[].audio.rate` |
| `Bitrate (kbps)` | **A** | `streams[].audio.bitrate` |
| `Channels` | **A** | `streams[].audio.channels` |
| `MPEG version` (MPEG4) | **P** | Encoder property or caps, not a first-class key |
| `Profile` (LC / Default) | **P** | `aac.caps.profile` |
| `Start alignment (frames)` | **M** | Not in the parser |
| `Encoder` (dlbpatenc) | **A** | `streams[].aac.encoder` |

## 5. Graphics

Four slots — `L-Band`, `Bug`, `Logo`, `Aston` — each with **Upload** and **Delete**.

| Control | | Backing |
| --- | --- | --- |
| `Upload` a graphic | **M** | No upload route exists for graphics. `GET /graphics/catalog` is **read-only** over a catalog file; media is expected to be on disk already. |
| `Delete` a graphic | **M** | Same |
| Firing a graphic on air | **A, but not on this screen** | `POST /graphics/cues` schedules a cue; `DELETE /graphics/cues/{id}` cancels it |
| Slates | **A, not drawn** | `GET /slates/catalog`, `GET /slates/media/{id}` exist and are not surfaced anywhere in the wireframe |

Media upload *does* exist for playout — `POST /playout/media` accepts an `UploadFile`. Graphics
would need an equivalent, plus catalog write-back.

## 6. Playlist input

| Control | | Backing |
| --- | --- | --- |
| `Create Playlist` numbered list | **A** | `POST /playout/playlists/{id}/items`, `PATCH`, `DELETE` |
| `Save` | **A** | `POST /playout/playlists/{id}/apply` |
| `Upload` | **A** | `POST /playout/media` |
| `Export` | **M** | No export route |
| `HLS (m3u8)` / `Video file (ts)` per item | **A** | Media items carry a URI |
| Scheduling / rundown (not drawn) | **A, unused** | A full rundown API exists — versions, clock, occurrences, executions, audit — and the wireframe surfaces none of it |

## 7. Running the instance

| Control | | Backing |
| --- | --- | --- |
| `Status: Running` | **A** | `GET /config/check-transcoder` → `{"message": "RUNNING"}` |
| `Start` | **P** | Only `POST /config/restart-transcoder` exists. It starts a stopped engine, so it covers Start — but there is no plain start, and no stop at all. |
| `Stop` | **M** | No route. Supervisor can do it; the API does not expose it. |
| `Started: <timestamp>` | **M** | `check-transcoder` returns the state name only. Supervisor's `getProcessInfo` carries `start`, but the route discards it. |
| `Current Input: udp://IP` | **A** | `GET /inputs` → `currentStreamId` + `streams[].uri` |
| Input switching | **A, not drawn** | `POST /inputs/{id}` sets `currentStreamId` |
| `Input Preview` / `Output Preview` (video) | **M** | No thumbnail, snapshot or preview route |
| `Audio Gain (dB)` −/+ | **M** | No config key, no route, nothing in the engine |
| Audio level meters | **M** | No telemetry of any kind |
| `View Logs` | **M** | No log route. Logs go to the container's stdout/file via `application.logger`. |
| `Input Stats` table (14 rows: codec, resolution, aspect, frame format, fps, B frames, rate control, bitrate, profile, level, symbol mode, bit depth, input type, network mode, security type) | **M** | `GET /inputs` returns `id`, `uri`, `state`. None of these 14 fields exist. |
| `Output Stats` table | **M** | No output-status route at all |
| `Output Controls` → `Output 1: HLS`, `URL:` | **P** | Readable from `GET /config`; not a status route |
| `Health`: Network in / Network out / CPU / GPU | **P** | `GET /stats` reports network in/out, CPU, memory, storage and disk writes; GPU is not measured |
| `Channel Actions ▾` | **?** | Drawn as a button; the menu is never expanded in the file, so its contents are unknown. Worth resolving before estimating — squeeze-back and SCTE triggers exist in the engine and may belong here. |

---

## Consolidated

### A. Missing, needs new backend work

1. **Channel name** — no identity field in the config
2. **Input probe / preview** — no route
3. **Input and output statistics** — 14+ fields per side, none exist; the single largest gap
4. **Video preview thumbnails** — input and output
5. **Audio gain and level metering** — absent from API *and* engine
6. **Log retrieval**
7. **GPU health metric** — CPU, memory, storage and network are served by `GET /stats`
8. **Graphics upload / delete**
9. **Archive rollover interval**
10. **Audio start alignment**
11. **Stop route**, and start time in the status response
12. **Playlist export**

### B. Present but problematic

1. **Frame format** — `#if 0` in the parser. The control cannot work. Cut it, or finish the engine code.
2. **Aspect ratio** — the engine hardcodes square pixels and always stretches. Cut it, or add a fit policy (`add-borders`) to the scaler.
3. **Frame rate** — integer-only. 29.97 / 59.94 unrepresentable. If NTSC rates are needed this is a parser change, not a UI change.
4. **Encoder settings are unvalidated passthrough** — nine controls on the video screen write free-form strings. Wrong values pass validation, write to disk, and fail at engine start with no attribution to the field.
5. **`Encoding engine` vs `Encoder`** — the newer screens replaced a real config key with an abstraction the config has no vocabulary for. Restore the older label.
6. **UDP output settings missing from the UDP screen** — the parser supports multicast/unicast/TTL/interface; the design doesn't ask for them.
7. **`Start`/`Stop` map to one restart route** — usable, but the semantics don't match the buttons.
8. **No authentication on any route.** `PUT /config` performs a real whole-config write on a published port with no credentials. Fine on a trusted network, a release blocker otherwise.

### C. Works today

Input transport and URI, SRT mode and latency, program selection, the entire HLS output block,
SCTE mode and PID, container, output naming, resolution, codecs, audio rate/channels/bitrate,
whole-config read and write with validation and dry-run, and engine restart.

The HLS output section is the strongest part of the design — every field maps to a real config key.

### D. Exists in the API, absent from the wireframe

Worth deciding whether to surface: slate firing (`/slates/*`), graphics cue scheduling
(`/graphics/cues`), the entire rundown subsystem (versions, clock, occurrences, executions, audit),
and input switching (`POST /inputs/{id}`).

---

## Recommended sequence

1. **Fix the silent input-array drop** — either parse `inputs[1..n]` or reject them. Silently
   discarding config is worse than failing.
2. **Decide the four dead controls** — frame format, aspect ratio, rollover interval, start
   alignment. Cut from the design, or schedule engine work. Do not ship inert controls.
3. **Build a status endpoint.** Input/output stats and health are the bulk of the missing surface
   and the thing an operator looks at most. This is the largest single piece of work.
4. **Add authentication** before the API is exposed anywhere untrusted.
5. **Restore config-key vocabulary** on the encoder fields, and consider a per-field validation pass
   over the passthrough properties so errors are attributable.
6. **Resolve `Channel Actions`** — the menu is undrawn and may hide further requirements.
