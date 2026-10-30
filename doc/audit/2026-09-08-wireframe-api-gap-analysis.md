# Nandi Transcoder wireframe — API gap analysis

## Scope and method

Source: Figma file `2G11DEYpFCRAww7KNOdbqz` ("Nandi-Transcoder"), page `Admin` (`0:1`), read through the
Figma MCP server on 2026-09-08. **41 top-level frames, 35 distinct screen names** — every frame's text
layer was extracted and mapped, not a sample.

Each wireframe field was checked against three sources, in this order of authority:

1. **The C++ parser** — `transcoder/app/src/application/config/parser/json/parser.cpp`. Authoritative.
   A field the parser does not read does nothing, whatever the schema says.
2. **The config schema** — `transcoder/config/doc/openapi.yaml`. Secondary, and known to diverge from
   the parser (`doc/codebase/27`).
3. **The REST surface** — the 55 routes on the running FastAPI app.

## Legend

| Mark | Meaning |
| --- | --- |
| ✅ | Config-backed. Named field, parser reads it, reachable via `PUT /config`. |
| ⚠️ | Encoder passthrough. Expressible only as untyped GStreamer `properties`/`caps` strings. No schema, no validation until the pipeline builds, key names depend on the selected encoder plugin. |
| 🔀 | Wrong layer. Real, but owned by a different API than channel config. |
| ❌ | No backend. Nothing in the parser, schema or REST surface. |

## Headline

- **Roughly half the channel-create form has no typed backend.** 13 video/audio fields are ⚠️ encoder
  passthrough; 9 more are ❌.
- **Whole screens have no API at all**: users/auth (6 screens), health monitoring, alerts, log files,
  channel CRUD, previews.
- **`PUT /config` is not the blocker.** Since it replaces the whole document, every config field is
  reachable. What is missing is *fields in the config format itself*.
- **One screen is already correct.** `Create new channel - HLS` (`733:253`) is written in real config
  vocabulary and maps almost 1:1 to the schema. The other channel-create screens are not. Treat that
  frame as the reference and reconcile the rest to it.

---

## 1. Channel identity and lifecycle

Screens: `Channel Control- All/Active/Archived/Show`, `Channel List`, `Channel List - 1`, `Create new channel`.

| Wireframe field | Status | Notes |
| --- | --- | --- |
| Channel `Name` / "Channel Alpha" | ❌ | No channel name in the config. `outputs[].name` is an output label. Channel identity lives in the playlist/rundown layer (`channel.yml`, `playlistId: main`). |
| Channel `Id` (101, 102, 54345) | ❌ | No channel id concept in config. |
| Active (4) / Archived (20) counts | ❌ | No channel collection to count. |
| `Create channel` / `Create new Channel` | ❌ | No create route. Nearest is `PUT /config`, which replaces the single global config. |
| `Edit Channel` | ❌ | Same. |
| `Clone Channel` | ❌ | No route. |
| `Delete Channel` | ❌ | No route. |
| `Import Channel` / `Export` | ❌ | No route. `PUT /config` with a document is the nearest equivalent. |
| `Start` / `Stop` (per channel) | 🔀 | Only `POST /config/restart-transcoder`, which restarts **the whole engine** — there is one engine process, not one per channel. |
| `Status` (Running) | 🔀 | `GET /config/check-transcoder` returns the Supervisor state. **Caveat: it reports `RUNNING` even when the input is dead and no output is being produced** — verified 2026-09-08. |
| `Errors` / `No Errors` | ❌ | No error-state route. |
| `Input Type` / `Output Type` columns | ✅ | Derivable from `inputs[0].transport.uri` and `outputs[].transports[].uri` schemes. |
| `Search for live channel` | ❌ | No channel collection to search. |
| `Channel Template`, `Clear Template` | ❌ | No template concept. |
| `Input Specifications` (Input Codec AVC, Input Resolution HD, Maximum Input Bitrate Max_20_MBPS) | ❌ | Input is probed, not declared. No such config fields. |
| `IAM Role` | ❌ | Not in the parser. S3 output uses `accessKey`/`secretAccessKey`/`bucket`/`region`. |

