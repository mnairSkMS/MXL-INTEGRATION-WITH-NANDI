# MXL integration with Nandi: what was done, what was tested, and how it compares with the unmodified transcoder

**Date:** 2026-11-09. **Repository:** this checkout (`main`). **Written for:** the project owner and the engineers who will decide on
Step 5 (an external encoder worker) and Step 6 (MXL Fabrics).

All numbers come from runs made in this checkout and are recorded with their raw output in `doc/mxl/` and in the handbook pages
[41](../codebase/41-mxl-integration.md) and [27](../codebase/27-known-gaps-contradictions-and-remediation.md). Where this report and the source
disagree, the source wins. Items that depend on hardware or software that was not available here are marked **integration gated**.

---

## 1. Summary

| Question | Answer |
| --- | --- |
| Can Nandi publish what it decodes as MXL flows? | **Yes.** Source tap (`inputs[n].mxlTap`). 14 tests pass. |
| Can Nandi publish its finished programme as MXL flows? | **Yes.** Programme output (`outputs[n].type = "mxl"`). 6 tests pass. |
| Can Nandi read MXL flows as a channel input? | **Yes.** `transport.uri = "mxl:///<domain>"`. 12 tests pass, including a loopback of one channel into another and a 4-hour soak. |
| Does adding MXL change the existing engine? | **Not measurably.** Same CPU, memory, threads, latency and output as the unmodified engine when no MXL feature is used (section 4.1). A tap costs extra CPU only on the channels that enable it (section 4.2). |
| Does a failing MXL write stop a channel? | **No.** Taps are fail-open: a missing or read-only domain, a killed reader or a stalled reader leave the on-air output clean (section 5.3). |
| Were faults found in the existing product? | **Yes, three**, all fixed in this checkout: a memory leak in every H.264-decoding channel, an uninitialised-flag bug in SCTE-35 handling, and a stale built UI (section 6). |
| What is not done? | The external encoder worker (Step 5), the flow registry, and MXL Fabrics (Step 6, needs a second host and network cards). Gate A limits are not agreed (section 8). |

Test totals on the rebuilt images: **41 of 41 MXL tests** (9 SDK, 14 source tap, 6 programme, 12 input), **12 of 12** engine unit tests,
**72 of 72** front-end tests, **346 of 346** API tests. Some tests failed in a few full-suite runs while the shared host was overloaded (kernel
out-of-memory killer, Docker restart); each passed when run alone. Those failures are load artifacts, not product faults.

---

## 2. What "vanilla" means here

"Vanilla" is the engine as received: the image tagged `utpl/transcoder-test:baseline`, built with no MXL (`ENABLE_MXL=0`), measured before any
change on 2026-10-30. The `:mxl` images add MXL v1.1.0 and the changes below. For each comparison the report states whether it is
"same channel with and without the feature" (a fair comparison) or "different workloads" (not comparable).

---

## 3. What was built

| Step | Deliverable | Where | Done |
| --- | --- | --- | --- |
| 0 | Baseline measurements of the unmodified engine | `tests/manual/mxl/baseline.sh`, `doc/mxl/baseline-2026-10-30/` | 2026-10-30 |
| 1 | MXL v1.1.0 SDK, tools and GStreamer plugin (`mxlsink`, `mxlsrc`) built into `utpl/base`; tmpfs volume for the MXL domain | `docker/utpl-base/Dockerfile`, `docker-compose.yml` | 2026-10-30 |
| 2 | **Source tap**: a decoded input published as one video and one audio flow, in a side pipeline | `inputs[n].mxlTap`, `MxlTap`, `MxlFrameGrid` | 2026-10-30, gaps closed 11-02 |
| 3 | **Programme output**: the finished programme (after compositing and audio processing, before encoding) published as flows | `outputs[n].type = "mxl"` | 2026-11-02 |
| 4 | **MXL input**: a channel reads MXL flows as its input; also in the channel form and API | `transport.uri = "mxl:///<domain>"`, `transport.mxl` | 2026-11-05 |
| 4a | Patch for an `mxlsrc` deadlock (plugin holds a lock while pushing caps) | `transcoder/patches/mxl/010-...patch` | 2026-11-05 |
| 4b | Input delay as a setting (`transport.mxl.delayMs`, default 500 ms) | `input/single.cpp`, schema, OpenAPI, UI | 2026-11-05 |
| 4c | **Delay learning**: learn each flow's age at start, relearn when persistently late, retry a lost writer every 900 ms | `input/single.cpp` | 2026-11-08 |
| 4d | Built channel UI rebuilt so it carries the MXL transport and `delayMs` | `api/src/transcoder_api/static/channel/` | 2026-11-06 |
| fix | H.264 decoder memory leak (gst-libav) patched in the base image | `transcoder/patches/gst-libav1.0/010-...patch` | 2026-11-09 |
| fix | `ScteEvent` uninitialised `in`/`out` flags | `lib/utpl/include/utpl/hls/types.h` | 2026-11-09 |

