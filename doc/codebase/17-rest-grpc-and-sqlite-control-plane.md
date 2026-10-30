# REST, gRPC, and SQLite Control Plane

The control plane consists of a FastAPI HTTP service, a private Unix-socket gRPC service implemented by the C++ transcoder, and a shared SQLite event database. It is a management interface, not a media transport.

The API also exposes a same-origin operator UI at `/operator`, graphics catalog/cue endpoints under
`/graphics`, and persisted managed-media/playlist endpoints under `/media` and `/playlists/{id}`.

The operator is a trusted-management surface. It reconstructs queue and cue ownership from server
state after reload, renders API strings through text DOM APIs, and does not implement authentication;
the supplied Compose profile therefore binds port 8080 to loopback.
The queue API represents desired state until the C++ playlist reconciliation layer has applied its
revision; clients must inspect `desiredRevision` versus `appliedRevision`.

An optional, disabled-by-default rundown layer exposes versioned YAML authoring below `/rundowns`.
It validates registered media and graphics catalog entries, then publishes an applied change as a
normal managed-playlist revision. See [30](30-transactional-rundown-service.md); it creates no
tables until `ENABLE_RUNDOWN_SERVICE=true` is set before API startup. `GET /rundowns/status` is the
deliberate exception to the route gating: it answers while the service is disabled so the UI can
state why scheduling is unavailable. It also carries the declarative channel bootstrap fields
(`bootstrapManaged`, `bootstrapFile`, `bootstrapState`, `bootstrapAppliedAt`, `bootstrapChecksum`,
`bootstrapError`, `bootstrapWarnings`), all defaulted so pre-existing clients are unaffected.

### Engine process control

`transcoder` and `transcoder_api` are separate supervisor programs, so every process-control
route moves the engine without disturbing the control plane answering the request. That is what
makes a stop route safe to expose at all: the API, the operator UI and the channel UI all stay
reachable while the channel is dark.

`restart-transcoder` is disruptive but self-healing - it ends with the channel back on air.
`stop-transcoder` is not: nothing restores the channel but an explicit `start-transcoder`. The
UI reflects that distinction with its own confirmation dialog rather than reusing the restart
one, whose "about 10 seconds" promise would be false.

Both stop and start are idempotent. Supervisor raises `NOT_RUNNING` (70) when stopping a
stopped process and `ALREADY_STARTED` (60) when starting a running one; both routes treat those
as success and report the state, because a caller asking for "stopped" got what it asked for.
Any other fault is a 500. Both return the state supervisor reports after the call, so a start
normally answers `STARTING` - poll `check-transcoder` for the outcome, since a configuration the
engine rejects surfaces as `BACKOFF` or `FATAL` rather than as a failure of the start.

The reason the stop route exists is probing. `GET /inputs/probe` opens the source itself, so the
input the engine currently holds cannot be probed while it runs; stopping releases the sockets.

### The channel configuration UI at `/channel`

A second same-origin page, separate from `/operator`. It is the provisioning screen: it reads
`GET /config`, validates with `PUT /config?dryRun=true`, commits with `PUT /config`, restarts
with `POST /config/restart-transcoder`, then polls `GET /config/check-transcoder` until the
supervisor reports `RUNNING`. It also drives `GET /inputs/probe` to fill an output ladder from
the source the operator typed in.

It does not replace `/operator`, and the two do not overlap: playout, rundown, graphics and
slates live only on the operator page, and engine configuration lives only here.

Source is `frontend/` (Vite, React 18, react-hook-form + zod, TanStack Query). `npm run build`
writes into `api/src/transcoder_api/static/channel`, which is both committed and rebuilt by the
`frontend-builder` stage in `docker/utpl-transcoder/Dockerfile`. Two cache rules are
load-bearing and pinned by `api/tests/channel_ui_test.py`:

* the index is `no-store`, because it names content-hashed assets and a cached copy keeps
  pointing at the previous build's bundles after an upgrade;
* the assets are `immutable`, because their names already change with their contents.

The Vite `base` is `/channel/`. Building without it produces a page whose every asset 404s.

Probing is blocked in the page while the supervisor reports `RUNNING` or `STARTING`.
`GET /inputs/probe` opens the source itself, so probing a single-subscriber source - an SRT
caller target, or a UDP port the engine already holds - can steal or duplicate the live
subscription. That guard is the UI's, not the API's.

The current operator UI deliberately exposes the live gRPC-backed `/inputs` switcher separately
from that desired queue. Its four-column Logo/L-band/Bug/Aston graphics layout, asset preview,
upload/drop workflow, and SSAI controls were migrated from the old frontend, but every active
control now calls this same-origin API. Logo and Bug remain empty catalog groups because no usable
old assets existed for them.

The unified `/operator` page now presents Playlist, Graphics, SSAI and Activity together. Managed
queue mutations persist desired SQLite state and a single API reconciler applies ordered snapshots
to the running `PlaylistInput` by private gRPC. Clients must wait for `appliedRevision` to equal
`desiredRevision`; `applyError` reports a rejected or unavailable application. Start/Pause remain
unsupported for the continuous-loop profile. Skip stays a direct live input switch.

For the focused operator guide to input enumeration, switching, and playlist configuration, see [the Input and Playlist REST API README](../api/README.md).

```mermaid
flowchart LR
  Client[REST client] --> API[FastAPI :8080]
  API -->|input status/switch| GRPC[Unix gRPC socket]
  GRPC --> Engine[C++ transcoder engine]
  API -->|event CRUD| DB[(events.sqlite)]
  Engine -->|poll unread events| DB
  API -->|config read/write and process control| FS[/opt/utpl/etc/config.json]
  API --> Supervisor[supervisor.sock]
```

## Runtime and generated files