> **Architectural note.** The wireframe assumes *many channels per installation*. The engine is
> single-channel: one `config.json`, one transcoder process under Supervisor, and only
> `transcoder.inputs[0]` is used. A multi-channel UI needs either one container per channel or a
> channel-management layer that does not exist today. **This is the single largest gap in the design.**

## 2. Input

Screens: `Channel Create - SRT/UDP/HLS/Playlist`, `Create new channel - *`.

| Wireframe field | Status | Config path |
| --- | --- | --- |
| Input type (SRT / UDP / HLS / Playlist / IP / Video file (ts)) | ✅ | `inputs[0].transport.uri` scheme — `srt://`, `udp://`, `http(s)://…m3u8`, `pls+file://` |
| `Input 1 URL` / `Input URL` | ✅ | `inputs[0].transport.uri` |
| `Caller` (network mode) | ✅ | `transport.srt.mode` — enum `listener` \| `caller` |
| SRT latency | ✅ | `transport.srt.latency` |
| SRT buffer size | ✅ | `transport.srt.bufferSize` |
| UDP multicast / interfaces | ✅ | `transport.udp.multicast`, `.interfaces` — note the parser also accepts the misspelling `mulitcast` (`doc/codebase/27`) |
| `Select Program` | ✅ | `inputs[0].selector.program` |
| `Audio Selector` / `Select PID` / `PID 1` | ❌ | **`selector` supports `program` only.** The parser's single `pid` key is the SCTE-35 *output* PID. No audio PID selection exists. |
| `Preview Input / Probe Input`, `Input Data:` (Resolution, FPS, Bitrate, Scan Type, Audio Information) | 🔀 | `GET /inputs` — runtime status, read-only, not config |
| `Input Preview` (video thumbnail) | ❌ | No preview/thumbnail route. `/player` and `/hls` serve output, not input. |
| `Security type` (AES-128) | ❌ | **No SRT passphrase or encryption field** in parser or schema. |
| `Network Mode` (Public) | ❌ | Not in the parser. |
| `Add input` / `Input 1` … | ⚠️ | The array accepts more entries, but **only `inputs[0]` is used**; extra inputs are silently ignored (`doc/codebase/27`). A UI offering "Add input" would mislead. |
| `Audio Gain (dB)` | ❌ | No gain or volume key anywhere in the parser. |

## 3. Output and archive

| Wireframe field | Status | Config path |
| --- | --- | --- |
| `New Output` / `Add output` / `Output Groups` | ✅ | `outputs[]`; `type` is `SINGLE` \| `GROUP` |
| `Main Stream` (output label) | ✅ | `outputs[].name` |
| `Output 1 Select Type` — TS | ✅ | `container` — `mpegts` \| `es` \| `fmp4` |
| `UDP/TS` destination | ✅ | `outputs[].transports[].uri` |
| `Archive` output | ✅ | File output via a `file://` URI |
| `Destination` (`/mnt/archive/channel_alpha`) + `Browse` | ⚠️ | Path is embedded in the `uri`. There is **no `destination` field** — `FileOutputTransportParameters` has **zero properties** in the schema. |
| `Rollover interval` (3600 Seconds) | ❌ | **No config field.** `segmentLength` exists for HLS output only. No `rollover`, `maxSize` or duration key for file output in the parser. |

## 4. Video parameters

The wireframe presents these as named fields. In the config, only four are named; the rest are
GStreamer encoder properties.

