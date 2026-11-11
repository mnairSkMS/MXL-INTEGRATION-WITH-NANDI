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
