# MXL integration with Nandi: project status

**As of 2026-11-25.** Steps 0 to 4, the delay learning and the memory-leak fix are committed, and so are Step 5's work packages WP4 (flow
registry), WP2 (SCTE-35 over MXL) and WP3 (external encoder worker); the commits are listed at the end. WP5 (failure behaviour) is committed too
(`e9b21da`, merged to `main` by pull request 1); WP6 (more than one audio track) is not started. Step 5 is **not finished**: the hour-long Gate A
run and the two-worker run have not been made, and the worker's CPU limit is not met in every run at 720p25 (see
[Step 5](#step-5-registry-scte-35-over-mxl-and-the-encoder-worker-2026-11-11-to-2026-11-13)). **Step 6 (MXL Fabrics) was skipped by the owner on 2026-11-24.** The OpenLive / Strom
integration is planned to follow Step 5, on one host, and is not started (see [After Step 5](#after-step-5-openlive-and-strom-integration-one-host)). This page is a summary for the project owner. The detail,
the evidence and the numbers sit in [41](doc/codebase/41-mxl-integration.md) (what was built and measured)
and [42](doc/codebase/42-mxl-implementation-plan.md) (the plan and its steps). Where this page and the source
disagree, the source wins.

## What the project is

This checkout tests whether MXL (Media eXchange Layer: raw video and audio shared between programs on one
host through memory) and MXL Fabrics (the same between hosts) can be integrated with Nandi, the UTPL
transcoder engine. The idea is that Nandi can **publish** its pictures and sound as MXL flows for other
programs, and **read** MXL flows as an input. Changing the engine is allowed for this purpose. The Fabrics half is not being pursued (Step 6 was skipped on 2026-11-24).

## At a glance

| Step | What | State |
| --- | --- | --- |
| 0 | Baseline: what the engine costs today, with no MXL | **Done** (one reference workload) |
| 1 | MXL v1.1.0 built into the existing images; MXL itself tested | **Done** (9 of 9 tests) |
| 2 | **Source tap**: publish a decoded input as MXL flows (`inputs[n].mxlTap`) | **Done**, gaps closed 2026-11-02 |
| 3 | **Programme tap**: publish the finished programme as MXL flows (`outputs[n].type = "mxl"`) | **Done** |
| 4 | **MXL input**: Nandi reads MXL flows as a channel input (`mxl:///<domain>` uri) | **Done** (12 tests; 20-minute and 4-hour soaks; delay learning) |
| 5 | Flow registry, SCTE-35 over MXL, external encoder worker, failure behaviour | **Mostly built, not finished.** WP4, WP2, WP3, WP5 committed; WP6 (multi-audio) not started. Gate A: CPU limit met at 1080p25, **missed in 1 of 5 runs at 720p25** (1.39 against 1.25); hour-long run not done |
| 6 | MXL Fabrics between two hosts | **Skipped** by the owner on 2026-11-24; not to be built |
| After 5 | OpenLive / Strom (Eyevinn) integration: Strom mixes Nandi's MXL cameras, Nandi encodes the programme | **Steps 0 and 1 done 2026-11-26**: readiness check ([00](doc/integration/00-readiness-check.md)); Strom image with MXL, bit-exact both ways with Nandi ([01](doc/integration/01-strom-image.md)). Strom blocks not started. One host only |

Test totals on the installed images, as of 2026-11-09 (before Step 5): **41 of 41** MXL tests (9 SDK, 14 source tap, 6 programme, 12 input),
12 of 12 engine unit tests, 72 of 72 front-end tests, 346 of 346 API tests. (A first full run of the day had one failure that did not come back;
see "Open questions".) Nothing else in the engine changed its behaviour (`tests/manual/mxl/engine-smoke.sh` gives the same output on the
`:baseline` and `:mxl` images).

**On the images rebuilt with Step 5 (2026-11-24), the full MXL suite passed 51 of 53** (9 SDK, 15 source tap, 7 programme, 14 input, 6 of 8
worker). The two failures: `worker/05` (the starved worker logged no late flow, though it recovered and the main engine was unaffected) and
`worker/07` (one cue 50.6 ms from the source's placement against a limit of 45 ms, the same in process). The engine unit tests, the API tests and
the front-end tests were not run again; on the scratch build the API suite (including the 9 new registry tests) and the 73 front-end tests passed.

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

### Step 5: registry, SCTE-35 over MXL and the encoder worker (2026-11-11 to 2026-11-13)

Plan: [doc/mxl/step5-plan-2026-11-11.md](doc/mxl/step5-plan-2026-11-11.md). Every change, with its evidence, is in
[doc/mxl/step5-change-log.md](doc/mxl/step5-change-log.md) (27 entries). The owner approved Q7, Q8 and the Gate A limits on 2026-11-11 (below).
Step 5 was built and first measured on a **scratch build**; the installed images were rebuilt and tested on 2026-11-24.

| Work package | State | What it is |
| --- | --- | --- |
| Trial (WP1) | Done | About 400 runs outside the repo: an ancillary-data flow (`video/smpte291`) carries a cue byte for byte and stays on the picture's grain. One failure mode was found and turned into three rules the carrier follows. |
| WP4: flow registry | **Committed** `2eeea6f` | One writer per flow (a second is refused, fail open); stale flows cleared once at start; configs that reuse a flow refused; `GET /mxl/flows` lists a domain's flows and whether a writer holds each. |
| WP2: SCTE-35 over MXL | **Committed** `0c7d6bc` | The programme output writes cues in a data flow, one grain per frame; an MXL input reads them and re-times them onto the channel's clock. Through MXL the cues sit -37 to +3 ms from where the source put them (in-process: -208 to +58 ms). A muxer bug (negative `pts_adjustment` moved a splice 13.2 hours) is worked around in the section, not patched. |
| WP3: encoder worker | **Committed** `8b9f845` | A main engine decodes and publishes; a worker reads MXL and encodes. Configs `config/mxl_split_main.json` and `mxl_split_worker.json`; compose service `transcoder-worker` (profile `mxl-worker`). Also: an all-MXL channel no longer goes through the 3 s prebuffer (it lost most frames in lumps), and a lip-sync fix that uses MXL's own clock. |
| WP5: failure behaviour | **Committed** `e9b21da` | Tests `worker/04` to `08`: worker killed (main unaffected, worker back in about 7 s), worker starved (main unaffected, one late-flow report, back in about 1 s), main restarted (worker has flows 0.5 s after main writes again), cues across a worker restart and across a main restart (events 103 to 111 carried, none lost or doubled). Scratch-build results; on the installed images (2026-11-24) the starved worker logged no late flow and event 103 was lost across the main restart. |
| WP6: more than one audio track | Not started | Needs a fix in the audio selector first. |

**Gate A, the worker against the same channel in-process** (1080p25):

| Limit | Result |
| --- | --- |
| Same streams, no decode errors | Met |
| No frame loss | Met (25.000 fps both, largest gap 40 ms) |
| Lip-sync within 40 ms | Met in 5 of 5 clean runs (-34 to +23 ms), with little room: the worker's own offset still moves up to 60 ms between runs, cause not known. Verified on this host only. |
| CPU at most 1.25 times in-process | **Installed images: met at 1080p25** in 9 of 9 runs (0.94 to 1.10 times on all CPUs, up to 1.23 on the fast cores only); **720p25: 4 of 5 runs** (one 1.39). The scratch build read 1.35 to 1.39; why it differs is not known. |
| SCTE cues within one frame of the authored splice time | Met for the worker; `input/14` goes about 0.8 ms over its own 45 ms limit in about one run in four |
| Keyframe at a splice point | Neither the in-process channel nor the worker produces one (0 of 9 each); not caused by MXL |
| 0 drops in a 1-hour run; 20 minutes with two workers | **Not run** |

Not done in Step 5: a read-only domain in the engine, Docker's own restart of the
worker, the SCTE tests for `time_signal`, HLS cue tags and 20 cues in 20 minutes (not checked which exist), a registered DID/SDID for the cue carrier
(the one used is a placeholder), and a cue that arrives at the source while main is down (it is lost; not measured). Open problems are listed in
[27](doc/codebase/27-known-gaps-contradictions-and-remediation.md); the handbook page is [41](doc/codebase/41-mxl-integration.md).

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
| Writer killed and restarted: time until the reader channel shows it again | The earlier 5.3 s (four runs) and 15.8 s (one) came from a slow measuring step; by the logs the reader is back in about 0.3 to 1 s (3 s idle limit, then the 900 ms retry). Test `input/03` passes |
| How far an MXL writer's lag moves (a Nandi programme output into an MXL input; `input-jitter.sh`) | Each flow alone moves 10 to 74 ms. The picture is a constant 160 to 180 ms later than the sound, so the two flows together spread 185 to 260 ms in three-minute runs and 358 ms in one 25 s run with every core busy. Default delay 500 ms. |
| 20-minute loopback soak | 0 reattaches, 0 errors; B's memory flat (one 2.4 MB step), B CPU 0.29 core; writer channel's memory +0.3 MB per minute (the H.264 decoder leak, fixed afterwards) |
| 4-hour loopback soak (2026-11-06, installed images) | 0 reattaches, 0 errors, nothing restarted. Reader B: memory flat at 198.8 MB (−0.4 MB per hour after the first hour), CPU 0.28 core. Writer A: memory 259.6 to 335.7 MB, a straight line of **17.9 MB per hour** (not tapering). Lip-sync of B's output **−93 ms** at start, middle and end: no drift. |

The CPU limit of about 0.18 core for a programme tap at 1080p25 was accepted by the project owner on
2026-10-30.

## What is left

### What to do next, in order

Jobs 1 to 5 are done or closed (2026-11-06 to 2026-11-09); the small repeat checks listed under them are not. Job 2 found a straight-line memory growth in the engine that writes flows; job 2b found its cause and it is fixed. Job 6 (Step 5) is mostly built (see above) and job 7 has not been started. The times in the table are rough estimates from how long the earlier steps took, not measurements.

| # | Job | Why | Estimate | Needs from the owner |
| --- | --- | --- | --- | --- |
| 1 | **Rebuild the built channel UI** (`api/src/transcoder_api/static/channel/`; sources are in `frontend/`) | **Done 2026-11-06, not yet committed.** Built as the Dockerfile does (type check, 72 of 72 front-end tests, build); the new bundle carries the MXL transport and `delayMs`, the old one carried neither; API suite 346 of 346. See [27](doc/codebase/27-known-gaps-contradictions-and-remediation.md). | Took about 15 minutes | Nothing |
| 2 | **Long soak, 240 minutes** (`tests/manual/mxl/input-soak.sh <minutes>`, a loopback) | The channel that writes flows grew 0.3 MB per minute in a 20-minute run; 20 minutes cannot tell a leak from warm-up. | Took 4 hours, unattended | **Done 2026-11-06.** The reader (the new MXL input) is flat. The writer grows 17.9 MB per hour in a straight line: **not warm-up**. Results in [41](doc/codebase/41-mxl-integration.md#four-hour-soak-result) and `doc/mxl/input-soak-2026-11-06/`. |
| 2b | **Find the cause of the writer's memory growth** | Job 2 showed 17.9 MB per hour on the writing side. | Took about two days of runs | **Done 2026-11-09.** It is not MXL: the H.264 decoder in Ubuntu's gst-libav 1.24.1 leaks a 104-byte `AVPacket` for every picture (about 17 MB per hour at 720p25) in any channel that decodes H.264. Fixed by `transcoder/patches/gst-libav1.0/010-avviddec-free-the-av-packet.patch`, built into the base image. Checked with valgrind (112,720 bytes in 52 blocks before, none after) and flat footprints in the runs afterwards. See [27](doc/codebase/27-known-gaps-contradictions-and-remediation.md) and [41](doc/codebase/41-mxl-integration.md). |
| 3 | **Smarter delay**: learn each flow's age, set the shift from that, relearn when late | Would allow a delay of about 100 ms instead of 500 ms. | 3 to 5 hours | **Built and tested 2026-11-08.** The input learns the oldest age of the flows (250 ms to 1 s), relearns when buffers are persistently late for 3 s, and retries a lost writer every 900 ms. Measured: 720p idle 4 ms, 720p busy 52 ms, 1080p idle 669 ms (one stalled second). **The default stays 500 ms** because of the 1080p result and the busy-host spread; owner decision on the latency limit still open. Tests `input/03`, `11` and the new `12-relearn` pass. |
| 4 | **Why the programme output writes its picture 160 to 180 ms after its sound** | Fixing it at the writer also allows a smaller delay. | 1 to 3 hours | **Explained, not changed.** Decoder latency: B-frame reordering (about 80 ms) plus frame-threaded decoding (about 80 ms). The threading share was not isolated by an experiment. |
| 5 | **The two oddities**: `input/11-delay` exiting silently once, and writer-restart recovery of 5.3 s / 15.8 s | Both rare. | 2 to 6 hours | **Partly done.** Writer-restart: the figures came from a slow probe step; by the logs recovery takes about 0.3 to 1 s (test 03 passes). `input/11-delay`: an error trap now names the failing line; the 20 repeats were **not run**. |
| 6 | **Step 5, registry, SCTE-35 over MXL and the encoder worker** ([above](#step-5-registry-scte-35-over-mxl-and-the-encoder-worker-2026-11-11-to-2026-11-13)) | The next planned step. | 2 to 4 working days estimated | **Mostly built 2026-11-11 to 2026-11-13** (WP4, WP2, WP3, WP5 committed; WP6 not started). Images rebuilt and full suite run 2026-11-24 (51 of 53). Left: WP6, the hour-long and two-worker runs, the two failed tests. |
| 7 | **Step 6, Fabrics** | Would link hosts. | Not estimated | **Skipped by the owner 2026-11-24.** |
| 8 | **OpenLive / Strom integration** ([below](#after-step-5-openlive-and-strom-integration-one-host)) | The production-mixer target: two Nandi cameras → MXL → Strom vision mixer → MXL → Nandi encode, controlled from OpenLive Studio. | Not estimated | Step 5; Q7, Q8, Gate A |

Job 6 was estimated at 2 to 4 working days. Job 7 is skipped. Job 8 is not estimated.

Order and rules:

- Do 1, then start 2. The soak needs a quiet host, so **do not build images or run other test suites while it runs**; code can be
  written for job 3 in the meantime, but built and tested only after the soak ends. Jobs 4 and 5 also need a quiet host for measuring.
- Job 6 started after the owner decided, on 2026-11-11: Nandi's main engine keeps the SCTE-35 timeline (Q7), every output fails open (Q8),
  and the Gate A limits for lip-sync, drops, latency, CPU and SCTE (see "Decisions waiting" below).
- Job 8 follows job 6. (The owner's 2026-11-11 rule, not to start 8 before Step 6 was built, ended when Step 6 was skipped on 2026-11-24.)
- Posting the `mxlsrc` report is independent of all of this and can happen at any time.

### Planned steps not started

| Step | What it needs |
| --- | --- |
| 5: what is left | WP6 (more than one audio track); the two tests that failed on the installed images (`worker/05`, `worker/07`); the 720p25 CPU run over the limit; the 1-hour Gate A run and the 20-minute run with two workers. The rest of Step 5 is built (see above). |
| 6: Fabrics | **Skipped** (owner, 2026-11-24). |
| After 5: OpenLive / Strom | MXL blocks in Strom, an MXL source and output type in OpenLive and its Studio, a Nandi consumer, end-to-end tests and an SRT-vs-MXL benchmark. See the next section. |

### After Step 5: OpenLive and Strom integration (one host)

**Follows Step 5. Work steps 0 (readiness check) and 1 (Strom image with MXL, round trips with Nandi: all pass;
[01](doc/integration/01-strom-image.md)) done 2026-11-26; step 2 not started.** The owner had deferred it on 2026-11-11 until Step 6 was built, and skipped Step 6 on 2026-11-24. The facts below were
checked on 2026-11-11. The readiness check, [doc/integration/00-readiness-check.md](doc/integration/00-readiness-check.md), re-checked them,
pinned new upstream commits (Strom `0df2e4b`, OpenLive `31fe60c`, Studio `b42e886`; MXL stays on v1.1.0), and corrected two of them: the upstream
checkouts had gone and were cloned again, and Strom's mixer also has a CPU compositor that can output `v210` directly. Where the table below
and that page disagree, that page wins.

**Target chain.** Two Nandi camera inputs → decode → MXL source taps → MXL flows → Strom vision mixer (cut, auto, audio mix,
graphics) → MXL programme output → Nandi MXL input or Step 5 encoder workers → HLS / DASH / SRT. OpenLive Studio is the operator UI.
OpenLive's backend drives Strom. Raw media goes through MXL only, never through the browser. It runs on one host with a shared
domain (called Mode A). Several hosts linked by MXL Fabrics (Mode B) are out of scope since Step 6 was skipped.

**What is open before it starts** (checked in the code on 2026-11-11, Step 5 line updated since):
- Step 5 is not finished (no 1-hour run; WP6 not started); the flow registry and the encoder worker are built and tested.
- The config parser rejects an MXL URI that names a host, so every program must share the one host's domain.
- Q7, Q8 and Gate A were decided on 2026-11-11 (below); the mixer's own failure policy is not decided.

**Facts checked on 2026-11-11**

| Item | Value |
| --- | --- |
| Strom | `../strom` at `f125401` (upstream main, clean). Rust/Axum backend, egui frontend, MIT/Apache-2.0. Image: Ubuntu 26.04, **GStreamer 1.28** from apt. No MXL. |
| OpenLive backend | `../open-live` at `157cde6` (clean). TypeScript, Node 22, CouchDB, zod. Source types `srt, efp, whip, test1, test2, html, clip`; output types `mpegtssrt, efpsrt, whep, recording, rtmp`. MIT. No MXL. |
| OpenLive Studio | `../open-live-studio` at `cad8bb1` (clean). React 19, Vite, Tailwind. Sources form: `src/pages/SetupPage/SourcesPanel.tsx`. MIT. |
| MXL | Nandi uses v1.1.0 (`8f3f109`), still the newest tag; main at `3aaa8ed`. Nandi builds `gst-mxl-rs` against **GStreamer 1.24** (Ubuntu 24.04). Strom would need it built against 1.28, from the same libmxl tag. |
| Strom's mixer output | The GPU mixer emits **RGBA in GL memory**. Strom's block rules say the consumer adapts its input. An MXL output must download it and convert to v210. That costs CPU, and the pipeline is not zero-copy. Nandi measured BGRA→v210 at 7.75 ms per 1080p frame. |
| This host | RTX 5060 Ti 8 GB, 28 cores, 15 GB RAM (about 9 GB in use). A 1080p v210 frame is about 5.5 MB per flow slot, so RAM limits how many 1080p flows can run. |

**Strom and OpenLive parts to reuse**
- **Strom's NDI block** (`backend/src/blocks/builtin/ndi.rs`) is the template for an MXL block:
  - It implements `BlockBuilder` with `get_external_pads` and `build`.
  - It has a `mode` setting (combined, video or audio).
  - It is hidden from the palette when its plugin is missing.
- **Strom block registration:** blocks are registered in `builtin/mod.rs`.
- **Strom output conversion:** `gst::download_front::convert_front` downloads and converts on the output side.
- **OpenLive's `activateStromFlow`** (`src/lib/flow-generator.ts`) builds one branch per source type. Each ends in
  `builtin.videoformat` → `builtin.time_offset` → `mixer:video_in_N`.
- **OpenLive outputs** hang off `pgm_out` and `main_out` (`src/lib/default-flow.ts`).
- **OpenLive capability probe:** `readStromBlocks` is the pattern for checking what Strom supports.

**Work, in order** (each step built and tested before the next):
0. **Readiness check.** Re-check Step 5 in the code. Pin the upstream commits. Write the version matrix and the risks.
1. **Strom image with MXL.** MXL v1.1.0 and `gst-mxl-rs` built against GStreamer 1.28. Check whether the `mxlsrc` deadlock patch
   (`transcoder/patches/mxl/010-…`) is still needed on the installed version. Run standalone `gst-inspect-1.0` checks and
   cross-container gst-launch round trips: Nandi → Strom, Strom → Nandi, reader before writer, and a writer restart.
2. **Strom blocks** `builtin.mxl_input` and `builtin.mxl_output`:
   - Input: domain from an allowlist, video and audio flow IDs, mode, system-memory outputs.
   - Output: raw inputs, conversion to v210, `async=false`. A stalled reader must not block `pgm_out`.
   - Unit tests. Strom's `pipeline_lifecycle_test` must still pass.
3. **OpenLive backend:**
   - Types and validation: an `mxl` source type and output type, with zod validation.
   - Flow generation: `flow-generator.ts` branches for the MXL source and output, and an actionable error when Strom lacks the MXL
     blocks.
   - Flow discovery: through the Step 5 registry (`GET /mxl/flows`, built).
   - Health: reported for each source and output.
   - Tests: the existing test suite must still pass.
4. **OpenLive Studio:** the MXL source type with video and audio flow pickers, an MXL programme output, and health and warning
   badges.
5. **Nandi side:**
   - Cameras: two camera channels with `mxlTap`.
   - Programme: a channel or Step 5 worker that reads Strom's programme and encodes several renditions.
   - SCTE-35: refuse SCTE-35 passthrough on an MXL input until Q7 is decided.
6. **Deployment:** a compose stack with Nandi, strom-mxl, open-live, studio and CouchDB, all pinned.
   - MXL domain: on a tmpfs volume.
   - Containers: none privileged, no host network, ports on 127.0.0.1.
   - Secrets: in a gitignored `.env`.
7. **End-to-end tests** that check the output media:
   - Switching: CUT and AUTO between two cameras, proven by image checks.
   - Sync and sources: lip-sync, video-only and audio-only sources.
   - Faults: a late writer, a lost camera, and restarts of Strom, Nandi and the workers. Activation and deactivation ×20.
   - Formats: 720p25, 1080p25, 1080p29.97 and 1080p50.
8. **Benchmark:** Nandi → SRT → Strom → SRT → Nandi, against the same chain over MXL.
   - Measures: CPU per process and system-wide, GPU, RAM and `/dev/shm`, latency p50/p95/p99, lip-sync and drift, drops,
     startup and recovery times, and the cost of the RGBA→v210 download.
   - Soak: a 4-hour run.
   - Results are reported as numbers. Pass or fail only once Gate A exists.
9. **Documents:** `doc/integration/` (architecture, implementation, configuration, deployment, testing, benchmark results, known
   limitations) and a handover.

**Rules**
- Changes to Strom, OpenLive and Studio go on local branches and are exported as patches into this repo. Nothing is pushed upstream,
  and no Nandi code goes into those public repositories.
- No failure policy is invented. Where the mixer's behaviour on a lost source is undecided, it is recorded and left to the owner.
- SCTE-35 now travels in an MXL data flow between Nandi engines (Step 5). Strom does not read or write it, so SCTE-35 through the Strom mixer is not
  supported; a configuration that needs it there is refused, not silently dropped.
- At most 4 heavy runs at once on the shared host.

**Done when** (the owner's acceptance demo):
- Two independent Nandi cameras go through MXL into the Strom mixer.
- The operator cuts and transitions from OpenLive Studio.
- The programme comes back out through MXL.
- Nandi or a Step 5 worker encodes it, and the output plays and decodes.
- Lip-sync, resources and latency are measured.
- Error and restart behaviour is tested.

### Gaps in what is already built

Not covered by any test:

- Dolby SDK paths (**integration gated**), for the taps and for the MXL input.
- A channel with a second input and a tap on either input; an MXL input as the second (alternate) input.
- An MXL input that is redundant or a playlist item (refused), interlaced or data flows, more than one audio flow.
- End-to-end latency of a loopback (the input adds its delay, 500 ms unless set; nothing more was measured), and a channel that stalls for
  longer than a flow's ring.
- Hardware decoders (the decoders here are software; a tap holding 30 frames would hold decoder surfaces).
- More than one audio track (only the first is tapped; WP6, not started).
- A 1080p programme flow (its cost is the case to watch against the CPU limit), a 1080p50 or 59.94
  programme flow, an interlaced programme output, graphics overlays in the programme picture, HLS or DASH
  outputs alongside an MXL output.
- Runs of more than four hours (the longest soak is 4 hours).

Not measured: write-to-read latency (p50/p95/p99); memory bandwidth; drift of the plugin's once-sampled
clock offset over hours.

Not done in Step 1: the `--build-arg` pass-through in `docker/build-docker.sh` (pass it to `docker build`
directly).

The built channel UI bundle was stale after Step 4; it was rebuilt on 2026-11-06 (job 1 above).

### Open questions about the behaviour seen

- **Memory (fixed, found by the 4-hour soak):** the engine that writes flows grew 17.9 MB per hour in a straight line. The cause is not MXL: the
  H.264 decoder in Ubuntu's gst-libav 1.24.1 leaks one 104-byte `AVPacket` per picture, in any channel that decodes H.264. The patch is in the base
  image. One thing is left unexplained: under heavy host load a patched channel showed a one-off step of 20 to 360 MB early in the run (larger with
  the MXL output), after which it stayed flat; it was not seen on a quiet host and its buffers were not identified. A full quiet-host hour of a
  programme channel with MXL output has not been run.
- **Lip-sync in the loopback soak:** the 20-minute run read −50 ms at the start and −10 ms in the middle and at the end, a one-frame step. The
  4-hour run read −93 ms at the start, in the middle and at the end: no step. Why the two runs differ in the absolute value (and from −61 ms in
  test `02`) is not known; the planned five repeats of test `02` were not run.
- **Audio flow lag:** the audio flow's lag behind the clock is mostly 305 ms but has single-minute bursts up
  to about 390 ms. There is no trend; the cause is not known. The planned 20-minute check that the bursts happen only in the first 30 s was not run.
- **Two intermittent test failures on 2026-11-05.** `input/11-delay` exited without a message in the first full run (it captures B's output
  with `ffmpeg`; a failed capture ended the script silently) and passed in all 8 later runs; the test now reports a failed capture in words.
  `input/03-writer-restart` failed once because its 2 s probe sometimes holds less than a second of usable picture (a capture starts mid-GOP and
  ffmpeg drops what comes before the first keyframe); the probe now judges the share of distinct pictures. The first is unexplained and has not repeated; its 20-run check was not done.
- **Writer-restart recovery time (explained):** the 5.3 s and 15.8 s figures came from the probe step; recovery by the logs is about 0.3 to 1 s.
- **Picture later than sound in the programme output (explained):** the video flow is written 160 to 180 ms after the audio flow, measured against
  their media times. It is decoder latency (B-frame reordering about 80 ms, frame-threaded decoding about 80 ms). The input's delay setting copes with it.
- The `utpl_scte_section_test` failure (fixed): `ScteEvent` left its `in` and `out` flags uninitialised; they are now set, and the test passed 400 of 400 runs.
- About 9 s of audio silence in one muxer-based reader was not explained.

### Decisions waiting for the project owner

| Decision | Current state |
| --- | --- |
| Gate A numeric limits (lip-sync, drops, latency, CPU) | **Approved 2026-11-11** for the worker: lip-sync within one frame (40 ms at 25 fps) of the in-process output; 0 drops in 1 hour; added latency at most `delayMs` plus 2 frames; CPU at most 1.25 times in-process; SCTE cues within one frame of the authored splice time, 20 of 20 (reworded 2026-11-12). **CPU on the installed images (2026-11-25): met at 1080p25, one run of five over at 720p25 (1.39); the scratch build had read 1.33 to 1.39.** Limits for the other steps are still provisional. |
| Default delay of an MXL input (500 ms) | Chosen from measurements, 140 ms above the worst spread seen; not agreed against a latency limit. The learning shift is built (job 3), but 1080p measured 669 ms once, so 500 ms stays. |
| Report the `mxlsrc` deadlock to the MXL project | The patch is in the images. A neutral report, reproduction and diff are ready in `../experiments/mxlsrc-report/` (no product or company named); reporting is outward-facing and **not done**. Before posting: check the GitHub profile, add nothing from the project, and check the employer's open-source policy before sending code (the report can describe the change in words instead). |
| Fail-open for a failed tap | Assumed and built; confirmed for outputs by Q8 on 2026-11-11 (fail open everywhere). |
| Where the SCTE-35 timeline lives (Q7) | **Decided 2026-11-11:** Nandi's main engine stays the master; workers receive the finished cue list over an MXL data flow. Built (WP2). |
| Stop condition for the project (Q18) | Proposed in [42, section 7](doc/codebase/42-mxl-implementation-plan.md#7-proposed-defaults-for-the-open-decisions), not approved. |
| Step 6, MXL Fabrics | **Decided 2026-11-24: skipped.** The OpenLive / Strom integration is limited to one host, and its multi-host acceptance item is dropped. |
| Q8 (fail policy per output type) | **Decided 2026-11-11:** fail open everywhere. A per-output `failPolicy` is not built. |
| Other section 7 defaults of the plan | Not approved. |
| Worker CPU limit | Met at 1080p25 on the installed images, so no change to the limit is needed for that format. Open: the 720p25 run over it (1 of 5), and why the scratch build read higher. |
| WP6 (more than one audio track) in or out of Step 5 | Open. |
| Commit WP5 | Done 2026-11-24 (`e9b21da`). |

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
| `5cbf012` | Rebuilt channel UI for the MXL input |
| `9d6e197` | MXL input: learn each flow's age, relearn when late, retry a lost writer every 900 ms |
| `6650595` | H.264 decoder memory-leak fix (gst-libav) and `ScteEvent` flags |
| `42d0842` | The MXL integration report |
| `69548e4` | Step 5 plan, change log, SCTE-35-over-MXL alignment trial |
| `2eeea6f` | Step 5, WP4: flow registry, `GET /mxl/flows` |
| `0c7d6bc` | Step 5, WP2: SCTE-35 over MXL |
| `8b9f845` | Step 5, WP3: external encoder worker, device-independent A/V alignment |
| `e9b21da` | Step 5, WP5: failure-behaviour tests (`worker/04` to `08`) and docs |