| Wireframe field | Status | Config path |
| --- | --- | --- |
| `Codec` (H264AVC) | ✅ | `streams[].codec` — `h264` \| `h265` \| `mp1` \| `mp2` \| `mp3` \| `aac` \| `ac3` \| `eac3` \| `passthrough` \| `es` |
| `Resolution` (720x480) | ✅ | `video.width`, `video.height` |
| `Frame rate` (25 fps) | ⚠️ | `video.rate` — **integer only**. The parser builds `Fraction(rate, 1)`, so **29.97 cannot be expressed** — yet `Channel Status` displays "29.97 fps". |
| Pixel format | ✅ | `video.format` — `I420`, `NV12`, … |
| `Bitrate` (2000 kbps) | ⚠️ | `h264.properties.bitrate` — a **string**, encoder-specific. Not `video.bitrate`; no such field exists. |
| `Encoding engine` (S/W codec, H/W codec) | ⚠️ | `h264.encoder` — GStreamer plugin name (`x264enc` = S/W). Changing this **renames every property key below it.** |
| `H264 Profile` (Main / high-10) | ⚠️ | `h264.caps.profile` |
| `H264 Level` (Auto / 4.2 / 3.1) | ⚠️ | `h264.caps.level`. The parser's only `level` key is the **logger** level. |
| `Rate control` (CBR) | ⚠️ | `h264.properties.*` — `pass` on x264enc; different on other encoders |
| `Symbol mode` (CABAC) | ⚠️ | `h264.properties.cabac` (x264enc) |
| `No of B frames` (0) | ⚠️ | `h264.properties.bframes` (x264enc) |
| `Bit depth` (8b) | ⚠️ | Encoder/caps dependent |
| `Tune` (Zerolatency), `Speed Preset` (Ultrafast), `Key-Int-Max` (15) | ⚠️ | `h264.properties.*` — **already correct** on the HLS frame; these are real x264enc property names |
| `Aspect ratio` (Default / 16:9) | ❌ | **No config field.** Absent from the parser entirely. |
| `Frame format` (Progressive) | ❌ | **Hardcoded.** `video.interlace` parsing sits inside `#if 0` with `FIXME: Enable the following code when stream filters support will be added` (`parser.cpp:975-984`), and the pipeline sets `Interlace::PROGRESSIVE` unconditionally (`transcoder/video.cpp:23`, `connector/complex/video.cpp:118`). **The dropdown offers a choice the engine overrides.** |

**Reference — how a real config expresses this** (`config/srt_to_hls_srt.json`):

```json
{
  "type": "video", "codec": "h264",
  "video": { "width": 1920, "height": 1080, "rate": 25, "format": "I420" },
  "h264": {
    "encoder": "x264enc",
    "properties": { "key-int-max": "50", "tune": "zerolatency",
                    "speed-preset": "veryfast", "bitrate": "6000" },
    "caps": { "profile": "high" }
  }
}
```

## 5. Audio parameters

| Wireframe field | Status | Config path |
| --- | --- | --- |
| `Codec` (AAC) | ✅ | `streams[].codec` |
| `Sampling Rate (Hz)` (48000) | ✅ | `audio.rate` |
| `Bitrate (kbps)` (64 / 128 / 192) | ✅ | `audio.bitrate` |
| `Channels` (2) | ✅ | `audio.channels` |
| `MPEG version` (MPEG4) | ⚠️ | Encoder property or caps |
| `Profile` (LC) | ⚠️ | `aac.caps.profile` |
| `Start alignment (frames)` | ❌ | Not in the parser. |

## 6. HLS output settings — the config-accurate screen

`Create new channel - HLS` (`733:253`) already speaks the config's own vocabulary. Nearly everything on it
is ✅, which is why it should be the model for the rest of the design.

| Wireframe field | Status | Config path |
| --- | --- | --- |
| `SingleDirectory` (True) | ✅ | `hls.singleDirectory` |
| `TransferMode` (http_put) | ✅ | `hls.transferMode` |
| `BaseFileName` (Channel) | ✅ | `hls.baseFileName` |
| `SegmentLength` (5) | ✅ | `hls.segmentLength` |
| `NumSegments` | ✅ | `hls.numSegments` |
| `Authentication` (Digest) + `Password` | ✅ | `hls.authentication`, `credentials` |
| `AdMarkers` (elemental) | ✅ | `hls.adMarkers` — `elemental` \| `enhanced` |
| `Transport.url` | ✅ | `transports[].uri` |
| `Container` (mpegts) | ✅ | `container` |
| `Scte.mode` (Passthrough) | ✅ | `outputs[].scte.mode` — `passthrough` \| `removal` |
| `Scte.pid` (600) | ✅ | `outputs[].scte.pid` |
| `Encoder` (x264enc) | ✅ | `h264.encoder` — named correctly here, unlike "Encoding engine" elsewhere |
| `H264 Pass` / `cbr` | ⚠️ | `h264.properties.pass` |
| `HLS MediaPackage e..` | ❌ | Not in the parser. |

