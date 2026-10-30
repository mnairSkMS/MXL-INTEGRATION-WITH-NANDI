# Transactional Rundown Service

The optional rundown service is a control-plane authoring layer for managed playlists - one per declared channel. It is disabled by default in code (`ENABLE_RUNDOWN_SERVICE=false`), but `docker-compose.playlist.yml` sets it to `true`, because the declarative channel bootstrap below depends on it. Static `pls+file://` playlists and the existing SQLite managed queue remain supported and unchanged when it is disabled.

## Safety and storage

On the first restart with `ENABLE_RUNDOWN_SERVICE=true`, the API makes a consistent SQLite backup beside `DB_PATH` named `*.before-rundown-v1`, then creates only new tables for rundowns, immutable versions, version items, graphics definitions, observed occurrences, executions, outbox commands, and audit history. It does not alter `events`, `media_assets`, `playlist_state`, `playlist_items`, or `graphics_cues`.

**v2 was a reset; v3 is a real migration.** Two paths meet in `_reset_rundown_schema`, and the difference matters.

v1 stored every authoring save as a `rundown_versions` row, so its history is an inseparable mix of publishes and abandoned intermediate saves that no later schema has a shape for. A pre-v2 database is therefore backed up as `*.before-rundown-v2`, its rundown tables are dropped and recreated, and the declarative channel file republishes on the same start. Deployments without a channel file must re-author their schedule.

**v3 only removes `rundown_drafts`**, so it must *not* take that path — falling through would drop ten tables to remove one, destroying publish history and as-run records. A v2 database is backed up as `*.before-rundown-v3`, the drafts table alone is dropped, and versions, occurrences, executions, outbox and audit rows all survive. The regression test is `test_v2_to_v3_drops_only_the_drafts_table_and_keeps_history`.

The existing managed queue remains the engine-facing execution projection. Publishing atomically projects the version's stable item IDs into that queue and bumps its normal revision. The existing reconciler sends that revision to `PlaylistInput` using `applyPlaylistSnapshot`; no new C++ media or M3U8 parser path is introduced.

If the current on-air item is absent from the new version, publishing retains it as a one-cycle bridge. Once the engine has changed to the first new stable item, the service removes the bridge in a normal follow-up revision. This avoids violating the engine rule that an on-air stable ID cannot disappear from a live snapshot.

## One write path: apply

`POST /rundowns/{id}/apply` is the only way to change the schedule. It reads the live version, applies the caller's change to an in-memory copy, validates the **result**, and publishes it — all inside one `BEGIN IMMEDIATE`. Either the whole change reaches air or nothing moves.

`apply_to_live` takes a mutate callback, so `apply`, `programs/import` and any future write share exactly one ordering of live-input check, writability probe, validation, projection and channel-file writeback to get right.

A caller supplies **either** an ordered operation list **or** a whole document:

| | `ops` | `document` |
|---|---|---|
| Shape | `[{op: "addGraphic", …}, …]` | a full rundown document |
| Suits | scripts, curl, surgical edits | the operator UI, which already holds the end state |
| Operations | `addGraphic`, `updateGraphic`, `removeGraphic`, `removeItem`, `insertItems` | — |

Supplying both, or neither, is `422`.

**Why an operation list rather than a sequence of small writes.** `_validate_timeline_constraints` is order-dependent: L-band and Aston share one squeeze resource, so a legal end state can be unreachable through a legal sequence of single writes. Moving one sponsor aside and another into its place is rejected at every intermediate step and accepted as a whole. Operations apply in order and validation runs **once**, on the merged result, which is what makes that expressible. The regression test is `test_the_sponsor_swap_that_separate_writes_cannot_express`.

**There is no draft.** `rundown_drafts` was removed in schema v3. An edit is either on air or it is unapplied text in someone's browser tab; there is no third, saved-but-not-live state to reason about, and no stale working copy for a restart to discard. `dryRun: true` covers "check before committing": it runs the identical merge and validation and rolls back, returning `version: null`.

