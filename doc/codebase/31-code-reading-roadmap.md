# Code-Reading Roadmap

A staged path for reading this codebase by actually reading the source, in an order where each stage
only depends on stages before it. Line counts were measured against this checkout
(`1.1.0-beta.2-skandha.3`) and are reading-effort estimates, not targets.

This roadmap is about **reading**. The handbook pages tell you what a subsystem does; this page tells
you in what order to open the files so that each one makes sense when you get to it. Read the linked
handbook page **after** the code for that stage, as a cross-check on what you concluded — not before,
or you will read the doc's summary instead of the code.

## Ground rules

1. **Read top-down through one execution path before reading any subsystem in breadth.** This code is
   callback-heavy: reading a file in isolation gives you a class, not a behavior.
2. **Examples are not the deployment.** `transcoder/config/` holds examples; what actually runs is in
   the top-level `config/`. (The former `sandbox/` tree was removed on 2026-10-16; see
   [24](24-sandbox-prototypes-and-artifact-status.md).)
3. **The header is the contract; the `.cpp` is the graph.** Most classes use Pimpl, so the public
   header is small and the real behavior lives in the anonymous-namespace implementation struct.
4. **Record the factory origin of every GStreamer element you meet** — standard GStreamer, in-tree
   UTPL plugin, patched upstream, Rust plugin, or Dolby SDK. Keep this list as you go; it becomes
   your mental packaging map and it is the thing most likely to differ between images.
5. **Do not trust a doc over the source, including this one.** If you find a divergence, add it to
   [27](27-known-gaps-contradictions-and-remediation.md).

## Orientation before Stage 1

Read these five short things first. Roughly 30 minutes total.

| File | Lines | Why |
| --- | --- | --- |
| `README.md` | 10 | What the four top-level directories are. |
| `AGENTS.md` | 39 | The source-of-truth ordering and where changes belong. |
| `version.txt` | 1 | The version every runtime claim must be checked against. |
| [00-scope-and-evidence.md](00-scope-and-evidence.md) | 30 | What each confidence label means. You will need these words. |
| [01-repository-map.md](01-repository-map.md) | 30 | Product vs. test vs. experiment. |

Then skim, do not study, [02-feature-catalog.md](02-feature-catalog.md). You are building a
vocabulary of feature names, not learning the features.

---

## Stage 1 — The build graph

**Goal:** know what actually gets compiled before you read anything that gets compiled.

Read in this order:

| File | Lines | What to extract |
| --- | --- | --- |
| `transcoder/CMakeLists.txt` | ~45 | C++17; subdirectories `lib`, `license`, `app`, `test`, `plugins`; Debug adds ASAN; install prefix `/opt/utpl`. |
| `transcoder/lib/CMakeLists.txt` | small | The four libraries: `common`, `crypto`, `license`, `utpl`. |
| `transcoder/lib/utpl/CMakeLists.txt` | — | The full external dependency list. This is the single most informative file about what the engine can do. |
| `transcoder/app/CMakeLists.txt` | — | The `transcoder` executable and the generated `transcoderGrpcApi` library. |
| `transcoder/plugins/gstreamer/**/CMakeLists.txt` | small | Targets `utplaudiomute`, `utplcaptionmute`, `gstdlbpatcommon`, `gstdlbpat{dec,mx,loud,enc}`. |

**Checkpoint.** You should be able to answer, without grepping: which four libraries exist, which
one links GStreamer, and how many in-tree GStreamer plugins are built.

Cross-check with [03-build-development-and-runtime.md](03-build-development-and-runtime.md).

---

## Stage 2 — Startup and configuration

**Goal:** follow one process from `main()` to the moment a pipeline is requested.

| File | Lines | What to extract |
| --- | --- | --- |
| `transcoder/app/src/main.cpp` | 203 | CLI options `-c` config, `-l` license, `-b` database, `-h` help. License is loaded and validated **before** `Application` exists. |
| `transcoder/app/src/application.cpp` | 508 | `Application::configure` order: logger/debug settings → full config → GStreamer plugin ranks → database path → license-server URI → input/output restriction validation. Then the SQLite provider, gRPC server, and transcoder worker are constructed. |
| `transcoder/app/src/application/config/parser/json/parser.cpp` | 1282 | The real configuration contract. |