The checked-in Python source imports generated files that are intentionally absent from this checkout:

- `src/transcoder_api/model/rest_api.py`, generated from `api/api.yaml`.
- `src/transcoder_api/transcoder_pb2.py`, its `.pyi`, and `transcoder_pb2_grpc.py`, generated from `transcoder/app/grpc/proto/transcoder.proto`.

Prepare the service before running it:

```bash
cd api
poetry install --with dev
poetry run poe codegen
DB_PATH=/path/to/events.sqlite poetry run poe run
```

The development command listens on `0.0.0.0:8080`. FastAPI also exposes generated interactive documentation at `/docs`, `/redoc`, and `/openapi.json`. Those runtime documents include the handwritten `/config` routes, whereas checked-in `api/api.yaml` does not.

There is no authentication or authorization middleware in the route source. Place the service behind a trusted management boundary and an authenticated reverse proxy; this is especially important because `/config` can overwrite configuration and restart the transcoder.

## HTTP conventions

| Item | Behavior |
| --- | --- |
| Content type | JSON request and response bodies. |
| Time format | Timezone-aware ISO 8601 for REST; stored in UTC SQLite text format. |
| Validation errors | Custom handler returns HTTP 400 with `{"message": "...", "code": "INVALID_REQUEST"}`. |
| Service errors | `TranscoderApiException` supplies its own HTTP status and one of the error codes below. |
| Identifiers | Input/stream IDs are UUID-formatted strings in REST. Scheduled-event IDs are integers. |
| Ordering | `GET /scheduled_events` has no explicit SQL `ORDER BY`; do not depend on response order. |

Error codes are `INVALID_REQUEST`, `INVALID_TIME_RANGE`, `DETAILS_TYPE_MISMATCH`, `MISSING_FIELDS`, and `UNAVAILABLE`.

## Route summary

| Method | Path | Purpose | Effective success status |
| --- | --- | --- | --- |
| GET | `/` | Liveness-style gateway message | 200 |
| GET | `/inputs` | List engine inputs and their streams | 200 |
| GET | `/inputs/probe` | Probe a prospective input URI with ffprobe | 200 |
| GET | `/inputs/{id}` | Get one input | 200 |
| GET | `/inputs/{id}/preview.jpg` | Latest preview frame of a live input (640 px JPEG); each request keeps the engine's preview on for 30 s | 200, 404, 503 |
| GET | `/inputs/{id}/preview.mjpeg` | Live preview as `multipart/x-mixed-replace` (MJPEG, up to 10 fps) for an `<img>` tag | 200, 404, 503 |
| POST | `/inputs/{id}` | Select the current stream | 200 |
| GET | `/operator` | Unified operator dashboard | 200 |
| GET | `/operator/tools/squeeze-box` | Graphics geometry authoring tool | 200 |
| POST | `/config/stop-transcoder` | Stop the engine, leave it stopped; API stays up | 200 |
| POST | `/config/start-transcoder` | Start a stopped engine | 200 |
| GET | `/channel` | Channel configuration UI (React SPA) | 200/404 |
| GET | `/channel/assets/*` | Content-hashed SPA bundles | 200/404 |
| GET | `/graphics/catalog` | List validated graphics assets and modes | 200 |
| GET | `/graphics/cues` | List cue/event ownership records | 200 |
| POST | `/graphics/cues` | Create an overlay and optional squeeze cue | 201 |
| DELETE | `/graphics/cues/{id}` | Cancel every scheduled event owned by a cue | 204 |
| GET/POST/DELETE | `/media*` | List, upload, import, and delete managed media | 200/201/204 |
| GET | `/media/{id}/thumbnail` | One cached JPEG frame of a media asset (`at` seconds, `width` px) | 200, 404, 422 |
| POST | `/media` | Upload one or more files in one multipart call (repeat the `file` field). Each file is stored or rejected on its own; the reply lists every outcome | 201 all stored, 207 any rejected, 422 over `MEDIA_MAX_UPLOAD_FILES` |
| GET/POST/PATCH/DELETE | `/playlists/{id}*` | Read and mutate desired managed queue state | 200/202 |
| POST | `/playlists/{id}/skip` | Switch to the next live stable item without changing queue revision | 202 |
| GET/POST | `/rundowns/{id}*` | Optional rundown control plane: one atomic validated `apply`, YAML program import, seed, loop clock, as-run, and audit | 200/201/202/409/422 |
| GET/POST/DELETE | `/graphics/cues*` | List lifecycle state, create a cue, or cancel all events owned by a cue | 200/201/204 |
| GET | `/scheduled_events` | List/filter scheduled events | 200 |
| POST | `/scheduled_events` | Create or update events | 200 |
| DELETE | `/scheduled_events/{id}` | Delete one event | 200 |
| GET | `/config` | Read the complete JSON config | 200, 404, or 422 |
| PUT | `/config` | Replace the complete JSON config; `?dryRun=true` validates only | 200, 400, 409, or 422 |
| GET | `/config/get-config` | Deprecated spelling of `GET /config` | 200, 404, or 422 |
| GET | `/config/get-inputs` | Read configured input array | 200 |
| GET | `/config/get-input/{input_id}` | Read one configured input by array index | 200 |
| GET | `/config/get-outputs` | Read configured output array | 200 |
| GET | `/config/get-output/{output_id}` | Read one configured output by array index | 200 |
| POST | `/config/set-inputs`, `/config/set-input/{id}`, `/config/set-outputs`, `/config/set-output/{id}` | Retired; replaced by `PUT /config` | 410 |
| GET | `/config/check-transcoder` | Read Supervisor process state | 200 or 500 |
| POST | `/config/restart-transcoder` | Stop/start Supervisor process | 200 or 500 |
| GET | `/config/restart-transcoder` | Deprecated spelling of the POST above | 200 or 500 |
| GET | `/license` | Describe the installed engine license | 200, 404, or 503 |
| GET | `/stats` | CPU, memory, storage, storage-write and network utilization | 200 |
| PUT | `/license` | Upload a license, then restart the engine on it; `?dryRun=true` checks only | 200, 400, 409, 422, 500, or 503 |
| DELETE | `/license` | Stop the engine, then remove the installed license | 200, 404, 409, or 500 |