`number` on a version is a true on-air generation counter, and `versions[]` is a history of what has been broadcast — a rejected or dry-run change never allocates one.

`POST /rundowns/{id}/seed` publishes a first version from the running playlist, for a channel that has no channel file and has never published. Without it `apply` would have nothing to mutate. It is `409` once a version exists.

Rundown v1 YAML remains the storage format. It is validated against registered managed media and the graphics catalog. **Every write path runs `_validate_timeline_constraints`**, so clip-fit and shared-squeeze violations are rejected before anything reaches air.

### Reset

`POST /rundowns/{id}/reset` (`reset_and_publish`) is the deliberate bulk action: it applies a mutation
that removes every item graphic and scheduled graphic from the live version and publishes the
result through `apply_to_live`, so it takes the same live-input check, writability probe,
validation, and channel-file writeback as any other change. Its `expectedVersionId` must be the
live version (`409` otherwise). The publish is audited as `schedule_reset` with the removed event
count and source version. It does not delete media, manual cues, or execution history. The
operator rundown grid does not expose it; it is an API action.

## Importing a program list

`POST /rundowns/{id}/programs/import` uploads a channel YAML — the same flat format as the declarative channel file below — and splices its programs into the live version. `position` is a zero-based index into the rundown's items; omitted, it appends. `expectedVersionId` and `dryRun` behave as on `apply`.

The file is lowered by `bootstrap.lower_to_rundown_v1`, **the same function the declarative bootstrap uses**, so an imported list inherits catalog validation, the `5s`/`600ms` duration grammar, stable UUIDv5 item identity, and the rule that an out-of-window `scheduled:` entry is dropped with a warning rather than deferred. Item-relative graphics come along attached to their items.

Three things are worth knowing:

- **It imports a program list, not the media.** Entries name media by relative path under `MEDIA_DIR`, and `_register_media` resolves each one. A missing file is `422` with that guidance, not a bare `404`: it is a fault in the document just submitted, and the fix is to upload the clip with `POST /media` first.
- **Only the newcomers are re-keyed.** `lower_to_rundown_v1` uniquifies keys within the file, but a slug can still collide with a live item — importing `clip.mp4` into a rundown that already has `opening` yields `opening-2`. Existing items keep their keys, because `stable_item_id` is `uuid5` of the key and is what the on-air row and every `playback_occurrences` join uses. Re-keying a live item would silently rewrite as-run history for a clip nobody touched.
- **Media registration happens before the write lock.** `_register_media` shells out to ffprobe and hashes whole files; doing that inside `BEGIN IMMEDIATE` would hold the lock across the slowest work in the request.

A channel YAML that declares a `channel:` other than the target rundown is refused, so a break reel cannot be imported onto the programme by mistake.

## Declarative channel bootstrap

`PLAYLIST_BOOTSTRAP_FILES` names one or more flat YAML files, each the source of truth for one channel's playlist and its graphics. A channel is one managed playlist, and a runtime holds one per engine input, in the order the files are listed: a channel with ad insertion declares its programme and its break reel this way, and both are validated and published identically. `PLAYLIST_BOOTSTRAP_FILE` remains supported as the single-file spelling. It is reconciled into SQLite by `preload_db` on every container start, before the managed M3U8 is materialized, and — unless `PLAYLIST_BOOTSTRAP_WRITEBACK=false` — **rewritten in full by every publish**. The file is therefore bidirectional: hand edits win at startup, and operator publishes win at publish time. A saved draft is the only thing that does not survive a restart.

```yaml
channel: main                     # optional; default $MANAGED_PLAYLIST_ID or "main"
name: Main rundown                # optional; default = channel id
timezone: Asia/Kolkata            # optional IANA; default UTC; used for naive `at:`

playlist:                         # required, >= 1 entry; ordered, loops
  - media: one-cent-call-1080p.mp4
  - media: coke-commercial-1080p-h264.mp4
    key: coke-break               # optional identity override
    graphics:                     # fires relative to this clip's start
      - use: lband-playboxtv      # graphics catalog asset id
        mode: reveal              # optional; catalog defaultMode
        at: 0                     # optional; default 0
        duration: 5s
        fade: 600ms

scheduled:                        # optional; absolute wall clock
  - use: logo-default
    mode: snap
    at: 2026-08-11T12:00:00Z      # ISO-8601, or naive local in `timezone`
    duration: 30s
```

