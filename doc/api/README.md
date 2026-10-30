# Input and Playlist API

```bash
API=http://<host>:8080
```

The API has no built-in authentication. Use it only on a trusted management network.

## Operator frontend

Open `http://<host>:8080/operator`.

The Compose profile binds this as `http://127.0.0.1:8080/operator`; its separate old-style playout
page is at `http://127.0.0.1:8080/operator/playout`. View the MPEG-TS output in VLC with
`udp://@:5555`. UDP 5556 is an identical diagnostic mirror intended for `ffprobe` or capture tools.
The supplied output contract is fixed H.264 1920×1080 at 25 fps, including when its playlist files
are lower resolution or frame rate.

Graphics cues accept `startInMs` values from `0` upward; `0` starts immediately. Scheduled events
may omit `time` for immediate execution or provide a current/future ISO 8601 timestamp up to 14 days
away. The former 15-second minimum lead-time rule no longer applies.

The managed on-air queue currently contains native/high-resolution Air Asia and Coke assets; One
Cent Call remains registered for operator insertion and verification. The old 320×180 files remain
in `media/queue` for diagnostics but should not be used for quality review. Compose ports 5555 and
5556 use a VLC-compatible single-slice preview relay. It preserves AAC 48 kHz stereo and the data
PID, but adds a second video encode and corresponding CPU/memory cost.

`GET /playlists/main` exposes `desiredRevision`, `appliedRevision` and `applyError`. Queue
mutations carry the current `expectedRevision`; a `202` means the persistent desired queue was
accepted, and it becomes live when `appliedRevision` catches up. The unified operator page polls
and displays that state automatically.

The unified **Operator** page combines live playlist, graphics and SSAI. It reads the actual engine
status from `GET /inputs`; Skip is an immediate switch, while persistent Insert, Reorder and Remove
are reconciled snapshots. Start/Pause and the old decorative progress indicator are not exposed.

- **Playlist** supports upload/import, duplicate enqueue, insert-after, reorder, remove and skip.
  It displays the running item and an Applying state until reconciliation succeeds.
- **Graphics** exposes Logo, Bug, static L-band/Aston sponsors, lifecycle countdowns, reload recovery,
  and functional Stop. L-band and Aston share a backend lock, so both Start buttons are unavailable
  while either cue is scheduled, on air, or restoring.
- **SSAI** schedules Cue Out and Cue In. Repeating browser-only automation is not provided.

With `docker-compose.playlist.yml`, static graphic files are mounted from `media/graphics` at
`/media/graphics` inside the container. Add new entries to
`api/src/transcoder_api/assets/graphics_catalog.json`; media URIs must be visible to the transcoder,
while thumbnails must be HTTP paths below `/operator/static`.

The catalog also contains shortened L-band and Aston MOV assets. They are returned with
`enabled: false` while `ENABLE_VIDEO_GRAPHICS=false` (the Compose default). Do not enable the flag
until the decoder's alpha, timing, cancellation, 20-cycle, and memory gates pass.

## Postman

A ready-made collection covering every endpoint, plus seven runnable multi-call workflows, is exported
to [`postman/`](postman/). Import both files, select the `skandha-local` environment, and run
`W0 Smoke test`. It documents three behaviours that are easy to get wrong: the rundown owns the managed
queue, rundown applies cannot overlap, and `DELETE /media` unlinks the file from disk.

## Optional YAML rundowns and the Program Schedule

For endpoint-by-endpoint Postman usage, see [Rundown API Guide](RUNDOWN_API_GUIDE.md).

