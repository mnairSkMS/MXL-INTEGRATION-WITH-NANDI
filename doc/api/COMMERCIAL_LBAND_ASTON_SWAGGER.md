# Inserting a Commercial L-band / Aston from Swagger UI

This guide walks through putting a sponsor **L-band** or **Aston** on air using the
Swagger UI at `http://127.0.0.1:8080/docs`.

There are two ways to do it, and this guide covers both:

| Method | Endpoint | Use it when |
|---|---|---|
| **A — Call it now** (manual cue) | `POST /graphics/cues` | You want the band on air immediately, or after a short delay measured in milliseconds. Nothing is saved; it is a one-shot. |
| **B — Schedule it** (rundown) | `PUT /rundowns/main/draft` → `POST /rundowns/main/publish` | You want the band to fire at a wall-clock time, or every time a given program starts. It is saved, versioned, and survives a restart. |

Both methods end up creating the same underlying pair of engine events — an
`image_overlay` plus a `squeeze` — so the on-air result is identical. The difference
is only *when* and *how durably* the cue is triggered.

---

## 0. Before you start

Open `http://127.0.0.1:8080/docs`. You should see the **Transcoding Appliance API**
page with sections including `graphics`, `schedule`, `playout`, and `rundowns`.

### 0.1 Check the transcoder is running and something is on air

Expand **`GET /playlists/main`** → **Try it out** → **Execute**.
You need a running transcoder with a confirmed on-air item, otherwise Publish
(Method B) will refuse. Method A works as long as the transcoder is up.

### 0.2 Pick your commercial asset

Expand **`GET /graphics/catalog`** → **Try it out** → **Execute**.

The response lists every asset. Sponsor bands are the ones with
`"kind": "lband"` or `"kind": "aston"`:

| `id` | Label | Kind | Media | Default duration |
|---|---|---|---|---|
| `lband-voltedge` | VoltEdge | lband | image | 15000 ms |
| `lband-aquapure` | AquaPure | lband | image | 15000 ms |
| `lband-apex-tyres` | Apex Tyres | lband | image | 15000 ms |
| `lband-greenfuel` | GreenFuel | lband | image | 15000 ms |
| `lband-pennzoil` | Pennzoil | lband | image | 15000 ms |
| `lband-playboxtv` | PlayboxTV | lband | image | 15000 ms |
| `lband-video` | L-band (video) | lband | **video** | 4200 ms (clip length) |
| `aston-static` | Aston | aston | image | 15000 ms |
| `aston-video` | Aston (video) | aston | **video** | 2200 ms (clip length) |

For each asset, note three fields from the response — you need them in the next steps:

- `supportedModes` — the only values `mode` will accept.
- `defaultDurationMs` — used when you omit `durationMs`.
- `enabled` — **if this is `false` the asset is refused.** Video assets
  (`lband-video`, `aston-video`) are only enabled when the API container runs with
  `ENABLE_VIDEO_GRAPHICS=true`. Image bands are always enabled.

### 0.3 Choose the animation mode

| `mode` | What the viewer sees |
|---|---|
| `reveal` | Program picture shrinks into the squeeze window while the band is revealed behind it. The usual choice for a commercial band. |
| `shrink_overlay` | Band fades in on top while the program shrinks. |
| `snap` | Band and squeeze appear instantly, no animation. `fadeMs` is ignored. |

### 0.4 The three rules that cause most rejections

1. **One sponsor at a time.** Only one `lband`/`aston` cue may be active. A second
   one returns `409 Conflict` — *"an L-band or Aston cue is already active; stop it
   first"*. Cancel the running cue first (step A4).
2. **Duration must contain both animation legs.** For `reveal`, `shrink_overlay`,
   and `fade`, you must have `2 × fadeMs < durationMs`. With `fadeMs: 600` your
   `durationMs` must exceed 1200. `422` otherwise.
3. **A band always needs a duration.** Only `logo` and `bug` may hold open-ended.
   Omitting `durationMs` is fine — the catalog default is applied — but an explicit
   `null` on an lband/aston still resolves to a default, never to "forever".

