# Step 5 change log

A running record of every change made for Step 5 (external encoder worker, flow registry, SCTE-35 over MXL), kept so that a final
report can be written from it. Newest entry last. Each entry says what changed, where, why, and what was observed. Plan:
[step5-plan-2026-11-11.md](step5-plan-2026-11-11.md).

Rules for this file: one entry per change or experiment; include the files touched, the commands that matter, and the result in numbers;
record failures and dead ends too; mark anything not committed or not pushed.

Legend: **Product** = changes under `transcoder/`, `api/`, `frontend/`, `docker/`, `tests/`, `config/`. **Docs** = under `doc/` or the root
pages. **Experiment** = under `../experiments/` (outside the repository). **Research** = reading only, nothing written to the repository.

---

## Entries

### 2026-11-11 · 01 · Research · Can SCTE-35 travel in an MXL flow?
- **What:** Read the MXL v1.1.0 headers in the `utpl/base:mxl` image; ran `gst-inspect-1.0 mxlsink` and `mxlsrc` in
  `utpl/transcoder-test:mxl`; cloned MXL `main` (3aaa8ed6, 2026-11-09) into the scratchpad and read its event-flow docs, data-flow code and
  upstream issues #270, #469, #496, #500, #580, #585, #588, #798; read the SMPTE ST 2110-41 registry; checked libklvanc, libklscte35,
  scte35-go, threefive.