The parser is the largest single file you will read in this stage and it repays slow reading. It is
one deeply nested function-per-section; read it section by section against a real example in
`transcoder/config/example/`, not straight through.

**Three things in the parser you must find yourself**, because they define real product limits and
every one of them is invisible from the schema:

1. Only `transcoder.inputs[0]` is consumed. Additional input objects are silently ignored.
2. The `adobe` HLS ad marker is not parsed or serialised; `elemental` and `enhanced` are live.
3. `scte.prerollTime` is never read; the 4000 ms class default in
   `lib/utpl/include/utpl/transcoder/types.h` always wins.

Then read the defaults these parse into:
`lib/utpl/include/utpl/transcoder/{input.h,types.h,output/parameters.h}`.

**Checkpoint.** Take `config/migrated-media-playlist.json` and trace three fields
end-to-end: one that reaches a GStreamer property, one that is validated and rejected, and one that
is parsed and discarded. Cross-check with
[05-configuration-reference.md](05-configuration-reference.md) — 423 lines, the longest handbook page,
and worth reading in full once you have the parser in your head.

---

## Stage 3 — The GStreamer wrapper layer

**Goal:** learn the vocabulary that every later file is written in. Do not skip this stage; every
subsequent file assumes it.

Read in this exact order — each depends on the previous:

| File | Lines | What to extract |
| --- | --- | --- |
| `pipeline/element.cpp` | 375 | `Element::make`, state changes, **pending async state tracking**. |
| `pipeline/pad.cpp` | 481 | Static/request/ghost pads, linking, probes, events. |
| `pipeline/bin.cpp` | 261 | Child ownership, ghost pads, traversal helpers. |
| `pipeline/caps.cpp` | 484 | UTPL codec/media parameters → GStreamer caps. The translation table between config vocabulary and GStreamer vocabulary. |
| `pipeline/pipe.cpp` | 239 | Owns the pipeline; translates bus state/error/EOS/buffering/tag messages into engine callbacks. |
| `pipeline/utils.cpp` | 111 | Standard queue and identity element helpers you will see everywhere. |

**What to watch for:** where a raw `Gst*` pointer's ownership is transferred, and where a request pad
is released. Smart pointers here wrap C objects and do not make GStreamer's reference rules go away.

**Checkpoint.** Read `pipe.cpp`'s bus handler and write down what happens on EOS versus on error.
You will need both in Stage 4. Cross-check with
[06-pipeline-abstractions.md](06-pipeline-abstractions.md).

---

## Stage 4 — The engine state machine

**Goal:** the spine of the whole product.

| File | Lines | What to extract |
| --- | --- | --- |
| `lib/utpl/include/utpl/transcoder/engine.h` | — | Public surface first. |
| `lib/utpl/src/utpl/transcoder/engine.cpp` | 571 | The state machine. |

Read `engine.cpp` in three passes:

1. **Pass 1 — the enum and the tables.** Around lines 32–60 the `State` enum, then near line 356 the
   explicit up-transition map, then the down-transition map, then the transition-handler map. The
   order of the enum is *not* the order of transitions; the maps are authoritative.
2. **Pass 2 — one full startup.** Follow `STOPPED → INPUT_ADD → INPUT_PROBE → CHANNEL_ADD →
   OUTPUT_ADD → CHANNEL_PREPARE → PLAYING` through the handler lambdas. Note that outputs are added
   before the channel is prepared.
3. **Pass 3 — teardown and failure.** `PIPELINE_STOP → OUTPUT_DEL → CHANNEL_DEL → INPUT_DEL →
   STOPPED`, plus how a bus error sets `PIPELINE_ERROR` and interrupts the loop.

Inside `INPUT_ADD` you will find input selection, which is three lines and decides a great deal:
one URI containing `pls+` → `PlaylistInput`; one ordinary URI → `SingleInput`; two URIs →
`StandardInput`.

**Checkpoint.** Draw the state machine from memory and check it against
[04-system-architecture-and-lifecycle.md](04-system-architecture-and-lifecycle.md). If your diagram
and the doc's disagree, the code decides.

---

## Stage 5 — One complete media path

**Goal:** the first time you see media actually flow. Read only the *simplest* path here; the
variants come in Stage 6.