## 7. SCTE-35 and ad insertion

Not shown on the channel-create screens beyond the HLS frame, but part of the config surface.

| Capability | Status | Config path |
| --- | --- | --- |
| Per-output SCTE mode / pid / null interval | ✅ | `outputs[].scte` |
| Ad marker flavour | ✅ | `hls.adMarkers` |
| Ingest source (segments vs manifest) | ✅ | `inputs[].transport.hls.scte.source` |
| Global preroll | ✅ | `transcoder.scte.prerollTime` |
| POIS endpoints | ✅ | `transcoder.scte.pois` |
| Scheduled ad breaks / splice events | 🔀 | `/scheduled_events` |

## 8. Graphics

Screens: the `Graphics` panel on every channel-create frame.

| Wireframe field | Status | Where |
| --- | --- | --- |
| `L-Band`, `Bug`, `Logo`, `Aston` | 🔀 | `/graphics/catalog` and `/graphics/cues`. Not config. Gated by `ENABLE_VIDEO_GRAPHICS`. |
| `Upload` | 🔀 | `POST /media` |
| `Delete` | 🔀 | `DELETE /graphics/cues/{id}` (a cue) or `DELETE /media/{id}` (an asset) |

> Animated MOV graphics are evaluation-only: two acceptance criteria fail (≈840 ms of black on air,
> ≈19 MB retained per start/stop cycle). See `doc/codebase/02`.

## 9. Playlist

Screen: `Create new channel - Playlist` (`699:146`).

| Wireframe field | Status | Where |
| --- | --- | --- |
| `Create Playlist`, items 1./2./3. | 🔀 | `GET/POST/PATCH/DELETE /playlists/{id}/items` |
| `Input Name` | 🔀 | Playlist item metadata |
| `Save` | 🔀 | `POST /rundowns/{id}/apply` |
| `Upload` | 🔀 | `POST /media`, `POST /media/import` |
| `Export` | ❌ | No export route. |
| Playlist as an input | ✅ | `inputs[0].transport.uri` = `pls+file:///…m3u8` plus `"playlist": {}` |

> Playlist input cannot use audio/video passthrough — explicit output codecs are required. Nested
> `pls+file://` playlists are unsupported.

## 10. Monitoring, stats and logs

Screens: `Channel Control- Show`, `Channel Status`, `Stats`, `Stats- Alerts`, `Stats- Logs`.

| Wireframe field | Status | Notes |
| --- | --- | --- |
| `Input Stats` / `Output Stats` (codec, resolution, bitrate, profile, level…) | 🔀 | Partly from `GET /inputs`; **output-side stats have no route** |
| `Health Monitoring` — `CPU Usage`, `GPU Usage`, `Network In`, `Network Out`, `1 minute period` | ❌ | **No metrics endpoint of any kind.** No Prometheus surface. |
| `Output Preview` | ❌ | Nearest is the `/hls` mount and `/player`, which is a stream, not a preview widget |
| `Processing Time` | ❌ | No route. |
| `Alerts`, `All Alerts`, `Alert Name`, `Message`, `Set at` | ❌ | **No alerts subsystem.** |
| `Log Files`, `Filename`, `Size`, `Last modified`, `Download` | ❌ | **No log API.** Logs go to `stdout`/`stderr` via Supervisor. |
| `View Logs` | ❌ | Same. |
| `Started: <timestamp>` | ⚠️ | Supervisor knows process uptime; not currently exposed by the API. |