`ENABLE_RUNDOWN_SERVICE` is `true` in the supplied Compose file, because the declarative channel
bootstrap (`PLAYLIST_BOOTSTRAP_FILE`) writes rundown versions. Its code default is `false`, which
leaves normal M3U8 and managed-queue operation unchanged. When enabled before API startup, it first
creates a consistent `DB_PATH.before-rundown-v1` backup and exposes the `/rundowns/{id}` apply,
import, seed, occurrence, execution, and audit endpoints. A schema at `2026-08-11-rundown-v2` is backed
up as `DB_PATH.before-rundown-v3` and only `rundown_drafts` is dropped, so publish and as-run history
survive; anything older is backed up as `DB_PATH.before-rundown-v2` and the rundown tables are dropped
and rebuilt. A published version projects to
the existing managed queue; it never changes the engine's M3U8 parser. See the
[rundown service reference](../codebase/30-transactional-rundown-service.md). The operator rundown grid uses
`/rundowns/status`, `POST /rundowns/{id}/apply`, and `POST /rundowns/{id}/time/resolve` for
timezone-safe Date/Time entry. It remains unavailable until the same feature flag is explicitly enabled
after acceptance testing. Duration, offset, and fade stay millisecond fields on the API even though the
Program Schedule displays decimal seconds.

Graphics catalog assets may include additive `defaultMode` and `defaultAnimationMs` values. New manual
and Program Schedule cues use those defaults; existing cue requests still supply an explicit `mode`.

`POST /rundowns/{id}/apply` is the single write path: it takes either an ordered `ops` list or a whole
`document`, validates the merged result, and publishes it in one transaction. `dryRun: true` validates
and rolls back. A stale `expectedVersionId` returns `409`. Apply also rewrites the declarative channel
file and reports the outcome in `channelFile`, so an applied change survives a restart. There is no
draft: a change is either on air or unapplied in the client.

`POST /rundowns/{id}/programs/import` uploads a channel YAML and splices its programs into the live
schedule at an optional `position`. It imports a program *list*, not the media, so upload clips with
`POST /media` first (one or more files per call, one result per file). `POST /rundowns/{id}/seed` publishes a first version from the running playlist for
a channel that has never published one.

`POST /rundowns/{id}/reset` accepts `{ "expectedVersionId": "…" }` and immediately publishes a
programs-only replacement of the live version through the same validated apply path. It does not
delete media, manual cues, or execution history, and it is recorded in the audit log as
`schedule_reset`.

## 1. Find inputs and switch targets

```http
GET /inputs
GET /inputs/{inputId}
```

```bash
# Number of logical inputs created by the engine
curl -sS "$API/inputs" | jq 'length'

# Number of switchable streams in the first input
curl -sS "$API/inputs" | jq '.[0].streams | length'

# Show inputs, stream UUIDs, URIs, states, and current selection
curl -sS "$API/inputs" | jq .
```

For a playlist, each entry in `streams` is one switchable playlist item. Do not use `GET /config/get-inputs` to count effective engine inputs: this version only uses `transcoder.inputs[0]`.

## 1b. Probe a source before you configure it

```http
GET /inputs/probe?uri=...&transport=srt|udp|hls|dash|file|playlist
```

Answers "is this source alive, and what is it carrying?" for a URI the operator has typed but
not yet saved. It runs `ffprobe`, not the engine, so it works before a channel exists.

**Probe before you start the channel, not during.** The route opens the source itself, and a
second reader on a single-subscriber source - an SRT caller target, or a UDP port the engine
already holds - can steal or duplicate the subscription.