### Input preview and media thumbnails

Two kinds of picture, sized for an operator screen, both cheap enough to leave running.

**Live inputs** come from the engine's preview tap ([07](07-inputs-and-ingest.md#input-preview)), so the API never opens a second connection to a source.

- `GET /inputs/{id}/preview.jpg` returns the newest 640-px frame. Poll it every 1–2 s.
  - Each request renews demand for 30 s, and the engine stops by itself once polling stops.
  - After a quiet spell the first request waits up to `PREVIEW_FIRST_FRAME_WAIT_SECONDS` (4 s) for a keyframe. It answers `503` when none arrives or the input has no video yet; retry.
  - `404` means the input id is unknown.
- `GET /inputs/{id}/preview.mjpeg` streams `multipart/x-mixed-replace` (MJPEG, up to 10 fps). Use it as an `<img src>`; no player is needed.
  - The engine decodes the input fully while at least one viewer is connected.
  - It stops about 15 s after the last viewer disconnects, or after `PREVIEW_LIVE_MAX_SECONDS` (30 min).
  - Each viewer holds one HTTP connection. Browsers allow about six per host over HTTP/1.1, so a wall of live tiles should use stills, with live only for the input an operator opens.

The engine and API must share a filesystem: the engine writes the frame and gRPC returns its path. They do in the shipped container.

**Media files**, from the library or queued, before they air:

- `GET /media/{id}/thumbnail?at=<seconds>&width=<px>` cuts one frame with `ffmpeg` and caches it under `MEDIA_DIR/.thumbnails/`.
  - Default `at` is 10% into the clip, at most 10 s, which skips opening black or slate frames.
  - `width` snaps up to 160/320/480/640/960/1280 so the cache stays bounded.
- Repeat requests are served from disk (`Cache-Control: public, max-age=86400`).
- At most `MEDIA_THUMBNAIL_CONCURRENCY` (2) cuts run at once.
- Deleting the asset deletes its thumbnails.
- `at` past the end is `422`.

### Media upload

`POST /media` takes a multipart form with one `file` part per file. One file and many use the same request:

```bash
curl -F file=@ep1.mxf -F file=@ep2.mxf -F file=@promo.ts http://host:8080/media
```

Each file is checked on its own: extension, size cap, `ffprobe` (a video stream is required), then storage under a UUID name. The call is not atomic. A rejected file leaves nothing on disk and does not undo the files stored before or after it.

The reply is always `{created, failed, results[]}`, even for one file. Each result has the original `filename`, the `status` that file alone would have got (`201`, `422`, …), and either `media` (id, name, relativePath, sizeBytes, probe) or `error`. The overall status is `201` when every file was stored and `207` when any was rejected. Before 2026-10-24 the endpoint took one file and answered with the bare media object. A single bad file was a `422`; it is now a `207` whose one result carries the `422`.

The server receives the whole multipart body before the handler runs, so one call briefly needs temporary disk for every file at once. Large episode files, or uploads that need per-file progress or retry, are better sent as one file per call. Clients can run two or three of those in parallel.


`api/api.yaml` advertises HTTP 201 for event creation, but the route has no `status_code=201` and tests assert HTTP 200. The effective contract is therefore 200 until code and schema are reconciled.

## Input status and switching API

The REST input routes proxy to gRPC over `unix:///tmp/transcoder.grpc.socket`. Their unused SQLAlchemy session dependency still opens the configured database.

### `GET /inputs`

Returns an array:

```json
[
  {
    "id": "3a6dcd44-7d41-45a2-a788-634bb5a49a03",
    "streams": [
      {
        "id": "93099222-d6a3-45a7-9e11-9153e2ad781f",
        "uri": "file:///media/a.ts",
        "state": "BUFFERED"
      }
    ],
    "currentStreamId": "93099222-d6a3-45a7-9e11-9153e2ad781f"
  }
]
```

Use the response—not the configuration array—to count effective engine inputs:

```bash
curl -sS http://transcoder:8080/inputs | jq 'length'
```

For a playlist, count its selectable items with:

```bash
curl -sS http://transcoder:8080/inputs | jq '.[0].streams | length'
```

These counts intentionally answer different questions. `GET /config/get-inputs | length` only counts JSON objects in the file. Before configuration version 1.5.0 the C++ parser consumed only `transcoder.inputs[0]`, so that configuration count could exceed the effective engine count. From 1.5.0 every object becomes an engine input (the second only as an ad-insertion alternate source), and a playlist input still reports its items in its own `streams` array rather than as separate inputs.

### `GET /inputs/{id}`

Returns the same `Input` shape. An unknown engine input maps gRPC `INVALID_ARGUMENT` to HTTP 404 with error code `INVALID_REQUEST`. A missing/unavailable gRPC service normally becomes HTTP 500 with code `UNAVAILABLE`.

### `POST /inputs/{id}`

Request:

```json
{
  "currentStreamId": "93099222-d6a3-45a7-9e11-9153e2ad781f"
}
```

On success, returns the updated `Input`. Error mapping:

