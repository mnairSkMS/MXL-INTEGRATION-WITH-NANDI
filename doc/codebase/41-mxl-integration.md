# MXL and MXL Fabrics Integration

**Status (2026-11-02): Phase 0 (baseline) and Phase 1 are done, and the source tap (Step 2) and the programme output (Step 3) are implemented and tested; an MXL input, an external encoder and Fabrics are not.** MXL
v1.1.0 and its GStreamer plugin are built into `utpl/base`, so every image has them. They are
**Build verified** and **Runtime verified** by the SDK tests in `tests/tests/mxl/sdk/` (see
[Phase 1 results](#phase-1-results)); the engine without MXL is measured in the
[Phase 0 baseline](#phase-0-baseline). The MXL code in `transcoder/` is the source tap
([Source tap](#source-tap-step-2)) and the programme output ([Programme tap](#programme-tap-step-3)); an
MXL input, a flow registry and Fabrics remain **integration gated** (planned, not implemented), and
`api/` has no MXL model (the engine config passes `mxlTap` and `mxl` through untouched).

This checkout exists to test integrating the [Media eXchange Layer](https://dmf-mxl.github.io/mxl/main/)
(MXL) and MXL Fabrics with Nandi (this UTPL engine; "Nandi Transcoder" is the product name in the
operator UI). This page records what the source says about the integration points, the Phase 1
results, corrections to the planning material, the test plan, and the decisions still open.

## Source material

| Document | What it is | How far to trust it |
| --- | --- | --- |
| [`doc/mxl/Nandi_MXL_Fabrics_Technical_Presentation_Agent_Brief.md`](../mxl/Nandi_MXL_Fabrics_Technical_Presentation_Agent_Brief.md) | Brief written to generate the review deck. Holds the MXL/Fabrics primer, four candidate architectures, V210 bandwidth model, tests T0–T15, gates A–E, workstreams WS-00–WS-09 and 18 open questions. Research cutoff 28 Oct 2026. | External MXL facts are cited to the MXL docs. Its Nandi description is labelled "project discussion", not code audit; the [answers from source](#what-the-source-answers) below replace it. |
| [`doc/mxl/Nandi + MXL + MXL Fabrics — Technical Architecture Review.pptx`](../mxl/) | The 47-slide deck generated from the brief, with speaker notes. | Its release-status slides are out of date; see [release status](#mxl-release-status). |

Use the brief's own evidence labels when you extend it (`VERIFIED EXTERNAL`, `TO VERIFY IN NANDI REPO`,
`PROPOSED DESIGN`, …). When a statement moves into this handbook, convert it to the labels in
[00](00-scope-and-evidence.md). Until something here is built and run, use only **Statically
implemented** (for existing code) and **Integration gated** (for MXL).

## MXL release status

Checked against <https://github.com/dmf-mxl/mxl/releases> on 2026-10-30. The page shows day and month
only; the year is inferred.

| Tag | Date | Status | Relevant contents |
| --- | --- | --- | --- |
| v1.1.0 | 9 Sep | **Latest (stable)** | `mxl-fabrics`; `gst-mxl-rs` GStreamer plugin (`mxlsrc`/`mxlsink`) with v210, float32 and ST 291 ancillary data; "the v1.0 Flow API stays compatible". |
| v1.0.2 | 11 Aug | Release | Not mentioned in the deck. |
| v1.1.0-rc1 | 5 Aug | Pre-release | |
| v1.1.0-beta1 | 9 Jun | Pre-release | |
| v1.0.1 | 7 May | Release | |

The deck (slides 1, 2, 10, 12, 45, 46) says v1.0.1 is the latest stable release and that Fabrics and
`gst-mxl-rs` exist only in a pre-release. That is out of date: **pin v1.1.0**. The brief's source
table already names v1.1.0 correctly. MXL ships an Ubuntu 24.04 devcontainer, the same base as
`utpl/base`.

## Phase 1 results

Everything here was measured on 2026-10-30 on the development host (28 cores) with MXL **v1.1.0**
(`gst-mxl-rs` 0.1.0, built from the same tag) in `utpl/base`, GStreamer 1.24.2. Costs are
single-threaded CPU per 1080p frame from `tests/tests/mxl/sdk/09-conversion-cost` (best of three);
they are measurements of this host, not guarantees.

### What was built

| Item | Where | Notes |
| --- | --- | --- |
| MXL SDK, tools, `gst-mxl-rs`, `gst-avsynctest-rs` | `docker/utpl-base/Dockerfile`, stage `mxl-builder` | `ENABLE_MXL=0` builds an empty stage and restores the image as it was (verified: no MXL files, 0.14 MB larger than `:baseline`; the SDK tests then report Skipped). `MXL_TAG`, `MXL_RUST_TOOLCHAIN` (1.90.0) and `MXL_JOBS` are build arguments. |
| Runtime install | stage `base` | Libraries and tools in `/usr/local`; `libgstmxl.so` in the GStreamer plugin directory; no `LD_LIBRARY_PATH` or `GST_PLUGIN_PATH` needed. |
| Tools present | `mxl-info`, `mxl-gst-testsrc`, `mxl-gst-sink`, `mxl-gst-looping-filesrc`, `mxl-data-probe` | **`mxl-video-probe` does not exist in v1.1.0**; the docs describe a later tool. Video is checked with `ffprobe`/`ffmpeg` frame checksums instead. |
| Domain volume | `docker-compose.yml`, volume `mxl-domain` at `/run/mxl` | tmpfs, `MXL_DOMAIN_SIZE` (default 256m). |
| SDK tests | `tests/tests/mxl/sdk/01…09`, helper `tests/framework/mxl.sh` | Run with `tests/run.sh mxl` in `utpl/transcoder-test`. All 9 pass. |
| Host-level checks | `tests/manual/mxl/cross-container.sh`, `engine-smoke.sh` | Need Docker on the host; not part of `run.sh`. |
| Image size | +16 MB per image (about 1.2 % of `utpl/base`) | `libmxl.so` 0.9 MB, `libgstmxl.so` 1.3 MB, tools 5.7 MB. |

The `gst-mxl-rs` crate is built against gstreamer-rs 0.24 and runs on GStreamer **1.24.2** without
changes, so the C API fallback in [42](42-mxl-implementation-plan.md) is not needed so far. The plugin
loads `libmxl.so` at run time from a path recorded at build time (inside the MXL source tree). The
Dockerfile builds from `/opt/mxl-src` and recreates that one path as a symlink in the runtime image;
if MXL is ever built elsewhere, that symlink must follow.

### Findings

| Finding | Evidence | Consequence |
| --- | --- | --- |
| Video v210 and Float32 audio pass through unchanged | Test 02: 90 frames of 1920×1080 at 30000/1001; every frame's checksum equals the no-MXL reference. Test 03: a −6.0 dBFS tone on channel 2 of 6 arrives on channel 2 only, other channels < −80 dB. | The format contract holds; channel order is preserved. |
| Fractional rates run in real time | Test 05: 25, 29.97, 50, 59.94, 60 fps each deliver 90 frames in the expected time (± 5 % + 0.3 s), and `mxl-info` reports the exact rational rate. | No drift at 30000/1001 and 60000/1001 over this short run. Long-run drift is **not** yet measured. |
| Video and audio flows share one timeline | Test 04: first video and audio timestamps in a muxed file differ by 12 ms (about 0.4 frame). | A proxy for sync, not a lip-sync measurement. |
| **The ring size is fixed by the plugin and varies with frame rate** | Test 06: 5 grains (167 ms) at 29.97 fps, 11 grains (184 ms) at 59.94 fps. `mxlsink` has no history property. | The planned `historyMs` setting does not exist in v1.1.0. A 1080p29.97 flow takes **27 MB**, 1080p59.94 **60 MB**, 720p29.97 **12 MB** of tmpfs. Two 1080p59.94 flows (CP1 + CP2) need about 120 MB: more than Docker's default 64 MiB `/dev/shm`. |
| A killed writer is detectable | Test 07: `mxl-info -f` shows `Active: false` after `kill -9`; `mxl-info -g` removes the flow; a new writer with the same id works. | Stale-flow cleanup is possible with existing tools. |
| **A reader on a dead flow waits forever** | Test 07: `mxlsrc` neither errors nor ends within 5 s on an inactive flow. | Every consumer, including a Nandi `mxl` input, needs its own watchdog (for example `Active` polling or a no-data timeout). |
| A stalled reader does not slow the writer | Test 08: reader stopped for 4 s; the 300-frame writer finished in 10.08 s (media time 10.01 s), and the reader kept working. | The property that makes optional consumers safe holds at the plugin level. |
| Cross-container, different user and read-only mount all work | `cross-container.sh`: reader as uid 1001 against a uid 1000 writer; the domain mounted `:ro`. | Consumers do not need write access to the producer's memory. |
| An undersized tmpfs fails visibly | `cross-container.sh`: 8 MB tmpfs → "Failed to create flow: No space left on device", writer exits non-zero. | A failing `mxlsink` posts a pipeline error. The Nandi tap must contain it (fail-open) instead of letting it stop the channel. |
| Existing plugins and the engine are unaffected | `fallbackswitch` and `dlbpatenc` still load (test 01). `engine-smoke.sh` on `:baseline` and `:mxl` gives the same output: AC-3 5.1 + 640×360 H.264 at 25 fps, decoding without errors. | Adding MXL to `utpl/base` did not change the transcoder. |

### Measured cost at 1080p (single thread)

| Operation | CPU per frame | Share of one core at 29.97 fps | At 59.94 fps |
| --- | ---: | ---: | ---: |
| BGRA → v210 (programme tap, CP2 write side) | 7.1 ms | 21 % | 42 % |
| v210 → BGRA (an MXL input into the compositor) | 8.1 ms | 24 % | 49 % |
| I420 → v210 (decoded-source tap, CP1 write side) | 0.6 ms | 2 % | 4 % |
| v210 → I420 (reader towards an encoder) | 0.6 ms | 2 % | 4 % |
| `mxlsink` ring write | 0.3 ms | 1 % | 2 % |
| `mxlsrc` ring read | 0.8–1.0 ms | 3 % | 5 % |

**Conversion to or from Nandi's BGRA compositor canvas costs about 12× the YUV conversion.** A CP1
tap (decoder output is YUV) and a worker reading towards an encoder are cheap. A CP2 tap and an MXL
input pay the BGRA cost. `videoconvert` can use more threads (`n-threads`), which is untested.

### Not yet measured

Long-run (hours) drift; behaviour with a changing source resolution or rate; latency from write to
read (p50/p95/p99); memory bandwidth; Dolby-processed audio; SCTE-35. These belong to Phase 2 and the
gates in the brief.

## Phase 0 baseline

What the engine costs **today**, with no MXL in the path (tests T0/T1). Measured 2026-10-30 with
`tests/manual/mxl/baseline.sh` on the unmodified `utpl/transcoder-test:baseline` image. Raw output is
in [`doc/mxl/baseline-2026-10-30/`](../mxl/baseline-2026-10-30/). One run per row, on a shared
28-core development host, so treat differences under about 5 % as noise.

**Workload:** a generated 1080p25 H.264/AAC source over UDP (`ffmpeg -re`, `testsrc2`) into the real
engine; outputs are x264 `veryfast` + `fdkaacenc` over UDP: 1080p 5 Mbit/s, and a second 720p
2.5 Mbit/s. 15 s warm-up, 30 s measured. Every output was captured and decoded without errors.

| | 1 output (1080p) | 2 outputs (1080p + 720p) | 2 outputs, `:mxl` image |
| --- | ---: | ---: | ---: |
| Engine CPU (cores) | 0.95 | 1.28 | 1.30 |
| Resident memory | 325 MB | 367 MB | 364 MB |
| Threads | 126 | 158 | 158 |
| Elements / pad links in the graph | 108 / 160 | 147 / 211 | 147 / 211 |
| Latency, UDP in → UDP out, p50 / p95 / p99 | 1491 / 1531 / 1532 ms | 1492 / 1531 / 1532 ms | 1492 / 1532 / 1533 ms |
| Latency, UDP in → built-in preview sink, p50 | 804 ms | 805 ms | 805 ms |

- **MXL in the image changes nothing measurable** (third column against the second).
- **Latency is high but steady:** about 1.5 s from the UDP input to each output with a spread of
  about 40 ms. It is the engine's own buffering, not MXL. An MXL tap must be compared against this.
- **A second output costs about 0.33 cores and 42 MB** (a 720p encode plus its branch).
- **The biggest single thread is the 1080p video path at about 0.43–0.45 cores** (the encoder's
  queue thread); the compositor and input demux are each about 0.1 core.
- **Built-in preview branch:** the engine always builds a separate preview sink (`preview-7`,
  `appsink`), unrelated to the outputs, about 0.8 s behind the input.
- **Element inventory of the 1-output graph (from creation logs):** `capsfilter` ×20, `queue` ×10,
  `tee` ×4, `videoconvert` ×4, `decodebin` ×3 (one per input stream), `encodebin` ×2, `compositor`
  ×1, `x264enc` ×1, `fdkaacenc` ×1, `udpsink` ×1, `appsink` ×1 (preview). This matches the tee and
  decode structure described above; no decode is duplicated.

**What it means for an MXL tap.** At this workload, the BGRA → v210 conversion of a CP2 tap is
7.1 ms × 25 fps ≈ **0.18 core**, about **19 % on top of a 1-output channel** (0.95 cores), before the
ring write. A CP1 tap (I420 → v210, 0.6 ms) would add about 0.015 core. Moving the 1080p encode into
an external worker saves none of the 0.43 core, which only moves.

**How the graph was captured.** The engine has no DOT export, and GStreamer 1.24.2 here lacks the
`pipeline-snapshot` tracer. The script instead reads `GST_DEBUG=GST_ELEMENT_FACTORY:4,GST_PADS:4`
logs (element creation and pad links, listed in `pad-links-*.txt`) and the `latency` tracer
(`GST_TRACERS="latency(flags=pipeline)"`), which the engine honours without any code change. This is
a link list, not a picture; render it if needed.

**Not covered:** 50/60 fps, interlaced input, Dolby audio, graphics overlays, SRT/RTMP inputs,
hardware encoders, long runs, and memory bandwidth. Add them before using these numbers for gates.

## Source tap (Step 2)

`inputs[n].mxlTap` ([05](05-configuration-reference.md#input-mxl-tap)) copies the **decoded source** of
an input (CP1, before the compositor and before any graphics or audio processing) into MXL flows. It
was built and tested on 2026-10-30. **Runtime verified** by `tests/tests/mxl/tap/`, first against a scratch
build and then against the engine installed in the rebuilt image (see
[the image check](#source-tap-verification-against-the-installed-engine)).

### How it works

| Part | Where | What it does |
| --- | --- | --- |
| Settings | `MxlTapParameters` in `input.h`; parser block in `parser.cpp`; `openapi.yaml` | `domain`, `video.flowId`, `audio.flowId`, `audio.channels`. A wrong block is refused at start. |
| Branch in the graph | `BaseInputStream::linkSrcPad` / `_createMxlTap` in `stream/input/base.cpp`, class `MxlTap` in `mxltap.cpp` | A leaky `queue` and an `appsink` on the tee of a decoded video or audio stream, built like the input preview. It can drop frames; it cannot hold the channel back. |
| Side pipeline | `MxlTap::_start`, `_describe` | `appsrc → [deinterlace, only for an interlaced picture] → videoconvert → v210 → mxlsink`, or `appsrc → audioconvert → audioresample → audiorate → F32LE → mxlsink`. It uses the channel's clock and base time, so buffer times mean the same moment in both pipelines, and the plugin maps them to MXL (TAI) time. Buffers are passed by reference (only the header is copied to change the time). The video `appsrc` holds up to 30 frames (at most 128 MB) while the writer waits for their time. |
| Frame grid | `MxlFrameGrid` (`mxlframegrid.cpp`), unit test `utpl_mxl_frame_grid_test` | Video frames are stamped `origin + n × period`, with `n` strictly increasing, because the MXL writer refuses a frame that falls in, or before, the slot of the last one. See the findings below. |
| Failure handling | `MxlTap::onFeed`, bus handler, `_checkIdle` | An error inside the side pipeline is logged and never reaches the channel. The pipeline is replaced on the next buffer, but not more than once every 5 s per tap. A new picture size or frame rate starts a new video flow. A tap whose stream delivers nothing for 3 s closes its flow and opens it again when data returns. A line of statistics is logged every 30 s. |
| One writer per flow | `MxlTap::create` | A second stream asking for a flow already in use is skipped with a warning. |
| Redundant inputs | `StandardInput::probe`, class `FallbackStream` | The tap sits on the stream **after the switch** between the two sources; the two single inputs get no tap. A failover is invisible in the flow. |
| Playlist inputs | `PlaylistInput::probe`, `BaseInputStream::setMxlTap(…, keepWhileUnlinked)` | The tap sits on the playlist's own audio and video streams and survives the unlink and link that every change of item makes; the items get no tap. |

### Results (720p25 H.264 + AAC source into a 720p25 channel, unless stated)

| Check (test) | Result |
| --- | --- |
| Flow content (`01`) | Video flow 1280×720 at 25/1, 50 of 50 frames; audio flow 48 kHz stereo with the 1 kHz tone; on-air output has audio and video and no decode errors. |
| Lip-sync (`02`) | Flash-to-beep offset: generated source **0 ms**; MXL flows **+14 ms** (earlier runs: +2, +31); the engine's on-air output **−41 ms**. 14 pairs in the MXL flows. The resolution is about one frame (40 ms), so the MXL figure is within one frame of the source. Limit used: 80 ms (provisional, Gate A). |
| On-air output unchanged (`03`) | Same streams, zero decode errors, same frame count with and without the tap. Engine CPU **0.38 → 0.40 cores** (an earlier run: 0.36 → 0.42). |
| Reader faults (`04`) | Readers stopped for 6 s and then killed with SIGKILL: largest gap between on-air video packets 40 ms (one frame), audio 21 ms, no decode errors; a new reader could join afterwards. |
| Unusable domain (`05`) | A path that cannot be created and a read-only directory: the engine keeps running and the on-air output is clean; the failure is logged; 8 start attempts for two taps in about 24 s. |
| Bad configuration (`06`) | Not a UUID, no domain, no flow, equal ids, 9 channels and a missing `flowId` are each refused at start with a message naming the key; a correct block starts. |
| Source changes size (`07`) | 720p → 1080p while running: the video flow followed (1280×720 → 1920×1080) and the engine kept running. |
| Interlaced source (`08`) | 1080i25 (top field first): flow 1920×1080 at 25/1. Engine CPU **0.57 → 0.63 cores** with the tap, including the deinterlace (`method=linear fields=top`). |
| Parameters (`utpl_mxl_tap_parameters_test`) | Defaults, enabling rules, UUID validation, survives copying `InputParameters`. The whole C++ suite passes (11 of 11, six repeat runs). |
| Redundant inputs (`09`) | Two UDP sources, 1 kHz and 2 kHz. The one on air is stopped; the audio flow follows to the other tone within the engine's switch time and the video keeps moving (at least 40 different pictures in 50 frames). The video tap is started **once**: a failover does not restart it. Which source goes on air first is a startup race (whichever fills its buffer first), so the test finds out and stops that one. |
| Playlist (`10`) | Two 14 s items (1 kHz, then 2 kHz). One flow carries both: the tone changes, the picture keeps moving, and the video tap starts once. Before the tap was kept through the unlink and link of an item change it started once per item and a reader attached to the old flow hung. |
| Frame rates (`11`) | 1080p50, 720p59.94 and 1080p29.97: each flow has the source's rate (`50/1`, `60000/1001`, `30000/1001`) and size, and a reader gets 100 frames in about the real-time duration plus start-up (2.43 s for 2.00 s; 2.15 s for 1.67 s; 3.58 s for 3.34 s). |
| AC-3 audio (`12`) | Stereo AC-3: the tone is on both channels. 5.1 AC-3 with a tone on the centre channel only: a 6-channel flow has it on channel 2 only (−18.1 dB, the other five at −91 dB: the layout is exact). The same source into a 2-channel flow: tone on both channels at −31.2 dB, because GStreamer's downmix matrix is normalised and puts a centre-only tone about 13 dB below its level. The Dolby SDK encoder and decoder paths are **integration gated** and were not exercised. |
| SCTE-35 (`13`) | A source with `splice_insert` events on PID 500: the engine reads them (6 non-null sections logged as "Acquired SCTE-35 section"), exactly two taps start (video and audio; the data PID takes none), both flows have the right content and the on-air output is clean. The channel's SCTE-35 output PID stays empty for this re-encoded channel with **or without** the tap (a control run), so the evidence is the engine's own log. |
| Source dropout (`14`) | A 1.5 s gap: the taps are not restarted and new readers get 50 different pictures and the tone. An 8 s gap: both flows are closed after 3 s without data and opened again when the source returns; new readers get picture and tone. Four tap starts in all (two at the start, two on return). |
| UI and API round trip | A hand-written `mxlTap` survives the channel form's save (`frontend/tests/configMapper.test.ts`, edited and unedited form) and the API's config write (`api/tests/config_test.py::test_an_inputs_mxl_tap_block_survives_a_write`). The form has no fields for it. |
| Frame grid (`utpl_mxl_frame_grid_test`) | 10 unit tests: consecutive slots, jitter, never a slot twice or an earlier one (5000 random frames), a moved frame is healed by a drop, repeats dropped, gaps kept, a step back by over a second asks for a restart, 29.97 never shares a slot over 100000 frames. |

The extra CPU of the source tap was measured at 720p25 (+0.02 to +0.06 core), 1080i25 (+0.02 to +0.11),
1080p29.97 (+0.05 and +0.18), 720p59.94 (+0.11 and +0.15) and 1080p50 (+0.15 and +0.40); the two figures are
two runs of the same test. This host (28 cores, shared) varies by 0.1 to 0.2 core from run to run, so read
them as ranges: the source tap costs roughly 0.05 to 0.4 core, rising with pixels per second.

### Findings made while building it

- **Audio needs `audiorate`.** `mxlsink` places each audio buffer at the sample index of its own
  timestamp. Decoded AAC frames carry timestamps rounded to the 90 kHz clock, so without `audiorate`
  neighbouring frames overlap or leave gaps, and `mxlsink` returned a flow error after a few dozen
  buffers. The Phase 1 audio tests used exact timestamps and could not show this.
- **The audio ring is not full when a flow is young.** In a reader pipeline that fed the video and
  audio flows into one muxer, the recorded audio began with about 9 s of silence, although the same
  flow read alone had every beep. The cause is **not established** (a muxer holding one branch while
  it waits for the other is the likely candidate); test `02` reads each flow into its own sink and
  measures from their timestamps instead.
- **Timestamps of the tap are the source's running time**, not the on-air presentation time (the
  engine adds about 1.5 s of buffering between them, see the baseline). Flows from different inputs
  or taps are comparable with each other, not with the on-air output.
- **The on-air output is 42 ms ahead in picture with this synthetic source** (flash before beep). It
  is within one frame of the resolution, so this is an observation, not a defect.
- **Test source offsets:** `silencedetect` times are relative to the audio stream's start while
  `ffprobe` picture times are absolute, and ffmpeg starts both about 1.4 s into the file; adding the
  audio start time (done in test `02`) removes a phantom 459 ms.

### Findings made while closing the gaps (2026-11-02)

Closing the gaps found four faults in the first version of the tap. Each one hid behind a test that used
a steady live source.

- **The MXL writer refuses a frame in the same slot as the last one, or an earlier one.**
  `PosixDiscreteFlowWriter::openGrain` returns `MXL_ERR_INVALID_ARG` for an index at or below the last
  committed one, and `mxlsink` turns that into a flow error with no message. Frame times inside the
  channel jitter (34, 40, 32, 44 ms apart when a source was cut off), so two frames could land in one
  40 ms slot and the side pipeline died. `MxlFrameGrid` now puts every frame on `origin + n × period`.
  A frame that collides takes the next slot; if the stream stays late after three such frames, one frame
  is dropped, which puts it back in step without leaving an empty slot. Without that "healing" the
  video stayed one slot (40 ms) behind the sound for good, and the lip-sync test read +55 ms.
- **A leaky `appsrc` holds one frame by default.** It stops at `max-bytes` (200 KB) as well as at
  `max-buffers`, and one raw frame is larger than that. While the writer waited for a frame's time every
  other frame was dropped, which left every other slot of the ring flagged invalid and made new readers
  hang. The side pipeline sets `max-bytes` itself (up to 128 MB, never less than three frames).
- **Sources that run ahead of the clock need a deep enough queue.** A playlist item read from a file
  reached the tap 0.68 s (video) and 0.36 s (audio) before its timestamp, so the writer waits that long
  per frame and the queue has to hold that many frames: 30 frames now (a live source needs none).
- **A tap on a stream that is unlinked and linked again** (a playlist's, at every item change) was
  destroyed and rebuilt each time, closing and recreating the flow. The tap is now kept through that,
  and a stream that delivers nothing for 3 s closes its flow, so a flow that is not written never looks alive.
- **By the plugin's source** (read, not measured), `mxlsrc` returns an error when it reads a slot that was
  skipped, since the writer flags those invalid. A reader of a tapped flow should be ready to start again
  after a gap in the source.
- **`tsp` details for SCTE-35 test sources:** `spliceinject` hangs when more than 12 events are queued
  from one file, and `ffmpeg -c copy` re-wraps SCTE-35 sections as PES and loses them; the sections must be
  played out with `tsp -P regulate`.

### Not covered

Passthrough streams (never tapped); more than one audio track (only the first is tapped); the Dolby SDK
paths (**integration gated**); a channel with a second input (the alternate source of SCTE ad insertion)
with a tap on either input; hardware decoders (the decoders here are software: a tap that holds up to 30
frames would hold decoder surfaces); and a run of hours (the 20-minute soak is in [the soak result](#soak-result); the
memory trend of +14.8 MB per hour needs a longer run to judge).

### Source tap verification against the installed engine

The images were rebuilt (`utpl/base:mxl` → `utpl/transcoder:mxl` → `utpl/transcoder-test:mxl`, which compile
the code into `/opt/utpl/bin/transcoder`) on 2026-10-30 and again on 2026-11-02 after the gaps were closed,
and the suites were run with **no scratch build mounted**:

| Suite (2026-11-02) | Result |
| --- | --- |
| `tests/run.sh mxl` (SDK + tap + programme) | **29 of 29 pass**: 9 SDK, 14 tap, 6 programme. |
| Lip-sync (`tap/02`) | Source 0 ms, MXL flows **+19 ms**, on-air −43 ms. |
| Engine CPU without / with the tap | 0.39 / 0.42 cores (720p25, `tap/03`); 0.53 / 0.61 (1080i25, `tap/08`); 1.75 / 1.96 (1080p50), 0.80 / 0.89 (720p59.94), 1.10 / 1.17 (1080p29.97) (`tap/11`). |
| Reader faults (`tap/04`) | Largest on-air gap 40 ms video, 21 ms audio. |
| Conversion cost per frame (`sdk/09`, 1080p, single thread) | BGRA → v210 7.75 ms (the programme tap); I420 → v210 0.72 ms (the source tap); `mxlsink` write 0.55 ms; `mxlsrc` read 1.05 ms; v210 → BGRA 8.57 ms; v210 → I420 0.81 ms. |
| C++ CTest (scratch build) | 12 of 12 pass, including `utpl_mxl_frame_grid_test`. |

Across all the runs the measured cost of the source tap is **+0.02 to +0.4 core**, rising with pixels per
second and varying by 0.1 to 0.2 core between runs of the same test on this shared host; these are
indications, not exact figures.

### Soak result

`tests/manual/mxl/tap-soak.sh 20` ran the source tap for 20 minutes on the installed engine (2026-11-02;
720p25 flash-and-beep source, video and audio flows; the series and the output are in
[`doc/mxl/soak-2026-11-02/`](../mxl/soak-2026-11-02/)). Once a minute it compares the newest position of each
flow with the system clock (the mean of ten samples), and it measures lip-sync through the flows at the start,
in the middle and at the end.

| Measure | Result |
| --- | --- |
| Video flow behind the clock | Mean **123.5 ms**, range 118.8 to 130.2 ms, trend **+0.9 ms per hour**. No drift. |
| Audio flow behind the clock | Mean 322.9 ms, mostly about 305 ms with bursts to 333, 353, 388, 337, 353, 370 and 333 ms in single minutes. The least-squares trend is −21 ms per hour, which is the scatter of the bursts, not a drift. The cause of the bursts is not established. |
| Lip-sync through the flows | **+7 ms** at the start, **+2 ms** in the middle, **+7 ms** at the end (14 pairs each; the source is 0 ms). |
| Engine memory | 247.6 MB at minute 1 to 252.2 MB at minute 20: **+14.8 MB per hour** on a straight line. Twenty minutes cannot tell a leak from warm-up. |
| Engine CPU | Mean **0.37 core**, one minute at 0.54 and one at 0.44. |
| Tap starts and errors | 2 and 0: neither tap restarted. |

This covers 20 minutes, not hours: the once-sampled clock offset of the plugin showed no drift in that time,
and the memory line needs a longer run (hours) to be judged. The Gate A limits are not agreed, so the run
judges only hard faults.

## Programme tap (Step 3)

An output of `"type": "mxl"` ([05](05-configuration-reference.md#output-mxl)) publishes the **finished
programme** (CP2: after the compositor, graphics and audio processing, before any encoder) as MXL flows.
It was built and tested on 2026-11-02. **Runtime verified** by `tests/tests/mxl/programme/`, first against
a scratch build and then against the installed engine (see
[the image check](#programme-tap-verification-against-the-installed-engine)).

### How it works

| Part | Where | What it does |
| --- | --- | --- |
| Settings | `OutputType::MXL` and `OutputParameters::getMxl()` (`output/parameters.h`); `MxlTapParameters` now in `mxlparameters.h`; parser branch `type: "mxl"`, helpers `_parseMxlFlows` and `_parseMxlVideo`; `application.cpp` validation; `openapi.yaml` `MxlOutputParameters` | `mxl.domain`, `mxl.video.{flowId,width,height,rate}`, `mxl.audio.{flowId,channels}`. The parser makes one video and one audio stream with the new `Codec::MXL`, so the channel treats them like any other output stream. A wrong block is refused at start with a message that names the key. The input's block and the output's share the validation. |
| Joining a processor | `channel.cpp` (the loop that finds a processor for each output stream) | Unchanged. An MXL video stream of the same size and rate as an on-air output joins that processor and shares its canvas; any other size or rate creates a processor of its own. Audio of the same channel count and rate shares a processor. |
| Raw flowline | `MxlFlowline` (`channel/processor/transcoder/mxlflowline.cpp`); the `Codec::MXL` branch in `VideoTranscodingProcessor::createFlowLine` and `AudioTranscodingProcessor::createFlowLine` | An `identity` and a ghost pad on the channel bin. Nothing is encoded. `compatible()` is always false: each MXL stream ends in one flow. |
| Output | `MxlOutput` (`output/mxl.cpp`), created by `Engine` | A bin with one `MxlTap` per stream (the first video and the first audio). If the tap cannot be created (no `mxlsink`, or the flow is already written) the stream goes to a `fakesink` and a warning is logged, so the flowline is never left unlinked. |
| Tap | `MxlTap` | The same class as the source tap: a leaky `queue` and an `appsink` in the channel, and a side pipeline with `videoconvert → v210 → mxlsink` (or the audio path). The processors' canvas is BGRA, so the conversion to v210 is done in the side pipeline; a progressive picture does not pass through a deinterlacer. The frame grid, the idle watchdog and the statistics line apply as for the source tap. |

### Results (720p25 H.264 + AAC source into a 720p25 channel, unless stated)

| Check (test) | Result |
| --- | --- |
| Flow content (`01`) | Video flow 1280×720 at 25/1, 50 of 50 frames with at least 40 different pictures; audio flow 48 kHz stereo with the 1 kHz tone (−21.1 dB; the 4 kHz band −54.1 dB); the channel's ordinary output still has audio and video and no decode errors. |
| On-air output unchanged, CPU (`02`) | Same streams and picture size with and without the MXL output; video frame counts within 25 of each other over two 10 s captures; no decode errors. Engine CPU **0.37 → 0.53 cores** on the scratch build and **0.38 → 0.48** on the installed engine (+0.10 to +0.16 core: a shared canvas, BGRA 720p25 to v210 and the audio branch). |
| Lip-sync (`03`) | Flash-to-beep offset: generated source **0 ms**; programme flows **−1 ms** (installed engine: **+5 ms**); the on-air output **−65 ms** (−42 ms). The programme flows are taken before the encoders, so they stay with the source; the on-air offset is added by the AAC encoder (about 43 ms) and the muxer. Limit: 80 ms from the source (provisional, Gate A). |
| Own size (`04`) | A 720p25 source with an on-air output of 720p and an MXL output of 1920×1080 at 25: the programme flow is **1920×1080** with a real picture (mean luma 122.8; black would be near 16), and the on-air output stays 1280×720 and clean. The extra canvas costs the compositor and scaler for 1080p; its CPU was not measured separately. |
| Source stops (`05`) | With the source gone for 8 s, the programme video head still advanced **125 of 125 grains** in 5 s: the channel keeps writing its picture. The audio head advanced 272 508 samples in the same 5 s (240 000 at real time): the channel's silence fill ran ahead for a while. After the source returned, new readers got 50 different pictures and the tone. |
| Bad configuration (`06`) | Eight wrong blocks (no `mxl`, no domain, not a UUID, no flow, equal ids, 9 channels, negative width, a rate that is not a number or fraction) are each refused at start with a message that contains the key. |
| UI | An output of type `mxl` is not shown in the channel form and a save keeps it exactly as it is, even when every form output is deleted (`frontend/tests/configMapper.test.ts`). |

### Findings

- **The flowline must hand its output to the output through a ghost pad on the channel bin.** The first
  version linked the `identity`'s own pad to the output's bin and GStreamer refused
  (`GST_PAD_LINK_WRONG_HIERARCHY`); the encoding flowline does the same with `addPad`.
- **The application's own validation (`application.cpp`) rejects an unknown output type** before the
  engine starts; a new type needs a case there as well as in the parser and the engine.
- **The audio head ran 13 % ahead of real time for part of a source outage** (see `05`). The channel
  produces the silence; the tap writes it as it comes. It is not a fault of the tap, but a reader that
  aligns by the flow's head should not assume exactly one second per second after an outage.

### Programme tap verification against the installed engine

With the images rebuilt on 2026-11-02 and no scratch build mounted, `tests/run.sh mxl/programme` passes
**6 of 6**. Flow content: tone at −21.1 dB, the 4 kHz band at −46.4 dB. Engine CPU 0.38 without and 0.48
cores with the MXL output (720p25, shared canvas). Lip-sync: source 0 ms, programme flows +5 ms, on-air
−42 ms. Own size: 1920×1080 flow from a 720p source, mean luma 122.0. Source stops: video head 125 of 125
grains in 5 s, audio head 272 803 samples. Programme-tap conversion cost: BGRA → v210 7.75 ms per 1080p frame
(`sdk/09`), which is why a 1080p programme flow is the case to watch against the CPU limit.

### Not covered

Dolby-processed audio (the equivalent tap on the Dolby connector; **integration gated**); more than one
audio stream; interlaced programme output (the canvas is progressive); graphics overlays in the picture
(the tap is after the compositor, so it should carry them, but this was not looked at); 1080p50 or
59.94 programme flows; SCTE-35 (it does not travel in a flow; decision Q7 is open); HLS or DASH outputs
alongside the MXL output; runs of hours.

## What the source answers

The brief lists items as "to verify in repo". Each one below was checked against this checkout's
source on 2026-10-30. Paths are relative to `transcoder/lib/utpl/src/utpl/transcoder/`. Status:
statically implemented (read, not run).

### Decode is already shared

`SingleInput` creates one `decodebin` per input stream (`input/single.cpp:280`). Before it is a
`tee` that splits passthrough from decoded media (`input/single.cpp:238`, named
`<kind>-passthrough-splitter`). Further down the graph there are more `tee`s:

| Where | Source | When it exists |
| --- | --- | --- |
| After the connector's raw-video selector, one branch per requested format | `channel/connector/complex/video.cpp:249` | Always, in the complex video connector |
| Same, for audio | `channel/connector/complex/audio.cpp:226` | Always, in the complex audio connector |
| Before the flowlines (encoders) | `channel/processor/transcoder/video.cpp:118` | Only with more than one flowline |
| After an encoder, to its outputs | `channel/processor/transcoder/flowline.cpp:328` | Only with more than one output sharing the encoder |

The deck's "Pattern A" (each output decodes separately) does not happen inside one Nandi channel. For
MXL, the case is **process isolation and external consumers** (deck slide 5's fallback argument),
not saving decodes. Benchmarks (T1) must report this, as the brief's section 7.3 requires.

### Where a processed-programme tap (CP2) fits

- Graphics are composited in the **connector**: `Compositor` is created at
  `channel/connector/complex/video.cpp:211`, fed by `fallbackswitch`. The processor has its own
  `_compositor` member and branches on it (`channel/processor/transcoder/video.cpp:88`, `:110`), but
  nothing ever assigns it. That path is unreachable.
- `VideoComplexConnector::getPad(params)` (`connector/complex/video.cpp:50`) already serves several
  raw-video formats. For each one it builds a `videoconvertscale` (`:284`) and `videorate` (`:315`)
  branch off the post-compositor `tee`. An MXL tap could register as one more consumer with
  `format=v210`, the output size and the frame rate, and reuse that conversion. This is a
  **proposed design**, not code.
- Audio has the same shape (`connector/complex/audio.cpp:226`). Dolby processing has its own
  connector; see [11](11-audio-and-dolby-processing.md).

### Interlace

`deinterlace` runs before the compositor (`connector/complex/video.cpp:151`), so a CP2 tap is
already progressive. That suits MXL's progressive-only V210 grains. Interlacing is re-applied per
output in the flowline (`processor/transcoder/flowline.cpp:169`). A tap on the decoded source
(CP1), before the connector, would still see interlaced media and needs its own deinterlace policy.

### Clock

Nothing in `transcoder/lib` or `transcoder/app` sets a pipeline clock, so GStreamer's default
monotonic system clock is used. MXL indexes grains on the TAI timeline (SMPTE ST 2059-1 epoch).
**`gst-mxl-rs` already does this mapping** (read in `rust/gst-mxl-rs/src/clock.rs` at v1.1.0, not
yet exercised in a long run). The MXL elements do not provide a pipeline clock. The pipeline keeps
its own clock, and each MXL element uses a constant offset `D = MXL time now − pipeline clock now`,
sampled once and **shared by all MXL elements in the same pipeline**. A writer adds `D` to a buffer's
timestamp to get the MXL time and grain index; a reader subtracts it. So a Nandi tap needs no separate
clock adapter, and the video and audio taps stay on one timeline. The risk is that `D` is sampled
only once: if the pipeline clock and the TAI clock drift apart over hours (NTP steps, slewing),
the mapping is not re-sampled. That is the thing to test in a long run.

### Source loss

The video and audio connectors both use `fallbackswitch` with a 0.5 s timeout
(`connector/complex/video.cpp:160–162`, `connector/complex/audio.cpp:141–143`). A tap after it
carries the fallback picture instead of stalling. Audio during an outage is a known gap: a live
input's output carries no audio while the source is absent (see
[27](27-known-gaps-contradictions-and-remediation.md)). An MXL audio flow would inherit that gap.

### Inputs

The input type is chosen by URI scheme in `SingleInput` (`input/single.cpp:486` onward): file,
`ndi://`, `udp://`, `srt://`, HTTP and HLS (`souphttpsrc`/`hlsdemux`, from about line 1089), plus
the RTMP and DASH branches described in [07](07-inputs-and-ingest.md). An `mxl` input (WS-02)
would be a new scheme branch there. It would also need parser, parameter, schema, API and UI support
(see [the contract](#configuration-contract)).

### Multiple audio flows

Every output stream takes the **first** audio stream of the programme input (see
[27](27-known-gaps-contradictions-and-remediation.md)). Named per-language or per-layout MXL audio
flows (brief section 6.3) depend on fixing that first.

## Environment constraints

| Constraint | Evidence | Consequence |
| --- | --- | --- |
| `/dev/shm` is Docker's default 64 MiB (67.1 MB) | No `shm_size` or `ipc` setting in `docker-compose.yml` | **Resolved for MXL by a dedicated tmpfs volume** (`mxl-domain` at `/run/mxl`, 256m by default); `/dev/shm` is left alone. The measured ring is 60 MB for one 1080p59.94 flow, so a single such flow nearly fills the default `/dev/shm` and two do not fit. See [Phase 1 results](#phase-1-results). |
| GStreamer 1.24.2 (image and host) | [00](00-scope-and-evidence.md); `gst-launch-1.0 --version` | **Resolved:** `gst-mxl-rs` v1.1.0 builds and runs on 1.24.2 without changes. |
| Rust toolchain | MXL requires Rust ≥ 1.87 (edition 2024). `mxl-builder` installs 1.90.0 through rustup; `dashsink2` uses 1.99.1 in the other stage; the remaining plugins use Ubuntu's cargo. | **Resolved:** a separate rustup toolchain in the MXL stage, as for `dashsink2`. |
| Dolby SDK | Static libraries vendored under `transcoder/plugins/gstreamer/dolbypat/ext/*/lib/linux/x86_64/` | The transcoder build is self-contained. Keep Dolby processing inside the licensed engine (brief section 6.3). |
| Images | `:baseline` images (`utpl/base`, `devel`, `transcoder`, `transcoder-test`) were built from this checkout **before** any MXL change on 2026-10-30. `:mxl` images were built after. | `:baseline` is the "today" reference for Phase 0 measurements; `:dash-out` images from 2026-10-27 are not used as evidence ([00](00-scope-and-evidence.md)). |
| Git | A repository was initialised on 2026-10-30; the first commit is the tree as received. | Each MXL step is its own commit and can be reverted. |
| Legacy test framework | `tests/run.sh proto/…` and `overlay/…` fail on both `:baseline` and `:mxl` in this environment: the framework could not fetch its Big Buck Bunny source (a 0-byte `tests/media/bbb.mov` appeared) and its helper processes (`tsplay`, an HTTP server) fail to start. | Not caused by MXL. Existing suites cannot be used as a regression check here until fixed; `tests/manual/mxl/engine-smoke.sh` is used instead. |

## Configuration contract

[AGENTS.md](../../AGENTS.md) treats the JSON parser, parameter classes, schema and examples, API
models and tests as one contract. A raw MXL output does not fit the current output model, which goes
codec → `encodebin` → mux → sink ([13](13-outputs-muxing-and-delivery.md)): it has no codec and no
mux. Agree its shape before writing code. For example, decide whether it is a new output protocol
that bypasses the flowline, or a connector-level tap configured next to the outputs. The same
applies to an `mxl` input.

## Test plan

These are the brief's tests (section 10.1), placed in this repository. T2 and the Phase 1 checks are
implemented and pass; the rest are planned.

| ID | Test | Where it will live | Depends on |
| --- | --- | --- | --- |
| T0 | Baseline channel: DOT graph, CPU, RAM, latency | `tests/` plus a config in `config/` | Image built from this checkout |
| T1 | Audited decode sharing / `tee` topology | DOT export from T0 | T0 |
| T2 | Standalone `mxlsink` → `mxlsrc`, V210 and Float32, 30000/1001, checked with `ffprobe` | `tests/tests/mxl/sdk/01…09` (**9 of 9 pass**), run in `utpl/transcoder-test`; cross-container and read-only checks in `tests/manual/mxl/cross-container.sh`. MXL is built into `utpl/base`; there is no separate image. | Done |
| T3–T4 | Nandi publishes CP1 (source) / CP2 (programme) | `tests/tests/mxl/tap/` (CP1, 14 tests) and `tests/tests/mxl/programme/` (CP2, 6 tests), both implemented and passing | Done for the cases listed in their sections; Gate A limits not agreed |
| T5–T7 | External QC + encoder readers; consumer restart; slow reader / overrun | `tests/tests/mxl/` | T4, fail-open/closed decision |
| T8–T10 | Discontinuity and 30000/1001 timing; multi-audio and Dolby; SCTE-35 during worker restart | `tests/tests/mxl/input/` and `tests/tests/mxl/` | Clock adapter, multi-audio fix, SCTE timeline decision |
| T11–T15 | Fabrics TCP then RDMA; hardware encoders; multi-channel soak; host failure; permissions | Later | Second host, NICs, hardware |

Follow [23](23-testing-and-validation.md) for media assertions: a zero exit status is not a pass.
Proposed acceptance gates A–E are in the brief, section 10.4. Agree their numeric limits **before**
testing starts.

## Workstreams and order

The element-by-element changes, their performance cost, and the per-layer implementation steps are
in [42](42-mxl-implementation-plan.md). As agreed on 2026-10-30, MXL is built into the existing images
(no separate test image), and the current codebase may be changed wherever integration and testing
need it.

1. **Phase 0 / WS-00 (done for the reference workload):** `git init`; `:baseline` images built
   **before** any MXL change; baseline CPU/RAM/latency and graph link list measured (T0, T1).
2. **WS-01 (done):** MXL built into `utpl/base` (inherited by every image); compose tmpfs domain;
   SDK qualification tests in `tests/tests/mxl/sdk/` (T2). Phase 0 baseline measured (T0/T1, see
   above). Not done: Gate A numbers have not been agreed.
3. **WS-02 / WS-03:** Nandi `mxl` input and CP1/CP2 output taps. Proposed defaults for the open
   decisions and the configuration shape are in [42, section 7](42-mxl-implementation-plan.md#7-proposed-defaults-for-the-open-decisions)
   and need approval first.
4. **WS-04 – WS-09:** audio/Dolby flows, external workers, SCTE event plane, registry and
   observability, Fabrics, accelerators. See the brief, section 11.

## Open decisions

These are from the brief's section 12 / deck appendix A8, plus the clock question above. Each blocks
WS-02/03:

- Where does the canonical SCTE-35 event timeline live, and how does an independent worker receive it? (Q7)
- Does a failed MXL tap fail the output open or closed, per output type? (Q8)
- What condition stops the project if MXL adds overhead without value? (Q18)
- Does the pipeline keep the monotonic clock with a TAI mapping in the adapter, or switch to a realtime/TAI pipeline clock?
- What is the configuration shape for an MXL output and input?

Record each answer here when it is made. Record any mismatch it creates in
[27](27-known-gaps-contradictions-and-remediation.md).
