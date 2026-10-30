# Codebase Audit — Current Tree vs. Original Snapshot

> **Status update — 2026-08-26, after remediation.** F-01, F-02, F-03, F-05 and F-06 have
> been fixed and verified; see `doc/audit/2026-08-26-remediation.md`. The Python suite is
> green (80 passed) and all five C++ test suites pass. **F-04 remains open** and is now the
> highest-severity outstanding item.
>
> **Two corrections to this report, established during remediation:**
>
> * **F-03 was rated too high.** The claim that a corrupt CRC_32 reaches the wire and is
>   discarded by downstream splicers is wrong: `handle_scte35_section` in mpegtsmux
>   recomputes the CRC over the declared section length before transmission
>   (`transcoder/patches/gstreamer1.0-plugins-bad/040-mpegtsmux-concurrent-scte.patch:60`,
>   and documented at `scte.h:56-58`). No ad break was ever silently lost. The missing
>   length validation was real and is fixed; the correct severity was **Medium**.
>   A follow-up probe also showed the opposite direction — a section declaring more bytes
>   than were supplied — was already safely rejected by `gst_mpegts_section_new`, so there
>   was no memory-safety issue in either direction.
> * **F-05 was mischaracterised.** `PROCESS_STARTED_AT` was called "effectively
>   unpatchable"; it is not — `monkeypatch.setattr` reaches it, which is how this report's
>   own probe proved the root cause. The product logic was correct throughout. F-05 was a
>   **test defect**, not a product defect.

**Date:** 2026-08-26
**Baseline:** `~/Downloads/skandha-transcoder-dev-original`
**Subject:** `skandha-transcoder-main`
**Method:** full recursive diff, line-by-line review of all changed and new files, plus
executed verification — Python test suite, mypy, a container build of the C++ tree, the
new SCTE unit test, and two purpose-written probes.

Every finding below is tagged **NEW** (introduced since the snapshot) or **PRE-EXISTING**
(inherited, but now load-bearing for new features). Findings marked **PROVEN** were
reproduced; the reproduction is quoted.

---

## 1. Executive summary

The new work is better than a typical AI-authored expansion. The SCTE-35 design is
genuinely thoughtful, the C++ compiles clean with zero warnings in new code, all nine new
SCTE tests pass, the feature flag is correctly enforced across all eleven rundown routes,
the documentation consolidation lost nothing, and the common security sinks — SQL
injection, XSS, `eval`, unsafe YAML, shell injection, path traversal — are all genuinely
absent. Section 7 records this in detail, because it is the accurate picture and it
narrows where attention is actually needed.

Four things need attention before this ships:

1. **The reconciler stops permanently on its first exception** and, until then, blocks the
   HTTP event loop. This is the single highest-consequence defect. *(F-01, F-02 — PROVEN)*
2. **A SCTE-35 section with trailing bytes is emitted with an invalid CRC_32**, which
   every compliant downstream receiver discards — silently losing ad breaks. Not reachable
   through any current shipped path, but the only thing preventing it is a check in a
   different language in a different process. *(F-03 — PROVEN)*
3. **The tree ships red.** One test fails and cannot pass by construction; mypy went from
   6 errors to 20. The project's own `poe test` task does not pass. *(F-05, F-06 — PROVEN)*
4. **Schema migrations run as an import side effect and can drop ten tables**, with a
   backup guard that silently skips backing up on a second run. *(F-07)*

Separately, **89 MB of live runtime state — including root-owned files — is sitting in the
source tree**, already listed in `.gitignore` but never removed. *(F-11)*

**Scale of change:** 40 new files, 16 deletions, 64 modified. ~6,000 new lines of Python,
~1,800 lines of UI JavaScript with no executable coverage, ~1,500 lines of C++ churn.

---

## 2. Findings table