All times in this API are **integer milliseconds**, and `durationMs` is the *complete*
visible interval: reveal-in and restore-out are counted inside it.

---

## Method A — Call it now (`POST /graphics/cues`)

Use this to punch a commercial band to air on demand.

### A1. Open the endpoint

In Swagger UI, expand the **graphics** section → **`POST /graphics/cues`** →
**Try it out**.

### A2. Paste the request body

Replace the sample body with:

```json
{
  "assetId": "lband-pennzoil",
  "mode": "reveal",
  "durationMs": 15000,
  "startInMs": 0,
  "fadeMs": 600
}
```

| Field | Required | Meaning |
|---|---|---|
| `assetId` | yes | An `id` from `GET /graphics/catalog`. |
| `mode` | yes | Must appear in that asset's `supportedModes`. |
| `durationMs` | no | Whole visible interval. Omit to take `defaultDurationMs`. Must be `> 0`. |
| `startInMs` | no | Delay before the cue starts. `0` (default) means immediately. |
| `fadeMs` | no | Length of each animation leg. `600` is the catalog default feel; use `0` for `snap`. |

**Aston instead of L-band** — same body, different asset:

```json
{
  "assetId": "aston-static",
  "mode": "reveal",
  "durationMs": 12000,
  "startInMs": 0,
  "fadeMs": 600
}
```

**Fire it 30 seconds from now** — set `"startInMs": 30000`.

**Video band** (only if `ENABLE_VIDEO_GRAPHICS=true`) — the clip length is fixed, so
let the catalog supply the duration and use no fade:

```json
{
  "assetId": "lband-video",
  "mode": "reveal",
  "startInMs": 0,
  "fadeMs": 0
}
```

### A3. Execute and read the response

Click **Execute**. A success is **`201 Created`**:

```json
{
  "id": "0c0f7f6e-2f1a-4f6a-9e2f-6a1b4d0e5c33",
  "assetId": "lband-pennzoil",
  "kind": "lband",
  "mode": "reveal",
  "phase": "on_air",
  "startsAt": "2026-08-13T10:15:00.000Z",
  "endsAt": "2026-08-13T10:15:15.000Z",
  "restoreUntil": "2026-08-13T10:15:16.200Z",
  "cancelable": true
}
```

- **Copy the `id`** — you need it to cancel early.
- `phase` is `scheduled` before `startsAt`, `on_air` while visible, `restoring`
  during the closing animation.
- `restoreUntil` is 1.2 s past `endsAt` for bands: that margin is the program
  picture animating back to full frame. The sponsor lock is held until then, so a
  back-to-back second band must wait for that timestamp, not `endsAt`.

Verify with **`GET /graphics/cues`** → **Execute**. Expired cues clean themselves up,
so an empty array means nothing is on air.

### A4. Take it off early (optional)

Expand **`DELETE /graphics/cues/{cue_id}`** → **Try it out** → paste the `id` from
step A3 → **Execute**. A success is **`204 No Content`**. This deletes the underlying
overlay and squeeze events and releases the sponsor lock.

### A5. Troubleshooting Method A

| Status | Message | Fix |
|---|---|---|
| `404` | graphics asset does not exist | `assetId` typo — re-check `GET /graphics/catalog`. |
| `422` | animation mode is not supported by this asset | Use a value from that asset's `supportedModes`. |
| `422` | video graphics are disabled until their runtime gate passes | Restart the API with `ENABLE_VIDEO_GRAPHICS=true`, or pick an image band. |
| `422` | duration must leave time for both animation legs | Raise `durationMs` or lower `fadeMs` so `2 × fadeMs < durationMs`. |
| `409` | an L-band or Aston cue is already active; stop it first | `GET /graphics/cues`, then `DELETE` the active one. |

---

## Method B — Schedule it (rundown API)

Use this when the commercial must fire at a set time of day, or automatically each
time a particular program starts. This path is saved and versioned.