```bash
# A clip under MEDIA_DIR
curl -sS -G "$API/inputs/probe" \
  --data-urlencode "uri=one-cent-call-1080p.mp4" \
  --data-urlencode "transport=file" | jq '{container, bitrateBps, video, audio}'

# A live UDP source, bounded at 8 seconds
curl -sS -G "$API/inputs/probe" \
  --data-urlencode "uri=udp://239.1.1.1:5000" \
  --data-urlencode "transport=udp" \
  --data-urlencode "timeoutSeconds=8" | jq '.video, .programs'

# Multicast on a multi-homed appliance: name the NIC, as transport.udp.multicast.interfaces does
curl -sS -G "$API/inputs/probe" \
  --data-urlencode "uri=udp://239.1.1.1:5000" \
  --data-urlencode "transport=udp" \
  --data-urlencode "interfaceAddress=10.0.0.5" | jq .

# SRT. latency is MILLISECONDS here, matching transport.srt.latency in the configuration;
# probedUri shows the microseconds libsrt was actually given.
curl -sS -G "$API/inputs/probe" \
  --data-urlencode "uri=srt://10.0.0.1:9000?mode=caller&latency=125" \
  --data-urlencode "transport=srt" | jq '.probedUri, .video'

# SRT listener: binds the port and waits for an encoder to call in (default 20 s)
curl -sS -G "$API/inputs/probe" \
  --data-urlencode "uri=srt://0.0.0.0:9000?mode=listener" \
  --data-urlencode "transport=srt" | jq '.waitedForCaller, .video'

# A playlist reports its FIRST ENTRY, not the M3U8 - the playlist is a schedule, not a container
curl -sS -G "$API/inputs/probe" \
  --data-urlencode "uri=pls+file:///runtime/main.m3u8" \
  --data-urlencode "transport=playlist" | jq '.probedUri'
```

Read `warnings` as well as the values. It is where the probe says a frame rate came from
`r_frame_rate` rather than `avg_frame_rate`, that a second video stream was ignored, or that no
bitrate was available.

Things worth knowing before you file a bug:

* **`bitrateBps` is null for `udp`, `srt` and `hls`.** ffmpeg derives it from size over
  duration and a live socket has neither. Only `file` and `playlist` report it.
* **`pid` is populated only for MPEG-TS.** An MP4 has `id` values too, but those are track
  numbers, so they are deliberately not reported as PIDs.
* **`scanType` is `unknown`, never assumed `progressive`.** If ffprobe could not determine the
  field order in the time allowed, the answer is "unknown", not a guess.
* **`transport` must match the URI scheme.** `transport=hls` with a `file://` URI is a 400, not
  a reinterpretation. Local paths are confined to `MEDIA_DIR`.
* **`ndi` always returns 422.** ffmpeg has no NDI input protocol.

Status codes: 400 for a URI the route will not accept, 404 for a missing file or playlist, 409
when two probes overlap or a listener port is already bound, 422 when the source cannot be
probed - unreachable, timed out, or not media.

## 2. Switch an input stream

```bash
INPUT_ID=$(curl -sS "$API/inputs" | jq -r '.[0].id')
TARGET_ID=$(curl -sS "$API/inputs" | jq -r '.[0].streams[1].id')

curl -sS -X POST "$API/inputs/$INPUT_ID" \
  -H 'Content-Type: application/json' \
  --data "{\"currentStreamId\":\"$TARGET_ID\"}" | jq .
```

The switch is asynchronous. Poll until it completes:

```bash
curl -sS "$API/inputs/$INPUT_ID" | jq -r '.currentStreamId'
```

Errors: unknown input/stream returns HTTP 404; a missing `currentStreamId` returns HTTP 400.

## 3. Add a playlist input

There is no working REST API to add a playlist. Configure it before starting or restarting the transcoder.

`playlist.m3u8`:

```m3u8
#EXTM3U
#EXTINF:-1,
file:///media/input-a.ts
#EXTINF:-1,
file:///media/input-b.ts
```

Configuration:

```json
{
  "transcoder": {
    "inputs": [
      {
        "transport": {
          "uri": "pls+file:///media/playlist.m3u8",
          "playlist": {}
        }
      }
    ]
  }
}
```

Use an absolute path visible to the transcoder. Playlist input cannot use audio/video passthrough; configure explicit output codecs.

The following routes are broken and must not be used for playlist deployment:

```http
POST /config/set-input/{index}
POST /config/set-inputs
```

## Limits

- Only `transcoder.inputs[0]` becomes an engine input.
- A nested `pls+file://...` playlist is unsupported. Flatten its items into one M3U8 file.
- An HTTP HLS `.m3u8` can be one playlist item, but it is not expanded into nested switch targets.

For full details, see the [control-plane reference](../codebase/17-rest-grpc-and-sqlite-control-plane.md) and [runtime verification](../codebase/28-playlist-input-api-runtime-verification.md).
