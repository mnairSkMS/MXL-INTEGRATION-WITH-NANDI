# Performance and Tooling Findings

Recorded 2026-08-06 against source version `1.1.0-beta.2-skandha.3`.

Each entry has a plain-language explanation, the technical detail, the evidence, and a proposed fix.
The **In plain terms** lines are deliberately non-technical and are meant for planning and
prioritisation conversations, not as a substitute for reading the code.

## Read this first: these are not speed fixes

**Nothing on this page has been profiled.** These are static findings from source reading.

The measured load for this product is 173–264% CPU at 1080p25 ([27](27-known-gaps-contradictions-and-remediation.md)),
and almost all of that is x264 software video encoding. Every item below is small next to that.

> **In plain terms:** the engine spends nearly all its energy compressing video. The issues on this
> page are more like a dripping tap than a burst pipe. Fixing them makes the system tidier, more
> reliable, and easier to scale — it will not make the picture come out faster. If raw speed is the
> goal, the lever is the video encoder (hardware offload or different settings), not this list.

Treat this page as **correctness, scalability, and maintainability** work.

## Priority summary

| # | Finding | Layer | Impact | Effort |
| --- | --- | --- | --- | --- |
| 1 | New gRPC connection built for every call | Python API | Medium | Low |
| 2 | Lock and lookup on every media buffer | C++ engine | Medium | Medium |
| 3 | Repeated event lookup on every media buffer | C++ engine | Low–Medium | Low |
| 4 | All four configuration write routes are broken | Python API | High (feature dead) | Low |
| 5 | Database write-ahead log grows unbounded | Runtime state | Medium | Low |
| 6 | Background loop polls even when idle | Python API | Low | Low |
| 7 | Avoidable copies in setters and signatures | C++ | Very low | Low |
| 8 | Debug build flags are hardcoded and partly dead | Build | Low | Low |
| 9 | Configuration parser is one 1,282-line function | C++ | Low now, high later | High |

---

## 1. A new gRPC connection is built for every call

**In plain terms:** every time the web service needs to ask the video engine a question, it sets up a
brand-new phone line, asks one question, then tears the line down. It should keep one line open and
reuse it. This happens several times a second, forever.

**Technical detail.** `_getChannel()` returns a freshly constructed `grpc.insecure_channel`, and all
four call sites wrap it in `with _getChannel() as channel:`, so the channel is created and destroyed
per call. gRPC channels are designed to be long-lived, thread-safe, and reused; they carry connection
setup and background machinery that is wasted when discarded immediately.

This is not a cold path. The background reconciler loops forever at 250 ms (when the rundown service
is enabled) or 1 s otherwise, and in the steady state still reaches gRPC through
`refresh_live_state` → `_live_input` → `getInputs`. Every REST request adds more.

**Evidence.**
- `api/src/transcoder_api/services/transcoder.py:22` — channel factory
- `api/src/transcoder_api/services/transcoder.py:62, 84, 116, 167` — the four call sites
- `api/src/transcoder_api/main.py:41-52` — the polling loop that drives it

**Fix.** Create the channel once at module scope (lazily, so import order stays safe) and reuse it.
Roughly ten lines. Keep the per-call `stub` construction; stubs are cheap.

---

## 2. A lock and a lookup happen on every media buffer

**In plain terms:** every single frame of video and chunk of audio that passes a monitoring point has
to take a numbered ticket, wait at a counter, and be looked up in a directory before anything
happens. The code already knows who it is — the ticket and the directory are unnecessary. This is the
busiest shared code path in the whole engine.

**Technical detail.** `Pad::_onProbe` is the single static dispatcher for all pad probes. On every
invocation it takes a `std::recursive_mutex`, performs an `unordered_map` lookup keyed on the probe
ID, then makes an indirect `std::function` call. A recursive mutex is more expensive than a plain
mutex — it tracks owning thread and recursion depth, and optimises poorly. The lookup resolves
information that is already known when the probe is registered.

The lock also couples GStreamer streaming threads to control-plane operations on the same pad.

**Evidence.**
- `transcoder/lib/utpl/src/utpl/pipeline/pad.cpp:418` — the dispatcher
- `transcoder/lib/utpl/include/utpl/pipeline/pad.h:76` — `std::recursive_mutex _callbacksGuard`
- `transcoder/lib/utpl/include/utpl/pipeline/pad.h:83` — `std::unordered_map<guint, ProbeCallback>`
- `transcoder/lib/utpl/src/utpl/pipeline/pad.cpp:204` — registration passes `this` as user data