> Requires `ENABLE_RUNDOWN_SERVICE=true`. Confirm with
> **`GET /rundowns/status`** → **Execute** and check `"enabled": true`.

The flow is always the same five calls:

```text
GET /rundowns/main
  -> POST /rundowns/main/draft        (fork a working copy)
  -> edit the document, add the graphic
  -> POST /rundowns/main/draft/validate
  -> PUT  /rundowns/main/draft        (save)
  -> POST /rundowns/main/publish      (go to air, make permanent)
```

### B1. Read the live schedule

Expand **`GET /rundowns/{rundown_id}`** → **Try it out** → `rundown_id` = `main` →
**Execute**.

Note `activeVersionId` and the `timezone`. If a `draft` object is already present,
someone is mid-edit; note its `revision` and skip to B3 rather than forking a new one.

### B2. Fork a draft

Expand **`POST /rundowns/{rundown_id}/draft`** → **Try it out** → `rundown_id` = `main`
→ body:

```json
{ "from": "active", "replace": false }
```

**Execute.** Expect **`201`** with `revision: 1` and a `document` object.

- `409` means a draft already exists. Either continue with it (B3), or resend with
  `"replace": true` to discard it — that is destructive.
- `409` on `"from": "active"` when nothing was ever published: use
  `{ "from": "playlist" }` instead to seed programs from the live queue.

**Copy the whole `document` object out of the response** into a text editor. You will
paste an edited version of it back in B4.

### B3. Find where the graphic goes

Inside the document:

```jsonc
{
  "version": 1,
  "rundown": { "id": "main", "name": "main", "timezone": "Europe/London", "playlistId": "main", "loop": true },
  "items": [
    {
      "key": "coke-ad",
      "stableItemId": "…",
      "mediaId": "…",
      "displayName": "coke.mp4",
      "durationMs": 30000,
      "timelineStartMs": 0,
      "position": 0,
      "graphics": []          // <-- item-start triggers live here
    }
  ],
  "scheduledGraphics": [],     // <-- wall-clock triggers live here
  "timelineDurationMs": 30000
}
```

Two places, two trigger types. Pick one.

#### B3a. Fire the band when a program starts (`item-start`)

Add an object to that program's `graphics` array:

```json
{
  "key": "coke-pennzoil-lband",
  "assetId": "lband-pennzoil",
  "mode": "reveal",
  "durationMs": 15000,
  "fadeMs": 600,
  "trigger": { "type": "item-start", "offsetMs": 0 }
}
```

- `key` must be unique across the whole document. It is your handle for this trigger.
- `offsetMs` is measured from the observed start of that program. `5000` puts the
  band up five seconds in.
- The band must fit inside the program: `offsetMs + durationMs` must not exceed the
  item's `durationMs`, or validation fails on clip-fit.

An Aston on the same item is the same shape with `"assetId": "aston-static"` — but
it may not overlap the L-band in time. Stagger them, e.g. L-band at `offsetMs: 0`
for 15000 ms, Aston at `offsetMs: 17000`.

#### B3b. Fire the band at a clock time (`wall-clock`)

Add an object to the top-level `scheduledGraphics` array instead:

```json
{
  "key": "pennzoil-1430",
  "assetId": "lband-pennzoil",
  "mode": "reveal",
  "durationMs": 15000,
  "fadeMs": 600,
  "trigger": { "type": "wall-clock", "startsAt": "2026-08-13T14:30:00Z" }
}
```

- `startsAt` is **UTC ISO-8601** and must be **within the next 14 days**, otherwise
  `422`.
- Do not hand-convert local time. Use **`POST /rundowns/{rundown_id}/time/resolve`**
  with `{"date": "2026-08-13", "time": "14:30:00"}` — it interprets the value in the
  rundown's timezone, rejects impossible or ambiguous daylight-saving times, and
  returns the exact `startsAt` string to paste here.

### B4. Validate before saving

Expand **`POST /rundowns/{rundown_id}/draft/validate`** → `rundown_id` = `main` → body:

```json
{ "document": { "…paste your edited document here…" } }
```