The channel form can edit an MXL input. A hand-written `mxlTap` block or a `mxl` output survives a save in the UI and in the API. The API
refuses to probe an MXL input.

---

## 4. Comparison with the unmodified transcoder

### 4.1 Does MXL in the image change the engine? (same workload, `:baseline` against `:mxl`)

Workload: generated 1080p25 H.264/AAC source over UDP into the real engine; outputs x264 `veryfast` + `fdkaacenc`, 1080p 5 Mbit/s plus 720p
2.5 Mbit/s; 15 s warm-up, 30 s measured; one run per row on a shared 28-core host (read differences under about 5 % as noise).

| Measure (2 outputs) | Unmodified (`:baseline`) | With MXL in the image (`:mxl`), no MXL feature used |
| --- | ---: | ---: |
| Engine CPU | 1.28 cores | 1.30 cores |
| Resident memory | 367 MB | 364 MB |
| Threads | 158 | 158 |
| Elements / pad links in the graph | 147 / 211 | 147 / 211 |
| Latency, UDP in to UDP out (p50 / p95 / p99) | 1492 / 1531 / 1532 ms | 1492 / 1532 / 1533 ms |
| Latency, UDP in to preview sink (p50) | 805 ms | 805 ms |
| Engine smoke test output (`engine-smoke.sh`) | AC-3 5.1 + 640×360 H.264 at 25 fps | identical |
| Image size | `utpl/base` | +16 MB (about 1.2 %) |

Reference points for the unmodified engine: one 1080p output costs 0.95 core and 325 MB; a second (720p) output adds about 0.33 core and 42 MB.
The engine's own latency is about 1.5 s with a spread of about 40 ms; it is the engine's buffering, not MXL.

### 4.2 What a tap or an MXL output costs (same channel with and without the feature)

| Feature | Test | Result |
| --- | --- | --- |
| Source tap, 720p25 | `tap/03` | Engine CPU 0.38 → 0.40 core (earlier run 0.36 → 0.42). Same streams, same frame count, zero decode errors. |
| Source tap, 1080i25 (with deinterlace) | `tap/08` | 0.57 → 0.63 core. |
| Source tap, range over formats | `tap/11` and others | +0.02 to +0.06 (720p25), +0.02 to +0.11 (1080i25), +0.05 to +0.18 (1080p29.97), +0.11 to +0.15 (720p59.94), +0.15 to +0.40 (1080p50). Rises with pixels per second; the host varies 0.1 to 0.2 core run to run, so read these as ranges. |
| Programme output, 720p25, shared canvas | `programme/02` | 0.38 → 0.48 core (installed engine); +0.10 to +0.16 core. Same streams and picture size; video frame counts within 25 over two 10 s captures; no decode errors. |
| Programme output at 1080p25 | measured conversion cost | The BGRA → v210 conversion costs 7.75 ms per 1080p frame, about 0.18 core at 25 fps. The project owner accepted a limit of about 0.18 core for this on 2026-10-30. |
| Programme output at its own size | `programme/04` | A 720p25 source with a 720p on-air output and a 1920×1080 MXL output: the flow is 1920×1080 with a real picture (mean luma 122.8), and the on-air output stays 1280×720 and clean. |
| Reading flows (MXL input) against reading H.264 over UDP, same picture and output | `input/09` | 720p25: **0.25 core** against 0.37; 1080p25: **0.59** against 0.80. The MXL input is cheaper because it decodes nothing; the v210 → canvas conversion costs 1.4 ms per 1080p frame (through I420), against 7.7 ms direct. |

### 4.3 Sound and picture alignment (flash-to-beep, 14 pairs per measurement; resolution about one frame, 40 ms)