| ID | Sev | Origin | Finding | Anchor |
|---|---|---|---|---|
| F-01 ✅ | **Critical** | NEW | Reconciler task dies permanently on any exception; API keeps serving 200s | `api/src/transcoder_api/main.py:41` |
| F-02 ✅ | **High** | NEW | Reconciler runs blocking DB work on the asyncio event loop at 4 Hz | `api/src/transcoder_api/main.py:41` |
| F-03 ✅ | ~~High~~ **Medium** | NEW | Section with trailing bytes emitted with invalid CRC_32 | `transcoder/lib/utpl/src/utpl/transcoder/event/scheduler/scte.cpp:162` |
| F-04 | **High** | PRE-EXISTING | No auth anywhere; `/config` writes arbitrary config and restarts the process over GET | `api/src/transcoder_api/routers/config.py:33` |
| F-05 ✅ | **High** | NEW | Test suite ships red — one test cannot pass by construction | `api/tests/rundown_test.py:766` |
| F-06 ✅ | **High** | NEW | Destructive schema migration at import time; backup guard skippable | `api/src/transcoder_api/db.py:33` |
| F-07 | Medium | NEW | mypy regressed 6 → 20 errors; `poe test` fails | `api/pyproject.toml:71` |
| F-08 | Medium | NEW | `videorate` replaced by `identity` under a name that still says "rate" | `transcoder/lib/utpl/src/utpl/transcoder/stream/input/image.cpp:62` |
| F-09 | Medium | NEW | `_srcNeedData` blocks a GStreamer streaming thread on a condition variable | `transcoder/lib/utpl/src/utpl/transcoder/stream/input/image.cpp:539` |
| F-10 | Medium | NEW | C++ delegates SCTE validation to Python across a process boundary, by comment | `transcoder/app/src/application/event/provider/sqlite.cpp:161` |
| F-11 | Medium | NEW | 89 MB of runtime state committed, including root-owned files | `runtime/` |
| F-12 | Medium | NEW | 15-second scheduling floor relaxed to `now` for all API clients | `api/src/transcoder_api/services/schedule.py:193` |
| F-13 | Medium | NEW | `services/rundown.py` is 1,796 lines / 60 functions in one module | `api/src/transcoder_api/services/rundown.py` |
| F-14 ✅ | Low | NEW | Deprecated `@app.on_event` instead of lifespan handlers | `api/src/transcoder_api/main.py:55` |
| F-15 | Low | NEW | `static/playout.html` is orphaned — 10 KB served by nothing | `api/src/transcoder_api/static/playout.html` |
| F-16 | Low | PRE-EXISTING | `delete_event` reads attributes off a deleted ORM instance | `api/src/transcoder_api/services/schedule.py:377` |
| F-17 | Low | NEW | Graphics catalog re-read and re-parsed from disk on every call | `api/src/transcoder_api/services/graphics.py:30` |
| F-18 | Low | NEW | `_cleanup_expired` loads the whole table and filters in Python | `api/src/transcoder_api/services/graphics.py:78` |
| F-19 | Low | NEW | Unused imports in new code | `api/src/transcoder_api/routers/playout.py:4` |
| F-20 | Low | NEW | Full `ffmpeg` added to the runtime image to obtain `ffprobe` | `docker/utpl-transcoder/Dockerfile:54` |
| F-21 ✅ | Low | NEW | `g_memdup2` leaks if `gst_mpegts_section_new` returns NULL | `transcoder/.../scheduler/scte.cpp:77` |
| F-22 | Low | NEW | Hardcoded 96 MiB frame budget with no config knob | `transcoder/.../stream/input/image.cpp:32` |

---

## 3. Detailed findings — the four that matter

### F-01 — Reconciler dies permanently on its first exception *(Critical, NEW, PROVEN)*

`api/src/transcoder_api/main.py:41-46`

```python
async def _playlist_reconciler():
    while True:
        db = SessionLocal()
        try:
            media.reconcile("main", db)
            if rundown_service.enabled():
                rundown_service.sync_active_rundowns(db)
                rundown_service.dispatch_outbox(db)
        finally:
            db.close()
        await asyncio.sleep(0.25 if rundown_service.enabled() else 1)
```

The `try` has only a `finally`. Any exception escaping `media.reconcile`,
`sync_active_rundowns`, or `dispatch_outbox` propagates out of `_playlist_reconciler`,
terminating the task created by `asyncio.create_task`. Nothing ever awaits that task, so
nothing restarts it.