| File | Lines | What to extract |
| --- | --- | --- |
| `transcoder/stream/input/base.cpp` | 189 | `BaseInputStream` and its tee-based fan-out. Every input stream is one of these. |
| `transcoder/input/single.cpp` | 1386 | Source bin construction per protocol, `decodebin`, dynamic pad callbacks, decoder autoplug sorting. |
| `transcoder/stream.cpp` | 86 | `Stream::create` — how a discovered stream becomes a typed object. |
| `transcoder/channel.cpp` | 1429 | The graph-building boundary. The largest file in the engine. |
| `channel/processor.cpp` | 31 | Tiny, but it is the dispatch point: `Processor::create`. |
| `channel/connector/passthrough.cpp` | 113 | The simplest connector. |
| `channel/processor/passthrough.cpp` | 266 | The simplest processor; also where `utplcaptionmute` is inserted. |
| `transcoder/output/single.cpp` | 245 | Output bin, pad streams, muxer ownership. |
| `output/muxer/mpegts.cpp` | 96 | `mpegtsmux` plus a tee. |
| `output/sink.cpp` | 67 | `OutputSink::create` protocol dispatch. Read this before any individual sink. |
| `output/sink/udp.cpp` | 87 | The simplest real sink. |

`channel.cpp` is long. Read it against a specific question — "how does one discovered input stream
reach one requested output stream in passthrough?" — and let the rest go until Stage 6.

**Checkpoint.** You can now describe a complete passthrough UDP-to-UDP path element by element.
Cross-check with [07-inputs-and-ingest.md](07-inputs-and-ingest.md),
[09-channel-routing-connectors-and-flowlines.md](09-channel-routing-connectors-and-flowlines.md), and
[13-outputs-muxing-and-delivery.md](13-outputs-muxing-and-delivery.md).

---

## Stage 6 — Transcoding paths

**Goal:** the variants, now that the skeleton is familiar. These are largely parallel to each other;
read video first and use it as the template for the others.

| File | Lines | Read for |
| --- | --- | --- |
| `channel/processor/transcoder/flowline.cpp` | 330 | **Read this first.** `encodebin` construction, pre-encoder convert/rate/capsfilter, and the output tee. Everything below feeds a flowline. |
| `channel/connector/complex/video.cpp` | 754 | Normalization, source selection, the compositor canvas, scaling of low-resolution/dummy/fallback sources. |
| `channel/processor/transcoder/video.cpp` | 255 | Owns a `Compositor`, registers the downstream target before connector preparation. |
| `channel/compositor.cpp` | 504 | `compositor` element, layers, crop/convert sub-bins, z-order, `destroyLayer`. Prerequisite for Stage 7. |
| `channel/connector/complex/audio.cpp` | 685 | Audio normalization, fallback and mute paths. |
| `channel/processor/transcoder/audio.cpp` | 219 | Audio flowlines and mute integration. |
| `channel/connector/complex/dolby.cpp` | 956 | The largest connector: Dolby decode/convert/metadata/encode. |
| `channel/processor/transcoder/dolby.cpp` | 324 | Dolby flowline selection. |

**Flowline sharing is the subtle idea in this stage.** A flowline can be shared between outputs with
compatible requirements, which is why changing one output can affect another. Find the matching
logic in `channel.cpp` before you believe any per-output change is local.

**Checkpoint.** Cross-check with [10-video-processing.md](10-video-processing.md) and
[11-audio-and-dolby-processing.md](11-audio-and-dolby-processing.md). Captions are genuinely thin —
[12](12-captions-and-ancillary-streams.md) is 9 lines because there is not much there; confirm that
by grepping the caption TODOs in `channel.cpp` yourself.

---

## Stage 7 — Playlists, events, and graphics

**Goal:** the parts that change the graph while it is running. This is where the hardest bugs live.

Read the playlist path first:

| File | Lines | Read for |
| --- | --- | --- |
| `playlist/parser.cpp` | 201 | M3U8 input-playlist parsing. Small and self-contained; also has its own CTest suite. |
| `transcoder/input/playlist.cpp` | 1351 | `concat`, current/pending/preloaded items, per-item `SingleInput` construction, looping, `_switchToItem`, `applyPlaylistSnapshot`. |

`_switchToItem` and `applyPlaylistSnapshot` are the two functions to read most carefully in the whole
engine. The pending-branch commit-at-EOS logic here was the root cause of a real three-item playlist
failure; the fix is described in [27](27-known-gaps-contradictions-and-remediation.md).

Then events and graphics:

