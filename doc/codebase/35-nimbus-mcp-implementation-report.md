# Nimbus-Style MCP Control Layer — Implementation Report

> Promoted from a root-level working note on 2026-08-18, unchanged apart from link paths. It was
> written on 2026-08-12 and **supersedes [33](33-mcp-interface-evaluation.md) wherever the two
> disagree** — 33 is the 2026-08-07 market survey and design sketch, this is the source-verified
> implementation assessment. Its Appendix A lists the corrections. Nothing here is implemented;
> there is still no MCP code in the tree.
>
> Provenance: the capability breakdown it answers is preserved at
> [`doc/misc/nimbus-capability-breakdown.md`](../misc/nimbus-capability-breakdown.md).

Written 2026-08-12 against the working tree at `1.1.0-beta.2-skandha.3`.

This report answers the question posed in `doc/misc/nimbus-capability-breakdown.md` (the original capability breakdown):

> Determine whether our existing control plane already contains the primitives that Veset exposed
> through MCP, and estimate how much work is required to add an MCP adapter safely.

Every claim about this codebase below was verified by reading the source in this checkout on
2026-08-12, not from prior documentation. Where the existing handbook page
[33-mcp-interface-evaluation.md](33-mcp-interface-evaluation.md) disagrees with the
source, the source wins and the discrepancy is recorded in
[Appendix A](#appendix-a--corrections-to-handbook-page-33).

---

## 0. Executive summary

**The control plane is in better shape for MCP than expected, and the blockers are narrower than
handbook page 33 implies — but they are absolute.**

Three findings dominate everything else:

1. **The domain layer is genuinely clean.** Every mutation already routes
   `router → service → validation → database`, exactly the architecture `doc/misc/nimbus-capability-breakdown.md` §6 asks
   for. The operator UI is a pure REST client — it calls the same endpoints through one `api()`
   helper in [operator.js:25](../../api/src/transcoder_api/static/operator.js#L25) and
   [rundown.js:14](../../api/src/transcoder_api/static/rundown.js#L14), and manipulates **no** internal
   state directly. The refactor that `doc/misc/nimbus-capability-breakdown.md` warns about ("flag places where the UI
   directly manipulates state") **is not needed here**. That is the single most expensive prerequisite
   in the whole checklist and we already have it.

2. **There is no identity anywhere in the system.** No auth, no CORS, no middleware, no user model,
   no actor column in the audit table. Veset's entire safety claim — "the same permissions,
   guardrails, and audit trail that Nimbus customers already rely on" — is a claim about something
   that *predates* their MCP server. We have nothing to inherit. This blocks every write tool and
   caps categories 9 and 10 at MISSING regardless of how much MCP code we write.

3. **The write path we would most want to expose is already broken.** All four config `set-*` routes
   in [config.py](../../api/src/transcoder_api/routers/config.py) are dead code that returns HTTP 200 with
   an error message — see [§3.3](#33-the-config-write-routes-are-dead). Nobody has noticed because
   nothing calls them.

**Scorecard across the 17 capabilities from `doc/misc/nimbus-capability-breakdown.md`:**

| Verdict | Count | Capabilities |
| --- | --- | --- |
| **READY** | 5 | Schedule query, current/next item, schedule validation, asset lookup, media metadata |
| **PARTIAL** | 7 | Runtime health, playlist mutation, event mutation, audit search, take-to-air, skip/cancel, durable jobs |
| **MISSING** | 4 | Ingest job state, configuration change, permissions, agent auditing |
| **UNSAFE TO EXPOSE** | 1 | Restart transcoder (destructive GET) |

**Recommendation:** ship a read-only MCP server now (Phase 1, ~2–3 days), because it needs no
prerequisites and covers the majority of what operators actually ask. Do not ship a single write tool
until identity exists. Total effort to full Nimbus parity: **roughly 6–8 weeks**, of which only about
one week is MCP code — the rest is the auth, telemetry, and ingest-state layers we would need anyway.

---

## 1. What Veset actually announced (re-verified 2026-08-12)

Handbook page 33 flagged its own sourcing as one day old and asked to be re-checked before use. I
re-checked it. **Nothing has changed in six days.**

The [announcement](https://www.veset.tv/news/introducing-the-veset-nimbus-mcp-server) (2026-08-06)
still lists exactly five capabilities:

| # | Verbatim wording | Risk tier |
| --- | --- | --- |
| 1 | "Monitor live channels and playout health" in real time | Read |
| 2 | "Query schedules, playlists, and media assets" without separate interfaces | Read |
| 3 | "Review ingest and audit logs" to trace events | Read |
| 4 | "Edit playlists and adjust configuration" with controlled actions | Write |
| 5 | "Take events live on air" when needed | On-air |

The security claim is unchanged and remains the whole architecture:

> "every action Claude can take runs through the same permissions, guardrails, and audit trail that
> Nimbus customers already rely on."

Framed by Veset as removing **"friction, not oversight."**

**Still true, and important:**

- **No public tool manifest, no source code, no schema.** The `doc/misc/nimbus-capability-breakdown.md` caveat holds
  completely. Everything past the five bullets is inference, and this report labels it as such.
- **No self-serve.** "Reach out to your account team to get set up." Design-partner rollout, not GA.
- **No third-party coverage.** Six days on, search still returns only Veset's own pages.
- **Roadmap stated:** "deeper automation, proactive alerting, and expanded configuration control"
  for future releases.

The [Nimbus API docs](https://apidocs.veset.tv/) confirm the 11 resource groups and the auth model
the MCP server inherits: **JWT bearer tokens**, minted in the UI as *Organization tokens* or
*Service provider tokens*, with published guidance to "assign a role with least privileges required
to do the job," create dedicated API-only roles, use short expiry, and rotate. Stateless, no session
persistence.

> **In plain terms:** Veset did not build a safe AI system. They built a permission system years ago,
> and the MCP server is a thin adapter that cannot exceed it. The lesson for us is that the hard part
> is not the MCP server.

---

## 2. What we have — verified control-plane inventory

### 2.1 The REST surface

Six routers mounted in [main.py](../../api/src/transcoder_api/main.py#L28-L33). **Note the `/config`
prefix** — handbook page 33 lists these routes unprefixed and is wrong (Appendix A).

| Router | File | Routes | Backed by |
| --- | --- | --- | --- |
| input | [input.py](../../api/src/transcoder_api/routers/input.py) | `GET /inputs`, `GET /inputs/{id}`, `POST /inputs/{id}` | gRPC, live engine |
| config | [config.py](../../api/src/transcoder_api/routers/config.py) | `GET /config/get-config`, `/config/get-outputs`, `/config/get-output/{id}`, `/config/get-inputs`, `/config/get-input/{id}`, `POST /config/set-*` ×4, `GET /config/check-transcoder`, `GET /config/restart-transcoder` | `config.json` file + Supervisor XML-RPC |
| schedule | [schedule.py](../../api/src/transcoder_api/routers/schedule.py) | `GET/POST /scheduled_events`, `DELETE /scheduled_events/{id}` | SQLite `events` |
| graphics | [graphics.py](../../api/src/transcoder_api/routers/graphics.py) | `GET /graphics/catalog`, `GET/POST /graphics/cues`, `DELETE /graphics/cues/{id}` | Catalog JSON + `graphics_cues` |
| playout | [playout.py](../../api/src/transcoder_api/routers/playout.py) | `GET/POST/DELETE /media*`, `GET /playlists/{id}`, `POST/PATCH/DELETE /playlists/{id}/items*`, `POST /playlists/{id}/{action}` | SQLite + gRPC reconciler |
| rundown | [rundown.py](../../api/src/transcoder_api/routers/rundown.py) | 17 routes: draft CRUD, validate, publish, reset, versions, occurrences, executions, audit, time/resolve | Transactional rundown service |

**Two distinct input surfaces exist and they are not the same thing** — a fact page 33 conflates:

- `/inputs` — **live engine state** over gRPC. This is the real one.
- `/config/get-inputs` — **static `config.json` contents** read off disk. Reflects what the file says,
  not what is running.

An MCP server must expose these as clearly different tools or the model will confuse desired state
with actual state.

### 2.2 The gRPC surface

Exactly four RPCs in [transcoder.proto](../../transcoder/app/grpc/proto/transcoder.proto):

```
getInputs(Empty) → InputList
getInput(GetInputParameters) → Input
setInputStream(SetInputStreamParameters) → Input      # ← the atomic "take"
applyPlaylistSnapshot(...) → ApplyPlaylistSnapshotResponse   # private, reconciler-only
```

`Input` carries `id`, `streams[]`, and `currentStreamId`. `InputStream` carries `id`, `uri`, and a
`StreamState` enum of `UNKNOWN | CREATED | BUFFERING | BUFFERED`.

**That enum is the entire machine-readable health vocabulary in the product.** There is no
`RUNNING / FAILED / STARTING / STOPPED`, no alarm model, no error field on the engine side.

### 2.3 The persistence layer

15 tables, all SQLite, defined in [model/db.py](../../api/src/transcoder_api/model/db.py). The
rundown service adds 10 of them additively without touching the original execution projection.

Confirmed live in `runtime/managed-playlist.sqlite`: the rundown service **is running in this
checkout**, with 2,402 rows in `rundown_audit_log` and playback occurrences advancing to sequence
293.

### 2.4 What does not exist, verified by exhaustive search

| Thing | Evidence |
| --- | --- |
| Authentication / authorization | No `middleware`, `CORS`, `Depends(auth)`, token or user model anywhere in `api/` |
| Any telemetry | `grep -rl "prometheus\|opentelemetry\|statsd"` over the tree (excluding `sandbox/`) returns **zero** files |
| Multi-channel | `media._live_input()` is hardcoded to `inputs[0]`; `config/channel.yml` declares `channel: main`, singular |
| Ingest job model | No job table, no state machine — media import is synchronous and raises on failure |
| Audit actor | `RundownAuditLog` has `action`, `subject_type`, `subject_id`, `details_json`, `created_at` — **no actor, no request ID, no interface field** |

---

## 3. The three blockers

### 3.1 No identity (blocks categories 6, 7, 8, 9, 10)

[main.py](../../api/src/transcoder_api/main.py) mounts six routers, a static directory, two exception
handlers and a background task. It mounts no middleware of any kind. There is no `Authorization`
header parsing anywhere in the tree.

This means the architecture `doc/misc/nimbus-capability-breakdown.md` §9 demands —

```
User → AI client → MCP → authenticated user context → existing authorization → operation
```

— cannot be built, because the third-to-last box does not exist. What we would ship today is exactly
the anti-pattern that document warns against, minus even the API key:

```
User → AI → (nothing) → everything
```

**A write-capable MCP server on the current API is an unauthenticated remote control for a broadcast
chain.** Read-only is defensible on a trusted network. Writes are not defensible at all.

### 3.2 No telemetry (blocks category 1 beyond a liveness ping)

`GET /config/check-transcoder` proxies Supervisor's `getProcessInfo` and returns
`{"message": "<statename>"}` — one string, describing whether a **process** is running. It says
nothing about whether video is flowing, whether the encoder is keeping up, whether the manifest is
advancing, or whether the output is connected.

Veset can advertise "monitor playout health" because a Metrics API backs it. We can currently answer
"is the process up," which is the least interesting question an operator ever asks.

The one genuinely useful signal we do have is derived, not measured: the reconciler mirrors engine
state into `playlist_items.state` (`on_air` / `preloading` / `queued` / `error`) via
[`refresh_live_state()`](../../api/src/transcoder_api/services/media.py), and `PlaylistState.apply_error`
captures the last reconciliation failure.

### 3.3 The config write routes are dead

All four `set-*` handlers in [config.py](../../api/src/transcoder_api/routers/config.py) follow this shape:

```python
async def set_outputs(post_data: Dict[str, Any]):
    data = read_config()
    try:
        json.loads(post_data)          # ← post_data is already a dict
        ...
    except:                            # ← bare except swallows the TypeError
        return {"message": "error parsing request"}
```

FastAPI has already parsed the body into a `dict`. `json.loads()` on a `dict` raises
`TypeError: the JSON object must be str, bytes or bytearray`. The bare `except` catches it and
returns **HTTP 200** with an error string.

**Consequence: `/config/set-outputs`, `/config/set-output/{id}`, `/config/set-inputs`, and
`/config/set-input/{id}` have never worked.** Configuration cannot be changed through the API at all.
Category 7 is therefore not "partially ready" — the entire surface it would wrap is non-functional.

There is a second defect in the same file: `restart_transcoder` references `stop_result` in its
success check, but only assigns it inside `if process_info["statename"] == "RUNNING"`. Restarting a
stopped transcoder raises `UnboundLocalError`, caught by the outer handler and reported as a 500
"error communicating with Supervisor" — a misleading diagnostic.

### 3.4 `GET /config/restart-transcoder` — fix regardless of MCP

Registered as a **GET** at [config.py:176](../../api/src/transcoder_api/routers/config.py#L176). It stops
the transcoder, sleeps 10 seconds, and starts it — taking the channel off air for at least that long,
behind a verb that every convention, crawler, browser prefetcher and cache treats as safe.

An agent exploring the API to orient itself will fire GETs. **This is the one capability classified
UNSAFE TO EXPOSE**, and it should be changed to POST whether or not MCP is ever built.

---

## 4. Category-by-category analysis

Each of the 14 categories from `doc/misc/nimbus-capability-breakdown.md`, scored against this tree.

### Category 1 — Channel / playout health inspection → **PARTIAL**

| | |
| --- | --- |
| **What we have** | Process liveness (`/config/check-transcoder`); engine stream states via `/inputs`; derived item states (`on_air`/`preloading`/`queued`/`error`) in `playlist_items`; `PlaylistState.apply_error`; `rundown_event_executions.state` + `error` |
| **What we lack** | Any quantitative signal — bitrate, frame rate, drops, encoder latency, output connection state, queue depth. No alarm model. No `RUNNING/FAILED/STARTING/STOPPED` for anything but the OS process |

Answering `doc/misc/nimbus-capability-breakdown.md`'s own checklist for this category:

- *Single method for current state?* — Partly. `/inputs` (engine) + `/playlists/main` (queue) cover it in two calls.
- *State machine or reconstructed from logs?* — State machine. `StreamState` enum plus mirrored item state. **Good.**
- *`RUNNING / FAILED / STARTING / STOPPED`?* — **No.** Only `UNKNOWN/CREATED/BUFFERING/BUFFERED`.
- *Active input/output determinable?* — Input yes (`currentStreamId`). **Output no** — outputs are config-file entries with no runtime state at all.
- *Alarms machine-readable?* — No alarm concept exists.
- *Timestamps available?* — Yes, throughout.
- *Queryable without shell access?* — Yes.

**What it takes:** a health aggregation endpoint (`GET /health/channel`) composing engine state,
manifest freshness, reconciler status and last error into one document — ~1 day, no new
infrastructure. Real health monitoring needs the metrics layer: GStreamer tracers (already present in
the build, unused), an exporter, and per-output runtime state — **2–3 weeks**.

**MCP tools:** `get_channel_health()`, `get_playout_state()`, `list_stream_states()`.

---

### Category 2 — Schedule inspection → **READY**

The strongest surface in the tree, by a wide margin.

| | |
| --- | --- |
| **What we have** | Full versioned rundown domain model — `Rundown`, `RundownDraft`, `RundownVersion`, `RundownVersionItem`, `RundownEventDefinition`, `PlaybackOccurrence`, `RundownEventExecution`. 17 REST routes. Timezone-aware civil-time resolution (`resolve_civil_datetime`, `resolve_local_wall_clock_time`). Structured document views that never require clients to parse YAML (`document_from_yaml`, `document_from_draft`, `document_from_version`) |

`doc/misc/nimbus-capability-breakdown.md` asks whether the code can answer four questions without scraping a UI:

| Question | Answer | Source |
| --- | --- | --- |
| *What is happening?* | Yes | `GET /playlists/main` → `currentItemId`; `GET /rundowns/status` |
| *What happened?* | Yes | `GET /rundowns/{id}/occurrences`, `/executions`, `/audit` |
| *What will happen?* | Yes | draft/version documents + `rundown_event_definitions` |
| *What should happen?* | Yes | `POST /rundowns/{id}/draft/validate`, `POST /rundowns/{id}/time/resolve` |

**All four. This is MCP-ready today, read-only, with no prerequisites.**

The one structural caveat: the schedule is a **continuous loop**, not a wall-clock grid. There is no
"what's on at 7 PM" in the broadcast sense unless a wall-clock trigger is defined. `get_schedule(from, to)`
must be expressed against the loop plus wall-clock event definitions, not against a linear timeline.

**What it takes:** direct wraps. **~0.5 days.**

**MCP tools:** `get_rundown()`, `get_current_item()`, `get_next_item()`, `get_schedule_window()`,
`validate_schedule()`, `resolve_time()`.

---

### Category 3 — Asset / media-library access → **READY**

| | |
| --- | --- |
| **What we have** | `MediaAsset` with `id`, `relative_path`, `display_name`, `size_bytes`, `sha256`, **`probe_json`**, `created_at`. `_probe()` in [media.py](../../api/src/transcoder_api/services/media.py) shells `ffprobe -show_streams -show_format -of json` at registration and **persists the full result**. Containment-checked paths, extension allowlist, SHA-256 dedupe, and a hard requirement that every asset carry at least one video stream |

This directly satisfies `doc/misc/nimbus-capability-breakdown.md` §3's domain model: source URI, media metadata, duration,
video streams, audio streams, subtitle streams are **all present** inside `probe_json`.

Against its checklist:

- *Assets searchable?* — Only `list_assets()` (ordered by creation). No filter or query. Minor gap.
- *Metadata normalized?* — Stored as raw ffprobe JSON. Usable by a model, but not a normalized schema.
- *Readiness determinable?* — Implicitly: registration **fails** unless probe succeeds and a video
  stream exists, so any asset in the table is playable. There is no explicit `readiness` field.
- *Renditions enumerable?* — No rendition concept. This is a playout appliance, not a MAM.
- *Source availability checkable?* — Yes, `_absolute_path()` + `path.is_file()`.
- *Asset IDs mappable to jobs/schedules?* — Yes, FK from `playlist_items` and `rundown_version_items`.

**What it takes:** wrap `list_assets` and add a search filter + a derived `readiness` projection.
**~1 day.**

**MCP tools:** `find_asset(query)`, `get_asset(id)`, `get_asset_media_info(id)` (projects `probe_json`),
`get_asset_readiness(id)`.

---

### Category 4 — Ingest status and troubleshooting → **MISSING**

This is the largest genuine gap, and `doc/misc/nimbus-capability-breakdown.md` is right that it matters more than it first
appears.

| | |
| --- | --- |
| **What we have** | `import_asset()` and `store_upload()` — both fully **synchronous**. Success returns a `MediaAsset`; failure raises `UnprocessableException` and the HTTP response carries the message. Uploads are atomic (temp file, fsync, probe, `os.replace`) with cleanup on failure |
| **What we lack** | Everything durable. No `IngestJob` table, no state, no progress, no `started_at`/`completed_at`, no `error_code`, no history |

`doc/misc/nimbus-capability-breakdown.md` warns: *"If ingest only exists as console logs, an MCP implementation will be much
weaker."* Our situation is slightly worse — **failed ingests leave no trace at all.** The exception
propagates to the HTTP caller and is gone. "Which ingests failed today?" is unanswerable in principle,
not merely inconvenient.

**What it takes:** an `IngestJob` table exactly as `doc/misc/nimbus-capability-breakdown.md` specifies, plus moving upload
and import onto the existing reconciler loop so they become asynchronous with durable IDs. The
`RundownOutbox` pattern already in the tree (dedupe key, attempt counter, `next_attempt_at`,
`last_error`) is a working template to copy. **~1 week.**

**MCP tools:** `list_ingest_jobs(filters)`, `get_ingest_status(asset_id)`, `get_ingest_errors(job_id)`;
`retry_ingest(job_id)` is a write and waits on identity.

---

### Category 5 — Audit-log interrogation → **PARTIAL**

| | |
| --- | --- |
| **What we have** | A real, populated, structured audit log. `RundownAuditLog` with `action`, `subject_type`, `subject_id`, `details_json`, `created_at`, written through a single helper `_audit()` at 11 call sites. Exposed at `GET /rundowns/{id}/audit`. **2,402 rows live in this checkout** |
| **What we lack** | Three things, and they are exactly the three an investigator needs |

Comparing against `doc/misc/nimbus-capability-breakdown.md` §5's target record:

| Field | Present? |
| --- | --- |
| `timestamp` | ✅ `created_at` |
| `actor` | ❌ **absent — no user model exists** |
| `action` | ✅ |
| `resource_type` / `resource_id` | ✅ `subject_type` / `subject_id` |
| `previous_state` / `new_state` | ⚠️ only whatever a call site chose to put in `details_json` |
| `request_id` | ❌ absent |
| `result` | ⚠️ implicit — failures usually aren't logged |

Two further limits:

1. **Scope.** Audit covers rundowns only. Playlist mutations, graphics cues, media deletion, input
   switching and config changes write **no** audit record. "What changed in the last hour?" misses
   most of what can change.
2. **Signal-to-noise.** The live table is dominated by machine-generated `occurrence_observed` rows
   (every clip transition, ~1 every 10 seconds). An agent asking "who changed the schedule" would
   wade through thousands of automated rows. Audit needs a category discriminator separating
   *operator intent* from *system observation* before it is useful to a model.

**What it takes:** add `actor`, `request_id`, `result` and `category` columns; generalise `_audit()`
out of the rundown service into a shared module; call it from playlist, graphics, media and input
mutations. **~1 week**, and it depends on identity for `actor` to mean anything.

**MCP tools:** `search_audit_events(...)`, `get_recent_changes(resource_id)`, `get_action_history(resource_id)`.

---

### Category 6 — Playlist modification → **PARTIAL** (architecture READY, safety MISSING)

**The architecture is already correct.** `doc/misc/nimbus-capability-breakdown.md` §6 demands
`LLM → MCP → ScheduleService → validation → engine → database` rather than direct DB writes. That is
precisely what exists:

```
POST /playlists/{id}/items
  → playout.add_playlist_item          (router: BEGIN IMMEDIATE, revision check)
  → media.enqueue                      (service: FK validation, renumber, revision bump)
  → reconciler (0.25s loop)
  → transcoder.applyPlaylistSnapshot   (gRPC, idempotent per revision)
  → engine
```

Safety properties already in place, all verified:

- **Optimistic concurrency.** Every mutating playlist route requires `expectedRevision` and returns
  **409** on staleness. A stale agent write cannot clobber a concurrent operator edit.
- **Write serialization.** `_lock_mutation()` issues `BEGIN IMMEDIATE` so revision-check and mutation
  are atomic under SQLite.
- **On-air protection, checked twice.** The router re-reads live engine state via
  `media._live_input_and_current()` rather than trusting the SQLite mirror, and refuses to delete or
  reorder the item currently on air (409, "use skip"). `media.delete_item()` independently refuses
  again at the service layer, and refuses to remove the final item.
- **Idempotent application.** `applyPlaylistSnapshot` is revision-keyed and safe to retry.

Validation coverage against `doc/misc/nimbus-capability-breakdown.md`'s list — gaps, overlaps, locked events, start times,
duration, missing assets, timing modes, on-air constraints:

| Check | Status |
| --- | --- |
| Missing assets | ✅ FK + `NotFoundException` in `enqueue()` |
| On-air constraints | ✅ enforced at both layers |
| Duration / timing modes | ✅ `_validate_timeline_constraints()` — item-relative graphics must fit inside the owning clip *including* restoration margin |
| Overlaps | ✅ shared-squeeze mutual exclusion, **including across the loop boundary** |
| Gaps | n/a — continuous loop, gaps are structurally impossible |
| Locked events | ❌ no lock concept |
| Start times | ✅ for wall-clock triggers via `resolve_civil_datetime` |

That timeline validator is genuinely sophisticated and is the kind of deterministic guardrail that
makes agent writes safe.

**Verdict:** `doc/misc/nimbus-capability-breakdown.md` says *"If that domain logic already exists, MCP replication is mostly
an adapter problem."* **It exists.** This is an adapter problem — gated only on identity and on
audit coverage for playlist mutations.

**What it takes:** ~1 day of adapter work, after Phase 2 (identity) and Phase 5 (audit generalisation).

**MCP tools:** `insert_item()`, `move_item()`, `remove_item()`, `replace_item_media()` — each taking
`expectedRevision` so the model must read before it writes.

---

### Category 7 — Configuration changes → **MISSING**

Not "partial." The surface an MCP config tool would wrap **does not function** — see
[§3.3](#33-the-config-write-routes-are-dead). All four `set-*` routes return HTTP 200 with
`{"message": "error parsing request"}` and change nothing.

Even after that bug is fixed, the shape is wrong for agent use. `write_config()` dumps JSON to
`/opt/utpl/etc/config.json` with **no validation whatsoever** — no schema check, no dry-run, no
rollback, no reload signalling. `doc/misc/nimbus-capability-breakdown.md` asks for `validate → dry-run → apply → rollback`.
We have `write file`.

There is a further trap specific to this codebase, documented in
[31-code-reading-roadmap.md](31-code-reading-roadmap.md) and
[32-performance-and-tooling-findings.md](32-performance-and-tooling-findings.md): the
1,282-line config parser **silently ignores several documented-looking fields** — only `inputs[0]` is
consumed, `video.interlace` sits inside `#if 0`, the `adobe` and `enhanced` ad markers sit inside
`#if 0`, and `scte.prerollTime` is parsed but never read.

> **In plain terms:** the configuration file has fields that look real, are accepted without
> complaint, and do nothing. A human learns this the hard way once. A language model will make the
> same mistake every single time unless it is told, in writing, which fields are dead.

**What it takes:** (a) fix the four routes; (b) add a `--validate-only` mode to the parser so config
can be checked without applying — this does not exist and is the keystone of the whole safe-write
story; (c) build `propose → validate → human confirm → apply` with rollback; (d) publish an
**effective**-schema MCP resource that marks the dead fields explicitly. **~2 weeks.**

**MCP tools:** `get_effective_config()` (read, safe now), `validate_config(proposal)`,
`propose_config_change()`, `apply_config_change()` — the last two behind elicitation and identity.

---

### Category 8 — Manual event-to-air control → **PARTIAL**

`doc/misc/nimbus-capability-breakdown.md` calls the critical question:

> Does your code already expose an atomic command such as `take(event)`?

**Yes.** Three of them, layered:

```
gRPC setInputStream(inputId, streamId)        # engine-level atomic switch
  ↑ transcoder.switchInputStream()            # service wrapper
  ↑ media.skip(playlist_id, db)               # "advance to next stable item"
  ↑ POST /playlists/{id}/skip                 # REST
```

`media.skip()` is a careful implementation: it refuses when `revision != applied_revision`
(a queue change is still being applied), refuses with fewer than two items, reads the **engine** for
the current item rather than the SQLite mirror, computes the next item modulo the queue length, and
calls the atomic switch. The LLM would request the operation; the existing playout system performs
the frame-critical switching — exactly the separation `doc/misc/nimbus-capability-breakdown.md` §8 requires.

**What is missing** is everything around it: no authorization check, no confirmation step, no audit
record, no post-condition verification, and no `validate_take_action()` equivalent. Also note
`POST /playlists/{id}/start` and `/pause` return **410 Gone** — managed playlists run continuously, so
"skip" is the only playout verb.

Sequencing the flow from `doc/misc/nimbus-capability-breakdown.md` §8 against what exists:

| Step | Status |
| --- | --- |
| resolve channel + event | ✅ single channel, item IDs stable |
| `get_current_playout_state()` | ✅ `/playlists/{id}` + `/inputs` |
| `validate_take_action()` | ⚠️ partial — revision and item-count guards only |
| authorization check | ❌ **missing** |
| require confirmation | ❌ missing (MCP elicitation would supply it) |
| `take_event_to_air()` | ✅ `media.skip()` / `switchInputStream()` |
| deterministic playout service | ✅ engine owns the switch |
| verify resulting state | ⚠️ reconciler mirrors it within 0.25 s, not returned synchronously |

**What it takes:** ~2 days of adapter and confirmation work — but this is the **last** thing to ship,
after identity, audit, and telemetry. Veset shipped it in v1.0 because a customer misconfiguring an
RBAC role is contractually the customer's problem. That defence is unavailable to us.

**MCP tools:** `skip_current_item()`, `switch_input_stream()` — both with `DestructiveHint` and
elicitation.

---

### Category 9 — Permission-aware AI actions → **MISSING**

Nothing exists. No `AuthenticationService`, `AuthorizationService`, RBAC, permissions, roles,
policies, claims, or JWT handling anywhere in `api/`. The question "could an MCP request preserve an
actual user identity?" has a one-word answer: **no**.

**This is the gating item for the entire write half of the report.** Categories 6, 7, 8 and 10 are
all blocked behind it.

**What it takes** — Veset's own model is a cheap, proven template worth copying wholesale:

1. Token table + `Authorization: Bearer` middleware.
2. A small role set — `viewer` (Tier 0 reads), `editor` (playlist/draft writes), `operator`
   (on-air actions). Grafana binds MCP tools to exactly this kind of pre-existing role split.
3. Per-integration token issuance with short expiry, stored outside version control.
4. Request-scoped identity propagated into the service layer so audit can record it.
5. **Enforce read-only at the dispatcher, not per tool.** [CVE-2026-46519](https://www.manifold.security/blog/mcp-server-kubernetes-readonly-bypass)
   was a high-severity read-only bypass in `mcp-server-kubernetes` caused by per-tool checks. One
   forgotten decorator is one bypass.

**~2–3 weeks.** This is ordinary web-application work, not research.

---

### Category 10 — Every agent action is auditable → **MISSING**

Follows directly from 5 and 9. `_audit()` cannot record an actor because no actor exists, and it
records nothing at all outside the rundown service.

`doc/misc/nimbus-capability-breakdown.md` is right that the target record should include `interface = MCP`,
`agent_session`, and `tool` alongside actor and result — and equally right that we should **reuse the
existing audit path rather than create an MCP-specific audit universe.** The `_audit()` helper is the
correct seam; it needs widening, not replacing.

One addition specific to LLM callers, from handbook page 33: log **the initiating operator and the
prompt**, not merely the resulting API call. A tool call reading `move_item(position=3)` is
uninterpretable after the fact without the sentence that produced it.

**What it takes:** folded into the Category 5 work — ~1 week combined, after identity.

---

### Category 11 — Natural-language cross-system reasoning → **READY** (emergent)

Nothing to build. This emerges once small composable primitives exist, and `doc/misc/nimbus-capability-breakdown.md` is
correct that this is where the value actually is.

Testing its worked example against our surface — *"Why won't tonight's film play?"*:

```
GET /rundowns/main/draft        → find the item and its media reference
GET /media                      → asset present? probe_json valid?
GET /playlists/main             → item state; applyError
GET /rundowns/main/executions   → did a graphics event fail?
GET /rundowns/main/audit        → what changed and when
```

**Four of five steps work today.** The one that breaks is the ingest step — `get_ingest_status()`
has no backing data (Category 4), which is precisely the failure the example was designed to
diagnose. That is a good argument for prioritising the ingest job model over more read tools.

The composability precondition holds: **no hidden UI state exists.** Everything the operator UI knows,
it learned from these same endpoints.

---

### Category 12 — Monitoring multiple channels → **MISSING** (architecturally, not just unbuilt)

This appliance is **single-channel by construction**, and not superficially:

- `media._live_input()` raises unless inputs exist and then returns `inputs[0]` unconditionally.
- The config parser consumes only `inputs[0]` (handbook page 32, finding 9).
- `config/channel.yml` declares `channel: main`.
- Every rundown route is `/rundowns/main` in the shipped UI.

`doc/misc/nimbus-capability-breakdown.md` §12's advice — provide `list_channel_health(state="warning")` rather than forcing
500 calls to `get_channel_status()` — is sound but **not actionable here**. There is one channel.

**The correct interpretation for our product:** batch queries belong at the *fleet* level, across
appliances, not within one. That is a different product (a management plane above the appliance), and
should be recorded as out of scope rather than as a gap. If fleet management is ever built, the MCP
server belongs there, not here.

---

### Category 13 — Live source visibility / control → **PARTIAL**

`doc/misc/nimbus-capability-breakdown.md` carefully distinguishes confirmed from inferred, and that distinction maps cleanly:

| Nimbus classification | Our equivalent | Status |
| --- | --- | --- |
| CONFIRMED: manual event-to-air | `setInputStream` / `media.skip()` | ✅ exists |
| LIKELY: live input state | `StreamState` enum on `/inputs` | ⚠️ four values, no error detail |
| LIKELY: live input selection | `POST /inputs/{id}` with `currentStreamId` | ✅ exists |
| LIKELY: skip/end live event | `POST /playlists/{id}/skip` | ✅ skip only — no "end" |
| NOT CONFIRMED: every transport setting | `/config/set-inputs` | ❌ broken (§3.3) |

Worth noting: `updateInput()` rejects a null `currentStreamId` with a 400 rather than silently doing
nothing, and maps gRPC `FAILED_PRECONDITION` to a 400 — the error semantics are already
model-legible.

---

### Category 14 — What MCP does not replace → **understood, and it constrains the design**

`doc/misc/nimbus-capability-breakdown.md` §14 asks the right question:

> Do not ask whether the transcoder itself understands MCP. Ask: does the transcoder have enough
> structured control APIs for a thin MCP service to expose?

**It does, for reads.** The layering is already right:

```
                     AI layer
                        ↓
              MCP server (new, thin)
                        ↓
        existing FastAPI control plane + services
                        ↓
              gRPC / SQLite / config.json
                        ↓
          UTPL engine — GStreamer, encoders, SCTE, muxers
```

The MCP layer must never enter the media data path. One concrete implementation hazard supports this:
`_getChannel()` in [transcoder.py](../../api/src/transcoder_api/services/transcoder.py) builds a **fresh
gRPC channel on every call** (handbook page 32, finding 1). An agent firing twenty exploratory tool
calls creates twenty channels to the engine's Unix socket. **Pool or memoise the channel before
exposing these paths to an agent** — a human clicking a UI generates a fraction of that call volume.

---

## 5. The replication scorecard

`doc/misc/nimbus-capability-breakdown.md`'s 22-row matrix, scored, with the concrete backing in this tree.

| Capability | Backend prerequisite | Status | Where it lives |
| --- | --- | --- | --- |
| List channels/services | Channel/service repository | **MISSING** | Single channel by construction (§12) |
| Channel health | Structured health/state API | **PARTIAL** | `/config/check-transcoder` + `StreamState`; no metrics |
| Current playout state | Runtime state API | **READY** | `/playlists/{id}`, `/inputs`, `refresh_live_state()` |
| Schedule query | Schedule/event domain model | **READY** | `rundown_*` tables, 17 routes |
| Current/next event | Timeline query | **READY** | `currentItemId`, ordered `queue_items()` |
| Schedule validation | Deterministic validation service | **READY** | `validate_document()`, `_validate_timeline_constraints()` |
| Asset lookup | Asset repository | **READY** | `MediaAsset`, `list_assets()` |
| Media metadata | Probe/metadata service | **READY** | `_probe()` → `probe_json` |
| Asset readiness | Asset processing state | **PARTIAL** | Implicit (probe gate); no explicit field |
| Ingest status | Durable ingest-job state | **MISSING** | Synchronous only, no job table |
| Ingest errors | Structured failure reporting | **MISSING** | Exceptions vanish with the HTTP response |
| Audit search | Structured audit service | **PARTIAL** | `rundown_audit_log`; rundowns only, no actor |
| Modify playlist | Schedule mutation service | **PARTIAL** | `media.enqueue/move/delete` — solid; needs identity |
| Insert/remove/move event | Event mutation API | **PARTIAL** | Draft path + `expectedRevision` 409s |
| Configuration change | Validated configuration service | **MISSING** | Routes broken; no validation, no dry-run |
| Take event to air | Atomic playout-control command | **PARTIAL** | `setInputStream` exists; no authz/confirm/audit |
| Skip/end event | Atomic playout command | **PARTIAL** | Skip yes; start/pause return 410; no "end" |
| Permissions | Existing RBAC/policy system | **MISSING** | Nothing at all |
| Agent auditing | Audit context propagation | **MISSING** | No actor, no request ID, no interface field |
| Multi-channel queries | Batch/filter APIs | **MISSING** | Out of scope for a single-channel appliance |
| Long-running jobs | Durable job IDs/state | **PARTIAL** | `RundownOutbox` is a working template; not generalised |
| Safe agent writes | validation + approval + idempotency | **PARTIAL** | Validation ✅ idempotency ✅ **approval ❌** |

**Two rows deserve emphasis.** *Safe agent writes* is two-thirds complete — the deterministic
validation and the idempotent, revision-keyed application are the parts that are usually hard, and
they already work. Only the approval leg is absent, and it is absent because identity is absent.
*Long-running jobs* has a working reference implementation in `RundownOutbox` (dedupe key, attempt
counter, backoff, `last_error`) that the ingest job model should copy rather than reinvent.

---

## 6. Implementation plan

### Phase 0 — Defect fixes (do these regardless of MCP) — ~1 day

| Fix | File |
| --- | --- |
| `GET /config/restart-transcoder` → **POST** | [config.py:176](../../api/src/transcoder_api/routers/config.py#L176) |
| Remove the dead `json.loads()` from all four `set-*` handlers | [config.py](../../api/src/transcoder_api/routers/config.py) |
| Replace bare `except:` with typed handlers returning correct status codes | [config.py](../../api/src/transcoder_api/routers/config.py) |
| Initialise `stop_result = None` before the `RUNNING` branch | [config.py](../../api/src/transcoder_api/routers/config.py) |
| Pool/memoise the gRPC channel | [transcoder.py](../../api/src/transcoder_api/services/transcoder.py) |

### Phase 1 — Read-only MCP server — ~2–3 days, **no prerequisites**

Roughly 400–500 lines beside the existing FastAPI app. FastAPI-to-MCP is close to mechanical.

**Tools:** `get_channel_health`, `get_playout_state`, `list_stream_states`, `get_rundown`,
`get_current_item`, `get_next_item`, `get_schedule_window`, `validate_schedule`, `resolve_time`,
`find_asset`, `get_asset`, `get_asset_media_info`, `list_media`, `get_playlist`, `list_graphics_cues`,
`graphics_catalog`, `list_scheduled_events`, `rundown_audit`, `rundown_executions`,
`rundown_occurrences`, `get_effective_config`.

Plus reads with **no REST endpoint today**, which is where the diagnostic value concentrates:

- `tail_manifest` — parse `runtime/main.m3u8`: segment count, discontinuities, media-sequence drift.
- `query_playlist_db` — bounded read of `runtime/managed-playlist.sqlite`: current item, queue depth,
  last apply result and revision.
- `query_events_db` — SCTE-35 and blackout history from the `events` table.
- `tail_logs` — Supervisor and GStreamer stderr.
- `describe_capabilities` — which SDK-gated features **this build** actually has (Dolby PAT, NDI,
  hardware encode), read from the CMake configuration rather than from documentation. This alone
  answers a recurring class of operator doubt that currently requires reading
  [02-feature-catalog.md](02-feature-catalog.md) and trusting it.

**Resources** (not tools): the JSON config schema, `config-documentation.yaml`, the current effective
config, the live manifest, `config/channel.yml`, and selected handbook pages. **The effective-schema
resource must mark the silently-ignored fields explicitly** (§7) — without it, an LLM handed only the
OpenAPI schema will confidently write config using every dead field and report success.

**Prompts** encoding existing runbooks: *diagnose black or frozen output*; *why did rundown item N
fail*; *pre-flight this rundown against media availability*; *explain the current config and flag
settings that are silently ignored*.

**Enforce read-only at the dispatcher.** Not per tool. See §9 and CVE-2026-46519.

### Phase 2 — Identity — ~2–3 weeks — **gates everything below**

Tokens, `viewer`/`editor`/`operator` roles, bearer middleware, request-scoped identity propagation.

### Phase 3 — Telemetry — ~2–3 weeks

Wire the unused GStreamer tracers, add an exporter, add per-output runtime state. Then build
`diagnose_*` tools that return **conclusions**, not readings — the Grafana/Sift lesson, and the
difference between a tool answering "what is the bitrate" and one answering "why does output 2 look
wrong."

### Phase 4 — Ingest job model — ~1 week

`IngestJob` table; move import/upload onto the reconciler; copy the `RundownOutbox` pattern. Unblocks
Category 4 and completes the Category 11 diagnostic chain.

### Phase 5 — Audit generalisation — ~1 week (after Phase 2)

Add `actor`, `request_id`, `result`, `category` to the audit record. Lift `_audit()` into a shared
module. Call it from playlist, graphics, media and input mutations. Separate operator intent from
system observation.

### Phase 6 — Draft writes — ~1 week (after Phases 2, 5)

**Start writes here, not on inputs — and it is not close.** The rundown draft is a single mutable row
that never reaches air, validated on every write, requiring a matching `expectedRevision` so stale
writes 409 rather than clobber. Publishing is a separate, human-gated step. An LLM writing a draft
that is then validated and human-published is a genuinely safe loop: the model **cannot put anything
to air by mistake**, and every edit carries an author trail.

Tools: `put_draft`, `validate_draft`, `resolve_time`, `export_draft`.

### Phase 7 — Config write path — ~2 weeks (after Phase 2)

Parser `--validate-only` mode first — it does not exist, it is small and self-contained, and it is
what makes the whole config write path safe. Then `propose → validate → confirm → apply` with
rollback.

### Phase 8 — On-air writes — ~1 week (after Phases 2, 3, 5, 6)

`skip_current_item`, `switch_input_stream`, `publish_rundown`, `apply_playlist_snapshot`,
`delete_media`, `restart_transcoder`. Every one annotated with MCP `DestructiveHint`, every one behind
**elicitation** — the 2026-07-28 spec's multi-round-trip pattern (`InputRequiredResult` with
`inputRequests` plus an opaque `requestState` the client echoes back) exists for exactly this. Log
the initiating operator **and the prompt**.

### Summary

| Phase | Effort | Gate |
| --- | --- | --- |
| 0 — defect fixes | ~1 day | none |
| 1 — read-only MCP | ~2–3 days | none |
| 2 — identity | ~2–3 weeks | none |
| 3 — telemetry | ~2–3 weeks | — |
| 4 — ingest jobs | ~1 week | — |
| 5 — audit | ~1 week | Phase 2 |
| 6 — draft writes | ~1 week | Phases 2, 5 |
| 7 — config writes | ~2 weeks | Phase 2 |
| 8 — on-air writes | ~1 week | Phases 2, 3, 5, 6 |

**~6–8 weeks to full parity. Only about one week of that is MCP code.** The rest is the auth,
telemetry and ingest-state layers this product needs whether or not an agent ever connects — which is
the useful reframing: **MCP is a forcing function for work that is already overdue.**

---

## 7. Risks specific to this codebase

**The silently-ignored config fields are the sharpest LLM-specific hazard.** `inputs[1..n]`,
`video.interlace`, the `adobe` and `enhanced` ad markers, and `scte.prerollTime` all look real,
are accepted without complaint, and do nothing. A human learns this once. A model will repeat it
every time unless an effective-schema resource states it in writing.

**Desired vs. actual state is easy to confuse.** `/inputs` is the engine; `/config/get-inputs` is a
file on disk. Name the tools so a model cannot mistake one for the other.

**Audit noise will mislead an agent.** 2,402 rows, overwhelmingly `occurrence_observed`. Without a
category filter, "what changed recently" returns machine chatter.

**gRPC channel churn.** One channel per call, and agents make many calls (§14).

**Read-only enforcement must be central.** CVE-2026-46519 is the precedent: a per-tool check is one
forgotten decorator away from being no check at all.

**Do not build the low-value tier.** There are a dozen FFmpeg MCP servers where the LLM writes a
command line and the server shells out. That pattern is worthless here — this product's value is the
state of a running pipeline, not the construction of a command.

> **In plain terms:** putting an AI in front of a command-line tool is a party trick. Putting one in
> front of a live system that is currently broadcasting is a product. This codebase is the second
> thing.

---

## Appendix A — Corrections to handbook page 33

Page 33 was written 2026-08-07 and contains route-level errors. Verified against source 2026-08-12:

| Page 33 claim | Actual |
| --- | --- |
| Routes `/get-config`, `/get-inputs`, `/set-input/{id}`, `/check-transcoder`, `/restart-transcoder` | All carry a **`/config`** prefix — `router = APIRouter(prefix="/config", ...)` at [config.py:11](../../api/src/transcoder_api/routers/config.py#L11) |
| Ingest group maps to "`/get-inputs`, `/set-input/{id}` … gRPC `getInputs`" | **Two distinct surfaces.** `/inputs` (gRPC, live) is a separate router from `/config/get-inputs` (file). Page 33 merges them |
| Media routes are `/media`, `/media/import`, `DELETE /media/{id}` | Also `POST /media` (multipart upload) — omitted |
| "Config write routes are broken" | Correct, but understated. They are **unreachable**: `json.loads()` on a dict, swallowed by a bare `except`, returning **HTTP 200** |
| Tier 0 tool `get_playlist` backed by `/playlists/{id}` | Correct, but the playlist family also has `POST`/`PATCH`/`DELETE` item routes and `POST /playlists/{id}/{action}` — where `start` and `pause` return **410 Gone** |
| "~35 REST routes and 4 gRPC RPCs" | 4 gRPC RPCs correct. REST count is closer to **50** (17 in the rundown router alone) |

Page 33's strategic analysis — the market survey, the tiering, the gap taxonomy, the recommendation
to ship read-only first — is sound and this report builds on it rather than replacing it.

---

## Appendix B — Verification method

Every codebase claim traces to source read in this checkout on 2026-08-12:

- All six routers and all five services read in full.
- `model/db.py` (15 table definitions) and `transcoder.proto` (4 RPCs) read in full.
- Telemetry absence established by `grep -rl "prometheus\|opentelemetry\|statsd"` over the tree
  excluding `sandbox/` — **zero** matches.
- Auth absence established by searching `api/` for `middleware`, `CORS`, `auth`, `token`, `api_key`.
- Live state read from `runtime/managed-playlist.sqlite` (read-only): 15 tables, 2,402 audit rows,
  occurrences at sequence 293.
- UI-to-API coupling established by extracting every `api(...)` call site from `operator.js` and
  `rundown.js` — 20 distinct endpoints, zero direct state manipulation.
- Test inventory: 51 Python tests across 5 files in `api/tests/`, 91 shell tests under `tests/`.

Nimbus claims re-verified 2026-08-12 against the vendor announcement and API docs. No new public
information since 2026-08-06; still no tool manifest, schema, or source.

---

## References

| Source | Type | Retrieved |
| --- | --- | --- |
| [Introducing the Veset Nimbus MCP Server](https://www.veset.tv/news/introducing-the-veset-nimbus-mcp-server) | Vendor announcement, 2026-08-06 | 2026-08-12 |
| [Veset Nimbus API documentation](https://apidocs.veset.tv/) | Vendor technical docs | 2026-08-12 |
| [CVE-2026-46519 read-only bypass](https://www.manifold.security/blog/mcp-server-kubernetes-readonly-bypass) | Security advisory | via page 33 |
| [containers/kubernetes-mcp-server](https://github.com/containers/kubernetes-mcp-server) | Open source — safety vocabulary | via page 33 |
| [grafana/mcp-grafana](https://github.com/grafana/mcp-grafana) | Open source — diagnosis-not-readout | via page 33 |
| [royshil/obs-mcp](https://github.com/royshil/obs-mcp) | Open source — closest structural match | via page 33 |
| `doc/misc/nimbus-capability-breakdown.md` | Capability breakdown this report answers | in-tree |
| [33-mcp-interface-evaluation.md](33-mcp-interface-evaluation.md) | Prior evaluation, 2026-08-07 | in-tree |
| [31-code-reading-roadmap.md](31-code-reading-roadmap.md) | Silently-ignored config fields | in-tree |
| [32-performance-and-tooling-findings.md](32-performance-and-tooling-findings.md) | Missing telemetry, gRPC channel churn | in-tree |
| [30-transactional-rundown-service.md](30-transactional-rundown-service.md) | The safest write surface in the tree | in-tree |