| Condition | gRPC status | REST status/code |
| --- | --- | --- |
| Unknown input or stream | `INVALID_ARGUMENT` | 404 / `INVALID_REQUEST` |
| Stream cannot be selected in its current state | `FAILED_PRECONDITION` | 400 / `INVALID_REQUEST` |
| Null/missing `currentStreamId` | No RPC | 400 / `INVALID_REQUEST` |
| Other RPC failure | Other | 500 / `UNAVAILABLE` |

For `PlaylistInput`, treat HTTP 200 as acceptance of the switch, not completion. In the runtime check, the POST response still showed the previous `currentStreamId`; the first follow-up GET showed the target. Poll `GET /inputs/{id}` until its `currentStreamId` matches, with an application timeout.

### Input models

| Model/field | Type | Meaning |
| --- | --- | --- |
| `Input.id` | UUID string | Engine input identifier. |
| `Input.streams` | array of `InputStream` | Selectable transports/items reported by the input implementation. |
| `Input.currentStreamId` | optional UUID string | Currently selected stream. |
| `InputStream.id` | UUID string | Stream/transport identifier. |
| `InputStream.uri` | URI string | Configured or playlist item URI. |
| `InputStream.state` | enum | `UNKNOWN`, `CREATED`, `BUFFERING`, or `BUFFERED`. |

The switching API is most useful for playlist/redundant inputs. A state response proves control state, not output continuity; validate PTS and resulting media.

## Scheduled-event API

### `GET /scheduled_events`

Optional query parameters:

| Parameter | Type | Filter |
| --- | --- | --- |
| `start_time` | aware ISO 8601 datetime | `event.time >= start_time`. |
| `end_time` | aware ISO 8601 datetime | `event.time <= end_time`. This filters start time, not the database `end_time` column. |
| `event_type` | event enum | Exact event type. |
| `is_read` | boolean | `true` selects non-null `read_time`; `false` selects null `read_time`. |

The response is an array of event objects. `read_time`, `creation_time`, and calculated `end_time` are internal database fields and are not returned.

### `POST /scheduled_events`

The request body is always an array, including for one event:

```json
[
  {
    "type": "squeeze",
    "time": "2026-08-03T12:30:00Z",
    "details": {
      "squeeze": {
        "squeeze_name": "l-band",
        "duration": 10000
      }
    }
  }
]
```

- An item with an existing `id` updates that row.
- An item without an ID creates a row and receives an integer ID.
- A non-existing supplied ID does not force that ID; it creates a new auto-incremented row.
- All items are validated in sequence and committed once at the end. An exception before commit leaves the transaction uncommitted, but explicit rollback handling is absent.
- If `time` is omitted/null, service code sets it to the current UTC time for immediate handling.
- If supplied, `time` may be the current time or any future time up to 14 days away. Past times are rejected. Omitting `time` requests immediate scheduling.
- `details` must contain a non-null property whose name matches `type`.

The route returns the created/updated events with effective HTTP 200.

### Slates

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/slates/catalog` | The curated list behind the operator page's slate picker |
| GET | `/slates/media/{id}` | The bytes the engine downloads when a slate fires |

The engine fetches slate media over HTTP with libcurl rather than reading it off disk, so
unlike `graphics_catalog.json` these entries are **not** `file://` URIs. Each carries an
absolute `mediaUri` pointing back at this API, which serves the file from `SLATES_DIR`
(default `/media/slates`). The id is resolved through the catalog and only the catalog's own
file name is ever joined to that directory, so no path from a request reaches the filesystem.

`hasAudio` tells the caller whether firing that slate will mute the programme: media with no
audio track of its own claims an audio blank for its duration. The operator page shows this
before the operator commits.

Firing is `POST /scheduled_events` with `type: "slate"` and the existing `Slate` schema.
**Stopping is `DELETE /scheduled_events/{id}`** — there is no stop endpoint. The provider's
`isValid()` then reports the event gone and the scheduler cancels it, running the same stop
path as a natural end, within one 500 ms poll.

#### Ad-break cue rules

`splice_insert` carries `auto_return` (default `true`), which sets
`break_duration_auto_return` on the emitted section. It was hardcoded `true` before this
field existed, so omitting it reproduces the old behaviour exactly.

| Command | `auto_return` | Manual cue-in | Closes on |
| --- | --- | --- | --- |
| `scte35_spliceinsert` | `true` (default) | rejected, 400 | its own duration |
| `scte35_spliceinsert` | `false` | required | `scte35_returntonetwork` |
| `scte35_timesignal` | n/a | rejected, 400 | `segmentation_duration` |

`POST /scheduled_events` enforces this:

- a `scte35_returntonetwork` is refused unless an open `scte35_spliceinsert` carries the
  same `splice_event_id` **and** that break set `auto_return: false`;
- a `scte35_timesignal` carrying a break-end `segmentation_type_id`
  (`0x23, 0x31, 0x33, 0x35, 0x37, 0x39, 0x3B`) is refused — a time_signal break has no
  manual cue-in;
- a `scte35_timesignal` carrying a break-start id (`0x22, 0x30, 0x32, 0x34, 0x36, 0x38,
  0x3A`) must supply `segmentation_duration`, since nothing else would ever close it;
- a `scte35_spliceinsert` carrying a break-end `segmentation_type_id` is refused for the
  mirror-image reason: the command is always out-of-network, so it opens an avail, and a
  descriptor that closes one contradicts it. `scte35_returntonetwork` is what closes the
  break.

Both checks are skipped when `scte35_base64` is supplied: the payload is then whatever the
section carries, already validated by `validate_scte35_base64()`.

"Open break" is derived from the `events` table rather than tracked separately —
`end_time` is already `time + duration` for a splice_insert and is indexed, so
`type = 'scte35_spliceinsert' AND time <= now AND end_time > now` is the whole lookup
(`find_open_break()` in `services/schedule.py`). There is no dedicated state to expire or
to drift from the schedule.