**Failure scenario.** The reconciler writes at 4 Hz while request handlers take
`BEGIN IMMEDIATE` (`routers/playout.py:21`, `services/rundown.py:674`, `:1164`,
`services/bootstrap.py:547`) against SQLite with `busy_timeout=5000`. One
`OperationalError: database is locked` under concurrent load — or any transient in
1,796 lines of rundown projection logic — and reconciliation stops **for the lifetime of
the process**. The API keeps answering 200s. Rundowns are accepted and never applied.
Graphics cues are queued and never dispatched. The playlist never advances. Nothing in the
operator UI indicates a problem, because the UI polls endpoints that are still healthy.

**Reproduced** with `main.py`'s exact structure:

```
reconcile() invocations after 0.3s: 3      # ~30 expected
task done?      True
task exception: RuntimeError("transient DB error (e.g. 'database is locked')")
--> the API process is still alive and serving; reconciliation is not.
```

**Fix.** Wrap the loop body in `try/except Exception`, log with traceback, and continue.
Add a supervision wrapper that restarts the task if it ever exits, and surface reconciler
health on an endpoint the UI can show. A background loop that silently stops is worse than
one that crashes the process.

---

### F-02 — Reconciler blocks the HTTP event loop *(High, NEW)*

Same site. `media.reconcile`, `sync_active_rundowns`, and `dispatch_outbox` are all
**synchronous** functions doing SQLite I/O, called directly from a coroutine with no
`run_in_executor`. Every tick stalls the entire event loop — all concurrent HTTP requests
included — for the duration of the reconciliation pass.

At `asyncio.sleep(0.25)` this happens four times a second, forever, and the work grows with
the size of the rundown. The two defects compound: F-02 creates the lock contention that
triggers F-01.

**Fix.** `await asyncio.to_thread(...)` for the whole body, or move reconciliation to a
dedicated thread with its own engine. Once off the loop, the 4 Hz poll is also worth
replacing with an event-driven wakeup on revision change.

---

### F-03 — SCTE-35 section emitted with an invalid CRC_32 *(High, NEW, PROVEN)*

`transcoder/lib/utpl/src/utpl/transcoder/event/scheduler/scte.cpp:144-168`

```cpp
std::vector<uint8_t> patched(raw);
...
patched[4] = (patched[4] & 0xFE) | ((adjustment >> 32) & 0x01);
...
auto  crcOffset = patched.size() - CRC_SIZE;
auto  crc       = ts::CRC32(patched.data(), crcOffset).value();
```

`crcOffset` is derived from the **buffer length**, not from the section's declared
`section_length`. The function verifies `raw.size() >= 14`, the table id (via GStreamer
parsing) and the command type — but never that `section_length + 3 == raw.size()`.

Given a buffer with trailing bytes, the code rewrites `pts_adjustment` at bytes 4–8
correctly, then writes the recomputed CRC into the **trailing padding**. GStreamer's
`gst_mpegts_section_new` then truncates the section to its declared length, discarding the
corrected CRC and shipping the original — which no longer matches the modified
`pts_adjustment`.

**Reproduced** with a valid 40-byte `splice_insert` plus 8 trailing bytes:

```
--- exact-length section (control) (input 40 bytes) ---
  emitted 40 bytes
  pts_adjustment bytes[4..8] = 01 8d be 98 50
  CRC stored = b1011ce1  computed = b1011ce1  -> VALID

--- section with 8 trailing bytes (input 48 bytes) ---
  emitted 40 bytes
  pts_adjustment bytes[4..8] = 01 8d be 98 50
  CRC stored = 6bfd86b5  computed = b1011ce1  -> *** INVALID ***
```

**Consequence.** A CRC-invalid `splice_info_section` is discarded by every SCTE-35
compliant ad splicer, packager, and compliance monitor. The break does not fire. The API
returned 200 and the transcoder logged a successful injection, so nothing surfaces the
failure until someone audits as-run logs against the affiliate feed.

**Reachability — stated precisely.** Not reachable today through any shipped path. All
three current write paths validate the length: `POST /scheduled_events`
(`services/schedule.py:validate_scte35_base64` checks
`total_length != len(data)`), `preload_db.py:143` (calls the same `validate_event`), and
the graphics/rundown services (which route through `create_or_update_events`). The exposure
is that the C++ has **no defence of its own** and explicitly says so — see F-10. Any future
ingest path reintroduces it silently.