**Fix.** `gst_pad_add_probe` accepts a `user_data` pointer and a `GDestroyNotify`. Heap-allocate the
callback at registration, pass it directly as `user_data`, and let GStreamer free it. That removes
both the lock and the map lookup from the per-buffer path. Contained to one file, but it changes
probe lifetime handling, so pair it with removal/teardown tests. See
[20](20-concurrency-memory-and-error-handling.md) before changing this.

---

## 3. A sticky event is re-fetched on every media buffer

**In plain terms:** to work out what time a frame belongs to, the code re-reads the same reference
note from a shelf for every frame, instead of writing the note down once when it changes. The correct
approach is already used elsewhere in this codebase.

**Technical detail.** `Pad::probeCurrentPts` calls `gst_pad_get_sticky_event(..., GST_EVENT_SEGMENT, 0)`
and then `gst_event_unref` on every buffer, purely to convert a PTS into a running time. The segment
changes rarely; the lookup and reference churn are per-buffer.

`VideoComplexConnector` already demonstrates the correct pattern: it caches the segment in a
`GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM` branch via `gst_event_copy_segment` and does only arithmetic in
the per-buffer branch.

**Evidence.**
- `transcoder/lib/utpl/src/utpl/pipeline/pad.cpp:229` — the per-buffer refetch
- `transcoder/lib/utpl/src/utpl/transcoder/channel/connector/complex/video.cpp:71-101` — the pattern to copy

**Fix.** Cache the segment on the downstream-event branch and use the cached copy per buffer.

---

## 4. All four configuration write routes are broken

**In plain terms:** the part of the web service that is supposed to save configuration changes cannot
work at all. It tries to unwrap a parcel that the framework has already unwrapped, and fails every
time. Anyone using this feature gets an error no matter what they send.

**Technical detail.** FastAPI has already parsed the request body into a `dict` by the time the
handler runs. Each setter then calls `json.loads(post_data)` on that `dict`, which raises. The
replace-all routes additionally declare `Dict[str, Any]` where an array is intended, so a correct
array payload is rejected before reaching the handler.

Confirmed at runtime on 2026-08-03: single-input setter returns HTTP 200 carrying an error, and the
replace-array route returns HTTP 400 ([28](28-playlist-input-api-runtime-verification.md)).

**Evidence.** `api/src/transcoder_api/routers/config.py:80/83`, `97/100`, `116/119`, `133/136`.

**Fix.** Delete the `json.loads` calls, correct the replace-all models to accept a list, validate with
the production parser or schema, and write atomically with a backup. This surface also has no
authentication — see [22](22-licensing-crypto-and-security.md). Tracked as P1 in
[27](27-known-gaps-contradictions-and-remediation.md).

---

## 5. The database write-ahead log grows unbounded

**In plain terms:** the database keeps a running notepad of recent changes and periodically copies
them into the main ledger. The copying is not happening, so the notepad is now three times bigger
than the ledger itself and still growing. Left alone it will eventually fill the disk.

**Technical detail.** In `runtime/`, `managed-playlist.sqlite` is 1.24 MB while
`managed-playlist.sqlite-wal` is 4.24 MB. SQLite normally checkpoints the write-ahead log
automatically, but a process that continuously holds open read/write sessions can prevent
checkpoints from completing. The reconciler opens a `SessionLocal()` on every tick.

**Evidence.**
- `runtime/managed-playlist.sqlite` (1,298,432 bytes) versus `runtime/managed-playlist.sqlite-wal` (4,235,392 bytes)
- `api/src/transcoder_api/main.py:41-52` — session opened per tick in a continuous loop

**Fix.** Run a periodic `PRAGMA wal_checkpoint(TRUNCATE)`, or tune `wal_autocheckpoint`, and confirm
the reconciler closes sessions promptly (it already uses `try/finally`). Add WAL size to whatever
monitoring is introduced.

---

## 6. The background loop polls even when nothing has changed

**In plain terms:** a worker checks the whole to-do list every quarter-second even when the list is
finished and nothing is waiting. It should check often when there is work and rarely when there is
not.

**Technical detail.** `_playlist_reconciler` runs unconditionally at a fixed interval. When
`state.revision == state.applied_revision` there is nothing to apply, yet the tick still performs
database reads and a gRPC round trip via `refresh_live_state`. Combined with finding 1, each idle
tick also builds and destroys a connection.