| File | Lines | Read for |
| --- | --- | --- |
| `transcoder/event.cpp` | 267 | The `Event` model. |
| `event/scheduler.cpp` | 421 | Dispatch loop and timing. Read before any subclass. |
| `event/scheduler/overlay.cpp` | 268 | Running-time alignment for new overlay pads. Read alongside `compositor.cpp` from Stage 6. |
| `event/scheduler/squeeze.cpp` | 61 | Smallest scheduler subclass; good template. |
| `event/scheduler/slate.cpp` | 191 | Note the update/stop TODOs. |
| `event/scheduler/{spliceInsert,returnToNetwork,timeSignal}.cpp` | 164/160/199 | The three SCTE-35 event types. |
| `transcoder/input/overlay.cpp` | 731 | The overlay input path. |
| `transcoder/stream/input/image.cpp` | 687 | Static image cadence; the bounded 96 MiB animated-graphic ring behind `ENABLE_VIDEO_GRAPHICS`. |

**The timestamp trap.** A new overlay pad has no timeline, so a position query returns
`GST_CLOCK_TIME_NONE` and image PTS would start at zero — which a long-running compositor discards as
late. Find the alignment fallback in `overlay.cpp`. Understanding this one interaction explains most
of the graphics history entries in [27](27-known-gaps-contradictions-and-remediation.md).

**Checkpoint.** Cross-check with [08-playlists-buffering-and-switching.md](08-playlists-buffering-and-switching.md)
and [15-events-overlays-slates-and-squeeze.md](15-events-overlays-slates-and-squeeze.md).

---

## Stage 8 — Delivery, SCTE conditioning, and the control plane

These three are independent of each other. Read whichever your work needs; read all three eventually.

### 8a. HLS and remaining sinks

| File | Lines |
| --- | --- |
| `output/sink/hls.cpp` | 432 |
| `hls/playlist.cpp` | 260 |
| `hls/streamer.cpp` | 872 |
| `hls/streamer/uploader/*.cpp` | — |
| `output/sink/{srt,s3,file}.cpp` | 102/145/59 |
| `output/group.cpp` | 114 |

Cross-check: [14-hls-http-and-s3.md](14-hls-http-and-s3.md).

### 8b. SCTE-35, ESAM, and blanking

| File | Lines |
| --- | --- |
| `esam/model.cpp` | 174 |
| `esam/client.cpp` | 226 |
| `transcoder/blanker.cpp` | 673 |

Cross-check: [16-scte35-esam-pois-blackout-and-blanking.md](16-scte35-esam-pois-blackout-and-blanking.md).
Note `SCTE35PointDescriptor` throws explicitly — find that line.

### 8c. Control plane

Read the contract before either implementation:

| File | Lines | Read for |
| --- | --- | --- |
| `app/grpc/proto/transcoder.proto` | 66 | Four RPCs. That is the entire engine-facing contract. |
| `app/src/application/transcoder/api/grpc.cpp` | 179 | The C++ server side. |
| `app/src/application/event/provider/sqlite/dao.cpp` | — | The events DAO. Note `_dbToModel` reads the `time` column into **both** start and end time — a documented open defect. |
| `api/src/transcoder_api/main.py` | 101 | Route assembly, static mounts, `/operator`. |
| `api/src/transcoder_api/services/transcoder.py` | 206 | REST → gRPC mapping. |
| `api/src/transcoder_api/routers/input.py` | 58 | The `/inputs` surface. |
| `api/src/transcoder_api/model/db.py` | 272 | The ORM. |
| `api/src/transcoder_api/routers/config.py` | 211 | Read this **critically**: all four setters call `json.loads()` on an already-parsed dict, and `restart_transcoder` has an uninitialized-variable path. Documented as broken, not yet fixed. |
| `api/src/transcoder_api/services/media.py` | 424 | Managed queue and the reconciler that calls `applyPlaylistSnapshot`. |
| `api/src/transcoder_api/services/{schedule,graphics}.py` | 219/270 | Event validation and graphics cues. |

Cross-check: [17-rest-grpc-and-sqlite-control-plane.md](17-rest-grpc-and-sqlite-control-plane.md)
(474 lines) and [../api/README.md](../api/README.md).

The optional rundown layer (`services/rundown.py`, 1219 lines, plus `routers/rundown.py`, 459) is
disabled by default and depends on everything above. Read it last, with
[30-transactional-rundown-service.md](30-transactional-rundown-service.md) open.

