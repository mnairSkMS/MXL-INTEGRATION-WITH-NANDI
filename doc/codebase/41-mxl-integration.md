# MXL and MXL Fabrics Integration

**Status (2026-10-30): Phase 0 (baseline) and Phase 1 are done, and the source tap (Step 2) is implemented; the programme tap, an MXL input and an external encoder are not.** MXL
v1.1.0 and its GStreamer plugin are built into `utpl/base`, so every image has them. They are
**Build verified** and **Runtime verified** by the SDK tests in `tests/tests/mxl/sdk/` (see
[Phase 1 results](#phase-1-results)); the engine without MXL is measured in the
[Phase 0 baseline](#phase-0-baseline). The only MXL code in `transcoder/` is the source tap
([Source tap](#source-tap-step-2)); an MXL input, the programme tap, a flow registry and Fabrics remain
**integration gated** (planned, not implemented), and `api/` has no MXL code.

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
| Side pipeline | `MxlTap::_start` | `appsrc → (deinterlace) → videoconvert → v210 → mxlsink`, or `appsrc → audioconvert → audioresample → audiorate → F32LE → mxlsink`. It uses the channel's clock and base time, so buffer times mean the same moment in both pipelines, and the plugin maps them to MXL (TAI) time. Buffers are passed by reference (only the header is copied to change the time). |
| Failure handling | `MxlTap::onFeed`, bus handler | An error inside the side pipeline is logged and never reaches the channel. The pipeline is replaced on the next buffer, but not more than once every 5 s per tap. A new picture size or frame rate starts a new video flow. |
| One writer per flow | `MxlTap::create` | A second stream asking for a flow already in use is skipped with a warning. |

### Results (720p25 H.264 + AAC source into a 720p25 channel, unless stated)

| Check (test) | Result |
| --- | --- |
| Flow content (`01`) | Video flow 1280×720 at 25/1, 50 of 50 frames; audio flow 48 kHz stereo with the 1 kHz tone; on-air output has audio and video and no decode errors. |
| Lip-sync (`02`) | Flash-to-beep offset: generated source **0 ms**; MXL flows **+31 ms**; the engine's on-air output **−42 ms**. 14 pairs in the MXL flows. The resolution is about one frame (40 ms), so the MXL figure is within one frame of the source. Limit used: 80 ms (provisional, Gate A). |
| On-air output unchanged (`03`) | Same streams, zero decode errors, same frame count with and without the tap. Engine CPU **0.38 → 0.40 cores** (an earlier run: 0.36 → 0.42). |
| Reader faults (`04`) | Readers stopped for 6 s and then killed with SIGKILL: largest gap between on-air video packets 40 ms (one frame), audio 21 ms, no decode errors; a new reader could join afterwards. |
| Unusable domain (`05`) | A path that cannot be created and a read-only directory: the engine keeps running and the on-air output is clean; the failure is logged; 8 start attempts for two taps in about 24 s. |
| Bad configuration (`06`) | Not a UUID, no domain, no flow, equal ids, 9 channels and a missing `flowId` are each refused at start with a message naming the key; a correct block starts. |
| Source changes size (`07`) | 720p → 1080p while running: the video flow followed (1280×720 → 1920×1080) and the engine kept running. |
| Interlaced source (`08`) | 1080i25 (top field first): flow 1920×1080 at 25/1. Engine CPU **0.57 → 0.63 cores** with the tap, including the deinterlace (`method=linear fields=top`). |
| Parameters (`utpl_mxl_tap_parameters_test`) | Defaults, enabling rules, UUID validation, survives copying `InputParameters`. The whole C++ suite passes (11 of 11, six repeat runs). |

The extra CPU is measured on a small 720p25 channel and a 1080i25 one; it is **not** yet measured for
1080p50/59.94, where the conversion cost grows with the frame rate.

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

### Not covered

Playlist inputs (a tap on a `pls+` input is untested; the setting is read for single inputs); redundant
two-URI inputs; passthrough streams (never tapped); more than one audio track (only the first is
tapped); 1080p50/59.94; Dolby audio; SCTE-35; a long run (hours), where the once-sampled clock offset
could drift; a UI round trip (the channel UI spreads the existing input object, so a hand-written
`mxlTap` should survive a save, read from the code, not run).

### Source tap verification against the installed engine

After the images were rebuilt (`utpl/base:mxl` → `utpl/transcoder:mxl` → `utpl/transcoder-test:mxl`,
which compile the new code into `/opt/utpl/bin/transcoder`), the suites were run again with **no
scratch build mounted**:

| Suite | Result |
| --- | --- |
| `tests/run.sh mxl/tap` | **8 of 8 pass.** Lip-sync: source 0 ms, MXL flows **+2 ms**, on-air −43 ms (the scratch build gave +31 ms; both are within the one-frame resolution of the measurement). Engine CPU without / with the tap: 0.38 / 0.44 cores (720p25), 0.56 / 0.61 (1080i25). Reader faults: largest on-air gap 40 ms video, 21 ms audio. |
| `tests/run.sh mxl/sdk` | **9 of 9 pass** (Phase 1, no regression). |
| `tests/manual/mxl/engine-smoke.sh` on a channel with no tap | Same output as before the change: AC-3 5.1 + 640×360 H.264 at 25 fps, decoding without errors. |
| C++ CTest (scratch build) | 11 of 11 pass, six repeat runs (one earlier failure of `utpl_scte_section_test` under load did not reproduce, see [27](27-known-gaps-contradictions-and-remediation.md)). |

The measured cost of the tap across the runs is **+0.02 to +0.06 core** on these 720p25 and 1080i25
channels. Run to run the CPU varies by a few hundredths of a core, so these are indications, not
exact figures.

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
| T3–T4 | Nandi publishes CP1 (source) / CP2 (programme) | `tests/tests/mxl/tap/` | WS-03, config contract |
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