#### UPID on a splice_insert

SCTE-35 carries descriptors on the `splice_info_section`, not on the command, so a
`splice_insert` may carry a `segmentation_descriptor` exactly as a `time_signal` does.
`scte35_spliceinsert` therefore accepts `segmentation_upid`, plus optional
`segmentation_upid_type` (default 15, URI) and `segmentation_type_id` (default 52, 0x34).

`segmentation_type_id` on either command is a value from ANSI/SCTE 35 Table 23, transcribed
at [38-scte35-segmentation-type-id-table.md](38-scte35-segmentation-type-id-table.md). Ids
absent from that table are reserved or private and are passed through as given.

The descriptor is emitted **only** when a UPID is supplied; without one the section keeps
its previous shape and an empty descriptor loop. Verified on the wire: the same cue-out
with a UPID gives `descriptor_loop_length` 35, without one gives 0.

The descriptor mirrors the break rather than being independently settable, so the two
cannot disagree about the avail: `segmentation_event_id` is the `splice_event_id` and
`segmentation_duration` is the `duration`. Built in
`event/scheduler/spliceInsert.cpp`, reusing the `ts::SpliceSegmentationDescriptor` pattern
from `timeSignal.cpp`.

#### `segmentation_upid` encoding

`segmentation_upid` is **literal ASCII text**: its characters are the UPID bytes, stored and
handed to the engine verbatim. Nothing is hex-decoded at any layer, so `"4578616D"` puts
those eight characters on the wire rather than the four bytes they would once have meant.

The `segmentation_upid_format` field and its `hex` mode were removed. They were ambiguous by
construction: a UPID such as `1234` is simultaneously valid hex and a plausible ASCII
identifier, so the same request produced different bytes depending on a flag the caller had
to remember to set.

A value that cannot be encoded as ASCII is rejected with `400`/`INVALID_REQUEST`. **This
validation previously ran only when `segmentation_cancel_indicator` was
`SEGMENTATION_EVENT_CANCELED`**, so a bad UPID on a normal avail was accepted with a success
code and then discarded by the engine — `read_time` never set, nothing logged, ad break
silently lost. Fixed 2026-08-29; regression tests in
`api/tests/segmentation_upid_test.py`.

How a decoder renders those bytes is governed by `segmentation_upid_type`: type `12` (MPU)
is defined as a 4-byte format identifier plus private data, so decoders split the string;
types `15` (URI) and `9` (ADI) render it as plain text.

### `DELETE /scheduled_events/{id}`

Deletes and returns the event. A missing row returns HTTP 400/`INVALID_REQUEST`. Deleting a row can make a scheduler-held event invalid, but cancellation/overlap behavior must be verified end to end.

## Event envelope

| Field | Required | Type/behavior |
| --- | --- | --- |
| `id` | No on create | Integer. Existing ID updates; returned for every persisted event. |
| `type` | Yes | `image_overlay`, `slate`, `squeeze`, `scte35_timesignal`, `scte35_spliceinsert`, or `scte35_returntonetwork`. |
| `time` | No | Aware ISO 8601 datetime. Missing/null means immediate; a supplied value is range checked. |
| `details` | Yes | Object containing the payload keyed by the exact event type. |

### Image overlay

```json
{
  "type": "image_overlay",
  "details": {
    "image_overlay": {
      "image_urls": ["https://assets.example/logo.png"],
      "geometry": { "x": 100, "y": 50, "width": 320, "height": 180 },
      "duration": 10000,
      "opacity": 0.8,
      "fade_in": 500,
      "fade_out": 500,
      "delay_between_images": 1000
    }
  }
}
```

| Field | Constraint/default | C++ consumption |
| --- | --- | --- |
| `image_urls` | Required URI array | Becomes overlay input URI sequence. Network accessibility is runtime-dependent. |
| `geometry.x/y` | Integers >= 0 in REST model; defaults 0 | Compositor location. |
| `geometry.width/height` | Integers >= 1 when provided | Target geometry. |
| `duration` | Optional integer >= 1 ms | Omitted becomes unlimited duration (`-1`) in C++ and leaves DB `end_time` null. |
| `opacity` | 0.0–1.0 | C++ event default is 1.0. |
| `fade_in`, `fade_out` | Integers >= 0 ms | Transition durations. |
| `delay_between_images` | Integer >= 0 ms | Converted to the reciprocal caps rate (for example 40 ms becomes 1000/40, or 25 fps). Static buffers preserve scheduler/compositor running-time PTS. |

### Slate

```json
{
  "type": "slate",
  "details": { "slate": { "url": "file:///media/slate.ts", "duration": 30000 } }
}
```

`url` and positive millisecond `duration` are required. The C++ scheduler has TODO-marked slate update/stop behavior, so cancellation, audio, and overlap semantics are not a stable promise.

### Squeeze

```json
{
  "type": "squeeze",
  "details": {
    "squeeze": {
      "duration": 12000,
      "input_window_norm": { "x": 0, "y": 0, "width": 1.0, "height": 1.0 },
      "output_window_norm": { "x": 0.166667, "y": 0, "width": 0.833333, "height": 0.833333 }
    }
  }
}
```

`duration` is required and positive. Provide either a `squeeze_name` matching `transcoder.squeeze[]` or both window objects. Windows are normalized fractions of the output frame (0..1), resolved by the engine against the real compositor raster; members may be null/omitted and retain internal sentinel/default values. The service checks for both window objects when the name is absent, and the C++ parser guards each independently.

### SCTE-35 splice insert

```json
{
  "type": "scte35_spliceinsert",
  "details": {
    "scte35_spliceinsert": { "splice_event_id": 2001, "duration": 30000 }
  }
}
```