- **Where:** nothing written to the repository.
- **Result:** data flows (`video/smpte291`, ST 291 ancillary data, one 4096-byte grain per frame) are in v1.1.0 and in our image
  (`mxlsink`/`mxlsrc` accept `meta/x-st-2038`, `data-flow-id`). Event flows exist only on MXL `main` (since 2026-10-13), unreleased, no
  `gst-mxl-rs` support, format still being discussed (#798). No registered identifier for SCTE-35 in either place.
- **Not in the image:** `st2038extractor`/`st2038combiner`, `rtpsmpte291pay` (gst-plugins-rs elements).

### 2026-11-11 · 02 · Docs · Step 5 execution plan written
- **What:** New page `doc/mxl/step5-plan-2026-11-11.md`: work packages WP0–WP8, proposed answers for Q7, Q8 and Gate A, the SCTE carrier
  options A–G, tests T5–T10 and S1–S8, risks.
- **Status:** not committed.

### 2026-11-11 · 03 · Docs · Plan revised: SCTE-104 deferred
- **What:** On the owner's direction ("scte 104 is not a priority") the plan now ships option B only: the unchanged SCTE-35 section in a
  private ANC packet of an MXL data flow. SCTE-104 (option A) is deferred behind the same carrier interface. The trial (WP1) shrank from
  2–3 days to about 1 day and checks frame alignment only. LGPL risk and test S8 removed or marked deferred.
- **Where:** `doc/mxl/step5-plan-2026-11-11.md` (sections 4.4, 4.5, 4.6, 4.7, 4.8, 6, WP table).
- **Status:** not committed.

### 2026-11-11 · 04 · Docs · This change log created
- **What:** `doc/mxl/step5-change-log.md`, requested by the owner so a report can be made once everything is done.
- **Status:** not committed.
- **Note:** `project-status.md` has uncommitted edits (the 2026-11-11 date and the OpenLive/Strom section) that were not made as part of
  this log; they are left as they are.

### 2026-11-11 · 05 · Experiment · SCTE-over-MXL alignment trial (WP1): tool built
- **What:** `../experiments/scte-over-mxl/` (outside the repository): `trial.cpp` (writer and reader), `build.sh`, `one.sh`, `matrix.sh`,
  `scan.sh`, `sdk/` (MXL v1.1.0 headers and library copied out of `utpl/base:mxl`), `results/`.
- **Writer:** v210 video (frame number in the first 8 bytes of each buffer) and an ST 2038 data flow with the same PTS. Cues are
  deterministic sections (CRC-32 valid, 28 to 2000 bytes) split over private ANC packets (placeholder DID 0x58/SDID 0x01, **not a registered
  value**), repeated in every grain from the frame they are known until their splice frame. Mode `same`: one pipeline for both flows. Mode
  `split`: one pipeline per flow, as Nandi's source tap builds one side pipeline per flow.
- **Reader:** MXL C API only. For each grain index it reads the video grain and the data grain of that index and checks that the data was
  written with the video frame in that grain, that every section reassembles byte for byte (CRC and content), and that parity and checksum of
  every ANC word are valid.
- **First run (150 frames, shared pipeline):** 44 grains with cues, 44 aligned, 4 of 4 sections byte-exact, empty grains
  (zero-length ST 2038 buffers) read back as empty grains.
- **Facts learned from the v1.1.0 source:** the data flow is a ring like video (5 frames at 25 fps); `mxlsink` converts ST 2038 to an RFC 8331
  grain padded to 4096 bytes and refuses more than 255 packets or 4096 bytes in one grain; the clock offset `D` (MXL time minus pipeline
  clock) is shared by the MXL elements **of one pipeline** and sampled once; timestamps map to the **nearest** slot, not the slot they fall in.
- **Status:** results in the next entries. Nothing in the repository tree changed for this entry.

### 2026-11-11 · 06 · Experiment · SCTE-over-MXL alignment trial: results
Run on the shared host (load about 0.2) with `utpl/transcoder-test:mxl` (MXL v1.1.0, GStreamer 1.24.2), 25 fps, 640x360 v210. About 400
runs of 7 to 28 seconds. Logs: `../experiments/scte-over-mxl/results/` (`matrix-*.txt`, `scan-*.txt`, `scan-half-*.txt`, `t5-*.txt`, `t9-*.txt`).

| Test | Setup | Result |
| --- | --- | --- |
| T1 | shared pipeline, 700 frames, 23 cues of 28, 60, 120, 255, 400, 1000, 2000 bytes | 253 of 253 cue grains aligned, 23 of 23 sections byte-exact, no bad ANC packet, largest grain 2797 bytes |
| Phase scan, shared pipeline | 27 offsets from the slot start, ±300 µs around it and around the half-slot rounding point, 2 runs each (plus 8 runs of 10 offsets in the first matrix) | 0 misaligned grains |
| Phase scan, one pipeline per flow | same offsets, 4 runs each (8 runs for 10 offsets in the first matrix) | 0 misaligned except at **+2 µs from the half-slot point: 2 of 4 runs, every cue grain (44) one frame late** |
| T5 late reader | joins 1, 2, 3 s after the writer; cue repeated every grain from known to splice (10 or 25 frames) | 7 of 7, 7 of 7, 6 of 6 reachable cues, all exact, at least 7 frames before the splice |
| T6 overlap | cues every 7 frames, 20-frame windows, up to 3 in one grain | 37 of 37 exact, 273 of 273 grains aligned |
| T7 capacity | one section of 2800 and of 3000 bytes | 2800 works (3896 bytes in the grain). 3000 (4179 bytes) makes the data sink stop: no data after the first oversize grain |
| T8 sparse, short read timeout | grains written only when there is a cue | 99 of 99 aligned, 9 of 9 exact; the reader cannot tell "no cue" from "late" |
| T9 late data read | sparse vs every-frame writing, data read 0 to 230 ms after the video | sparse: **stale grains from an earlier ring lap** for never-written indices (8 of 48 and 4 of 23 cue grains misread); every-frame: 0 stale, a late read gets "too late" |
| Empty grain | zero-length ST 2038 buffer | valid; reads back as an empty grain |

- **Findings:** the carrier works on v1.1.0 with no change to MXL. Three rules for WP2: write video and data from one side pipeline;
  write a data grain for every video frame; refuse a cue that would take a grain over 4096 bytes. Written into the plan, section 4.6.
- **Probable cause of the one-frame shift (not proved):** MXL maps a timestamp to the *nearest* slot, and each pipeline samples its own clock
  offset once. At the half-slot point a difference of about a microsecond between the two samples flips the result. The shared pipeline
  shares one sample and never flipped.
- **Not tested:** inside Nandi itself (the tap is not changed yet); other frame rates (29.97, 50, 59.94); interlaced; a writer restart in
  the middle of a cue; a real DID/SDID; 4-hour run; a cue grain read through `mxlsrc` instead of the C API.
- **Files changed in the repository:** `doc/mxl/step5-plan-2026-11-11.md` (4.6 result, 4.7 writer row, risk table), this log. Not committed.

### 2026-11-11 · 07 · Product · Flow registry, engine side (WP4)
- **What:** `MxlRegistry` (new): `getWriter(domain, flowId)` and `collectGarbage(domain)` over libmxl, loaded with `dlopen` (`libmxl.so.1`), headers
  used only when the build has them (`__has_include`), a stub otherwise. `MxlTap::create` now (a) runs the clean-up once per domain per process,
  before this process writes anything there, and (b) refuses the tap, with a warning and fail-open, when a writer in another process holds the
  flow. `Application::configure` refuses a configuration in which two writers (tap or programme output) use one flow, or a channel writes a flow
  its own MXL input reads.
- **Where:** `transcoder/lib/utpl/include/utpl/transcoder/mxlregistry.h`, `src/utpl/transcoder/mxlregistry.cpp` (new), `mxltap.cpp`,
  `lib/utpl/CMakeLists.txt`, `transcoder/app/src/application.cpp`.
- **Why:** plan WP4 (one writer per flow, stale-flow cleanup). Built with `dlopen` so that the engine needs no SDK and the `ENABLE_MXL=0` image is unchanged.
- **Result:** scratch build in `utpl/devel:mxl` succeeded; C++ ctest 12 of 12 pass.
- **Status:** not committed.

### 2026-11-11 · 08 · Experiment · What MXL v1.1.0 does with locks and clean-up (read and measured)
- **Layout:** `<domain>/<id>.mxl-flow/{flow_def.json, data, access, grains/ | channels}`.
- **A writer holds a shared flock on `data` and on every grain file; a reader holds none.** Both show in `/proc/locks` by device and inode.
- **`mxlIsFlowActive` and `mxlGarbageCollectFlows` take an exclusive lock for an instant** (MXL `lib/src/flow.cpp`, `Instance.cpp`): a writer being
  created at that instant can fail to take its lock. Not reproduced.
- **The GStreamer plugin runs the clean-up itself** when `mxlsink` or `mxlsrc` starts (`rust/gst-mxl-rs/src/mxlsink/imp.rs:512`,
  `mxlsrc/mxl_helper.rs:430`). Seen: a `mxlsrc` started on a flow whose writer was killed left the domain empty. This is why the periodic 60 s
  clean-up in the plan was dropped (plan WP4 status).
- **A writer ended with SIGTERM leaves its flow behind**, like a killed one.
- **Plan change:** no periodic clean-up; one at start, with a fixed moment and a log line.

### 2026-11-11 · 09 · Product · Flow registry, API side (WP4)
- **What:** `GET /mxl/flows`: every flow of the domain (`MXL_DOMAIN`, default `/run/mxl`) with id, label, format, size or channels, rate,
  group hints and `writerActive`. `writerActive` is read from `/proc/locks` (device and inode of the flow's data file), never by taking a lock.
  A missing domain answers 200 with `available: false`.
- **Where:** `api/src/transcoder_api/services/mxl.py`, `routers/mxl.py` (new), `main.py` (router registered), `api/tests/mxl_test.py` (new, 9 tests).
- **Result:** 9 of 9 new tests pass; the whole API suite 352 passed, 3 skipped (run in `utpl/api-builder:latest-check` with `PYTHONPATH` on the mounted
  source). Read against real writers (video and audio, 29.97 and 25 fps): `writerActive` true; the flow of a writer killed with SIGKILL:
  `writerActive` false.
- **Not done:** `api/api.yaml` has no entry (it lists only the schedule, input and slate routes; `/stats`, `/config` and `/license` are not in it either).
- **Status:** not committed.

### 2026-11-11 · 10 · Product · Test `tests/tests/mxl/tap/15-registry`
- **What:** one test: (1) two writers on one flow and a read-write flow are refused with the flow named; (2) a flow held by a live writer in another
  process is not written by the tap (warning logged, the other writer's 320-wide description is still the flow's, the tap's audio flow is written,
  engine and other writer alive, on-air output decodes clean); (3) a flow left by a `kill -9` is gone after the engine starts and a live one is not.
- **Result:** passes on the scratch build (twice). **Fails on the installed engine without the registry** (three FAIL lines: the read-write
  config is not refused, no warning, no clean-up line), so it can fail.
- **A mistake caught on the way:** the first version read the width with a label (`Frame width`) that `mxl-info` does not print, so that check
  was empty. Replaced with a read of `flow_def.json`.
- **Status:** not committed.

### 2026-11-11 · 11 · Docs · Handbook updated for the registry
- **What:** `41-mxl-integration.md` (new section "Flow registry (Step 5, WP4)", status paragraph), `02-feature-catalog.md` (row, gap sentence),
  `26-implementation-index.md` (row), `17-rest-grpc-and-sqlite-control-plane.md` (route row and section), `05-configuration-reference.md` (one-writer
  rule), `23-testing-and-validation.md` (fifteen tap tests, API test), `27-known-gaps-contradictions-and-remediation.md` (four P3 items), the plan (WP4 status).
- **Not edited:** `AGENTS.md` still says a flow registry is not built; flagged in page 27.
- **Status:** not committed.

### 2026-11-11 · 12 · Experiment · Regression run of the MXL suites against the new build
- **What:** `tests/run.sh mxl` (SDK 9, tap 15, programme 6, input 12) in `utpl/transcoder-test:mxl` with the scratch build mounted.
- **Result:** 41 of 42 pass, including `tap/15-registry`. The one failure, `tap/05-domain-unusable` (read-only domain case), is an artefact of
  running as root: root can write into a `chmod 555` directory, so the domain was not unusable. Proved both ways: the same test **fails as root on the
  installed engine without my changes**, and **passes as a normal user** (`--user $(id -u)`) on the new build. This is the reason page 23 says to run
  the container with `--user`.
- **Not run:** the 20-minute and 4-hour soaks; the installed images were not rebuilt, so the registry is runtime verified in a scratch build only.
- **Cleanup:** root-owned `tmp/` directories and a result file the runs left under `tests/` were removed through a container.
- **Status:** nothing committed. Uncommitted from this work: the files of entries 07 to 11 and this entry.

### 2026-11-11 · 13 · Docs · Owner decisions recorded
- **Decided by the owner (asked and answered in this session):** Q7 Nandi main keeps the SCTE-35 master timeline; Q8 fail open everywhere (no
  `failPolicy: "closed"` in Step 5); Gate A limits as proposed (lip-sync within 1 frame, 0 drops in 1 hour, latency at most `delayMs` + 2 frames, CPU at
  most 1.25 times the in-process channel, SCTE 20 of 20). WP4 committed without editing `AGENTS.md` (the owner chose to leave it).
- **Where:** `doc/mxl/step5-plan-2026-11-11.md` (status line, WP table row 0, WP5 header).
- **Effect:** WP2 (SCTE carrier in Nandi), WP3 (the worker) and WP5 (failure behaviour) are no longer blocked.

### 2026-11-11 · 14 · Product · WP2 writer: SCTE-35 into an MXL data flow
- **What:** `mxl_carrier` (new library, no GStreamer): ST 2038 packing of a section into private ANC packets (placeholder DID 0x58 / SDID 0x01, **not
  registered**, one constant), `unpack`, a size model of the grain (`fitsInGrain`, `maxSectionBytes`), `CueQueue` (the writer's per-frame cues:
  countdown to the splice frame, stays 5 frames at countdown 0 after it, immediate cues 5 frames, cues that no frame carried for 3 s are dropped as stale),
  `Deduper` (the reader's). `MxlTap` (video, programme output) now writes a second chain `appsrc ! queue ! mxlsink data-flow` **in the same side
  pipeline** (trial rule 1), one grain for every video frame, empty when there is no cue (rule 3), and refuses a cue over the grain size (rule 2).
  `MxlOutput::putScteData` hands cues to that tap. Config: `outputs[n].mxl.data.flowId` (parser, `MxlTapParameters::getDataFlowId`); an output with a
  data flow is in SCTE passthrough mode, without one it carries none. The one-writer rules cover the data flow too.
- **Where:** `transcoder/lib/utpl/include/utpl/transcoder/mxlcarrier.h`, `src/.../mxlcarrier.cpp`, `mxltap.{h,cpp}`, `mxlparameters.h`, `output/mxl.{h,cpp}`,
  `CMakeLists.txt`; `transcoder/app/src/application/config/parser/json/parser.cpp`, `application.cpp`; tests `lib/utpl/test/src/mxlCarrier.cpp` (21 tests),
  `tests/tests/mxl/programme/07-scte-data/test.sh`, `tests/framework/mxl_cues.py` (an independent decoder and checker).
- **Design change from the trial:** the copy carries a countdown in frames, not a splice frame number, because the writer does not know the grain index
  (the clock offset belongs to the sink's pipeline). The reader adds the countdown to the time of the grain it read. The cue's identity for dedupe is
  the section's bytes within a 1.5 s window, not an event id.
- **Result:** C++ ctest 13 of 13 pass (carrier: 21 tests: round trips at every fragment boundary and at the largest section, several cues in a grain,
  a flipped bit and a truncated buffer are reported, countdown at 25, 29.97, 50 and 59.94 fps with +-4 ms of jitter, stale and bounded queue, dedupe).
  Product test: 728 grains in 29 s with **no gap between consecutive grains**, 690 with cues; 24 of 24 sections the engine read from the source while the
  reader ran are in the flow **byte for byte**; every cue's splice time is the same in all its counting-down copies and the first countdown-0 copy is on it;
  cues are in up to 73 grains each; the picture flow and on-air output are unaffected.
- **A mistake on the way:** my first edit of `MxlOutput::putScteData` did not apply (a command run from the wrong directory; its error was lost in a
  backgrounded command) and the first test run showed 0 cues taken. Found with a temporary log line, removed. Two checker mistakes were also fixed (it
  merged separate arrivals of the identical heartbeat section; it could not tell a cue that arrived after the reader stopped).
- **Not done:** the reader (WP2 reader side), config for the input and UI, the alignment against the in-process output (S1), 25/29.97/50 fps in the engine,
  a near-limit cue through the real sink (only the size model is tested), real DID/SDID.
- **Status:** not committed.

### 2026-11-12 · 15 · Product · WP2 reader: an MXL input can take the SCTE-35 data flow
- **What:** `transport.mxl.data.flowId` on an MXL input. `SingleInput::_createMxlFlows` adds `mxlsrc data-flow-id ! queue ! fakesink` with a probe on the
  `mxlsrc` pad that decodes each grain (`mxl_carrier::unpack`), drops repeats (`Deduper`), works out the splice on the channel's time (the grain's running
  time + the input's learned shift + countdown x frame period; the period from the caps, or from the spacing of grains), re-times the section with the
  DASH path's `scheduler::sectionFromBase64` and `buildRunningTimeMap`, and calls the channel's SCTE-35 callback. The data flow is in the reattach watch
  (a reader whose writer has gone is attached again) but not in the learning of the shift, which it follows; it takes cues only once the shift is known.
- **Where:** `input.h` (`MxlInputTransportParameters`), `input/single.cpp`, parser `_parseMxlInput`, `application.cpp` (one-writer check also covers it).
- **Status:** not committed.

### 2026-11-12 · 16 · Product · The muxer bug, and the workaround
- **Found by:** `input/14-scte-alignment` (first version): splices in B were 47,695 s (13.2 hours) from the picture. Logging the numbers showed the
  re-timed `pts_adjustment` was right (8,589,407,258 = 2^33 - 527,334 ticks, a negative adjustment held mod 2^33) and the error appeared after the muxer.
- **Cause (read in the GStreamer 1.24.2 source in `utpl/base-builder:dash-test`, `gstbasetsmux.c` around line 2010):** `section_data[4] |= pts_adjust >> 32`
  never clears the bit that was in the section. With bit 32 set on the way in and a small sum on the way out, the output is 2^32 too large.
- **Workaround:** `scheduler::withSpliceTimeZeroed(raw)` sets the authored splice time of a splice_insert or time_signal to 0 and redoes the CRC; the
  adjustment is then the plain splice time, never negative, and pts_time + pts_adjustment is unchanged. 3 new unit tests in `test/src/scte.cpp`
  (the CRC is valid, the fields are kept; the case without the workaround has bit 32 set and with it not; sections with no splice time are left alone).
- **Not changed:** `sectionFromBase64` and its other users (the DASH input, the scheduler), which can meet the same bug; the muxer itself (a patch needs the
  GStreamer packages rebuilt). Recorded in page 27.
- **Status:** not committed.

### 2026-11-12 · 17 · Product · Schema, API model and channel form for the data flows
- **What:** `openapi.yaml` (`MxlInputTransportParameters.data`, `MxlOutputParameters.data`); channel form: field "SCTE-35 data flow id (optional)", type, schema
  (UUID, not equal to the other flows), both mappers. The API passes `transport.mxl` through untouched, so `data` survives a write (the existing test).
  The output's `mxl.data` has no form field, like the rest of the programme output.
- **Result:** `tsc --noEmit` clean, vitest 73 of 73 (one new test in `configMapper.test.ts`: read, write back, left out when emptied).
- **Status:** not committed.

### 2026-11-12 · 18 · Product · Tests 13 and 14 and their helpers
- **What:** `input/13-scte-data` (A to B through the data flow; B's PID 500 has the source's events with the same contents; at most 3 of A's events missing),
  `input/14-scte-alignment` (a flash-and-beep source with the events, `scte-sync` in `framework/mxl.sh`; the distance from each splice to the latest flash
  frame, in the source, in A's output, in B's), `framework/scte_events.py`, `scte_align.py`; the framework got `MXL_OUT_SCTE` and `MXL_INPUT_DATA_FLOW`.
- **Result (scratch build):** 13 passes: all 11 events (100 to 110) arrive, byte-for-byte in their event fields. 14 passes: **the source puts all 12 events
  520.0 ms after a flash; through MXL they are -37.1 to +2.9 ms from that, not drifting; in-process (A) -22 to +58 ms in the first run, -208 to -128 ms in the
  second**. First version of 14 compared B with A and failed (up to 95 ms), because A is the less accurate of the two; it now compares with the source.
- **Mistakes on the way:** my edit of `MxlOutput::putScteData` did not apply the first time (see entry 14); the first checker merged identical heartbeats;
  `tsp -I ip 127.0.0.1:4444` is refused as "not multicast" (the input is `-I ip 4444`); `ffmpeg -c copy` would lose SCTE-35 (so `tsp` captures); the capture ends in the
  middle of an AAC frame, so the decode check leaves the last seconds out.
- **A loss:** the scratch build directory was emptied when the session restarted; the engine was rebuilt in full (about 25 minutes).
- **Status:** not committed.


### 2026-11-12 · 19 · Product · Regression of the whole MXL suite on the WP2 build
- **What:** `tests/run.sh mxl` in `utpl/transcoder-test:mxl`, as a normal user (`--user`), engine from the scratch build with all WP2 changes
  (carrier, writer, reader, parser, muxer workaround).
- **Result:** 45 tests, 45 passed, 0 failed, 0 skipped (sdk 9, tap 15, programme 7, input 14). It took about 40 minutes of wall-clock time, the tests being real-time
  media runs. The tests run before WP2 (all except programme 07, input 13 and 14) give the same results as before: no regression from the data-flow work.
- **Not covered:** the installed images (scratch build only), and the items listed in page 41 as not tested (other frame rates through the engine with cues,
  interlaced, a cue near the grain limit through the real sink, a writer restart in the middle of a cue, a late reader, a 4-hour soak).
- **Clean-up:** the result file and the test temporary directories are removed; no root-owned files are left.
- **Status:** not committed.

### 2026-11-12 · 20 · Plan · Gate A SCTE row reworded (owner decision)
- **What:** the SCTE row of Gate A changed from "every cue on the same frame as the in-process output" to "every cue within ±1 frame of the splice time the source
  authored, 20 of 20". Reason: entry 18 showed the in-process output is the less accurate of the two (22 to 208 ms off, drifting). Tests S1 and S7 in the plan
  follow the new wording. The owner approved the change on 2026-11-12.
- **Status:** committed with WP2.

### 2026-11-12 · 21 · Product · Engine: an all-MXL channel takes a live input without the three-second buffer
- **What:** `engine.cpp`: `SingleInput(paced)`, `paced` true when the channel has any output that is not of type `mxl`. A channel with only MXL outputs (the main engine of a split channel) gets the input as an SRT, DASH or MXL input does.
- **Why:** the first 1080p split test lost most pictures. A reader of main's video flow saw 135 of 300 pictures in 12 s, in lumps of three seconds. Traced with a temporary log line in the tap (removed): frames reached the tap 2.5 s old, 62 at once, then nothing for 2.5 s. Cause in `single.cpp`: a live input has a multiqueue with a three-second level and underrun/overrun handlers that block and release the data; a clock-paced output keeps it full, and an MXL-only channel has none. Proved: with an SRT input (no buffer) the same channel gave 300 of 300; with a UDP source and an on-air output beside the tap, 278 of 300.
- **Result (scratch build):** 299 of 300 (720p) and 298 of 300 (1080p) pictures in 12 s, flash and beep once a second.
- **Tried and dropped:** a clock-wait (`identity sync=true`) in front of the tap's leaky queue: better (171 of 300) but the lumps stayed.
- **Not changed:** playlist and multi-URI inputs (they would still write lumps); the full regression of the MXL suite was started on this build (entry 24).
- **Status:** not committed.

### 2026-11-12 · 22 · Product · Worker configs and Compose service
- **What:** `config/mxl_split_main.json` (RTMP from the relay to an MXL output with video, audio, data), `config/mxl_split_worker.json` (the MXL input with the same three flows, SRT listener 9093 and HLS, 1080p25 x264 and fdkaac, SCTE-35 passthrough PID 543); `docker-compose.yml`: service `transcoder-worker`, profile `mxl-worker`, domain and `config/` mounted read-only, `restart: unless-stopped`, engine only.
- **Checked:** both files start in the test image (the only errors were the missing RTMP source and the missing domain directory of a bare container); `docker compose --profile mxl-worker config` resolves.
- **Not checked:** the pair brought up from the images (not rebuilt), the engine with the domain mounted read-only.
- **Status:** not committed.

### 2026-11-12 · 23 · Tests · `worker/01-split`, `02-splice-keyframe`, `03-cost`
- **What:** `tests/tests/mxl/worker/` and `tests/framework/mxl_worker.sh` (run P in process, then main plus worker, same source and encoder), `scte_idr.py`; `mxl.sh` got `MXL_OUT_KEYINT` and `MXL_OUT_X264OPTS`. The domain is in `/dev/shm` (a domain on the disk did not keep up at 1080p).
- **Result (1080p25):** streams, frame rate (25.000 both), largest gap (40 ms both), decode and events (8 and 8) match. **Lip-sync -23 against -80 ms (57 ms; 55 to 61 in four runs, two sources; 17 ms at 720p): over the 40 ms limit.** **CPU 0.84 against 1.13 cores (1.35 times; 1.38 at 720p): over 1.25.** **Keyframe at a splice: 0 of 9 in process and 0 of 9 in the worker.**
- **Mistakes on the way:** the first checker counted scene-cut keyframes at the flashes as keyframes at the splices (the flash source makes x264 cut a scene every second), so the keyframe test moved to a moving picture with scene cut off; the first version ran P and the split one after the other on one pass of a source file that plays once (the second run saw no events), so the source is started again for each run; the `ffmpeg` decode check counted the pre-keyframe complaints of a capture that starts mid-GOP and the truncated last AAC frame; `mxl_start_source sync` at 1080p could not be told from a bursty engine until the source's own delivery was read from the UDP packets (smooth).
- **Status:** not committed. `01` and `03` fail on the Gate A limits, on purpose, until the limits are decided.

### 2026-11-12 · 24 · Product · Lip-sync of a split channel: video put on MXL's grain grid, audio wait, exact stamping
- **Why:** `worker/01-split` put the worker's output 55 to 70 ms from the in-process output (limit 40 ms) and the difference changed from run to run.
- **Found by taking the hop apart** (main flows, a worker that only re-publishes, the worker's encoded output; flash-minus-beep over repeated runs, one run at a time):
  - Raw flows of the main engine, six runs: `2 1 54 -11 4 18` ms. `mxlsink` puts a frame in grain `timestamp_to_index(pts + base time + D)` (read from the plugin's source at v1.1.0: `render_discrete.rs`, `clock.rs`) and the sound to the sample, so the video was snapped to a 40 ms grid by an amount that depended on when the source started.
  - The audio tap waited for the video's alignment for 2 s counted from the tap's creation, which for a worker is long before its first picture, so the worker's sound was written unmoved.
- **Changed:** `MxlFrameGrid::alignmentDelay` (the move, from a time in MXL's clock, a rate and a margin; exact 128-bit arithmetic), `MxlFrameGrid::timeOfFrame` (exact stamps at fractional rates, opt-in with `setExact`, so the old tests keep their meaning), `isStarted`; in `MxlTap` an `Alignment` shared by the video and audio taps of one programme (keyed by domain and video flow id): the video tap works the move out when its grid starts (`mxlGetTime` through dlopen, as `MxlRegistry` loads libmxl) and the pictures, the sound and the data grains are written moved by it (the cues still count from the pictures' own places); the audio tap waits up to 2 s from its first buffer. 7 new unit tests (`MxlFrameGridAlignment`), 17 of 17 pass.
- **Result:** raw flows of the main engine, six runs, `2 3 3 4 4 4` (from `2 1 54 -11 4 18`). Worker minus in process on a live source, one run at a time: -34, -22, -20, +23, -16, -25 ms (limit 40 ms). The worker's own offset still moves by up to 60 ms (-18 to -76 ms) with the cause not found; a one-frame outlier shows in the main engine's flows too (44 ms in 1 of 4).
- **Also found:** with the tsp file source over UDP a channel with only MXL outputs writes video 24 ms ahead of the sound (4 of 4), and 0 ms when it also has an on-air output. A live ffmpeg source (UDP or SRT) gives +3 ms. So `worker/01-split` now uses a live source (`sync`), and the SCTE events across a split are checked in `worker/02`. Cause not found.
- **Mistakes on the way:** six runs side by side on the 28-core host starve the encoders and give 435 ms or no pairs, so the measurements are one run at a time; the scratch build directory was emptied again by a session restart (rebuilt in full, about 25 minutes); an experiment script with a bare `wait` hung because it also waited for the engines.
- **Status:** not committed.

### 2026-11-12 · 25 · Product · SCTE-35 carrier version 2: the countdown in milliseconds
- **Why:** with whole frames the splice of a cue could be up to half a frame off and, on a tie, a frame (`input/14`: -37 to +43 ms).
- **Changed:** `mxl_carrier::VERSION` 2, `Cue::countdown` in milliseconds (never negative, clamped at 24 days), the reader adds it to the grain's time; the independent decoder `tests/framework/mxl_cues.py` and its checks (splice time equal across copies to a millisecond; the countdown falls by a frame period per grain). 21 unit tests adapted, pass. `programme/07` and `input/13` pass.
- **Result:** `input/14`, 4 runs: -32 to +46 ms of the source's placement, spans of 28 to 44 ms inside a run, **one run in four had an event 0.8 ms over the 45 ms limit**. The scatter is the engine's own (the demuxer's mapping of a section's splice time; in process it is hundreds of ms), not the carrier's. Recorded in page 27.
- **Status:** not committed.

### 2026-11-12 · 26 · Product · Regression and where the worker stands
- **Regression of the whole MXL suite** (48 tests, scratch build with the grid alignment and the audio wait, before the millisecond countdown): 46 passed, 2 failed: `input/14` (one event 73.5 ms from the source's placement in that run) and `worker/03-cost` (1.39 times). After the millisecond countdown: `programme/07`, `input/13`, `worker/01`, `worker/02` re-run and pass; `input/14` passes in 3 runs of 4. The engine change (entry 21) alone had given 47 of 48.
- **Gate A for the worker (1080p25):** streams, frame rate, gaps and events met; lip-sync met with little room; **CPU not met (1.35 to 1.39 times)**; keyframes at splices not met by the in-process channel either.
- **Status:** not committed.

### 2026-11-13 · 27 · Tests · Failure behaviour of a split channel (WP5): `worker/04` to `08`
- **Why:** the plan's WP5 table (Q8, fail open) had no test.
- **What:** `tests/tests/mxl/worker/04-worker-killed` (T5), `05-worker-slow` (T6), `06-main-restarted` (T9), `07-cue-worker-restart` (T10 and S5), `08-cue-main-restart` (S6). Helpers in `tests/framework/mxl_worker.sh` (`worker_probe`, `worker_main_unaffected`, `worker_cpus`, `worker_cue_restart`, `worker_cue_report`); `mxl_start_engine` takes `MXL_ENGINE_WRAP` (a command put before the engine, used for `taskset`). No engine change.
- **Result (scratch build, 720p25):** all five pass. The main engine did not notice a dead or starved worker (flow lag within 25 ms of before, CPU equal, no error). A killed worker was back 7.2 s after its start. A worker held to 0.03 of a CPU stopped its output, logged one late flow and was back about 1 s after the load went. The worker had its flows 0.5 s after a restarted main engine wrote again. A worker restarted after event 102 carried events 103 to 111, none lost, none twice, within 30 ms of the source's placement. Numbers in page 41.
- **Found:** (1) a worker that is slow but not stopped does not read late (at normal priority with three busy loops it kept 0.21 CPU, its output and logged nothing), so `05` starves it with nice 19; (2) a cue that reaches the source while the main engine is down is lost, because the source sends each section once, and the kill in `08` fell between two events, so that case is described and not measured. Both in page 27.
- **Mistakes on the way:** the first probe was too strict (a 2 s capture can start between keyframes and hold nothing, so the first check now retries); the first main-CPU figure in `05` was measured over the starvation and the probe after it (17 s counted as 12), which made an unchanged main look 40% busier; the two lives of a worker overlap in time (each engine counts its timestamps from its own start), so the cue tests capture each life to its own file; TSDuck prints event ids in hex.
- **Status:** not committed.