| Path | Offset |
| --- | --- |
| Generated source | 0 ms |
| Through the source tap's flows | +14 ms (earlier runs +2, +31); +19 ms in the longer run |
| Through the programme output's flows | −1 ms (scratch build), +5 ms (installed) |
| The engine's own on-air output, measured while a tap runs | −41 to −43 ms; the encoders add it, it is not the tap (the tap flows stay with the source) |
| Through a loopback (channel A's flows, A's on-air output, channel B's output) | −5/−7 ms, −42/−43 ms, −56/−61 ms: B is 14 to 18 ms from A's on-air output |
| 4-hour loopback, channel B's output | −93 ms at start, minute 120 and end: no step, no drift |

The provisional limit is 80 ms from the source (Gate A not agreed). Run to run on the shared host the offsets move by 10 to 25 ms; the five-repeat
spread check of test `02` was **not run**.

---

## 5. What was tested

### 5.1 MXL itself (SDK, 9 tests, all pass)

Video v210 and Float32 audio pass through bit-for-bit (frame checksums equal to a no-MXL reference); a tone on channel 2 of 6 arrives on channel 2
only; 25, 29.97, 50, 59.94 and 60 fps run in real time with the exact rational rate reported; a killed writer is detectable and a new writer with the same
id works; a stalled reader does not slow the writer (a 300-frame writer finished in 10.08 s for 10.01 s of media); a reader in another container, as another
user, or on a read-only mount works; an undersized tmpfs fails visibly. Existing plugins (`fallbackswitch`, `dlbpatenc`) still load.

### 5.2 Source tap (14 tests, all pass)

Flow content; lip-sync; on-air output unchanged; readers stopped for 6 s and killed (largest on-air gap 40 ms video, 21 ms audio); unusable domain;
bad configuration (6 cases refused with a message naming the key); picture size change 720p → 1080p while running (the flow followed); interlaced
1080i25; redundant two-source inputs (the tap starts once; the audio follows the failover); playlist inputs (one flow through every item); frame rates
1080p50, 720p59.94 and 1080p29.97; AC-3 stereo and 5.1 (the channel layout is exact: tone on the centre channel only, −18.1 dB against −91 dB elsewhere);
SCTE-35 source (6 sections read, exactly two taps start, on-air output clean); source dropouts (a 1.5 s gap does not restart the taps; an 8 s gap closes
the flows after 3 s and reopens them when the source returns); a unit test of the frame grid (10 cases, including 5000 random frames and 100000 frames at
29.97).

### 5.3 Fail-open behaviour (the engine's output is protected from MXL faults)

| Fault | Result |
| --- | --- |
| Reader stopped 6 s, then killed (SIGKILL) | Largest on-air gap 40 ms video, 21 ms audio; no decode errors; a new reader joins afterwards. |
| Domain path that cannot be created / read-only | The engine keeps running, the on-air output is clean, the failure is logged; 8 start attempts for two taps in about 24 s. |
| Source gone 8 s (programme output) | The video head still advanced 125 of 125 grains in 5 s: the channel keeps writing. |
| Wrong configuration | Refused at start with the key named: 6 cases (tap), 8 cases (programme output), 14 cases (input). |

### 5.4 Programme output (6 tests, all pass)

Flow content (tone −21.1 dB, 4 kHz band −54 to −46 dB, 50 of 50 frames with at least 40 different pictures); on-air output unchanged; lip-sync; own picture
size; source stops; eight wrong configurations refused. The audio head ran about 13 % ahead of real time for part of a source outage (the channel's
silence fill); a reader that aligns by the head should not assume exactly one second per second after an outage.

### 5.5 MXL input (12 tests, all pass)

| Test | Result |
| --- | --- |
| Loopback (`01`) | A (UDP source → programme output) → B (MXL input → x264/AAC over UDP): 1280×720 at 25/1, 55 to 57 of 60 sampled pictures different, tone −20.5 to −21.1 dB, largest gap 40 ms video and 21 ms audio, no decode errors or log errors. |
| Lip-sync (`02`) | See 4.3. |
| Writer killed and restarted (`03`) | B keeps running, shows nothing of A while it is dead, and shows A again after A publishes. Recovery measured by logs is about **0.3 to 1 s** (3 s no-data limit, then the new 900 ms retry). Earlier readings of 5.3 s and 15.8 s came from a slow measuring step in the test, not from the engine. |
| Flows missing at start (`04`) | B started 10 s before any flow exists waits, then shows A's moving picture (39 of 40 samples different). |
| Wrong configuration (`05`) | 14 wrong inputs refused with the key named; a correct input starts. |
| One flow (`06`) | Video only: no audio stream, picture moving. Audio only: audio stream only, 1 kHz tone −6.0 dB left, −91 dB right. |
| Frame rates (`07`) | 25.000, 29.970, 50.000 and 59.940 fps measured; largest gaps 40, 33, 20 and 17 ms. |
| Writer replaced (`08`) | A red 640×360 flow replaced by a blue 1280×720 one: output stays 1280×720, no decode errors. |
| CPU (`09`) | See 4.2. |
| Republished (`10`) | An MXL input with its own tap and a programme output: both pairs of flows carry the moving picture (50 of 50 frames). |
| Delay (`11`) | Default 500, 1000 and 2000 ms: continuous output, no flow late. 0 ms: reported late within seconds, naming `transport.mxl.delayMs`. |
| Relearn (`12`) | New in this round: after the flows are disturbed the input prints that it learned the shift again; room is not negative. |

### 5.6 Soaks

| Run | Reader B (MXL input) | Writer A (programme output) |
| --- | --- | --- |
| 20 minutes (scratch build) | 0 reattaches, 0 errors, memory flat (one 2.4 MB step), CPU 0.29 core | +0.3 MB per minute (the decoder leak, below); video flow behind the clock mean 123.5 ms, trend +0.9 ms per hour: no drift |
| **4 hours** (2026-11-06, installed images) | 0 reattaches, 0 errors, nothing restarted; memory flat at 198.8 MB from minute 90 (−0.4 MB per hour); CPU 0.28 core | 259.6 → 335.7 MB: a straight line of **17.9 MB per hour** (cause found afterwards: section 6.1); CPU 0.41 core flat |
| After the leak fix (62-minute runs) | n/a | Plain decode, a channel without MXL and a channel with the programme output are all flat in their clean window |

---

## 6. Faults found in the existing product and fixed

### 6.1 Every H.264-decoding channel leaked memory (not MXL)

- **Symptom:** the writer in the 4-hour soak grew 17.9 MB per hour in a straight line, about 430 MB a day.
- **How it was narrowed:** a channel with no MXL at all grew the same; so did `udpsrc ! tsdemux ! h264parse ! avdec_h264 ! fakesink`. The audio decoder,
  the demuxer, the GStreamer objects, the number of decoder threads, `MALLOC_ARENA_MAX=1` and B-frames were each ruled out. FFmpeg's own decoder, given
  the same stream, was flat.
- **Cause:** Ubuntu's gst-libav 1.24.1 (`gstavviddec.c`) allocates an `AVPacket` for every picture and only unrefs it, leaking its 104-byte struct each
  time (about 17 MB per hour at 720p25). Valgrind on a finite file source: **112,720 bytes lost in 52 blocks before the patch, none after**.
- **Fix:** `transcoder/patches/gst-libav1.0/010-avviddec-free-the-av-packet.patch` (frees the packet), rebuilt and installed by `docker/utpl-base/Dockerfile`.
  This is an improvement over the unmodified product: **every** Nandi channel that decodes H.264 had this leak, with or without MXL.
- **Left unexplained:** under heavy host load a patched channel showed a one-off step early in the run (+20 to +100 MB without MXL output, +250 to +360 MB
  with it), after which it stayed flat. It was not seen on a quiet host and its buffers were not identified.

### 6.2 `ScteEvent` left its `in` and `out` flags uninitialised

A time-signal that carried no start or end type left `in` holding whatever the memory held, so a break start sometimes also counted as a break end (wrong
HLS CUE-IN handling) and `utpl_scte_section_test` failed once. Fixed by initialising both; the test then passed **400 of 400** repeated runs.

### 6.3 The built channel UI was stale

The committed UI bundle carried neither the MXL transport nor `delayMs`. It was rebuilt as the Dockerfile does (type check, 72 of 72 front-end tests, build);
the API suite passes 346 of 346.

### 6.4 Faults in the new code, found and fixed while building

Tap: the writer refuses a frame in the same 40 ms slot as the last one (fixed with a frame grid); the side pipeline's queue had a hidden 200 KB limit
and dropped every other frame; file and playlist sources run up to 0.7 s ahead of the clock; every playlist item change rebuilt the tap. Programme output:
the flowline needed a ghost pad on the channel bin; the application's own validation rejects an unknown output type. Input: `mxlsrc` can deadlock a
channel (a patch is in the images and the engine also works around it, so a stock plugin works too), a reader never notices a new writer (a no-data
watchdog restarts it), timestamps are in the writer's past (one shared shift with a delay), and the 1 ms audio buffers needed `audiobuffersplit`.

---

## 7. The delay of an MXL input (the main cost of reading flows)

An MXL flow's timestamps are in the writer's past by the writer's own delay (about 0.8 s for a programme taken after processing), and a Nandi programme
output writes its picture 160 to 180 ms later than its sound. The input puts all flows on live time plus a single delay (`transport.mxl.delayMs`, default
500 ms). This delay adds to the input's end-to-end latency (the unmodified engine's UDP-in to UDP-out latency is about 1.5 s). The end-to-end latency of a
full MXL chain was **not measured**.