## 11. Users, roles and authentication

Screens: `Login - Operator`, `Login - Admin`, `Login - License`, `Users`, `Create user`, `Settings - User Profile`.

| Wireframe field | Status |
| --- | --- |
| `Login` (Username, Password, Show password) | ❌ |
| `Users` list, `User Role` (Admin / Operator), `Container assigned` | ❌ |
| `Create User` | ❌ |
| `User Profile`, `Email Id`, `Role`, `Reset Password`, `Save` | ❌ |
| `Username` in the header of every screen | ❌ |

> **There is no authentication anywhere in the API.** Every route, including `PUT /config` and the
> Supervisor restart, is unauthenticated, and Compose publishes port 8080 on all interfaces. This is a
> P1 in `doc/codebase/27` and it became materially more urgent once `PUT /config` started performing a
> real write. Six wireframe screens depend on a subsystem that does not exist.

## 12. Licensing

Screens: `Licence` (463:2), `Login - License` (849:39), `3 Buttons` (849:60), `Online Server` (852:212),
`Create License - 1` (850:99), `Create License - 2` (852:283), `View License List` (852:232).

The `Create License` frames were duplicated from an encoder frame and still carry its **layer names**
(Codec, Rate control, Bitrate, …), but the rendered text is license fields. Read the rendered frames,
not the layer names.

| Wireframe field (rendered) | Status | Notes |
| --- | --- | --- |
| `Licence` → `Browse` | ✅ | `PUT /license` (multipart `file`). The engine binary validates the upload (`transcoder --license-info`); anything it would not start on is refused with 422, otherwise the file is replaced and **the engine restarts**. `?dryRun=true` validates only. |
| `Licence` → `Info about licence` | ✅ | `GET /license`: status, name, id, validity window, offline, max sessions. |
| Remove the installed license (not drawn) | ✅ | `DELETE /license` stops the engine, then removes the file. The channel stays off air until a new upload. |
| `Login - License` | ❌ | A plain username/password login. No authentication exists anywhere (§11). |
| `3 Buttons`: `Create License Server`, `Start License Server Online`, `View License List` | 🔀 | Vendor-side license administration, deliberately not in the appliance API: issuing needs the signing private key. |
| `Create License Server` (`Create License - 1`): License Key, License Name, Output Path, Start Date, Expiry Days, Expiry Seconds, ID, Sessions, Offline Mode | 🔀 | One-to-one with the `licenseGenerator` CLI options (`-l -n -o -s -t -T -i -c -O`), wrapped by `transcoder/license/make-license.sh`. No HTTP route anywhere. |
| `Activate License Server` (`Create License - 2`): Private Key, Input License, Output License | 🔀 | One-to-one with `licenseSigner -p -i -o`. No HTTP route anywhere. |
| `Online Server`: `Start`, `Stop` | ❌ | The license server (`license-server/`) exposes only `POST /create_session` and `POST /heartbeat/{session_id}`; nothing starts or stops it over HTTP. |
| `View License List`: Id, Name, Start Date, Expiry Days, Sessions, OfflineMode, search, `Create License` | ❌ | Nothing records issued licenses. The generator writes loose `.lic` files and the license server's database holds only sessions. |
| License server address | ✅ | `application.licenseServer.uri` |

The license format carries exactly the fields these screens show (`id`, `name`, `valid_since`, `ttl`,
`max_sessions`, `offline`). It has no per-feature entitlements, and none of the rendered screens asks for
any.

## 13. Settings / Network

| Wireframe field | Status |
| --- | --- |
| `Network` (nav item) | ❌ — no network configuration API |
| `Settings` (nav item) | ⚠️ — depends what it contains; `application.logger`, `gstreamer.plugins` and `licenseServer.uri` are config-backed |

---

## Consolidated gaps

### A. No backend at all (❌) — needs a product decision