**Lowering, not a second schema.** The flat document is transpiled into the canonical rundown v1 document and handed to `parse_yaml`, so it inherits catalog validation, animation timing rules, and stable UUIDv5 item identity. `RundownVersion.source_yaml` is non-nullable and re-parsed on every read, so a canonical rundown document has to exist regardless.

### Writeback

`lift_from_rundown_v1` is the inverse of the lowering, and `format_duration` inverts `parse_duration` (`5000 → 5s`, `600 → 0.6s`, `6100 → 6.1s`). The sequence in `publish` is ordered around one rule: never leave a file describing a publish that did not happen.

1. **Preflight, before any on-air change.** The bootstrap path must be set, writeback enabled, and the parent directory genuinely writable — probed by creating and removing a file, because the container runs as root and `os.access` reports success on directories root cannot write through. Failure raises `409` while the queue is still untouched.
2. `BEGIN IMMEDIATE`, materialize the version, project the queue, supersede the prior version, create wall-clock commands, delete the draft row.
3. Stage the replacement as `channel.yml.tmp` in the same directory and `fsync` it.
4. `db.commit()`.
5. `os.replace`, then restore the original `uid`/`gid`/`mode` captured by `os.stat` beforehand — the rename installs a new inode, and a root-owned file the operator cannot edit without `sudo` would be a regression.
6. A failure at step 5 is audited as `channel_file_write_failed` and returned in `channelFile`. The publish is already on air and is not rolled back.

A crash between 3 and 5 leaves a stale file, which the next start simply reapplies. That is the harmless direction.

**Every field is emitted explicitly** (`mode`, `at`, `duration`, `fade`) rather than relying on catalog defaults, so a machine-written file cannot change meaning when the catalog changes. An item `key:` is emitted only where it differs from the recomputed slug, so the common file stays terse. A generated-by header makes the rewrite visible in `git diff`.

**The round-trip invariant:** `lower(lift(doc))` reproduces the same canonical YAML, so the start after a publish reports `bootstrapState: "unchanged"` and creates no version. This is what makes writeback safe rather than a source of churn. It holds unconditionally for item-relative graphics. It does *not* hold for `scheduled:` wall-clock entries once their time passes: `_lower_graphic` drops those with a warning, which is correct, and the restart then reports `applied`.

**Comments are lost the first time Publish rewrites the file**, and terse hand-written entries expand to their explicit form.

**The mount must be a directory.** Docker bind-mounts a single file by inode, so `os.replace` onto a file-mounted path fails with `EBUSY`. `docker-compose.playlist.yml` mounts `./config:/opt/utpl/channel` read-write and sets `PLAYLIST_BOOTSTRAP_FILES: /opt/utpl/channel/channel.yml,/opt/utpl/channel/ads.yml`. A publish is written back to the file that declares the channel being published, so several channels in one directory cannot overwrite each other.

**Durations require a unit** (`5s`, `600ms`, `1.5s`); `at: 0` is the only accepted bare value. A bare-integer fallback would make `duration: 5` a silent five-millisecond graphic.

**Item keys slug from the media basename** (`Ad Spot.mp4` → `ad-spot`, repeats become `-2`, `-3`), because `stable_item_id = uuid5(namespace, "{channel}:item:{key}")` is what `playback_occurrences` joins on. A positional key would rewrite every downstream item's identity on a single insert and break as-run history for unrelated clips. Renaming a media file changes item identity; pin it with an explicit `key:`.