**Fix.** Two lines in `sectionFromBase64`: compute
`declared = (((raw[1] & 0x0F) << 8) | raw[2]) + 3` and reject when `declared != raw.size()`.
Add the padded-section case to `transcoder/lib/utpl/test/src/scte.cpp` —
`rejectsUninjectableAndMalformedSections` currently covers a `splice_null`, an empty
buffer, and a 5-byte buffer, but not a well-formed section of the wrong length.

---

### F-04 — No authentication on an API that writes config and restarts the process *(High, PRE-EXISTING)*

There is no authentication, authorisation, CORS policy, security header, or rate limit
anywhere in the FastAPI app. That was defensible when the surface was event scheduling.
This cycle it grew an operator UI, a media upload path with a **10 GB** default body limit
(`services/media.py:39`), rundown publishing, and playlist control — all mounted on the
same unauthenticated app as the pre-existing `/config` router, which:

- writes arbitrary JSON to `/opt/utpl/etc/config.json` (`routers/config.py:33`);
- restarts the transcoder over a **GET** (`routers/config.py:176`) — reachable by CSRF or a
  prefetching browser;
- swallows every error in bare `except:` clauses, including `KeyboardInterrupt`;
- defines **three functions named `get_outputs`** (lines 50, 56, 65); the first two are dead
  through shadowing;
- calls `json.loads()` on an already-parsed `dict` in all four setters, so
  `/config/set-outputs` and `/config/set-inputs` can **never succeed** — they always fall
  into the bare `except` and return `{"message": "error parsing request"}`.

This cycle reformatted the file (quote style, blank lines) without fixing any of it. The
operator UI does not call `/config` — verified against `operator.js` and `rundown.js` —
so the new work does not newly *depend* on it, but it now sits behind the same open door.

**Fix.** Decide the trust model explicitly. If this is not on an isolated management VLAN,
add authentication before shipping. Regardless: make `restart-transcoder` a POST, delete
the two shadowed functions, replace bare `except:` with `except Exception`, and either fix
or remove the two setters that cannot work.

---

## 4. Regression register

Behaviour changed for functionality that already existed.

| Change | Assessment |
|---|---|
| **`videorate` → `identity`** (`image.cpp:62`) — affects *all* image overlays, not just the new animated path. The element is still named `ELEMENT_NAME_RATE` / `"image-rate"`, so the pipeline graph now lies about what the stage does. The comment's rationale (videorate rebased late-attached overlays to PTS 0) is plausible, but nothing now enforces cadence for static overlays either. | **Unintended scope** — needs stream-level verification with `ffprobe`/TSDuck before shipping. Rename the element regardless. |
| **15-second scheduling floor relaxed to `now`** (`services/schedule.py:196`) — was `now + 15s <= ev.time`, now `now <= ev.time`. Changed to enable immediate graphics cues, but it applies to every existing scheduling client. | **Intentional, under-scoped.** If the floor existed because the transcoder needs preroll lead time, events at `now` are accepted with 200 and silently dropped downstream. Verify, then either restore the floor for SCTE event types or document the new contract in `api.yaml`. |
| **Timing map moved inside `if (gstSection)`** in `timeSignal.cpp` — previously set unconditionally, even when section construction failed. | **Intentional and correct.** Strictly better. |
| **`expected=0` removed** from supervisord preload program. | **Intentional**, comment explains the EXITED-vs-BACKOFF reasoning. Verify against `supervisord-dependent-startup` behaviour before relying on it. |
| **`delete_event` gained a `commit=False` path** that `flush()`es a delete then reads attributes off the deleted instance (`schedule.py:377`). | **Pre-existing pattern, newly extended.** Capture the response model before deleting. |
| **`rest_api.py` and `transcoder_pb2*.py` now committed** — previously generated. | **Improvement with a caveat.** It exposed 14 latent type errors that `ignore_missing_imports` had been hiding (see F-07). Those were never fixed. Committed generated code also needs a drift check in CI. |

---

## 5. Residue register — delete list