1. Channel identity, and all channel CRUD (create, edit, clone, delete, archive, import, export)
2. Multi-channel operation — the engine is single-channel by construction
3. Aspect ratio
4. Frame format / interlace — dead code, engine forces Progressive
5. Archive rollover interval
6. Audio PID selection (only program selection exists)
7. Audio start alignment
8. SRT security / passphrase (AES-128)
9. Network mode
10. Audio gain
11. Fractional frame rates (29.97) — the parser is integer-only
12. Users, roles, login, password reset — **6 screens**
13. Alerts — **1 screen**
14. Log listing and download — **1 screen**
15. Health metrics: CPU, GPU, network in/out — **2 screens**
16. Input and output preview
17. Channel templates and input specifications
18. License administration — issued-license list and license-server start/stop — **2 screens**, vendor side

### B. Encoder passthrough (⚠️) — needs an API design decision

`bitrate`, `encoder`/encoding engine, `profile`, `level`, rate control, CABAC, B-frames, bit depth, tune,
speed preset, key-int-max, MPEG version, audio profile.

These are untyped string maps under `properties`/`caps`. Consequences for the UI:

- **A form cannot be generated from the schema** for any of them.
- **Property names change with the encoder.** If the UI hardcodes x264enc names and the operator picks a
  hardware engine, every setting silently stops applying.
- **Nothing is validated until the pipeline builds**, so a bad value surfaces as a failed engine start
  rather than a form error.

The fix is a server-side translation layer: expose typed fields (`bitrate`, `profile`, `rateControl`) and
map them to encoder properties per plugin. That is an API change, not a UI change.

### C. Wrong layer (🔀) — the UI must call a different route

Preview/probe, graphics, playlist editing, media upload, scheduled events, start/stop, license upload
(`/license`), license issuing (vendor-side CLI tools).
These work, but a "channel config" form that submits them together with `PUT /config` will not.

---

## Recommended decisions, in priority order

1. **Resolve the multi-channel question first.** Everything in §1 depends on it, and it may change the
   deployment model (one container per channel vs. a management layer). Nothing else in the UI is
   safely designable until this is settled.
2. **Decide typed-vs-passthrough for encoder settings** (§B). This determines whether the video/audio
   panels can be generated or must be hand-built per encoder.
3. **Cut or build the four dead controls** — aspect ratio, frame format, rollover interval, audio PID.
   Frame format is the most misleading: it presents a choice the engine ignores.
4. **Scope authentication.** Six screens assume it; the API has none, and the config write path is now
   live and unauthenticated.
5. **Place license administration.** The appliance side (`Licence` tab) is served by `/license`. The
   create/sign/list/start-stop screens need the signing key and belong in a vendor-side service, which
   does not exist yet beyond the CLI tools.
6. **Reconcile the channel-create screens to the HLS frame** (`733:253`), which already uses correct
   config vocabulary.
7. **Fix the frame-rate type** if 29.97 is required — the `Channel Status` screen already shows it, but
   the parser cannot express it.

## Evidence

| Claim | Source |
| --- | --- |
| Parser key list | `transcoder/app/src/application/config/parser/json/parser.cpp` |
| Interlace is dead code | `parser.cpp:975-984` (`#if 0`), `lib/utpl/src/utpl/transcoder/channel/processor/transcoder/video.cpp:23` |
| Frame rate is integer-only | `parser.cpp` — `Fraction(video["rate"], 1)` |
| SRT fields limited to latency/mode/bufferSize | `parser.cpp:319-347` |
| `pid` is SCTE-only | `parser.cpp:820` |
| `selector` supports only `program` | `parser.cpp:459-464` |
| Encoder properties are free-form strings | `openapi.yaml` — `GstEncoderParameters` |
| File output has no schema properties | `openapi.yaml` — `FileOutputTransportParameters` |
| Only `inputs[0]` is used; schema/parser divergence | `doc/codebase/27` |
| Route inventory | Running FastAPI app, 55 routes |
| Supervisor reports RUNNING with a dead input | Verified on the live container, 2026-09-08 |