Both non-negative `splice_event_id` and positive millisecond `duration` are required. The calculated database `end_time` is start plus duration.

### SCTE-35 return to network

```json
{
  "type": "scte35_returntonetwork",
  "details": { "scte35_returntonetwork": { "splice_event_id": 2001 } }
}
```

Requires non-negative `splice_event_id`. It has no calculated `end_time`.

### SCTE-35 time signal

```json
{
  "type": "scte35_timesignal",
  "details": {
    "scte35_timesignal": {
      "scte35_descriptors": [
        {
          "segmentation_event_id": 1001,
          "segmentation_cancel_indicator": "SEGMENTATION_EVENT_NOT_CANCELED",
          "segmentation_duration": 2700000,
          "segmentation_upid_type": 1,
          "segmentation_upid": "0x64757061fa",
          "segmentation_type_id": 52,
          "segment_num": 1,
          "segments_expected": 1,
          "sub_segment_num": 1,
          "sub_segments_expected": 1,
          "delivery_restrictions": {
            "archive_allowed_flag": "ARCHIVE_ALLOWED",
            "device_restrictions": "NONE",
            "no_regional_blackout_flag": "NO_REGIONAL_BLACKOUT",
            "web_delivery_allowed_flag": "WEB_DELIVERY_ALLOWED"
          }
        }
      ]
    }
  }
}
```

| Descriptor field | Type/constraint |
| --- | --- |
| `segmentation_event_id` | Required integer. |
| `segmentation_cancel_indicator` | Required: `SEGMENTATION_EVENT_NOT_CANCELED` or `SEGMENTATION_EVENT_CANCELED`. |
| `segmentation_duration` | Optional integer in 90 kHz ticks. Presence sets the duration flag. |
| `segmentation_upid_type` | Optional integer 0–255. |
| `segmentation_upid` | Optional even-length hexadecimal string; spaces and leading `0x` are normalized by REST. |
| `segmentation_type_id` | Optional integer. |
| `segment_num`, `segments_expected` | Optional integers. |
| `sub_segment_num`, `sub_segments_expected` | Optional integers; service requires both for type IDs `0x34`, `0x30`, `0x32`, `0x36`, `0x38`, `0x3A`, `0x44`, and `0x46` in its cancellation-validation branch. |

Delivery restriction enum values:

| Field | Values |
| --- | --- |
| `archive_allowed_flag` | `ARCHIVE_NOT_ALLOWED`, `ARCHIVE_ALLOWED` |
| `device_restrictions` | `NONE`, `RESTRICT_GROUP0`, `RESTRICT_GROUP1`, `RESTRICT_GROUP2` |
| `no_regional_blackout_flag` | `REGIONAL_BLACKOUT`, `NO_REGIONAL_BLACKOUT` |
| `web_delivery_allowed_flag` | `WEB_DELIVERY_NOT_ALLOWED`, `WEB_DELIVERY_ALLOWED` |

The current service requires UPID/type/segment fields when `segmentation_cancel_indicator` equals `SEGMENTATION_EVENT_CANCELED`. This rule is source-backed but counterintuitive and needs protocol-level verification before being treated as normative SCTE behavior. A time signal's calculated DB `end_time` equals its start time.

## Configuration and process-control routes

The handwritten router reads the config from `TRANSCODER_CONFIG_PATH` (see the write route
below) and always targets:

```text
/var/run/supervisor.sock
Supervisor program name: transcoder
```

These two are constants, not environment variables.

### Read routes

- `GET /config` returns the complete parsed JSON object. `GET /config/get-config` is the original spelling, kept as a deprecated alias.
- `GET /config/get-inputs` and `/get-outputs` return the corresponding arrays.
- `GET /config/get-input/{index}` and `/get-output/{index}` use Python list indexing. Out-of-range positive indexes return HTTP 200 with `{"message":"... doesn't exist"}`. Negative indexes are not rejected and access from the end of the array.
- A missing config file returns HTTP 404 and invalid JSON returns HTTP 422, both in the `{"message","code"}` shape.

### Write route

`PUT /config` replaces the whole document. It is a provisioning surface: the engine parses its
config once at start, so a write takes effect only after `POST /config/restart-transcoder`. The
response reports `restartRequired`.

`services/engine_config.py` performs the write in three stages:

1. **Structural validation.** The document must be an object carrying `version` and a
   `transcoder` object whose `inputs` and `outputs` are non-empty arrays of objects;
   `application` must be an object if present. Unknown keys pass through untouched. This is
   deliberately not a schema validation: `transcoder/config/doc/openapi.yaml` diverges from the
   parser, so checking against it would refuse configs the engine accepts. A failure returns
   HTTP 422 naming the offending path, and the file on disk is not touched.
2. **Writability probe.** The containing directory is probed by creating and removing a dotfile
   rather than inspected with `os.access`, which reports success for root on a directory it
   cannot write through. A failure returns HTTP 409.
3. **Atomic replace.** The document is written to `<name>.tmp`, fsynced, and renamed over the
   original, with the previous file's uid, gid and mode reapplied because the rename installs a
   new inode. `EBUSY` returns HTTP 409 explaining that a single-file bind mount cannot be
   replaced and that the containing directory must be mounted instead.

`?dryRun=true` runs stages 1 and 2 and returns `{"state":"validated"}` without writing.

The config path is `TRANSCODER_CONFIG_PATH`, defaulting to `/opt/utpl/etc/config.json`.
`docker-compose.playlist.yml` points it at a file inside the `./config` directory mount, and
`supervisord.conf` passes the same variable to the engine, so both read one path.

The four `POST /config/set-*` routes are retired and return HTTP 410 naming `PUT /config`. They
never wrote anything, and they reached only `transcoder.inputs` and `transcoder.outputs`,
leaving `version`, `squeeze`, `scte`, `blanking`, `blackout` and the whole `application` object
unaddressable.