| Path | Size | Note |
|---|---|---|
| `runtime/` | **89 MB** | Live state: 26 HLS segments, `main.m3u8`, `managed-playlist.sqlite` + `-shm`/`-wal`. Already in `.gitignore`, never removed. |
| `runtime/managed-playlist.sqlite.before-rundown-v1/v2/v3` | 9.8 MB | Migration backups. **`v3` is owned by root** and will break any non-root packaging or cleanup step. |
| `api/events.sqlite`, `api/events.sqlite.before-rundown-v1`, `events.sqlite` (root) | 422 KB | Stale databases in three locations. |
| `media/queue/*.mp4` (4 files) | — | Root-owned test media. |
| `api/src/transcoder_api/static/playout.html` | 10 KB | **Orphaned.** `/operator/playout` 307-redirects to `/operator#playlist`; nothing serves this file. |
| `api/src/transcoder_api/static/squeeze-box.html` | 1.6 KB | Single-line minified authoring tool, unreferenced from the UI, reachable only at an undocumented route. Promote to a readable page or remove. |
| Unused imports | — | `routers/playout.py` (`pathlib`, `PlaylistItem`, `PlaylistState`), `routers/schedule.py` (`EventId`), `services/schedule.py` (`ApiEventId`). |

Total repository size is **284 MB**, of which ~89 MB is state that should never have been
there.

**Documentation deletions were checked individually and are clean** — see section 7.

---

## 6. Remaining findings

**F-06 — Import-time destructive migrations.** `api/src/transcoder_api/db.py` executes
`create_all`, `PRAGMA`, `ALTER TABLE`, and `DROP TABLE` at **module import**, so they run on
every process start, every `--reload` cycle, and during test collection.
`_reset_rundown_schema` can drop ten tables and destroy as-run history. Its safety net,
`_backup_rundown_database`, returns early when the backup file already exists — so a
**second** destructive run proceeds with no fresh backup, silently overwriting nothing and
protecting nothing. The file's own comment concedes "this repository intentionally has no
migration runner." Move this behind an explicit entrypoint step, and make the backup path
unique per run.

**F-07 — Type checking regressed.** mypy on the baseline: **6 errors**, all in the legacy
`routers/config.py`. mypy on the current tree: **20 errors in 6 files**. The increase is
mostly *newly visible* rather than newly introduced — committing the generated
`model/rest_api.py` let mypy resolve types that `ignore_missing_imports` had been erasing —
but they were left unfixed, so `poe mypy` and therefore `poe test` both fail. Two are
genuinely new: `services/graphics.py:212-213`, where `squeeze` is inferred as
`dict[str, int]` and then assigned a `str` and a `dict`. Annotate it `dict[str, Any]`.

**F-05 — A test that cannot pass.** `tests/rundown_test.py:766`
`test_clock_counts_a_cycle_per_completed_loop` asserts `cycle == 3` and gets `1`.

Root cause: `services/rundown.py:75` defines `PROCESS_STARTED_AT = utc_now()` at module
import. `clock_for` filters occurrences to `observed_at >= PROCESS_STARTED_AT`
(`rundown.py:1506`). The test backdates its origin occurrence 75 seconds — i.e. to *before*
process start — so the filter discards it and the cycle count collapses to 1. No test
overrides the constant; being module-level and import-time, it is effectively unpatchable.

**Proven** by moving the constant back one day at collection time, with no other change:

```
1 passed, 1 warning in 0.05s
```

The `PROCESS_STARTED_AT` restart-anchoring logic is itself sound and correctly reasoned —
the deployment runs a single uvicorn process with no `--workers`
(`docker/utpl-transcoder/supervisord.conf:7`), so there is no cross-worker inconsistency.
The defect is that whoever added the guard did not re-run the suite. Make the anchor
injectable (module-level function or app state) and the test passes honestly.

**F-09 — Blocking wait on a GStreamer streaming thread.** `image.cpp:539` has
`_srcNeedData` — an appsrc `need-data` callback — block on
`_videoQueueChanged.wait(...)`. Shutdown is handled correctly (the destructor sets
`_preloadTaskStop` and notifies before `join()`), but blocking inside a need-data callback
is the wrong shape: that is what appsrc's internal queue and `enough-data` signal exist
for. Note also a lock-order concern: the producer takes `_videoQueueGuard` then
`_preloadGuard` (`image.cpp:381`), so any path taking them in the opposite order would
deadlock. Worth an explicit ordering comment at minimum.

**F-10 — Cross-language validation coupling.** `sqlite.cpp:161` documents the assumption
outright: *"The REST layer has already validated the section."* That is a correctness
dependency spanning two languages and two processes, enforced only by a comment. It is what
makes F-03 latent rather than exploitable — and what will make it live again the moment
anyone adds an ingest path.

