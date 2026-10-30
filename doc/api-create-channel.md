# Create Channel — REST API guide for the front end

Audience: the front-end developer building the **Create New Channel** section of the operator UI.

Scope: everything the UI needs to define a channel (inputs, outputs, encoders, SCTE) and bring it
on air, using the REST control plane in `api/`.

> **Status of this document**
>
> * It describes the code as pushed to GitHub today. Read it against `api/src/transcoder_api/`.
> * **The current feature work is not pushed yet — it lands after testing.** Build against the
>   contract below; the endpoints documented as *available* are stable and will not change shape.
> * **Some APIs this screen wants do not exist yet and are being fixed.** They are listed in
>   [Not available yet](#not-available-yet) with the workaround the UI should use in the meantime.
>   Do not design the screen around them until they are pushed.

---

## 1. What "a channel" is here

The appliance is **single-channel**. There is no channel table and no `POST /channels`.

A channel is one JSON document — the **engine configuration** — holding:

| Part | Meaning |
|---|---|
| `transcoder.inputs[]` | the source(s). More than one entry = ordered failover. |
| `transcoder.outputs[]` | the destinations, each with its own encoder ladder and SCTE policy. |
| `application` | logging and licensing. Optional. |

The engine **parses this document once, at start**. Nothing written through the API takes effect
until the engine is restarted. That single fact shapes the whole screen: *Create Channel* is a
provisioning form with an explicit **Apply & Restart** step, not a live editor.

The managed playlist/rundown side (`/playlists/main`, `/rundowns/main`) is a *separate* surface
that runs on top of an already-configured channel. The channel id is fixed to `main`
(env `MANAGED_PLAYLIST_ID`). It is out of scope for this screen except as the "what next" link.

---

## 2. Base URL and conventions

```
http://<appliance>:8080
```

* No authentication on the control plane today. Do not build a login flow against it.
* Request and response bodies are JSON unless stated.
* Errors from the application layer come back as:

  ```json
  { "message": "'transcoder.inputs' must not be empty", "code": "INVALID_REQUEST" }
  ```

  `code` is one of `INVALID_REQUEST`, `INVALID_TIME_RANGE`, `DETAILS_TYPE_MISMATCH`,
  `MISSING_FIELDS`, `UNAVAILABLE`. **The HTTP status carries the real meaning — switch on the
  status, not on `code`.**
* Some routes (`/config/check-transcoder`, `/config/restart-transcoder`) are plain FastAPI
  `HTTPException`s and return `{"detail": "..."}` instead. Handle both shapes.

### Status codes the Create Channel screen will see

| Status | Meaning | What the UI should do |
|---|---|---|
| `200` | OK | — |
| `400` | Malformed request body / bad query param | Field-level error |
| `404` | No config file on disk yet | Treat as "unconfigured", show an empty form |
| `409` | Destination unwritable, config directory missing, bind-mount trap, port already bound | Blocking banner — this is an install problem, not a form problem |
| `410` | Retired route | Bug in the client. Use `PUT /config`. |
| `422` | Document is structurally unusable, or JSON on disk is corrupt | Field-level error, message names the path |
| `500` | Supervisor or filesystem failure | Blocking banner, offer retry |

---

## 3. The create flow

```
 ┌─ 1. Load current state ──────────────────────────────────────────────┐
 │  GET /config                → existing document, or 404 if none      │
 │  GET /config/check-transcoder → engine RUNNING / STOPPED / ...       │
 └──────────────────────────────────────────────────────────────────────┘
                              │
 ┌─ 2. Define the input ───────────────────────────────────────────────┐
 │  GET /inputs/probe?uri=…&transport=…  → codecs, resolution, rate,   │
 │                                          PIDs, programs, warnings   │
 │  Use the result to prefill the output encoder defaults.             │
 └─────────────────────────────────────────────────────────────────────┘
                              │
 ┌─ 3. Validate the assembled document ────────────────────────────────┐
 │  PUT /config?dryRun=true    → 200 {"state":"validated"} or 409/422  │
 └─────────────────────────────────────────────────────────────────────┘
                              │
 ┌─ 4. Commit ─────────────────────────────────────────────────────────┐
 │  PUT /config                → 200 {"state":"written",               │
 │                                    "restartRequired": true}         │
 └─────────────────────────────────────────────────────────────────────┘
                              │
 ┌─ 5. Bring it on air ────────────────────────────────────────────────┐
 │  POST /config/restart-transcoder                                    │
 │  then poll GET /config/check-transcoder until RUNNING               │
 │  then confirm output: /hls/master.m3u8 or /player                    │
 └─────────────────────────────────────────────────────────────────────┘
```

Step 3 is not optional in the UI. `PUT /config` with `dryRun=true` is the only pre-flight the
API offers, and it also proves the config directory is writable before the operator commits.

---

## 4. Endpoints

### 4.1 `GET /config`

The whole engine configuration document.

```http
GET /config  →  200
```

```json
{
  "version": "1.1.0",
  "application": { "logger": { "...": {} } },
  "transcoder": { "inputs": [ … ], "outputs": [ … ] }
}
```

| Status | Cause |
|---|---|
| `404` | No file at `TRANSCODER_CONFIG_PATH` (default `/opt/utpl/etc/config.json`). **This is the "no channel yet" case** — open the form blank. |
| `422` | The file on disk is not valid JSON, or is not a JSON object. Show the message; it names the parse error. |

`GET /config/get-config` is the original spelling and still works, marked deprecated. Use
`GET /config`.

**Read-only convenience routes** (same data, narrower):

| Route | Returns |
|---|---|
| `GET /config/get-inputs` | `transcoder.inputs` array |
| `GET /config/get-input/{index}` | one input by **array index** |
| `GET /config/get-outputs` | `transcoder.outputs` array |
| `GET /config/get-output/{index}` | one output by **array index** |

`{index}` is the position in the array, not an id. If the index is out of range these return
**`200` with `{"message": "input_id doesn't exist"}`** — not a 404. Prefer `GET /config` and
index client-side; it is one request and has no such trap.

---

### 4.2 `GET /inputs/probe` — inspect a source before saving it

Drives the "Detect" / "Test source" button. Returns what is actually in the stream, so the UI can
prefill output resolution, frame rate, audio layout and the SCTE PID instead of asking the
operator to type them.

```http
GET /inputs/probe?uri=srt://103.248.72.80:8000?mode=caller&transport=srt&timeoutSeconds=15
```

> URL-encode `uri`. SRT URIs contain `?` and `&`.

| Query param | Required | Notes |
|---|---|---|
| `uri` | yes | The source endpoint. |
| `transport` | yes | `srt` \| `udp` \| `hls` \| `file` \| `playlist` \| `ndi` \| `rtmp`. Must match the URI scheme (`rtmp` covers `rtmp://` and `rtmps://`). |
| `timeoutSeconds` | no | 1–30. Default **10**; **20** for an SRT *listener*, which waits for an encoder to call in. Out of range → `400`. |
| `interfaceAddress` | no | Multicast interface for `udp`. |

Response (fields abbreviated):

```json
{
  "uri": "srt://103.248.72.80:8000?mode=caller",
  "transport": "srt",
  "probedUri": "srt://103.248.72.80:8000?mode=caller",
  "container": "mpegts",
  "isLive": true,
  "durationSeconds": null,
  "bitrateBps": 8000000,
  "waitedForCaller": false,
  "video": {
    "index": 0, "codec": "h264", "profile": "High",
    "width": 1920, "height": 1080,
    "frameRate": 25.0, "frameRateExact": "25/1",
    "scanType": "progressive", "fieldOrder": "progressive",
    "pixelFormat": "yuv420p", "bitrateBps": null, "pid": 256
  },
  "audio": [
    { "index": 1, "codec": "aac", "channels": 2, "channelLayout": "stereo",
      "sampleRateHz": 48000, "language": "eng", "isDefault": true, "pid": 257 }
  ],
  "data": [ { "index": 2, "codec": "scte_35", "pid": 4269 } ],
  "programs": [
    { "programNumber": 1, "serviceName": "…", "pmtPid": 4096,
      "pcrPid": 256, "streamIndexes": [0, 1, 2] }
  ],
  "warnings": ["…"]
}
```

How the form should use it:

| Probe field | Prefills |
|---|---|
| `video.width` / `.height` / `.frameRate` | output `streams[].video.width/height/rate` |
| `audio[0].sampleRateHz` / `.channels` | output `streams[].audio.rate/channels` |
| `data[].pid` where `codec == "scte_35"` | output `scte.pid` — and enable SCTE passthrough by default |
| `programs[]` with more than one entry | show a program picker → `inputs[].selector.program` |
| `warnings[]` | show as non-blocking notes. A second video stream, cover art, audio-only source and similar land here. |
| `video` absent, `audio` non-empty | audio-only source. This is a **success**, not an error. |

Failures:

| Status | Cause |
|---|---|
| `400` | `timeoutSeconds` out of range; URI/transport mismatch; `ndi` (probing NDI is not implemented — the message says so) |
| `404` | `file`/`playlist`: the media does not exist |
| `409` | SRT listener: the port is already bound |
| `422` | Probe timed out, ffprobe failed, stream carries no usable streams, empty playlist, playlist entry outside `MEDIA_DIR` |

Notes for the client:

* `scanType` is never guessed — `unknown` is a real answer, present it as such.
* Local filesystem paths are never echoed back in errors; don't expect one to point at.
* An SRT *listener* probe binds the port for up to `timeoutSeconds`. Show a live countdown and
  don't fire two listener probes at the same port concurrently — the second gets `409`.

---

### 4.3 `PUT /config` — write the channel

**The only write path for the engine configuration.** It replaces the document wholesale.

```http
PUT /config?dryRun=true
PUT /config
Content-Type: application/json
```

The body is the complete document (see §5). Partial bodies are not merged — whatever you send
*is* the new file.

Dry run:

```json
{ "state": "validated", "path": "/opt/utpl/etc/config.json", "restartRequired": false }
```

Real write:

```json
{ "state": "written", "path": "/opt/utpl/etc/config.json", "restartRequired": true }
```

`restartRequired` is **always `true`** on a successful write. Treat it as a reminder to run
step 5, not as a condition to test.

Validation is **structural only**, and deliberately so — the C++ parser accepts things the
published schema does not, so the API refuses only what would make the engine fail to start:

| Rule | Message on failure | Status |
|---|---|---|
| body is a JSON object | (FastAPI validation error) | `400` |
| `version` present | `config is missing 'version'` | `422` |
| `transcoder` is an object | `config is missing a 'transcoder' object` | `422` |
| `transcoder.inputs` is a **non-empty array** | `'transcoder.inputs' must be an array` / `must not be empty` | `422` |
| `transcoder.outputs` is a **non-empty array** | same, for outputs | `422` |
| every entry in both arrays is an object | `'transcoder.outputs[1]' must be an object` | `422` |
| `application`, if present, is an object | `'application' must be an object` | `422` |

**Unknown keys are preserved verbatim.** The API does not strip fields it does not recognise, so
a round trip `GET /config` → edit → `PUT /config` never loses operator hand-edits. The UI must
do the same: edit the document you fetched, do not rebuild it from just the fields the form
knows about.

Environment failures — these are install problems, present them as a blocking banner:

| Status | Message | Meaning |
|---|---|---|
| `409` | `the engine config directory does not exist: …` | bad mount |
| `409` | `the engine config directory is not writable: …` | read-only mount |
| `409` | `… is bind-mounted as a single file, so it cannot be replaced` | the config was mounted as a file, not a directory. The write is atomic (`os.replace`), which needs the containing directory. |
| `500` | `could not stage a replacement for …` / `could not replace …` | disk failure |

A rejected write leaves the previous file **untouched**. Nothing is half-applied.

---

### 4.4 `GET /config/check-transcoder` — engine state

```json
{ "message": "RUNNING" }
```

The value is the raw supervisor state name: `RUNNING`, `STARTING`, `STOPPED`, `STOPPING`,
`BACKOFF`, `FATAL`, `EXITED`, `UNKNOWN`.

* `BACKOFF` or `FATAL` after a restart means **the new config was rejected by the engine.** This
  is the main way a bad-but-structurally-valid config shows up. Say so explicitly in the UI and
  point the operator at the container logs.
* `500` with `Error communicating with Supervisor: …` means the API cannot reach supervisor —
  socket permissions or path. Not a config problem.

---

### 4.5 `POST /config/restart-transcoder` — apply the config

```json
{ "message": "Program 'transcoder' restarted successfully." }
```

* Stops the engine if it is `RUNNING` / `STARTING` / `BACKOFF`, waits a grace period
  (`TRANSCODER_RESTART_GRACE_SECONDS`, default **10s**), then starts it.
* A stopped engine is simply started — restarting a stopped channel is not an error.
* **The call blocks for the grace period.** Set a client timeout of at least 30s and show a
  determinate progress state; do not let the operator fire it twice.
* `GET /config/restart-transcoder` also works and is **deprecated** — it has side effects. Use POST.
* `500` → supervisor unreachable.

**This takes the channel off air for the duration.** The UI must confirm before calling it on a
channel that is currently `RUNNING`.

### 4.6 Retired routes — do not call

`POST /config/set-inputs`, `/set-outputs`, `/set-input/{id}`, `/set-output/{id}` all return
**`410 Gone`** with:

> this route never wrote anything and has been retired; use PUT /config to replace the whole
> configuration document

They are still in the OpenAPI document as deprecated. If any existing front-end code calls them,
it has been silently writing nothing.

---

## 5. The channel document

Full field reference: [configuration-reference.md](configuration-reference.md). Below is what the Create
Channel form needs to produce. A shipped working example is in [config/config.json](../config/config.json).

### 5.1 Skeleton

```json
{
  "version": "1.1.0",
  "transcoder": {
    "inputs":  [ { "transport": { "uri": "…", "<scheme>": { … } } } ],
    "outputs": [ { "name": "…", "type": "single", "container": "mpegts",
                   "transports": [ … ], "streams": [ … ], "scte": { … } } ]
  }
}
```

`version` gates parser behaviour. Below `1.4.0` the parser makes `outputs[].container`
**mandatory** — the shipped config is `1.1.0`, so always emit `container`.

### 5.2 Input

One entry per source; **multiple entries are failover alternatives, in order.**

```json
{
  "transport": {
    "uri": "srt://103.248.72.80:8000?mode=caller",
    "srt": { "mode": "caller", "latency": 120 }
  },
  "selector": { "program": 1 }
}
```

The **presence of the sub-block selects the transport type**, and it must match the URI scheme.
Schemes: `file://` `udp://` `srt://` `http(s)://` `hls+file://` `pls+file://` `ndi://` `rtmp(s)://` `s3://`.

| Transport | Sub-block | Fields the form should expose |
|---|---|---|
| `srt` | `srt` | `mode`: `caller` when the URI host is remote, `listener` when it is `0.0.0.0`/local — **derive this, don't ask**. `latency` ms, default `120`, ~4× RTT on a lossy WAN. `bufferSize` advanced/omit. |
| `udp` | `udp` | `multicast.interfaces[]` — interface names like `eth0`. Operator-supplied; never guess. |
| `hls` | `hls` | `bandwith` (**note the misspelling — the parser reads `bandwith`; `bandwidth` is ignored**), `credentials.username`/`.password`, `scte.source`: `segments` \| `manifest`. |
| `file` | `file: {}` | marker only, no fields |
| `playlist` | `playlist: {}` | marker, used with `pls+file://`. `mode` and `id` appear in shipped configs but **the parser does not read them** — do not emit them. |
| `ndi` | `ndi` | `ndiName` or `urlAddress`. Not probeable — the form must accept these untested. |

`selector.program` only for multi-program MPEG-TS; omit otherwise. Populate the picker from
`probe.programs[]`.

### 5.3 Output

```json
{
  "name": "hls-output",
  "type": "single",
  "container": "mpegts",
  "transports": [ { "uri": "hls+file:/runtime/hls",
                    "hls": { "numSegments": 6, "segmentLength": 4,
                             "singleDirectory": true, "vodMode": false,
                             "adMarkers": ["enhanced"] } } ],
  "streams": [ … ],
  "scte": { "mode": "passthrough", "pid": 4269 }
}
```

| Field | Values | Guidance for the form |
|---|---|---|
| `type` | `single` \| `group` | `single` unless building an HLS ladder. `group` is an HLS multi-variant parent and is HLS-only. |
| `name` | string | Only strictly needed for grouping, but always emit a stable slug — it is how the operator identifies the row. |
| `container` | `mpegts` \| `es` \| `fmp4` | `mpegts` for TS/SRT/UDP/HLS-TS; `fmp4` HLS only; `es` raw. |
| `transports[]` | — | Required for a standalone `single` and for a `group`; **ignored** for a `single` inside a group. |
| `scte.mode` | `passthrough` \| `removal` | Default to `passthrough` when the probe found a `scte_35` data stream, else `removal`. |
| `scte.pid` | 0–8192 | Prefill from the probe's SCTE PID; `500` if unknown. |
| `scte.nullInterval` | ms, default `0` | `0`. ~`100` only for a downstream splicer heartbeat. Advanced. |

Output transport sub-blocks mirror the input side: `.srt` (`mode`, `latency`, `bufferSize`),
`.udp`, `.hls`, `.s3`, `.file`. For an **HLS ladder**, a `group` output holds only `type`,
`name` and `transports`; each variant is a separate `single` output carrying a `link` stream
naming the group, and inherits `container`/`scte` from it.

### 5.4 Streams (the encoder ladder)

At least one stream per output; an empty `streams[]` is a parse error at engine start.

```json
{ "type": "video", "codec": "h264",
  "video": { "width": 1920, "height": 1080, "rate": 25, "format": "I420" },
  "h264": {
    "encoder": "x264enc",
    "properties": { "key-int-max": "50", "tune": "zerolatency",
                    "speed-preset": "veryfast", "bitrate": "6000" },
    "caps": { "profile": "high" }
  } }
```

```json
{ "type": "audio", "codec": "aac",
  "audio": { "rate": 48000, "channels": 2 } }
```

Two things the form must get right:

1. **The codec name is also the key of the encoder block.** `"codec": "h264"` pairs with an
   `"h264": { … }` object; `"codec": "ac3"` with `"ac3": { … }`. Rename the codec, rename the block.
2. **`properties` values are strings, even numeric ones.** `"bitrate": "6000"`, not `6000`. They
   are passed straight through to the GStreamer element. Nothing validates them here — a typo
   surfaces as a `BACKOFF` engine state after restart, with the reason only in the logs. Keep the
   UI to a curated list of known properties rather than a free key/value grid.
3. **Audio bitrate goes in the encoder block, not `audio.bitrate`.** The engine parses
   `audio.bitrate` and never uses it. The channel UI writes `"aac": { "encoder": "fdkaacenc",
   "properties": { "bitrate": "128000" } }` (bits/s; `avenc_mp2` and `avenc_ac3` likewise) and,
   for AC-3 at 48 kHz, `"ac3": { "encoder": "dlbpatenc", "properties": { "bitrate": "192k" } }`.
   It keeps an encoder the config already names.
4. **Frame format maps to `video.interlace` and `video.fieldOrder`.** Progressive is written by
   omitting both; interlaced is `"interlace": "interleaved"` with `"fieldOrder": "tff"` or
   `"bff"`, and is H.264 only.

`properties` and `caps` are element-specific and **not enumerable through the API** — see
[Not available yet](#not-available-yet). Source the option lists from
[configuration-reference.md § `outputs[].streams[].<codec>`](configuration-reference.md).

---

## 6. After the channel is up

Not part of this screen, but these are what the "channel created" success state should link to:

| Surface | Route |
|---|---|
| Live HLS manifest | `GET /hls/master.m3u8` (CORS-open, `no-store`) |
| Live DASH manifest | `GET /dash/manifest.mpd` (or `/dash/<baseFileName>.mpd`) for a `dash+file:/runtime/dash` output; served from `DASH_DIR` like `/hls` |
| Browser monitor page | `GET /player` |
| Operator UI | `GET /operator` |
| Managed playlist snapshot | `GET /playlists/main` |
| Rundown availability | `GET /rundowns/status` — returns `enabled: false` with a reason when the rundown workspace is off; reachable even then |

A channel whose input is a playlist (`pls+file://`) also needs media and a rundown:
`POST /media` (multipart upload) or `POST /media/import`, then `POST /rundowns/main/seed` to
publish a first version from what is already running, then `POST /rundowns/main/apply` to edit it.

---

## 7. Not available yet

Being fixed; not in the pushed code. **Do not build the screen assuming these exist** — each row
names what to do in the meantime.

| Missing | Effect on the screen | Workaround for now |
|---|---|---|
| **Channel CRUD** — `POST/GET/DELETE /channels` | There is no list of channels and no channel id to pass anywhere. "Create Channel" is really "configure *the* channel", and creating a second one overwrites the first. | Single-channel UI. If a config already exists (`GET /config` ≠ 404), warn that saving replaces the running channel. |
| **Per-section config writes** | Every save sends the whole document, so two operators editing different sections overwrite each other. There is no ETag, revision or `expectedVersion` on `PUT /config`. | `GET /config` immediately before every `PUT`, diff client-side, and warn if it changed under the operator. |
| **Output destination validation** | `/inputs/probe` covers sources only. A typo'd SRT destination or unwritable HLS directory is not caught until after restart. | Client-side URI syntax checks, plus a clear "verify after restart" step. |
| **Encoder / codec / property enumeration** | The UI cannot discover which encoders and which `properties` keys the build supports. | Hardcode the lists from `configuration-reference.md`; keep them in one client-side constants module so they are cheap to update. |
| **Structured engine health** | `check-transcoder` returns a supervisor state string only — no start error, no per-output status, no bitrate or connection state. | Poll `check-transcoder`; treat `BACKOFF`/`FATAL` as "config rejected" and tell the operator to check container logs. |
| **Start / stop separate from restart** | Only `restart-transcoder` exists. There is no way to take a channel off air and leave it off, or to start without a stop. | Expose one **Apply & Restart** action. Do not offer Start/Stop buttons. |
| **Config templates / presets** | No server-side presets endpoint. | Ship presets as client-side JSON fixtures (SRT→HLS, SRT→SRT — mirror `config/config.json` and `config/srt_to_hls_srt.json`). |

Playlist-side gaps worth knowing, since they bound what the success state can offer:
`POST /playlists/{id}/start` and `/pause` return **`410`** — managed playlists run continuously;
only `skip` is available. Playlist mutations are optimistic-concurrency controlled via
`expectedRevision` and return `409 "playlist revision is stale"` when it does not match.

---

## 8. Worked example — SRT in, HLS + SRT out

```http
GET /inputs/probe?uri=srt%3A%2F%2F103.248.72.80%3A8000%3Fmode%3Dcaller&transport=srt
```

→ h264 1920×1080 @25, aac 48 kHz stereo, `scte_35` on PID 4269.

```http
PUT /config?dryRun=true
```

```json
{
  "version": "1.1.0",
  "application": {
    "logger": {
      "transcoder": { "level": "log",
        "console": { "enable": true, "colors": true, "stderr": true } },
      "gstreamer": { "level": "warning",
        "console": { "enable": true, "colors": true, "stderr": false } }
    }
  },
  "transcoder": {
    "inputs": [
      { "transport": { "uri": "srt://103.248.72.80:8000?mode=caller",
                       "srt": { "mode": "caller", "latency": 120 } } }
    ],
    "outputs": [
      {
        "name": "hls-output",
        "type": "single",
        "container": "mpegts",
        "transports": [
          { "uri": "hls+file:/runtime/hls",
            "hls": { "numSegments": 6, "segmentLength": 4,
                     "singleDirectory": true, "vodMode": false,
                     "adMarkers": ["enhanced"] } }
        ],
        "streams": [
          { "type": "audio", "codec": "aac",
            "audio": { "rate": 48000, "channels": 2 } },
          { "type": "video", "codec": "h264",
            "video": { "width": 1920, "height": 1080, "rate": 25, "format": "I420" },
            "h264": { "encoder": "x264enc",
                      "properties": { "key-int-max": "50", "tune": "zerolatency",
                                      "speed-preset": "veryfast", "bitrate": "6000" },
                      "caps": { "profile": "high" } } }
        ],
        "scte": { "mode": "passthrough", "pid": 4269 }
      },
      {
        "name": "srt-output",
        "type": "single",
        "container": "mpegts",
        "transports": [
          { "uri": "srt://0.0.0.0:9000?mode=listener",
            "srt": { "mode": "listener", "latency": 120 } }
        ],
        "streams": [
          { "type": "audio", "codec": "ac3",
            "audio": { "rate": 48000, "channels": 2 },
            "ac3": { "encoder": "dlbpatenc", "properties": { "bitrate": "192k" } } },
          { "type": "video", "codec": "h264",
            "video": { "width": 1920, "height": 1080, "rate": 25, "format": "I420" },
            "h264": { "encoder": "x264enc",
                      "properties": { "key-int-max": "50", "tune": "zerolatency",
                                      "speed-preset": "veryfast", "bitrate": "6000" },
                      "caps": { "profile": "high" } } }
        ],
        "scte": { "mode": "passthrough", "pid": 4269 }
      }
    ]
  }
}
```

→ `200 {"state":"validated","restartRequired":false}`

```http
PUT /config                       → 200 {"state":"written","restartRequired":true}
POST /config/restart-transcoder   → 200 (blocks ~10s)
GET /config/check-transcoder      → poll until {"message":"RUNNING"}
GET /hls/master.m3u8               → confirm segments are rolling
```

---

## 9. Source of truth

| Topic | File |
|---|---|
| Config routes | `api/src/transcoder_api/routers/config.py` |
| Config validation & atomic write | `api/src/transcoder_api/services/engine_config.py` |
| Probe route | `api/src/transcoder_api/routers/input.py` |
| Probe implementation & timeouts | `api/src/transcoder_api/services/probe.py` |
| Response models | `api/src/transcoder_api/model/rest_api.py` |
| Error → HTTP status mapping | `api/src/transcoder_api/exceptions.py` |
| Every config field | `configuration-reference.md` |
| Working example config | `config/config.json` |

The live OpenAPI document is served at **`GET /openapi.json`**, with Swagger UI at **`/docs`**.
Generate the client from it rather than hand-writing request types — but note it still lists the
retired `set-*` routes as deprecated rather than gone.