**Evidence.**
- `api/src/transcoder_api/main.py:41-52`
- `api/src/transcoder_api/services/media.py:371-394` — the early-return idle path still calls `refresh_live_state`

**Fix.** Back off to a slower interval when the revision is applied and the outbox is empty, and
return to the fast interval on a pending change. Note the existing constraint: this loop must stay in
a single API worker until a SQLite lease exists ([27](27-known-gaps-contradictions-and-remediation.md)).

---

## 7. Avoidable copies in setters and signatures

**In plain terms:** several functions are handed a photocopy of a document when they only needed to
glance at the original. Harmless individually, and none of it is on the busy path — worth tidying
when touching the files anyway.

**Technical detail.** Setters take `std::string` by value and then copy again into the member rather
than moving. Separately, roughly 77 places in `lib/utpl` pass a `std::shared_ptr` by value, each
causing an atomic reference-count increment and decrement.

**Evidence.**
- `transcoder/lib/utpl/include/utpl/transcoder/input.h:271, 279`
- `transcoder/lib/utpl/include/utpl/transcoder/output/parameters.h:90, 98, 106, 114, 122`

**Fix.** Use `std::move` in the setters, or take `const std::string &`. Pass `shared_ptr` by
`const &` where ownership is not transferred. Low value on its own; `clang-tidy` finds these
mechanically (see tooling below).

---

## 8. Debug build flags are hardcoded and partly dead

**In plain terms:** the debug build always switches on an expensive memory-checking mode, and there
is no way to turn it off without editing the build file. A second line meant to pass the same option
to a later build step refers to a setting name that does not exist, so it does nothing.

**Technical detail.** `-fsanitize=address` is appended unconditionally to `CMAKE_CXX_FLAGS_DEBUG`, so
a plain non-ASAN debug build is not selectable. The following block sets `CMAKE_LINKER_FLAGS_DEBUG`,
which is **not a CMake variable** — the correct names are `CMAKE_EXE_LINKER_FLAGS_DEBUG` and
`CMAKE_SHARED_LINKER_FLAGS_DEBUG`. The build still links correctly because CMake forwards
`CMAKE_CXX_FLAGS_DEBUG` to the link line, so the effect is dead code rather than breakage — but the
stated intent and the actual mechanism differ, which is a trap for the next person.

**Evidence.** `transcoder/CMakeLists.txt:16-22`.

**Fix.** Put sanitizers behind an option such as `UTPL_SANITIZE=address|thread|undefined|off`, and
either correct the linker variable name or delete it.

---

## 9. The configuration parser is one 1,282-line function

**In plain terms:** the entire rulebook for reading the configuration file is written as a single
enormous nested passage, fifteen levels deep in places. It works, but it is the hardest file in the
project to change safely — and it is precisely the file that defines what customers are allowed to
configure.

**Technical detail.** `parser.cpp` is 1,282 lines with a maximum indentation of 60 columns (about 15
nesting levels, deepest at line 372). It is also where the four invisible product limits live: only
`inputs[0]` is consumed, `video.interlace` is inside `#if 0`, the `adobe`/`enhanced` ad markers are
inside `#if 0`, and `scte.prerollTime` is never read.

This is a maintainability finding, not a performance one — parsing happens once at startup.

**Evidence.** `transcoder/app/src/application/config/parser/json/parser.cpp`.

**Fix.** Extract one function per configuration section, preserving behaviour exactly, with parser
tests added first. High effort; schedule it deliberately rather than opportunistically. Follow the
config-change recipe in [25](25-agent-workflows-and-change-recipes.md).

---

# Tooling Recommendations

## The largest gap: there is no telemetry at all

**In plain terms:** the system has no dashboard, no gauges, and no alarms. The only way to find out
whether it is healthy is for a person to capture the video output and inspect it by hand. For
something intended to run continuously, this is the most valuable gap to close.

**Verified absence:** no `prometheus`, `opentelemetry`, `/metrics`, or `statsd` anywhere in the tree.

**Recommendation.** Add `prometheus-fastapi-instrumentator` to the API for request and latency
metrics, and a small exporter for engine facts that currently require manual capture: current
playlist item, input stream states, applied-versus-desired revision, reconciler lag, WAL size, and
buffer or PTS continuity counters.

## GStreamer diagnostics you already have and are not using

