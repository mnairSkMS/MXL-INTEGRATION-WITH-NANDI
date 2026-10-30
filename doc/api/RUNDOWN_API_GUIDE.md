# Rundown API Guide

This guide explains the **Program Schedule / rundown** API at `http://127.0.0.1:8080`.
It is available only when `ENABLE_RUNDOWN_SERVICE=true`.

## The short version

A rundown is a list of programs plus graphics triggers. There is **one** kind of stored object:

```text
live version  --- POST /apply (validate + publish) --->  new live version
```

- A **version** is an immutable record of a change that reached air. `versions[]` is a history of
  what has been broadcast, not a history of keystrokes.
- **There is no draft.** A change is either on air or it is unsaved text in your client. Use
  `dryRun: true` to check a change before committing to it.

`POST /rundowns/{id}/apply` reads the live version, applies your change to a copy, validates the
**result**, and publishes it — all in one transaction. Either the whole change reaches air or nothing
moves.

> **If `PLAYLIST_BOOTSTRAP_FILE` is set**, a flat channel file is the source of truth and is applied
> on every container start. Unless `PLAYLIST_BOOTSTRAP_WRITEBACK=false`, **Apply rewrites that file**,
> so an applied change survives a restart — applying is what makes a change permanent.
> `GET /rundowns/status` reports `bootstrapManaged`, `bootstrapFile`, `bootstrapWriteback`, and the
> last outcome. See [30](../codebase/30-transactional-rundown-service.md).

| Goal | Use these endpoints |
|---|---|
| See what is live | `GET /rundowns/main` then `GET /rundowns/main/versions/{activeVersionId}` |
| Add or change a graphic | `POST /apply` with `ops` |
| Edit the whole schedule at once | `POST /apply` with `document` |
| Check before committing | the same call with `"dryRun": true` |
| Add a list of programs from a file | `POST /programs/import` |
| First schedule on a fresh channel | `POST /seed` |
| See what is on air right now | `GET /clock` |
| See whether a trigger fired | `GET /executions` |

All durations, offsets, and fades in the API are integer milliseconds. A duration is the complete
visible interval: fade/reveal in and out are included inside it.

## 1. Status and inspection

| Request | Simple meaning | Technical result |
|---|---|---|
| `GET /rundowns/status` | Is scheduling switched on? | Returns `enabled`, a 250 ms observation interval, its timing limitation, and the channel-file governance fields. |
| `GET /rundowns/{id}` | What is live? | Returns `activeVersionId`, version history, timezone, and managed playlist ID. |
| `GET /rundowns/{id}/versions/{versionId}` | Open one version. | The complete structured document: programs, graphics, stable IDs, durations, timeline positions. |

`GET /rundowns/main` is normally the first request. Copy `activeVersionId`, then
`GET /rundowns/main/versions/{activeVersionId}` for a document you can read or edit.

## 2. Changing the schedule: `POST /apply`

Supply **exactly one** of `ops` or `document`. Both, or neither, is `422`.

| Field | Meaning |
|---|---|
| `ops` | An ordered list of operations. Best for scripts and surgical edits. |
| `document` | A complete edited schedule. Best when you already hold the end state, as the operator UI does. |
| `expectedVersionId` | Optional. Must match what is on air, or `409`. Omit to force. |
| `dryRun` | Optional. Validate the merged result and roll back. Returns `version: null`. |

### Operations

| `op` | Required | Optional |
|---|---|---|
| `addGraphic` | `assetId`, and exactly one of `itemKey` or `startsAt` | `key`, `mode`, `durationMs`, `fadeMs`, `offsetMs` |
| `updateGraphic` | `key` | `mode`, `durationMs`, `fadeMs`, `offsetMs`, `startsAt` |
| `removeGraphic` | `key` | — |
| `removeItem` | `key` | — |
| `insertItems` | `items` | `position` (zero-based; omitted appends) |

Omitted `mode`, `durationMs` and `fadeMs` fall back to the catalog's `defaultMode`,
`defaultDurationMs` and `defaultAnimationMs`. An omitted `key` is generated from the item and asset
and uniquified.

### Example: a sponsor L-band at the start of the Coke break