This surface still has no authentication and no secret redaction. Put it behind an
authenticated management boundary before exposing it beyond a trusted network.

### Adding a playlist input

There is no dedicated add-playlist endpoint. The verified procedure is:

1. Create an M3U8 file beginning with `#EXTM3U`, with each item represented by `#EXTINF` followed by its URI.
2. Set `transcoder.inputs[0].transport.uri` to `pls+file:///absolute/path/playlist.m3u8` and include `"playlist": {}`.
3. Configure explicit output audio/video codecs; playlist input rejects passthrough.
4. Validate paths, license, database, encoder/plugin availability, and output configuration outside the REST setter.
5. Restart the transcoder so it re-reads the config.
6. Verify `GET /inputs` returns one input with the expected M3U8 items in `streams`, then inspect output media.

From configuration version 1.5.0 every `transcoder.inputs` object becomes an engine input. `inputs[1]` is used only as the alternate source of SCTE ad insertion (`role: secondary`, see [34](34-scte-ad-insertion.md)). The complete single-input fixture is in [28](28-playlist-input-api-runtime-verification.md).

### Supervisor routes

- `GET /config/check-transcoder` returns `{"message":"<statename>"}` from Supervisor or HTTP 500.
- `POST /config/restart-transcoder` stops the process when its state is `RUNNING`, `STARTING` or `BACKOFF`, waits `TRANSCODER_RESTART_GRACE_SECONDS` (default 10), and starts it. A process in any other state is started without a stop. `GET` remains available as a deprecated alias because that is how the route shipped.

Both routes depend on Unix-socket permissions and Supervisor availability.

## License routes

`routers/license.py` and `services/license.py` manage the one license file the engine reads at
start. The path is `TRANSCODER_LICENSE_PATH`, defaulting to `/opt/utpl/etc/transcoder.lic`;
`supervisord.conf` passes the same variable to the engine's `-l`, so both read one path.