**In plain terms:** GStreamer ships with built-in inspection tools that need no code changes — just
environment variables. Several of them target exactly the class of bug this project has already hit
by hand.

| Tool | What it gives you |
| --- | --- |
| `GST_TRACERS=leaks` | Reports leaked objects and unreleased pads at shutdown. This is the automated form of the pad/layer ownership bugs recorded as history in [27](27-known-gaps-contradictions-and-remediation.md). |
| `GST_TRACERS=latency` | Per-element latency, useful for the graphics timestamp issues in [15](15-events-overlays-slates-and-squeeze.md). |
| `GST_TRACERS=stats` | Buffer and event flow rates per pad. |
| `GST_DEBUG_DUMP_DOT_DIR` | Pipeline graph dumps. [21](21-logging-observability-and-debugging.md) already recommends these; make them an automatic artifact on test failure. |
| `gst-shark` | Third-party tracing front end if per-element profiling is wanted. |

## Missing sanitizer: thread safety

**In plain terms:** the project checks for memory mistakes but never checks for timing mistakes
between simultaneous tasks — and this codebase runs a lot of things at once.

ASAN is configured; **ThreadSanitizer and UndefinedBehaviorSanitizer are not**.
[20](20-concurrency-memory-and-error-handling.md) lists seven concurrent execution contexts: GLib main
loop, transcoder worker, GStreamer streaming threads, event scheduler, HLS uploader, image/overlay
workers, and the FastAPI server. That is exactly the profile where TSAN pays for itself. Add both as
build options alongside the fix for finding 8.

## Static analysis

**In plain terms:** automated proof-readers that catch whole categories of mistake before anyone runs
the code. None are currently configured.

No `clang-tidy` or `cppcheck` configuration exists. Enabling `bugprone-*`, `performance-*`, and
`cppcoreguidelines-*` would mechanically find finding 7, and also the deprecated GLib `GValueArray`
usage already tracked as P3 in [27](27-known-gaps-contradictions-and-remediation.md).

## Build speed

**In plain terms:** compiling this project is slower than it needs to be. Three well-established
tools cut rebuild time substantially with no change to the code and no risk to output.

Not currently used: `ccache` (caches compilation results), Ninja (faster build scheduler than Make),
and `mold` (much faster linker). Link-time optimisation is also absent — worth measuring, but it will
not beat encoder cost, so do not expect a runtime win.

## Python tooling

**In plain terms:** one modern tool can replace the current formatter and do linting as well, far
faster.

- Replace `black` with **ruff**, which formats and lints in a single, much faster tool.
- `mypy` is configured but excludes generated models (`exclude = ['rest_api\.py$']` in
  `api/pyproject.toml`), so that surface is unchecked.

## Fix the API schema drift structurally

**In plain terms:** the API instruction manual is written by hand and has drifted from what the
software actually does. Rather than correcting it repeatedly, have the software write its own manual.

[27](27-known-gaps-contradictions-and-remediation.md) records that `api.yaml` omits the config,
graphics, media, playlist, and operator routes, and advertises HTTP 201 where FastAPI returns 200.
FastAPI can emit its own OpenAPI document. Generating `api.yaml` from the running application and
diffing it in CI inverts the current direction and removes this entire class of drift permanently.

Note the ordering constraint: `api.yaml` is currently the **input** to `poe codegen`
(`datamodel-codegen` builds `model/rest_api.py` from it), so this inversion needs a deliberate plan,
not a quick switch.

## Encoding performance, if speed is the real goal

**In plain terms:** if the actual aim is to use less CPU, the answer is to let a graphics card do the
video compression instead of the processor. That is a deployment decision more than a coding one.

`x264enc` with `veryfast`/`zerolatency` is already a sensible software choice. Meaningful gains come
from hardware offload (VA-API, NVENC), which [02](02-feature-catalog.md) correctly marks as
integration gated. Prove any such path with decoded output before claiming support — see
[19](19-gstreamer-patches-and-external-plugins.md) and `doc/misc/encoder-integration.md`.

---

## Related

The missing telemetry noted above is the main technical blocker for the diagnosis tools proposed in
the MCP interface evaluation, [33](33-mcp-interface-evaluation.md). The gRPC channel-per-call finding
and the broken config write routes also constrain that design.

## Maintenance

When an item here is fixed, record the observed before/after measurement and the source version, move
the entry to a resolved list, and update the owning subsystem page. When an item is measured and
found not to matter, say so explicitly rather than deleting it — a disproved finding is useful
information.