**Blast radius.** Each application replaces `playlist_items` for the channel, publishes a new immutable version, supersedes all prior versions, **deletes any operator draft**, and cancels pending commands from every superseded version. The draft is discarded rather than kept because reopening an operator's stale edits after a restart that was meant to replace them would hide what actually happened. Canceling a not-yet-started cue deletes its rows from the `events` table; on-air and restoring graphics are left alone. `media_assets`, `playback_occurrences`, `rundown_audit_log`, and dispatched `events` history are retained. Referenced media is auto-registered (ffprobe + SHA-256) if it is not already imported.

**Idempotence.** A rewrite is skipped when the document checksum matches the active published version *and* the projected items match the live queue. The checksum alone is not sufficient, because the Playlist panel can mutate `playlist_items` without creating any version. The skip still records a `bootstrap_unchanged` audit row, and `preload_db` still forces one snapshot replay per engine process.

**Ordering is load-bearing.** A fresh engine boots from the materialized `/runtime/main.m3u8` with generated stream IDs and rebinds its on-air item *by URI* on the first snapshot (`PlaylistInput::applyPlaylistSnapshot`, `allowInitialUriRebind`). If the M3U8 were written from the pre-bootstrap queue while the snapshot carried only new URIs, the engine would refuse the snapshot permanently. The bootstrap therefore runs before the materialization step it shares with the legacy M3U8 seed.

**Expired wall-clock graphics are skipped with a warning.** Nothing re-reads the file later, so a `scheduled:` entry whose time has passed, or that falls beyond the 14-day scheduling window, is dropped rather than deferred, and reported in `bootstrapWarnings`.

**Failure policy.** All destructive work runs inside one `BEGIN IMMEDIATE`, so any rejection leaves the database byte-identical and the previous playlist on air. By default the failure is logged, audited, and surfaced at `GET /rundowns/status` while startup continues. `PLAYLIST_BOOTSTRAP_STRICT=true` additionally exits `preload_db` non-zero. Neither mode darks the channel: with `startsecs=0` and `autorestart=false` a non-zero exit still lands in `EXITED`, and `supervisord-dependent-startup` keys off the event name alone, so `transcoder` and `transcoder_api` start regardless.

`GET /rundowns/status` reports `bootstrapManaged`, `bootstrapFile`, `bootstrapWriteback`, `bootstrapState` (`absent`/`applied`/`unchanged`/`failed`), `bootstrapAppliedAt`, `bootstrapChecksum`, `bootstrapError`, and `bootstrapWarnings`. The Program Schedule workspace shows a banner from these fields. With writeback on, the banner says the opposite of what it used to: publishing is what makes a change permanent, and an unpublished draft is the only impermanent thing.

## The rundown grid

`/operator` renders the queue and the schedule as **one table** in the Playlist panel. The two used to be separate panels — a queue with no time, and a five-lane timeline with no content — which meant neither answered what an operator actually asks: what is on now, what is next, at what time, and what graphics ride on it.

The **queue snapshot is the spine**: it is what plays, in the order it plays, and it is what `insertAfter`/`moveItem` index into. The schedule document is joined onto it by stable item ID, which is exact because `_queue_projection` creates each `PlaylistItem` with `id = stable_item_id`. Each program row expands to list its scheduled graphics; wall-clock graphics belong to no program and get their own group at the foot of the table, without which they would be unreachable from the UI. A queue row with no matching schedule item is the `_queue_projection` one-cycle bridge, tagged as such with em dashes for every schedule column.

It uses structured JSON; the browser never parses or writes YAML. `GET /rundowns/status` is deliberately available even when the optional service is disabled, so the UI can state why scheduling is unavailable without probing internal configuration. When it is disabled the module hands the table back to `operator.js`, which still lists and reorders the queue using ffprobe durations, with no start times and no graphics.

### The clock, and why it re-anchors

`PlaylistItem.started_at` is a dead column — nothing writes it. The only observed timing is `playback_occurrences.observed_at`. So the on-air row's start is observed fact and every other row is projected from it:

```
loopEpoch   = observedStart(onAirItem) - timelineStartMs(onAirItem)
rowStart[i] = loopEpoch + timelineStartMs[i] + (i < onAirIndex ? loopDurationMs : 0)
```