The license is encrypted and signed with keys that exist only inside the transcoder binary, so
the API cannot read one itself. Every inspection runs
`$TRANSCODER_BINARY -l <file> --license-info` (binary default `/opt/utpl/bin/transcoder`), which
performs the engine's own load, signature check and decrypt and prints one JSON object on its
last stdout line (see [22](22-licensing-crypto-and-security.md#inspecting-a-license)). A missing
binary, a timeout, or a binary that prints no report returns HTTP 503.

- `GET /license` returns the report plus `path`: `status` (`valid`, `expired`, `notYetValid`,
  or `invalid` with `error`), `id`, `name`, `validSince`, `validUntil`, `ttl` (all milliseconds
  since the epoch or milliseconds), `offline`, `maxSessions`, `createdAt`, and `checkedAt`. An
  installed file the engine would refuse is still described with HTTP 200 so the UI can show
  why; no installed file returns HTTP 404.
- `PUT /license` takes a multipart `file` field. The upload is written to `<name>.upload` next
  to the installed license and inspected there. It is refused with HTTP 422, leaving the
  installed license and the engine untouched, when it is empty or over 64 KiB, when the report
  is not `valid`, or when it is an online license and the engine config has no
  `application.licenseServer.uri` beginning `http://` or `https://` (the same rule
  `Application::configure` enforces). An accepted upload is renamed over the installed file,
  keeping its uid, gid and mode, and the engine is then restarted through the
  `restart-transcoder` implementation. **The restart takes the channel off air** for
  `TRANSCODER_RESTART_GRACE_SECONDS` (default 10) plus engine start-up; the response carries
  `state: "installed"`, the report as `license`, and the Supervisor state as `engine`. If the
  restart fails the license stays installed and the error says so. `?dryRun=true` inspects the
  upload and returns `state: "validated"` without installing it or touching the engine.
- `DELETE /license` stops the engine through the `stop-transcoder` implementation, then removes
  the file, and returns `state: "removed"` with the engine state. The stop comes first so a
  failed stop leaves the license in place. With no license installed it returns HTTP 404
  without stopping anything. The engine cannot start again until a license is uploaded:
  `POST /config/start-transcoder` still reports `STARTING`, and the engine then exits with
  "Empty license path provided, or it points to a non-existing file" and Supervisor settles in
  `BACKOFF`/`FATAL`.

The same single-file bind-mount rule as `PUT /config` applies: the license must live inside a
read-write mounted directory. A missing or unwritable directory returns HTTP 409, and so does
an `EBUSY` rename or a remove that the mount refuses. `docker-compose.playlist.yml` keeps the
license at `/runtime/license/transcoder.lic` and copies the seed license into it only when
`/runtime/license` does not exist yet, so a deleted license stays deleted across container
restarts.

Issuing licenses — generating, signing, and listing what was issued — is deliberately absent. It
needs the signing private key, which belongs on the vendor side, not on an appliance. The
`transcoder/license/tools` CLIs and `make-license.sh` cover it; the separate license server
(`license-server/`) implements only online session creation and heartbeat.

## Resource utilization route

`GET /stats` (`routers/stats.py`, `services/stats.py`) reports utilization for the container the
API and the engine share, for the engine process alone, and for the host. Every figure is read
from kernel counters - `/proc` and the container's cgroup v2 files - with no subprocess or added
dependency; one sample is about a dozen small reads and measures under half a millisecond.

Rates need two readings, so a background task started in the API lifespan samples every
`STATS_SAMPLE_INTERVAL_SECONDS` (default 2, floored at 0.5) and keeps the report for the last
window. The route returns that report without waiting, so any number of pollers cost nothing
extra. A request that arrives before the first sample takes one reading itself and returns null
rates with `windowSeconds: null`.

| Field | Source | Meaning |
| --- | --- | --- |
| `cpu.container` | `cpu.stat` `usage_usec` | `coresUsed` over the window; `percent` of `capacityCores`, which is the `cpu.max` quota, else the `cpuset.cpus.effective` count |
| `cpu.engine` | `/proc/<pid>/stat` utime+stime | The transcoder process alone, found by its `TRANSCODER_BINARY` path; `pid` null while it is stopped |
| `cpu.host` | `/proc/stat` | Whole-machine busy percent, including other containers |
| `memory.container` | `memory.current` − `inactive_file` | Excludes reclaimable page cache, as `docker stats` does; `limitBytes` is `memory.max`, else host memory |
| `memory.engine` | `/proc/<pid>/status` `VmRSS` | Resident memory of the transcoder |
| `memory.host` | `/proc/meminfo` | `usedBytes` is `MemTotal − MemAvailable` |
| `storage[]` | `statvfs` | One entry per filesystem holding the appliance's paths (`HLS_DIR`, `MEDIA_DIR`, and the directories of `DB_PATH`, `TRANSCODER_CONFIG_PATH`, `TRANSCODER_LICENSE_PATH`; `STATS_STORAGE_PATHS` overrides); `percent` as `df` computes it |
| `storageWrite.container` | `io.stat` `wbytes` | Bytes reaching the disk, counted once per physical device |
| `storageWrite.engine` | `/proc/<pid>/io` `write_bytes` | Bytes the engine writes, as it writes them |
| `network` | `/proc/net/dev` | Receive and transmit bits per second per interface and in total, loopback excluded |

Three behaviours are deliberate:

- **Device-mapper layers are not summed.** `io.stat` lists every layer of an LVM or dm-crypt
  stack with the same bytes, so the sum multiplies the write rate by the stack depth - which is
  what `docker stats` BlockIO shows. Only devices whose sysfs `slaves` directory is empty count.
- **Writes carry a one-minute average.** The kernel flushes dirty pages in bursts up to
  `vm.dirty_expire_centisecs` (30 s by default) after the write, and the engine writes each HLS
  segment at once, so a 2 s rate swings between zero and a spike. `averageBytesPerSecond` is over
  the last 60 s (`averageWindowSeconds` says how much history there was). Over that window the
  disk and engine figures converge, the disk trailing by what is still in the page cache.
- **A counter that goes back gives null, not a negative rate.** An engine restart resets its
  per-process counters; its rates are null until a full window has passed on the new process.

Rates are over the container's network namespace, so they are the channel's traffic, not the
host's. The route has no authentication and reveals host memory and CPU load; it sits behind the
same trusted-network boundary as the rest of this API. GPU utilization is not reported.

## gRPC contract

`transcoder/app/grpc/proto/transcoder.proto` defines package `app.grpc`, service `TranscoderApi`:

| RPC | Request | Response | Behavior |
| --- | --- | --- | --- |
| `getInputs` | `google.protobuf.Empty` | `InputList` | Serializes all channel inputs. |
| `getInput` | `GetInputParameters { string id }` | `Input` | `INVALID_ARGUMENT` for unknown ID. |
| `setInputStream` | `{ string inputId, string streamId }` | `Input` | Selects stream; can return `INVALID_ARGUMENT` or `FAILED_PRECONDITION`. |

The server listens insecurely on the local Unix socket `unix:///tmp/transcoder.grpc.socket` and enables the default gRPC health-check service. Unix filesystem access is its trust boundary.

## SQLite event contract

The SQLAlchemy service creates this logical schema:

| Column | Type/nullability | Use |
| --- | --- | --- |
| `id` | integer primary key | Event ID. |
| `type` | non-null string, indexed | Event type enum string. |
| `time` | non-null UTC datetime text, indexed | Scheduled start. |
| `end_time` | nullable UTC datetime text, indexed | Calculated stop/end for selected event types. |
| `details` | non-null JSON string | Type-specific payload. |
| `creation_time` | server-default UTC datetime | Insert time. |
| `read_time` | nullable UTC datetime | Set by C++ when the event is polled. |

`DB_PATH` selects the Python database, defaulting to `./events.sqlite`. The same exact file must be passed to the transcoder with `-b`.

The C++ provider polls unread events in a time range, converts them into typed scheduler events, then sets `read_time` in a separate read/write transaction. Relevant limitations:

- Polling and marking read are not one atomic claim transaction, so multi-consumer behavior is unproven.
- Rows are marked read even when their type/payload cannot be mapped into a runtime event.
- `markProcessed()` is empty; there is no success/error completion state.
- DAO `_dbToModel()` mistakenly reads `time` into the model's `endTime` instead of reading `end_time`.
- API update preserves the existing row's `read_time`; updating an already-read event does not make it unread/reschedulable.

## Verification checklist

Before deploying this control plane:

1. Run API code generation and `pytest`, then add automated tests for inputs/gRPC and every `/config` route, which the existing suite does not cover.
2. Compare runtime `/openapi.json` with checked-in `api/api.yaml`, especially status codes and config routes.
3. Exercise valid/invalid event models, aware timestamps, immediate events, boundary times, multi-item rollback, update of read events, and deletion during execution.
4. Verify gRPC unavailable/unknown/precondition mappings and Unix-socket permissions.
5. Inspect SQLite concurrency, restart recovery, duplicate polling, read marking, and event overlap/cancellation.
6. Validate media outcomes: extracted frames for overlays/squeeze/slate and TSDuck/manifest inspection for SCTE-35. HTTP success alone is insufficient.
7. Put authentication, TLS termination, CSRF-safe methods, audit logging, configuration validation, and atomic writes in front of any production config/process-control workflow.