**Execute.** Expect `{"valid": true, "itemCount": …, "graphicsCount": …, "checksum": "…"}`.
This writes nothing. It checks media registration, that the asset and mode exist and
match, unique keys, clip-fit, **L-band/Aston overlap**, the 14-day wall-clock window,
and the `2 × fadeMs < durationMs` animation rule. Fix any `422` here before saving.

### B5. Save the draft

Expand **`PUT /rundowns/{rundown_id}/draft`** → `rundown_id` = `main` → body:

```json
{
  "expectedRevision": 1,
  "document": { "…the same document you just validated…" }
}
```

`expectedRevision` must equal the draft's current `revision` from B2, or you get
`409` and nothing changes. The response returns the **new** `revision` — note it.

Nothing is on air yet. You may repeat B3–B5 as often as you like; each save is one
row update and creates no version.

### B6. Publish

Expand **`POST /rundowns/{rundown_id}/publish`** → `rundown_id` = `main` → body:

```json
{ "expectedRevision": 2 }
```

using the revision returned by B5. **Execute.** Expect **`202 Accepted`**.

Check the `channelFile` field in the response:

- `{"state": "written", …}` — on air and permanent.
- `"skipped"` — no bootstrap file configured, or writeback is off. On air; whether it
  survives a restart depends on your deployment.
- `"failed"` with an `error` — **it is still on air**, but the next container restart
  will revert it. Fix the file permission and publish again.

Then poll **`GET /playlists/main`** until `appliedRevision` equals `desiredRevision`.
That is when the schedule has actually landed on the queue.

### B7. Confirm the commercial fired

| Endpoint | What it tells you |
|---|---|
| `GET /rundowns/main/occurrences` | Which program starts were observed. |
| `GET /rundowns/main/executions` | Per-trigger state — `dispatchedAt`, the `cueId` it created, and `error` if it failed. |
| `GET /graphics/cues` | What is on air right now, including cues the scheduler raised. |
| `GET /rundowns/main/audit` | Who changed what. |

A published trigger that fired shows up in `/executions` with a `cueId`; that same id
appears in `/graphics/cues` while the band is visible.

### B8. Pull all scheduled graphics (optional)

**`POST /rundowns/{rundown_id}/reset`** removes every scheduled graphic while keeping
the programs. It atomically publishes a programs-only replacement and cancels future
work. Manual cues from Method A and the version history are untouched.

To remove just the one commercial, do not use reset — fork a draft, delete that
`graphics` entry by its `key`, validate, save, publish.

### B9. Troubleshooting Method B

| Status | Cause | Fix |
|---|---|---|
| `404` on `GET /rundowns/main/draft` | Normal right after a publish — publish consumes the draft. | Fork a new one with `POST /draft`. |
| `409` on `PUT /draft` | `expectedRevision` is stale; someone else saved. | Re-`GET` the draft, re-apply your edit on the fresh document, retry. |
| `409` on `POST /draft` | A draft already exists. | Continue with it, or resend with `"replace": true`. |
| `409` on publish, before the queue moves | The channel file cannot be written. | Fix the path/permission. Nothing went to air half-done. |
| `422` L-band/Aston overlap | Two sponsor bands overlap in time. | Stagger them; leave ~1.2 s past the first band's end for the restore. |
| `422` clip-fit | The band runs past the end of its program. | Reduce `durationMs` or `offsetMs`. |
| Publish refused, no on-air item | Transcoder not running, or the queue has nothing playing. | Start the transcoder, confirm with `GET /playlists/main`. |

---

## Which method should I use?

- **Ad-hoc sponsor break, operator watching the output** → Method A. One call, one
  response, cancel with a DELETE.
- **A commercial that must run at 14:30, or on every airing of a program** → Method B.
  It survives restarts, is auditable through `/executions`, and validation catches
  overlaps before they can reach air.

They coexist. A manual cue from Method A takes the sponsor lock, so a scheduled
trigger that lands while it is up will fail with a conflict recorded in
`/executions` — check `GET /graphics/cues` before punching a manual band near a
scheduled one.