Rows above the on-air item have already played this cycle, so they show their **next** airing. Recomputing `loopEpoch` from the current item re-anchors the grid at every transition.

**This is not a stylistic choice.** Measured on the demo channel, real item durations differ from their probed nominal values by up to 300 ms per item — `toyota-etios` probes at 15905 ms and actually ran 16204 ms. Projecting forever from a fixed epoch accumulates that error, and any skip or Apply invalidates the projection outright. Re-anchoring bounds the error to one item's deviation.

`timelineStartMs` is a cumulative sum that treats an unprobed clip as zero, so every row after such a clip is understated. Those rows render `—` rather than a confident wrong time.

`GET /rundowns/{id}/clock` serves the anchor: `syncedAt` (`MIN(observed_at)` for the active version — when this schedule started running), `currentItemId`, `currentStartedAt`, `loopDurationMs`, `cycle`, and `serverNow`. `serverNow` exists so the browser can correct for skew between the container clock and the operator's machine; without it every projected row time is wrong by that difference. Every field is nullable or zero before the observer has seen a transition. It is served rather than mined from `GET /occurrences` because that endpoint is capped at the newest 200 rows — about 40 minutes at a 37-second loop — and so cannot answer when the schedule started.

### Rendering at 1 Hz without fighting the operator

The old queue table did a full `replaceChildren` every second, which was safe only because it held no controls. The grid holds expansion state and buttons, so the render is split: `renderGridStructure` rebuilds the DOM only when a signature over the version, item and graphic keys, queue membership, queue state and edit revision changes, while `refreshGridLive` runs every second and writes only the cells whose value is a function of wall-clock time — projected start, the observed/projected label, the on-air countdown, and execution outcomes. Rebuilding at 1 Hz would flicker the table and collapse every open row.

- `GET /rundowns/{id}/versions/{versionId}` returns a published version, carrying stable item IDs, resolved media durations, cumulative timeline positions and event definition IDs. The live one is named by `activeVersionId` from `GET /rundowns/{id}`.
- `POST /rundowns/{id}/apply` applies `ops` or a `document` and publishes, returning the new version, an `applied` count, a `channelFile` outcome and any `warnings`. `dryRun: true` validates and rolls back. A stale `expectedVersionId` returns `409` and changes nothing.
- `POST /rundowns/{id}/programs/import` splices a channel YAML's programs in; see below.
- `POST /rundowns/{id}/seed` publishes a first version from the running playlist; `409` once one exists.
- `POST /rundowns/{id}/time/resolve` turns a Date and Time entered in the rundown's IANA timezone into the canonical UTC `startsAt`; it rejects invalid, nonexistent, ambiguous, or out-of-window civil times.
- `GET /rundowns/{id}/clock` returns the loop anchor the grid projects start times from; see above.

The grid edits a copy of the live version in the browser and reports **Live**, **Unapplied**, or **Not applied** with the rejection reason beside the buttons. **Apply** puts the buffer on air in one call; **Revert** discards it and reloads what is on air, confirming first because it is destructive. Inspector fields are deliberately permissive while an operator types: duration, offset, fade, and wall-clock values are checked on Apply.

**There is no Validate button.** It ran exactly the preflight Apply runs and posted to the same endpoint with `dryRun: true`, so it never was the thing keeping a bad change off air — `apply_to_live` validates inside `BEGIN IMMEDIATE` and rolls back, leaving the database byte-identical and the previous schedule playing. Its only distinct effect was setting a `validatedRevision` that nothing read, left over from when a draft had to be validated before it could be published. `dryRun` remains on the API, where it is genuinely load-bearing: it is how the YAML import renders its preview before anything reaches air, and how a script can test a change it is not ready to commit.

**Unapplied edits live only in the tab.** They survive neither a browser reload nor a restart. That is the honest cost of removing the saved draft, and the banner says so.