```http
POST /rundowns/main/apply
Content-Type: application/json
```

```json
{
  "expectedVersionId": "current-active-version-id",
  "ops": [
    {
      "op": "addGraphic",
      "itemKey": "coke-break",
      "assetId": "lband-playboxtv",
      "mode": "reveal",
      "durationMs": 5000,
      "fadeMs": 600,
      "offsetMs": 0
    }
  ]
}
```

`durationMs: 5000` means five seconds from the start of reveal to the completed restoration.

### Why operations, and not one call per change

L-band and Aston share one squeeze resource, so **a legal end state can be unreachable through a
legal sequence of single changes**. Swapping which sponsor owns a slot is rejected at every
intermediate step:

```json
{
  "ops": [
    { "op": "removeGraphic", "key": "coke-lband" },
    { "op": "addGraphic", "itemKey": "coke-break", "assetId": "aston-static", "offsetMs": 0,
      "durationMs": 5000 },
    { "op": "addGraphic", "itemKey": "coke-break", "assetId": "lband-playboxtv", "offsetMs": 12000,
      "durationMs": 5000 }
  ]
}
```

Operations apply in order and validation runs **once**, on the result, so this is accepted while the
same three calls made separately are not.

### What Apply validates

Media registration, graphic asset and mode, unique keys, known clip duration, clip-fit including the
sponsor restoration margin, L-band/Aston squeeze overlap, wall-clock range, and animation timing.

### What Apply returns

`202` with the new `version`, an `applied` count, a `channelFile` outcome, and any `warnings`.

`202` means the desired queue revision was accepted; poll `GET /playlists/main` until
`appliedRevision` equals `desiredRevision`.

`channelFile` is `{"state": "written", "path": "..."}` on success, `"skipped"` when writeback is off
or no bootstrap file is configured, and `"failed"` with an `error` when the swap did not happen.
**A `failed` writeback still means the schedule is on air** — it only means the next restart will
revert it. If the file cannot be written at all, Apply refuses with `409` *before* the queue moves,
so an unpersistable change never reaches air half-done.

`warnings` reports wall-clock triggers dropped because their time had already passed. A published
version is an as-run record, so its scheduled times inevitably fall into the past; they are dropped
on the next change rather than blocking it.

## 3. Importing a list of programs

`POST /rundowns/{id}/programs/import` uploads a channel YAML — the same flat format as
`config/channel.yml` — and splices its programs into the live schedule.

```http
POST /rundowns/main/programs/import
Content-Type: multipart/form-data
```

| Field | Meaning |
|---|---|
| `file` | The YAML. Maximum 1 MiB, UTF-8. |
| `position` | Optional zero-based index into the programs. Omitted, it appends. |
| `expectedVersionId`, `dryRun` | As on `apply`. |

```yaml
playlist:
  - media: promo.mp4
  - media: coke-commercial.mp4
    graphics:
      - use: lband-playboxtv
        at: 0
        duration: 5s
        fade: 600ms
```

**This imports a program list, not the media.** Entries name media by relative path under
`MEDIA_DIR`. Upload the clips with `POST /media` first; a missing file is `422` saying so.

Two behaviours worth knowing:

- **Only new items are renamed on a key collision.** Importing a clip whose slug matches a live item
  yields `opening-2`; the live `opening` keeps its key and its identity, because as-run history joins
  on it.
- **A file declaring a different `channel:` is refused**, so a break reel cannot be imported onto the
  programme by mistake.

## 4. Starting a schedule from nothing

`POST /rundowns/{id}/seed` publishes a first version from the playlist that is already running, for a
channel with no channel file that has never published. Apply edits the live version, so without a
first version there is nothing to change.

```json
{ "name": "Main rundown" }
```

`201` with the published version. `409` once a version exists — use `apply` from then on.

## 5. Clearing every graphic: `POST /reset`

```bash
curl -X POST localhost:8080/rundowns/main/reset \
  -H 'Content-Type: application/json' \
  -d '{"expectedVersionId": "<live version id>"}'
```

Publishes the live programs with every item graphic and scheduled graphic removed, through the
same validated write path as `apply`. It answers `202` with the new `version`, the
`removedEventCount`, and the `channelFile` writeback outcome; a stale `expectedVersionId` is `409`.
Media, manual cues, and execution history are untouched. The audit log records it as
`schedule_reset`.