**F-13 — Module granularity.** `services/rundown.py` is 1,796 lines and 60 functions
covering YAML parsing, validation, versioning, timeline projection, wall-clock scheduling,
occurrence tracking, and outbox dispatch. It is well-organised internally and clearly
written, but it is seven responsibilities in one file and out of step with the rest of the
tree. Natural split: `rundown/parse.py`, `rundown/versions.py`, `rundown/projection.py`,
`rundown/dispatch.py`.

**F-17 / F-18 — Graphics service efficiency.** `catalog()` re-reads and re-parses
`graphics_catalog.json` from disk on every call, including once per `_asset()` lookup, with
no caching or mtime check. `_cleanup_expired` does `db.query(GraphicsCue).all()` and filters
in Python rather than in SQL. Both are trivial fixes; neither is urgent at current scale.

**F-20 — Runtime image weight.** Full `ffmpeg` was added to the *runtime* stage
(`docker/utpl-transcoder/Dockerfile:54`) solely for `ffprobe`. On Ubuntu that is the correct package for the
binary, but it is worth a deliberate decision on image size and on codec licensing posture
for a commercially shipped appliance.

**F-21 / F-22 — Minor C++.** Two sites in `scte.cpp` (lines 77, 177) leak the `g_memdup2`
allocation if `gst_mpegts_section_new` returns NULL. `image.cpp:32` hardcodes a 96 MiB
frame budget — roughly 11 frames at 1080p RGBA, about 2 at 4K — with no configuration knob.

---

## 7. Verified sound

Recorded because it is the accurate picture, and because it narrows where review effort
should go. Each of these was actively checked, not assumed.

**Build and test execution**
- The C++ tree **configures and builds clean** in `utpl/devel:latest` — zero errors, zero
  warnings attributable to new code.
- The new SCTE test suite **passes 9/9** (`utpl_scte_section_test`).
- The Python suite is **77 passed, 1 failed** — the single failure fully root-caused above.

**Security sinks — all genuinely absent**
- SQL is parameterised throughout; the only raw SQL is `BEGIN IMMEDIATE` and
  `DROP TABLE IF EXISTS {name}` over a fixed constant set.
- No `eval`, `exec`, `pickle`, `os.system`, or `shell=True` anywhere.
- `yaml.safe_load` / `yaml.safe_dump` used exclusively.
- `subprocess.run` uses an argv list resolved through `shutil.which`, with `timeout=30`
  and `check=False` — correctly done.
- Media path traversal is properly blocked: `_safe_relative_path` rejects absolute paths and
  `..` components, `_absolute_path` re-checks after `resolve()`, which also catches symlink
  escapes.
- The operator UI contains **no `innerHTML`, `insertAdjacentHTML`, `outerHTML`,
  `document.write`, or `eval`** across 1,409 lines of JS — there is no DOM-XSS sink. Path
  segments are `encodeURIComponent`-wrapped.

**SCTE-35 design**
- `validate_scte35_base64` (`services/schedule.py:107`) is genuinely rigorous: table_id,
  `section_length` consistency, section size cap, encryption flag, command-type match
  against event type, and `out_of_network_indicator` direction. The error messages explain
  the actual problem. This is good work.
- Shifting the whole section via `pts_adjustment` rather than rewriting the splice time is
  the right call — it preserves operator bytes exactly and avoids re-serialisation loss.
  The reasoning is correctly documented.
- **Running-time-map keys verified byte-for-byte equivalent** to the pre-refactor inline
  construction: `"splice-time"` for time_signal, `"event-<id>-splice-time"` for
  splice_insert (`utils/gstreamer.h:227-231`). The scheduler refactor introduced no key
  drift. Keying the map off the emitted section rather than the event object is also
  correct for the raw-section path.

**Feature gating**
- All **11** rundown routes are gated — verified by inspection and confirmed by
  `test_clock_is_hidden_while_the_optional_service_is_disabled`. When
  `ENABLE_RUNDOWN_SERVICE` is unset, no rundown tables are created and no migration marker
  is written. The inertness claim holds.