To clear the schedule, apply a document whose programs carry no graphics, or send `removeGraphic` operations. The former **Reset schedule** button and `POST /rundowns/{id}/reset` were removed: they were a second entry point onto `apply_to_live` that expressed nothing an ordinary apply cannot, and a one-click destructive action on live triggers is not worth a dedicated route.

Item-relative events require a known probed clip duration and must fit inside it, including the sponsor restoration margin. Item-relative Logo/Bug events are finite. L-band and Aston share the squeeze resource; their intersections are rejected on every write path. In practice only two sponsors *on the same item* can collide: item windows are disjoint and clip-fit keeps each sponsor inside its own item, so the cross-item and loop-boundary branches of `_validate_timeline_constraints` are unreachable while clip-fit holds. Wall-clock starts must remain timezone-aware and inside the existing 14-day window.

The UI shows Duration, Offset, and Fade in decimal seconds with 0.01-second precision. Duration is the complete visible interval: fade/reveal in and out are contained within it, not added after it. API documents, YAML, SQLite, and runtime calls remain integer milliseconds (`durationMs`, `offsetMs`, and `fadeMs`) for compatibility. Animated modes require room for both animation legs; that constraint is reported by the Apply that would have breached it.

## Default graphics and published-event inspection

The catalog declares an additive `defaultMode` for each asset. New manual and Program Schedule cues use fade for Logo/Bug and reveal for L-band/Aston; previously published definitions retain their configured mode. Custom catalogs without that field retain a compatible type-based fallback.

Selecting an event in a published version shows its immutable definition and latest execution record in the inspector. Removing it changes only the local buffer; it does not affect the active schedule until Apply. Publishing a replacement version cancels pending/retrying commands and not-yet-started scheduled cues from the superseded version, while on-air/restoring graphics finish normally.

Automatic cue commands are inserted into the outbox only 750 ms before their planned execution. This keeps a future L-band/Aston event from holding the shared squeeze lock while an earlier, non-overlapping cue is still eligible. The 250 ms input observation remains control-plane timing, not frame-accurate timecode.

```yaml
version: 1
rundown:
  id: main-break
  name: Main break
  timezone: UTC
  playlistId: main
  loop: true
items:
  - key: airasia
    media:
      relativePath: air-asia-billboard.mp4
      sha256: optional-registered-sha256
    graphics:
      - key: airasia-lband
        assetId: lband-voltedge
        mode: reveal
        durationMs: 15000
        fadeMs: 600
        trigger: {type: item-start, offsetMs: 0}
scheduledGraphics:
  - key: fixed-logo
    assetId: logo-default
    mode: snap
    trigger: {type: wall-clock, startsAt: 2026-08-06T12:00:00Z}
```

Item keys and graphics keys are required, URL-safe identifiers. A queue-item stable UUID is derived from the rundown ID and item key, so a re-import preserves identity when the key is unchanged. Relative media paths must already be imported below `MEDIA_DIR`; an optional SHA-256 must match the registered asset. Item-relative graphics use `item-start` plus a non-negative `offsetMs`. Wall-clock graphics must be timezone-aware and within the scheduler's 14-day window.

## API and execution records

- `POST /rundowns/{id}/apply` is the single write path, taking `ops` or a `document`; `dryRun: true` validates without writing.
- `POST /rundowns/{id}/programs/import` splices a channel YAML's programs into the live version; `POST /rundowns/{id}/seed` publishes a first version from the running playlist.
- `POST /rundowns/{id}/publish` returns `202`: queue application remains asynchronous and is shown by the existing `GET /playlists/main` desired/applied revision fields.
- `GET /rundowns/{id}`, `/occurrences`, `/executions`, and `/audit` expose version, as-run, and operational history.

The service polls the already-available engine input status at 250 ms while enabled and records observed item starts. It writes graphics dispatches to a persistent outbox before calling the existing catalog cue service; commands retry with bounded backoff and executions retain failures. This is a control-plane observation timestamp, not a frame-accurate engine timecode guarantee.

Keep a single API worker with the supplied SQLite profile. Multi-worker deployment requires a database lease around reconciliation and outbox dispatch before it is supported.