## Channels and rundown ids

A rundown id is a channel id. With `PLAYLIST_BOOTSTRAP_FILES` listing more than one channel file
(for example `channel.yml` for the programme and `ads.yml` for an ad-insertion break reel), each
channel is its own rundown and managed playlist: `/rundowns/main/...` edits the programme and
`/rundowns/ads/...` the reel. The live-input check and the channel-file writeback act on the input and
file of the rundown addressed, so an apply to `ads` never touches the programme.

## 6. Resolving a local time

| Request | Simple meaning | Technical result |
|---|---|---|
| `POST /rundowns/{id}/time/resolve` | Convert an entered local date/time safely. | Interprets `date` and `time` in the rundown's IANA timezone, returns UTC `startsAt`, and rejects invalid or ambiguous daylight-saving times. |

```json
{ "date": "2026-08-06", "time": "14:30:15.25" }
```

To clear the schedule, apply a document whose programs carry no graphics, or send `removeGraphic`
operations for the keys you want gone. There is no bulk-clear endpoint: removing live triggers is the
same kind of change as any other, and goes through the same validated write path.

## 7. What actually happened?

| Request | Simple meaning | Technical result |
|---|---|---|
| `GET /rundowns/{id}/clock` | What is on now, and when did this schedule start? | The loop anchor: `syncedAt`, `currentItemId`, `currentStartedAt`, `loopDurationMs`, `cycle`, and `serverNow`. |
| `GET /rundowns/{id}/occurrences` | When did each program start? | Up to 200 observed playlist item starts/ends. Observation is control-plane timing, not frame-accurate timecode. |
| `GET /rundowns/{id}/executions` | Did a graphic trigger fire? | Up to 200 dispatch records with planned time, dispatch time, cue ID, state, and error. |
| `GET /rundowns/{id}/audit` | What changed the schedule? | Up to 200 audit records for applies, seeds, and canceled future work. |

### Projecting start times from `/clock`

The channel is a loop, so no program has a fixed wall-clock time. `/clock` gives the one instant that
is observed rather than computed — when the on-air item actually started — and everything else is
derived from it:

```
loopEpoch   = currentStartedAt - timelineStartMs(on-air item)
rowStart[i] = loopEpoch + timelineStartMs[i] + (i is before the on-air item ? loopDurationMs : 0)
```

Re-derive `loopEpoch` whenever `currentItemId` changes. Do **not** project forever from `syncedAt`:
real clip durations differ from their probed values by up to a few hundred milliseconds each, so a
fixed anchor drifts, and a skip or an Apply invalidates it outright.

`serverNow` is the container's clock. Compute `skew = serverNow - Date.now()` once and add it to your
local clock, or every time you display will be wrong by the difference between the two machines.

Every field is nullable or zero before the observer has seen a transition — a freshly published
version has no occurrence yet. `cycle` is `0` then, and `1` when the loop length is unknown because no
clip has a probed duration.

## Common responses

| Code | Meaning | What to do |
|---|---|---|
| `200` / `201` | A read or a seed succeeded. | Continue. |
| `202` | A live change was accepted. | Poll `GET /playlists/main` for reconciliation. |
| `404` | Feature disabled, missing rundown, or unknown graphic/item key. | Enable `ENABLE_RUNDOWN_SERVICE=true`, or check the key against the live version. |
| `409` | Someone else changed the schedule, the queue is still applying, a version already exists, or the channel file is not writable. | Re-read the live version and retry with its `activeVersionId`. Read the message: a channel-file conflict needs the mount fixed, not a retry. |
| `422` | The change itself is invalid. | Read the validation message; correct timing, media, asset, overlap, or keys. |
| `503` | The API cannot confirm on-air state. | Start or repair the transcoder, then retry. |

## Related APIs

- `GET /media` lists media available to a program.
- `GET /graphics/catalog` lists valid graphic `assetId` and `supportedModes` values.
- `GET /playlists/main` reports managed playlist reconciliation after Apply.
- `GET /graphics/cues` shows currently active manual or scheduled cues.