**Documentation consolidation — checked file by file, nothing lost**
- `hardware-acceleration.md` content survives in `doc/misc/encoder-integration.md:76-88`,
  reformatted as a vendor table and **expanded** with guidance on why passing the device
  through does not by itself select a hardware encoder.
- `docker-running.md` content survives in `doc/misc/docker-deployment.md:8-16`.
- License docs went 6 files → 2, 16.6 KB → 13.0 KB, but every topic checked (OpenSSL usage,
  private-key handling, tamper resistance, obfuscation, signing) is still present.
- **No dangling references** to any deleted filename anywhere in `doc/` or `README.md`.
- Removal of generated `.html` and `doc/dist/*.zip` matches this tree's documentation
  hygiene convention.

**Correctly judged engineering that could look wrong at a glance**
- Bounded `make -j4` in the Dockerfile is deliberate and well-reasoned (~1 GB per compiler
  process), with a `BuildJobs` override. Not a limitation — a considered choice.
- TSDuck is **not** a new dependency; `lib/utpl` already linked it (`CMakeLists.txt:22`) and
  three other translation units already use it. `ts::CRC32` adds nothing.
- Single uvicorn process with no `--workers`, so the import-time migration and
  `PROCESS_STARTED_AT` carry no cross-worker race in the shipped deployment.
- Moving the SCTE-35 `#if 0` block so `"enhanced"` became reachable (`parser.cpp:735`) is a
  real fix, correctly leaving Adobe's unimplemented flavour disabled with an explanatory
  comment.

---

## 8. Prioritised remediation checklist

**Before shipping**
1. F-01 — wrap the reconciler body in `try/except Exception`, log, continue; supervise the task.
2. F-02 — move reconciliation off the event loop via `asyncio.to_thread`.
3. F-03 — validate `section_length + 3 == raw.size()` in `sectionFromBase64`; add the padded-section test case.
4. F-04 — settle the trust model; at minimum make `restart-transcoder` a POST and fix the bare `except:` clauses.
5. F-05 — make `PROCESS_STARTED_AT` injectable so the failing test can pass honestly. Get the suite green.

**Before the next feature lands**
6. F-06 — move migrations out of import scope; make backup filenames unique per run.
7. F-11 — delete `runtime/`, the `.before-rundown-*` backups, and the stray `events.sqlite` files; resolve the root-owned files.
8. F-07 — fix the 20 mypy errors, starting with the two genuinely new ones in `graphics.py`.
9. F-08 — verify the `identity` swap at stream level with `ffprobe`/TSDuck; rename `ELEMENT_NAME_RATE`.
10. F-12 — confirm whether the 15-second floor was load-bearing; restore or document.

**Cleanup**
11. F-15, F-19 — delete `playout.html` and the unused imports.
12. F-10 — either add C++-side validation, or make the cross-process contract explicit in `doc/codebase/16-scte35-esam-pois-blackout-and-blanking.md`.
13. F-13 — split `services/rundown.py`.
14. F-17, F-18, F-21, F-22 — efficiency and hygiene as capacity allows.

**Also worth doing:** the operator UI is ~1,800 lines with no executable coverage.
`api/tests/ui/` exists as a harness but is not wired into the suite. That is the largest
untested surface in the tree.

---

## 9. Reproduction

```bash
# Diff baseline
diff -rq --exclude=__pycache__ --exclude=.mypy_cache --exclude=.pytest_cache \
  ~/Downloads/skandha-transcoder-dev-original .

# Python suite (venv with pyproject deps + supervisor)
cd api && PYTHONPATH=$PWD/src DB_PATH=/tmp/t.sqlite python -m pytest tests/ -q
cd api && PYTHONPATH=$PWD/src python -m mypy .

# C++ build and SCTE tests, per AGENTS.md
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src:ro" -v "$BUILD:/build" \
  -w /build utpl/devel:latest cmake -DCMAKE_BUILD_TYPE=Release /src/transcoder
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src:ro" -v "$BUILD:/build" \
  -w /build utpl/devel:latest make -j4 utpl_scte_section_test
docker run --rm ... ./lib/utpl/test/utpl_scte_section_test
```

The F-03 CRC probe and the F-01 reconciler probe are standalone programs written for this
audit; both are reproduced inline in their respective findings.

**No source file was modified by this audit.** The only file added is this report.