- **Cause of the 160 to 180 ms:** decoder latency in the writer (B-frame reordering about 80 ms plus frame-threaded decoding about 80 ms). The
  frame-threading share was not isolated by an experiment.
- **Learning (built 2026-11-08):** the input waits 250 ms to 1 s for the first buffer of every flow and takes the largest age, relearns after 3 s of
  persistently late buffers (at most every 10 s), and retries a lost writer every 900 ms.

| Condition (delay actually needed, with learning) | Result |
| --- | --- |
| 720p25, idle host | 4 ms |
| 720p25, host busy | 52 ms |
| 1080p25, idle host | 669 ms (one stalled second) |
| Without learning (earlier, three-minute runs) | spread 185 to 260 ms; 358 ms in one 25 s run with every core busy |

**Decision taken: the default stays 500 ms**, because of the 1080p result and the busy-host spread. It is a margin chosen here, not an agreed limit.

---

## 8. What is not done, not measured, or not agreed

| Item | State |
| --- | --- |
| Step 5: external encoder worker reading MXL, flow registry | **Not started.** Waits for the owner's decisions Q7 (where the SCTE-35 timeline lives), Q8 (fail policy per output type) and Gate A. |
| Step 6: MXL Fabrics between two hosts | **Not started; integration gated** (needs a second host and network cards). |
| Gate A numeric limits (lip-sync, drops, latency, CPU) | Not agreed; tests print results and use provisional limits. |
| 500 ms default against a latency limit | Not agreed. |
| Stop condition for the project (Q18) | Proposed in page 42, not approved. |
| Reporting the `mxlsrc` deadlock to the MXL project / noting the gst-libav leak upstream | Reports prepared, **not posted** (outward-facing; the owner's call). |
| Repeat checks | Not run: `input/11-delay` ×20 (a rare silent exit once, an error trap now names the failing line); lip-sync ×5; a 20-minute check that audio-lag bursts occur only in the first 30 s; a repeat of the 1080p jitter run; a full quiet-host hour of a programme channel with MXL output. |
| Not covered by tests | Dolby paths and GPU or hardware encoders (**integration gated**); SRT/RTMP inputs through the taps; graphics overlays; memory bandwidth; drift beyond 4 hours; end-to-end latency of the MXL chain; a second audio flow; SCTE-35 travelling in a flow (it does not). |
| Baseline coverage | Phase 0 used one reference workload (1080p25, two outputs, one run per row, shared host). 50/60 fps, interlaced input, long runs and hardware encoders were not baselined on the unmodified engine. |

---

## 9. How to reproduce

| What | Command or file |
| --- | --- |
| All MXL tests | `docker run … utpl/transcoder-test:mxl … bash run.sh mxl` (see `tests/README.md`) |
| Baseline | `tests/manual/mxl/baseline.sh` |
| Soak | `tests/manual/mxl/input-soak.sh <minutes>` |
| Delay measurement | `tests/manual/mxl/input-jitter.sh` |
| Memory slope per pipeline variant | `tests/manual/mxl/writer-memory.sh` |
| Engine unchanged check | `tests/manual/mxl/engine-smoke.sh` |
| Details and raw output | [41](../codebase/41-mxl-integration.md), [27](../codebase/27-known-gaps-contradictions-and-remediation.md), `doc/mxl/` |

Run the heavy tests on a quiet host. At most four heavy runs at once; a busy shared host caused the only failures seen in full-suite runs.