---

## Stage 9 — Plugins, patches, and the edges

By now you have met these elements in call sites and can read them as implementations rather than
mysteries.

| Area | Files | Cross-check |
| --- | --- | --- |
| In-tree mute elements | `plugins/gstreamer/utpl/{audiomute,captionmute}/src/` | [18](18-custom-gstreamer-plugins.md) |
| Dolby PAT elements | `plugins/gstreamer/dolbypat/dlbpat*/src/` | [18](18-custom-gstreamer-plugins.md) |
| Upstream patches | `transcoder/patches/gstreamer1.0-plugins-bad/*.patch` (five) | [19](19-gstreamer-patches-and-external-plugins.md) |
| Licensing and crypto | `lib/license/`, `lib/crypto/` | [22](22-licensing-crypto-and-security.md) |
| Common utilities | `lib/common/` | — |

Read the five patches in full. They are small, and they explain behavior in `mpegtsmux` and
`hlsdemux` that the call sites depend on but do not describe.

To test a GStreamer hypothesis outside UTPL, write a minimal `gst-launch-1.0` pipeline in a scratch
directory. The former `sandbox/` examples were removed on 2026-10-16
([24](24-sandbox-prototypes-and-artifact-status.md)).

---

## Stage 10 — Tests as executable documentation

Read tests last, as confirmation, not first, as introduction.

| Layer | Location | Size |
| --- | --- | --- |
| C++ unit tests | `lib/{common,crypto,license,utpl}/test/src/` | 4 CTest suites: base64/m3u8/time, cipher/hasher/signature, license serialize, playlist parser. |
| Shell integration | `tests/tests/` | 75 `test.sh` cases: 45 `proto`, 12 `audio`, 9 `scte`, 3 `misc`, 2 `blackout`, 2 `squeeze`, 1 `overlay`, 1 `jira`. |
| Python API | `api/tests/` | 27 test functions across `routing_test` (11), `rundown_test` (8), `graphics_test` (4), `playout_test` (4). |

Read `tests/framework/` before any individual `test.sh`; the cases are thin over shared helpers. A
case establishes only what it asserts — several assert process behavior rather than media
correctness. Cross-check: [23-testing-and-validation.md](23-testing-and-validation.md).

---

## Suggested schedule

Stages are sequential; the estimates assume you are reading carefully and running things, not
skimming.

| Stage | Focus | Rough effort |
| --- | --- | --- |
| Orientation + 1 | Docs, build graph | Half a day |
| 2 | Startup and config parser | 1–2 days |
| 3 | Pipeline wrappers | 1 day |
| 4 | Engine state machine | 1 day |
| 5 | One complete passthrough path | 2 days |
| 6 | Transcoding paths | 2–3 days |
| 7 | Playlists, events, graphics | 3 days |
| 8 | Delivery, SCTE, control plane | 2–3 days |
| 9 | Plugins, patches, edges | 1–2 days |
| 10 | Tests | 1 day |

Roughly three weeks to genuine working familiarity with the engine; Stages 1–5 alone (about a week)
are enough to make a scoped change safely with review.

## Shortcuts by role

These are entry ramps, not replacements for the stages above.

- **Config or REST work:** Orientation → 2 → 8c → 4. Skip 5–7 initially.
- **Graphics or events work:** Orientation → 3 → 4 → 6 (compositor only) → 7.
- **Playlist or playout work:** Orientation → 3 → 4 → 5 → 7 (playlist half) → 8c (`media.py`).
- **Packaging or release work:** Orientation → 1 → 9 → [03](03-build-development-and-runtime.md).
- **Debugging a live pipeline:** Orientation → 3 → 4 → [21](21-logging-observability-and-debugging.md),
  then the owning subsystem stage.

## Standing habits

- Keep the running list of GStreamer factories and their origin from Ground Rule 4.
- Keep a list of every `#if 0`, `TODO`, and `FIXME` you pass. Several define real product limits and
  are the difference between "the schema accepts it" and "it does something."
- When code and a handbook page disagree, the code wins and
  [27](27-known-gaps-contradictions-and-remediation.md) gets a row.
- Before changing anything, read [25-agent-workflows-and-change-recipes.md](25-agent-workflows-and-change-recipes.md).
