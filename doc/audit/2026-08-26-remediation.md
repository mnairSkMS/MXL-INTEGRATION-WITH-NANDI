# Remediation — F-01, F-02, F-03, F-05, F-06

**Date:** 2026-08-26
**Scope:** the five **NEW** findings in F-01–F-06 from
`doc/audit/2026-08-26-codebase-audit.md`. **F-04 is PRE-EXISTING and remains open.**

Two incidental Low findings were closed in passing because they sat inside code already
being changed: **F-14** (deprecated `@app.on_event`) and **F-21** (`g_memdup2` leaks).

---

## Results

| | Before | After |
|---|---|---|
| Python suite | 77 passed, **1 failed** | **80 passed, 0 failed** |
| mypy | 20 errors | 20 errors (no regression) |
| C++ `ctest` | 5 suites, SCTE 9 tests | 5 suites pass, SCTE **10** tests |
| Full C++ build | clean | clean, 0 errors, 0 new warnings |

---

## F-01 + F-02 — the reconciler

`api/src/transcoder_api/main.py`

The pass was extracted into a synchronous `_reconcile_once()` and is now run with
`asyncio.to_thread`, so blocking SQLite and gRPC work no longer stalls the event loop. The
loop body is wrapped in `except Exception` with `log.exception`, re-raising
`CancelledError` first so shutdown still works. `@app.on_event` was replaced with an
`asynccontextmanager` lifespan (closing **F-14**), and a done-callback logs loudly if the
task ever ends on its own — the failure being fixed is silence, and the fix should not
reintroduce a quieter version of it.

**New test:** `api/tests/reconciler_test.py` pins both properties — that a failing pass
does not end the loop, and that reconciliation runs off the event loop thread.

Both assertions were checked against the pre-fix loop shape and **both fail against it**
(1 call instead of ≥3; `MainThread` instead of a worker), so they genuinely discriminate
rather than merely passing.

## gRPC deadlines *(supporting F-02)*

`api/src/transcoder_api/services/transcoder.py`

All four stub calls were issued with no deadline, so a wedged engine would block the caller
indefinitely — moving work to a thread would have kept the API responsive while leaving the
reconciler itself hung forever. Added `GRPC_TIMEOUT_SECONDS` (env-overridable, default 5s)
to all four. Each existing handler already falls through to `UNAVAILABLE` on
`DEADLINE_EXCEEDED`, and in `applyPlaylistSnapshot` that surfaces via `media.reconcile` as a
retryable `apply_error` — the desired shape, no handler changes needed.

## F-06 — schema DDL out of import scope

`db.py`, `preload_db.py`, `main.py`, `services/rundown.py`, `tests/bootstrap_test.py`

All module-level DDL moved into `initialize_schema()`, called from `preload_db.run()` —
which supervisord runs to completion before either the API or the transcoder opens the
database (`dependent_startup_wait_for=preload_db:exited`).

`preload_db` is deliberately configured to **fail open** (`autorestart=false`,
`startsecs=0`) so a bad preload cannot take the channel dark, so the API cannot assume the
schema is current. Added a read-only `verify_schema()`, called from the lifespan handler.
`SCHEMA_READY` is tri-state — `None` until something has actually looked — so only a schema
inspected *and found stale* disables rundown routes; an unchecked schema never fires, which
keeps this inert in tests and outside the served app.

`_backup_rundown_database` no longer returns early when the backup name is taken. That was
the sharp edge: a second destructive run proceeded with **no backup of the data it was
about to drop**. A taken name now gets a timestamped sibling, and a backup that cannot be
written raises rather than migrating unprotected.

**Verified:** importing `transcoder_api.db` — and the whole app — against a fresh `DB_PATH`
now creates **no database file at all**. Migrating a v2 fixture drops `rundown_drafts`,
preserves versions/occurrences/audit rows, and lands `2026-08-20-rundown-v3`; a second run
writes `…before-rundown-v1.20260826T113945` instead of silently skipping.

## F-05 — the red test

`services/rundown.py`, `tests/rundown_test.py`

**This was a test defect, not a product defect.** The restart-anchoring behaviour is correct
and is unchanged. Added a `_process_epoch()` accessor and routed the three read sites
through it, so the anchor's meaning is explicit and tests have a supported seam. The test
now moves the epoch back before backdating its occurrence, making the 75-second scenario it
describes actually constructible.

## F-03 — section length validation

`scte.cpp`, `test/src/scte.cpp`

Per the plan, the untested direction was probed **first**. A section declaring *more* bytes
than supplied turns out to be **already safely rejected** by `gst_mpegts_section_new`, at
the parse step, before any CRC write — so there is no out-of-bounds read and the finding
correctly stayed Medium rather than being re-ranked.

`sectionFromBase64` now rejects any buffer whose `section_length + 3` disagrees with its
size, mirroring the check `services/schedule.py::validate_scte35_base64` already performs —
the engine's own guard, for sections arriving by any route other than REST. Also closed the
two `g_memdup2` leaks (**F-21**).

**Verified:** the audit's original probe, re-run against the rebuilt library, now reports
`rejected (null)` for the padded section that previously produced
`CRC stored = 6bfd86b5, computed = b1011ce1`. The valid section still round-trips with a
correct CRC.

---

## Still open

- **F-04 (High, PRE-EXISTING)** — no authentication; `/config` writes arbitrary config and
  restarts the process over GET. Now the highest-severity item in the report.
- **F-07 onward** — mypy backlog, the `videorate`→`identity` stream-level verification, the
  15-second scheduling floor, 89 MB of committed runtime state, `rundown.py` module split.
- **Deferred by decision:** gRPC channel-per-call (`_getChannel()`, ~4×/second). Deadlines
  address the blocking risk; the connection churn remains an efficiency matter.
