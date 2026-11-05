# MXL integration with Nandi: project status

**As of 2026-11-05.** Steps 0 to 4 are committed; the commits are listed at the end. This page is a summary for the project owner. The detail,
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
| 4 | **MXL input**: Nandi reads MXL flows as a channel input (`mxl:///<domain>` uri) | **Done** (10 tests; 20-minute soak) |
| 5 | External encoder worker reading MXL, registry of flows | **Not started** |
| 6 | MXL Fabrics between two hosts | **Not started** (needs a second host and network cards) |

Test totals on the installed images: **39 of 39** MXL tests (9 SDK, 14 source tap, 6 programme, 10 input), 12 of 12
engine unit tests, 71 of 71 front-end tests, 346 of 346 API tests. Nothing else in the engine changed
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
the loopback, lip-sync through it, a writer that is killed and comes back, a channel started before its flows exist, ten
kinds of bad configuration, a video-only and an audio-only input, 25 / 29.97 / 50 / 59.94 fps, a writer replaced by one
with another picture size, CPU against an H.264 UDP input, and an input that is itself tapped and feeds a programme
output. The channel form can edit an MXL input; the API refuses to probe one.

Two things found while building it matter beyond this step:

1. **`mxlsrc` can deadlock a channel.** It holds its own lock while it pushes its caps, and the engine blocks an input's pads until the
   input plays; the two wait for each other and the channel never starts. Worked around in the engine (a queue behind the reader
   and a dropped query). Worth reporting to the MXL project.
2. **The timestamps of a flow are in the past by the writer's own delay**, about 0.8 s for a programme taken after processing. A
   channel working on live time took such frames for late, and nothing came out. The input now puts all of its flows on live time
   plus 300 ms with one shift, which keeps sound and picture as the writer had them.

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
| MXL input CPU, against the same picture as H.264 over UDP | 720p25 0.25 against 0.36 core; 1080p25 0.57 against 0.77 core (it decodes nothing) |
| Lip-sync through a loopback (channel A's flows, A's on-air output, channel B's output) | +8, −64 and −53 ms in one run: B is 11 ms from A's on-air output |
| Writer killed and restarted: time until the reader channel shows it again | 5.9 s after the writer publishes again (3 s idle limit, measured in steps of about 4 s) |
| 20-minute loopback soak | 0 reattaches, 0 errors; B's memory flat (one 2.4 MB step), B CPU 0.29 core; writer channel's memory +0.3 MB per minute |

The CPU limit of about 0.18 core for a programme tap at 1080p25 was accepted by the project owner on
2026-10-30.

## What is left

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
- End-to-end latency of a loopback (the input adds a 300 ms margin; nothing more was measured), and a channel that stalls for
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

### Open questions about the behaviour seen

- **Memory:** an engine that writes flows rose 14.8 MB per hour in the 20-minute source-tap soak, and the writer
  channel of the 20-minute loopback soak rose 0.3 MB per minute (18.5 MB per hour) in a straight line. The channel
  that only reads flows stayed flat. So the growth looks like it is on the writing side, but 20 minutes cannot tell a leak from
  warm-up. A run of several hours is needed.
- **Lip-sync step in the loopback soak:** channel B's output read −50 ms at the start and −10 ms in the middle and at the
  end, a one-frame step with no log line around it. Not explained.
- **Audio flow lag:** the audio flow's lag behind the clock is mostly 305 ms but has single-minute bursts up
  to about 390 ms. There is no trend; the cause is not known.
- One earlier failure of `utpl_scte_section_test` was not explained and has not come back.
- About 9 s of audio silence in one muxer-based reader was not explained.

### Decisions waiting for the project owner

| Decision | Current state |
| --- | --- |
| Gate A numeric limits (lip-sync, drops, latency, CPU) | Not agreed. The tests print results and use provisional limits (for example 80 ms lip-sync from the source). |
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
| (next commit) | Step 4: MXL input |
