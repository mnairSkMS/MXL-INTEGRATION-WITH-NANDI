# MXL integration with Nandi: project status

**As of 2026-11-06.** Steps 0 to 4 are committed (the last commit is `2ce01ad`); the commits are listed at the end. Nothing is uncommitted. The
next work is planned under [What to do next](#what-to-do-next-in-order) and has **not been started**. This page is a summary for the project owner. The detail,
the evidence and the numbers sit in [41](doc/codebase/41-mxl-integration.md) (what was built and measured)
and [42](doc/codebase/42-mxl-implementation-plan.md) (the plan and its steps). Where this page and the source
disagree, the source wins.

## What the project is

This checkout tests whether MXL (Media eXchange Layer: raw video and audio shared between programs on one
host through memory) and MXL Fabrics (the same between hosts) can be integrated with Nandi, the UTPL
transcoder engine. The idea is that Nandi can **publish** its pictures and sound as MXL flows for other
programs, and **read** MXL flows as an input. Changing the engine is allowed for this purpose.

## At a glance

| Step | What | State |
| --- | --- | --- |
| 0 | Baseline: what the engine costs today, with no MXL | **Done** (one reference workload) |
| 1 | MXL v1.1.0 built into the existing images; MXL itself tested | **Done** (9 of 9 tests) |
| 2 | **Source tap**: publish a decoded input as MXL flows (`inputs[n].mxlTap`) | **Done**, gaps closed 2026-11-02 |
| 3 | **Programme tap**: publish the finished programme as MXL flows (`outputs[n].type = "mxl"`) | **Done** |
| 4 | **MXL input**: Nandi reads MXL flows as a channel input (`mxl:///<domain>` uri) | **Done** (11 tests; 20-minute soak) |
| 5 | External encoder worker reading MXL, registry of flows | **Not started** |
| 6 | MXL Fabrics between two hosts | **Not started** (needs a second host and network cards) |

Test totals on the installed images: **40 of 40** MXL tests (9 SDK, 14 source tap, 6 programme, 11 input), 12 of 12
engine unit tests, 72 of 72 front-end tests, 346 of 346 API tests. (A first full run of the day had one failure that did not come back; see
"Open questions".) Nothing else in the engine changed
its behaviour (`tests/manual/mxl/engine-smoke.sh` gives the same output on the `:baseline` and `:mxl` images).

## What has been completed

### Step 0: baseline (2026-10-30)

The unmodified engine was measured with a 1080p25 source and one or two outputs
([41, Phase 0 baseline](doc/codebase/41-mxl-integration.md#phase-0-baseline); raw output in
[`baseline-2026-10-30/`](doc/mxl/baseline-2026-10-30/)).

| | 1 output (1080p) | 2 outputs (1080p + 720p) |
| --- | ---: | ---: |
| Engine CPU | 0.95 core | 1.28 cores |
| Memory | 325 MB | 367 MB |
| Latency, UDP in to UDP out | about 1.5 s, steady | about 1.5 s, steady |

Putting MXL into the image changed none of these numbers measurably.

### Step 1: MXL in the images (2026-10-30)

- MXL v1.1.0 and its GStreamer plugin (`mxlsink`, `mxlsrc`) are built into `utpl/base`, so every image has
  them. There is no separate test image. A tmpfs volume for the MXL domain is in `docker-compose.yml`.
- Nine SDK tests (`tests/tests/mxl/sdk/`) pass: video and audio come through unchanged, frame rates from 25 to
  60 fps run in real time, a killed writer is detectable, a stalled reader does not slow the writer, and a
  reader in another container or user, or on a read-only mount, works.
- Things learned that shaped the later steps:
  - The size of a flow's ring is fixed by the plugin (5 frames at 25 and 29.97 fps, 11 at 59.94 fps). A
    1080p59.94 flow takes 60 MB, so two do not fit in Docker's default 64 MiB `/dev/shm`. That is why the
    MXL domain has its own tmpfs.
  - A reader on a dead flow waits for ever. Any consumer, including the Nandi MXL input (Step 4), needs its own
    watchdog.
  - Converting Nandi's BGRA canvas to MXL's v210 costs 7.75 ms per 1080p frame, against 0.72 ms from the
    decoder's YUV. This is why the programme tap is the expensive one.
  - The plugin converts pipeline time to MXL time itself, so no clock adapter was written. This was read
    from its source. Drift over hours is still not measured (see below).

### Step 2: source tap (2026-10-30, gaps closed 2026-11-02)

A decoded input is published as one video flow and one audio flow, without touching the channel's own
outputs. It runs in a side pipeline, so a failing MXL write cannot stop the channel (**fail-open**).

Covered and tested: ordinary single inputs, redundant two-source inputs (tapped after the switch), playlist
inputs (one flow runs through every item), 720p25, 1080i25 (deinterlaced), 1080p50, 720p59.94, 1080p29.97,
a changing picture size, AC-3 stereo and 5.1, a source carrying SCTE-35 (ignored by the tap), a source that
stops and comes back, a domain that is missing or read-only, a bad configuration, and a hand-written `mxlTap`
block surviving a save in the UI and the API.

Four real faults were found while closing the gaps and fixed:

1. The MXL writer refuses a frame in the same 40 ms slot as the last one, or earlier. A cut-off source killed
   the tap. Fixed with a frame grid (`MxlFrameGrid`, with its own unit test).
2. The side pipeline's queue had a hidden 200 KB limit, so it held one frame and dropped every other frame.
3. File and playlist sources run up to 0.7 s ahead of the clock; the queue was too short for them.
4. Every playlist item change destroyed and rebuilt the tap.

### Step 3: programme tap (2026-11-02)

An output of `"type": "mxl"` publishes the finished programme (after compositing and audio processing,
before encoding) as flows. It can have its own picture size and frame rate and keeps writing when the source
stops. The UI does not show it but keeps it through a save. Eight different wrong configurations are each
refused at start with a message naming the key.

### Step 4: MXL input (2026-11-05)

A channel can take MXL flows as its input: `transport.uri = "mxl:///run/mxl"` plus the flow ids in `transport.mxl`. One
channel's programme output can be another channel's input (the loopback), or any other MXL writer on the host. Tested:
the loopback, lip-sync through it, a writer that is killed and comes back, a channel started before its flows exist, fourteen
kinds of bad configuration, the delay setting, a video-only and an audio-only input, 25 / 29.97 / 50 / 59.94 fps, a writer replaced by one
with another picture size, CPU against an H.264 UDP input, and an input that is itself tapped and feeds a programme
output. The channel form can edit an MXL input; the API refuses to probe one.

Two things found while building it matter beyond this step:

1. **`mxlsrc` can deadlock a channel.** It holds its own lock while it pushes its caps, and the engine blocks an input's pads until the
   input plays; the two wait for each other and the channel never starts. **Fixed in the plugin** by a patch
   (`transcoder/patches/mxl/010-mxlsrc-release-locks-before-set-caps.patch`, applied when the image is built), and still worked around in
   the engine (a queue behind the reader and a dropped query) so that a stock plugin works too. Proved by taking the engine's
   measures out: with the stock plugin the loopback test fails and channel B stops at "switching to PLAYING"; with the patched plugin the
   same engine passes with the same numbers. **Not yet reported to the MXL project.** A report that names no product or company is prepared
   outside this checkout, in `../experiments/mxlsrc-report/` (the text, a 104-line reproduction that needs only GStreamer and `mxlsrc`, and the
   change as a diff). The reproduction was run 4 times: the stock plugin blocks every time, the patched one answers every time. Its `README.md` has
   the checklist to go through before posting. **Nothing has been posted; that is the project owner's call.**
2. **The timestamps of a flow are in the past by the writer's own delay**, about 0.8 s for a programme taken after processing. A
   channel working on live time took such frames for late, and nothing came out. The input now puts all of its flows on live time
   plus a **delay with one shift**, which keeps sound and picture as the writer had them. The delay is the setting
   `transport.mxl.delayMs`, **default 500 ms**, chosen from measurements (below); it was a fixed 300 ms, which one measurement
   exceeded. The engine warns, naming the setting, when a flow arrives later than the delay allows.

Also: a reader never notices a new writer, so the input restarts a reader that has had no data for 3 s (this was a Step 1
finding). And converting the v210 picture to I420 first, then to the canvas, costs 1.4 ms per 1080p frame instead of 7.7 ms.

### Key measurements (installed engine, shared 28-core host; read differences under about 5 % as noise)

| Measure | Result |
| --- | --- |
| Source tap CPU cost | +0.02 to +0.4 core, rising with pixels per second; varies 0.1 to 0.2 core between runs of the same test |
| Programme tap CPU cost | +0.10 to +0.16 core at 720p25 with a shared canvas |
| Lip-sync (flash against beep), generated source | 0 ms |
| Lip-sync through the tap flows | +19 ms (source tap), +5 ms (programme tap) |
| Lip-sync of the engine's on-air output | −42 to −43 ms (the encoders add this; it is not the tap) |
| 20-minute soak, video flow behind the clock | mean 123.5 ms, trend +0.9 ms per hour: no drift |
| 20-minute soak, lip-sync through the flows | +7, +2, +7 ms at start, middle and end |
| 20-minute soak, engine CPU | mean 0.37 core; 0 tap restarts; 0 errors |
| Largest on-air gap while a reader is killed | 40 ms video, 21 ms audio |
| MXL input CPU, against the same picture as H.264 over UDP | 720p25 0.25 against 0.37 core; 1080p25 0.59 against 0.80 core (it decodes nothing) |
| Lip-sync through a loopback (channel A's flows, A's on-air output, channel B's output) | −7, −43 and −61 ms in one run: B is 18 ms from A's on-air output |
| Writer killed and restarted: time until the reader channel shows it again | 5.3 s after the writer publishes again in four runs, 15.8 s in one (3 s idle limit, measured in steps of about 4 s) |
| How far an MXL writer's lag moves (a Nandi programme output into an MXL input; `input-jitter.sh`) | Each flow alone moves 10 to 74 ms. The picture is a constant 160 to 180 ms later than the sound, so the two flows together spread 185 to 260 ms in three-minute runs and 358 ms in one 25 s run with every core busy. Default delay 500 ms. |
| 20-minute loopback soak | 0 reattaches, 0 errors; B's memory flat (one 2.4 MB step), B CPU 0.29 core; writer channel's memory +0.3 MB per minute |

The CPU limit of about 0.18 core for a programme tap at 1080p25 was accepted by the project owner on
2026-10-30.

## What is left

### What to do next, in order

Job 1 is done. Job 2 was started on 2026-11-06 at 10:44 and is running (its result is not in yet). The rest has not been started. The times are rough estimates from how long the earlier steps took, not measurements.

| # | Job | Why | Estimate | Needs from the owner |
| --- | --- | --- | --- | --- |
| 1 | **Rebuild the built channel UI** (`api/src/transcoder_api/static/channel/`; sources are in `frontend/`) | **Done 2026-11-06, not yet committed.** Built as the Dockerfile does (type check, 72 of 72 front-end tests, build); the new bundle carries the MXL transport and `delayMs`, the old one carried neither; API suite 346 of 346. See [27](doc/codebase/27-known-gaps-contradictions-and-remediation.md). | Took about 15 minutes | Nothing |
| 2 | **Long soak, 4 to 8 hours** (`tests/manual/mxl/input-soak.sh <minutes>`, a loopback; 4 hours = 240) | The channel that writes flows grew 0.3 MB per minute in a 20-minute run. Twenty minutes cannot tell a leak from warm-up. | 4 to 8 hours, unattended | **Running since 2026-11-06 10:44 as a 240-minute run** (expected end about 14:50; log outside the repo in `../experiments/mxl-soak-2026-11-06/`). No builds or other tests meanwhile. |
| 3 | **Smarter delay**: wait for the first buffer of every flow, learn the constant gap between picture and sound, and set the shift from that | Would allow a delay of about 100 ms instead of 500 ms, so less latency. | 3 to 5 hours | Whether lower latency is wanted before Step 5 |
| 4 | **Find why the programme output writes its picture 160 to 180 ms after its sound** | Fixing it at the writer also allows a smaller delay. | 1 to 3 hours; the cause may not be found | Nothing |
| 5 | **Look at the two oddities**: `input/11-delay` exiting silently once in 9 runs, and writer-restart recovery taking 5.3 s four times and 15.8 s once | Both are rare, so a fix may not be possible. | 2 to 6 hours | Nothing |
| 6 | **Step 5, the external encoder worker** (below) | The next planned step. | 2 to 4 working days | The three decisions below |
| 7 | **Step 6, Fabrics** (below) | Needs a second host and network cards. | About 1 to 2 days once the hardware exists | Hardware |

Jobs 1 to 5 are about one working day of work. Jobs 1 to 6 are about 3 to 5 working days. Job 7 cannot be estimated until the hardware exists.

Order and rules:

- Do 1, then start 2. The soak needs a quiet host, so **do not build images or run other test suites while it runs**; code can be
  written for job 3 in the meantime, but built and tested only after the soak ends. Jobs 4 and 5 also need a quiet host for measuring.
- Do not start 6 before the owner has decided: where the SCTE-35 timeline lives (Q7), what happens when an output fails, per output type
  (Q8), and the numeric pass/fail limits for lip-sync, drops, latency and CPU (Gate A). They are in "Decisions waiting" below.
- Posting the `mxlsrc` report is independent of all of this and can happen at any time.

### Planned steps not started

| Step | What it needs |
| --- | --- |
| 5: encoder worker | A container reading MXL and encoding; moving a channel's outputs to it; how SCTE-35 reaches it; flow registry (one writer per flow, stale-flow cleanup); consumer-restart, slow-reader and multi-audio tests. |
| 6: Fabrics | Two hosts, TCP first, then RDMA hardware, compared with a compressed SRT feed under load. **Integration gated**: needs a second host and network cards. |

### Gaps in what is already built

Not covered by any test:

- Dolby SDK paths (**integration gated**), for the taps and for the MXL input.
- A channel with a second input and a tap on either input; an MXL input as the second (alternate) input.
- An MXL input that is redundant or a playlist item (refused), interlaced or data flows, more than one audio flow.
- End-to-end latency of a loopback (the input adds its delay, 500 ms unless set; nothing more was measured), and a channel that stalls for
  longer than a flow's ring.
- Hardware decoders (the decoders here are software; a tap holding 30 frames would hold decoder surfaces).
- More than one audio track (only the first is tapped).
- A 1080p programme flow (its cost is the case to watch against the CPU limit), a 1080p50 or 59.94
  programme flow, an interlaced programme output, graphics overlays in the programme picture, HLS or DASH
  outputs alongside an MXL output.
- Runs of hours.

Not measured: write-to-read latency (p50/p95/p99); memory bandwidth; drift of the plugin's once-sampled
clock offset over hours.

Not done in Step 1: the `--build-arg` pass-through in `docker/build-docker.sh` (pass it to `docker build`
directly).

The built channel UI bundle was stale after Step 4; it was rebuilt on 2026-11-06 (job 1 above).

### Open questions about the behaviour seen

- **Memory:** an engine that writes flows rose 14.8 MB per hour in the 20-minute source-tap soak, and the writer
  channel of the 20-minute loopback soak rose 0.3 MB per minute (18.5 MB per hour) in a straight line. The channel
  that only reads flows stayed flat. So the growth looks like it is on the writing side, but 20 minutes cannot tell a leak from
  warm-up. A run of several hours is needed.
- **Lip-sync step in the loopback soak:** channel B's output read −50 ms at the start and −10 ms in the middle and at the
  end, a one-frame step with no log line around it. Not explained.
- **Audio flow lag:** the audio flow's lag behind the clock is mostly 305 ms but has single-minute bursts up
  to about 390 ms. There is no trend; the cause is not known.
- **Two intermittent test failures on 2026-11-05.** `input/11-delay` exited without a message in the first full run (it captures B's output
  with `ffmpeg`; a failed capture ended the script silently) and passed in all 8 later runs; the test now reports a failed capture in words.
  `input/03-writer-restart` failed once because its 2 s probe sometimes holds less than a second of usable picture (a capture starts mid-GOP and
  ffmpeg drops what comes before the first keyframe); the probe now judges the share of distinct pictures. The first is unexplained.
- **Writer-restart recovery time varies:** 5.3 s in four runs, 15.8 s in one.
- **Picture later than sound in the programme output:** the programme output writes its video flow 160 to 180 ms later than its
  audio flow, measured against their media times. It is why the MXL input needs a delay of several hundred milliseconds. Not
  looked into; a smaller difference at the writer would allow a smaller delay.
- One earlier failure of `utpl_scte_section_test` was not explained and has not come back.
- About 9 s of audio silence in one muxer-based reader was not explained.

### Decisions waiting for the project owner

| Decision | Current state |
| --- | --- |
| Gate A numeric limits (lip-sync, drops, latency, CPU) | Not agreed. The tests print results and use provisional limits (for example 80 ms lip-sync from the source). |
| Default delay of an MXL input (500 ms) | Chosen from measurements, 140 ms above the worst spread seen; not agreed against a latency limit. A smarter shift (learn the constant difference between the flows) would allow about 100 ms; not built. |
| Report the `mxlsrc` deadlock to the MXL project | The patch is in the images. A neutral report, reproduction and diff are ready in `../experiments/mxlsrc-report/` (no product or company named); reporting is outward-facing and **not done**. Before posting: check the GitHub profile, add nothing from the project, and check the employer's open-source policy before sending code (the report can describe the change in words instead). |
| Fail-open for a failed tap | Assumed and built; never confirmed. |
| Where the SCTE-35 timeline lives (Q7) | Open. SCTE-35 does not travel in an MXL flow. |
| Stop condition for the project (Q18) | Proposed in [42, section 7](doc/codebase/42-mxl-implementation-plan.md#7-proposed-defaults-for-the-open-decisions), not approved. |
| Q8 (fail policy per output type) | Open; a per-output `failPolicy` is not built. |
| Other section 7 defaults of the plan | Not approved. |

### Limits of the evidence

- One shared development host, one run per row in most places.
- The existing `proto/` and `overlay/` suites do not run in this environment (they could not fetch their
  source video or start their helpers), on `:baseline` as well as `:mxl`. They cannot serve as a regression
  check; `engine-smoke.sh` and the MXL suites were used instead.
- Nothing here has run on production hardware, with Dolby, or between hosts.

## How to run things

| What | Command |
| --- | --- |
| All MXL tests on the installed images | `tests/run.sh mxl` (inside `utpl/transcoder-test:mxl`, run with `--user $(id -u):$(id -g)`) |
| One group | `tests/run.sh mxl/tap`, `mxl/programme`, `mxl/input`, `mxl/sdk` |
| Soak of the source tap | `tests/manual/mxl/tap-soak.sh [minutes]` |
| Soak of the MXL input (a loopback) | `tests/manual/mxl/input-soak.sh [minutes]` |
| Baseline of the unmodified engine | `tests/manual/mxl/baseline.sh` |
| Cross-container checks | `tests/manual/mxl/cross-container.sh` |

Configuration keys: [05, source tap](doc/codebase/05-configuration-reference.md#input-mxl-tap),
[05, output MXL](doc/codebase/05-configuration-reference.md#output-mxl) and
[05, input MXL](doc/codebase/05-configuration-reference.md#input-mxl).

## History

| Commit | What |
| --- | --- |
| `1b7a9d3` | Initial import of the tree as received |
| `0e69ca8` | Step 1: MXL v1.1.0 built into `utpl/base`, SDK tests |
| `75abb4c` | Step 0: baseline script and measurements of the unmodified engine |
| `6df9e2b` | CPU limit decision and the plugin's clock mapping recorded |
| `65adad6` | Step 2: source tap on single inputs |
| `d4b0cc6` | Step 2 gaps closed; Step 3 programme output |
| `12daf1d` | This status file |
| `b711685` | Step 4: MXL input |
| `7cd9bbf` | This status file names the Step 4 commit |
| `2ce01ad` | `mxlsrc` deadlock patch in the images; the MXL input's delay is a setting (`transport.mxl.delayMs`, default 500 ms) |
