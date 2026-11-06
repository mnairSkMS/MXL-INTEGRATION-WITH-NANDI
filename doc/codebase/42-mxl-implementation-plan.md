# MXL Implementation Plan

**Status (2026-11-02): Steps 0, 1, 2 and 3 are done (baseline, MXL in the images, source tap, programme tap); Steps 4–6 are plan only.** Element names
and paths were read from this checkout's source. The performance figures in section 4 are
**illustrative calculations** from frame sizes, except where marked **measured**; the measured
conversion and ring costs are in [41, Phase 1 results](41-mxl-integration.md#phase-1-results) and
where they differ from the calculations, the measurements win.

Read [41](41-mxl-integration.md) first for background, release status and open decisions. Paths are
relative to `transcoder/lib/utpl/src/utpl/transcoder/` unless shown otherwise.

**Ground rules (agreed 2026-10-30):**
- **No separate MXL image.** MXL is built into the existing image chain (`utpl/base` → `utpl/devel` →
  `utpl/transcoder` → `utpl/transcoder-test`).
- **Changes to the current codebase are allowed** wherever integration and testing need them,
  including Dockerfiles, compose files, the media graph, parser, API, UI and tests. The AGENTS.md
  contract and validation rules still apply.

## 1. Summary

- **Nandi's default media path is not replaced.** Every channel without MXL configuration builds
  exactly the graph it builds today.
- **Most MXL work adds elements.** A publishing tap is a new branch off an existing `tee`, and the
  existing branches keep running.
- **Only two changes replace existing elements,** and only for channels that opt in:
  1. An **MXL input** replaces the network source, demuxer and decoder for that input with `mxlsrc`
     elements.
  2. An **output moved to an external encoder worker** removes that output's conversion, encoder,
     muxer and sink from Nandi. They are rebuilt in the worker behind an `mxlsrc`.
- **The cost is mostly memory bandwidth and pixel conversion.** The programme canvas is 8-bit
  **BGRA**, so publishing it as V210 is an RGB → YUV 4:2:2 10-bit conversion, not a repack. Moving an
  encode out of Nandi through MXL roughly triples the memory traffic for that output and leaves the
  encoding work unchanged ([section 4](#4-performance-impact)).

## 2. Today's element graph

These are the elements a transcoding channel builds today, in order. Optional elements are in
brackets.

| Stage | Video | Audio | Source |
| --- | --- | --- | --- |
| Source | `srtsrc` / `udpsrc` / `filesrc` / `rtmp2src` + `flvdemux` / `souphttpsrc` + `hlsdemux` or `dashdemux2` / `ndisrc` | (same source) | `input/single.cpp:486` onward |
| Split and decode | passthrough `tee` → `decodebin` (demux + parse + decoder, e.g. `avdec_h264`) → decoded raw video, usually I420 or NV12 | `decodebin` → raw audio | `input/single.cpp:238`, `:280` |
| Connector | `clocksync` (per input) → `input-selector` → `deinterlace` → `fallbackswitch` (0.5 s) → `compositor`, **BGRA** canvas → `tee` | `clocksync` (per input) → `input-selector` → `audioconvert` → `fallbackswitch` → `utplaudiomute` → `tee` | `channel/connector/complex/video.cpp:11`, `:65`, `:151–249`, `:426`; `audio.cpp:62–226`, `:488` |
| Per-consumer branch | `queue` → `capsfilter` → `videoconvertscale` → `capsfilter` (BGRA, size) → `videorate` → `capsfilter` (rate) | `capsfilter` → `audioconvert` → `audioresample` → `capsfilter` | `video.cpp:255–345`; `audio.cpp:244–291` |
| Processor | `identity` → [`tee` + `queue` when there is more than one flowline] | likewise; Dolby uses `dlbpat*` in its own connector | `channel/processor/transcoder/video.cpp:24`, `:118` |
| Flowline | `videoconvert` → `capsfilter` (output pixel format, default I420) → [`videorate`] → [`interlace`] → `encodebin` (e.g. `x264enc`) → [`tee` → `queue` per output] | `encodebin` (AAC, AC-3 …) | `channel/processor/transcoder/flowline.cpp:118–175`, `:328` |
| Mux and sink | `mpegtsmux` → `srtsink` / `udpsink`; `splitmuxsink` / `hlssink` / `awss3hlssink`; `isofmp4mux`; `dashsink2` | (same mux) | `output/`, `output/sink/` |

## 3. What changes, element by element

### 3.1 Phase 1: qualification inside the existing images (WS-01)

**Replaced: nothing in the media graph.** Channels build the same graph; the images gain MXL.

| Added | Where |
| --- | --- |
| `mxlsink`, `mxlsrc` (from `libgstmxl.so`, installed in the GStreamer plugin directory), `libmxl.so` (on the default library path), MXL tools (`mxl-info`, `mxl-gst-testsrc`, `mxl-gst-sink`, `mxl-gst-looping-filesrc`, `mxl-data-probe`; `mxl-video-probe` is not in v1.1.0) | `utpl/base`, so every image built on it has them |
| Test pipelines: `videotestsrc` → `videoconvert` → `capsfilter` (v210) → `queue` → `mxlsink`; `mxlsrc` → `videoconvert` → sink; the same for `audiotestsrc` → F32LE | `tests/tests/mxl/sdk/`, run by `tests/run.sh` in `utpl/transcoder-test` |

### 3.2 Processed-programme tap, CP2 (WS-03/WS-04)

**Replaced: nothing.** The tap adds one consumer to the connector `tee`, built by the same
`getPad(params)` branch builder the processors already use.

| Path | Added elements | Notes |
| --- | --- | --- |
| Video | `queue` (**leaky**, small) → `videoconvertscale` → `capsfilter` (`format=v210`, output size, `colorimetry=bt709`) → `videorate` → `capsfilter` (rate) → `mxlsink flow-id=… domain=…` | Reuses the `video.cpp:255–345` builder with a v210 parameter set. The tap is progressive already, because `deinterlace` runs before the compositor. |
| Audio | `queue` (leaky) → `audioconvert` → `audioresample` → `capsfilter` (`F32LE`, 48 kHz, N channels) → `mxlsink` | Reuses the `audio.cpp:244–291` builder. Dolby-processed audio needs the equivalent tap on the Dolby connector. |

The leaky `queue` is required. A `tee` waits for every branch, so without it a stalled `mxlsink`
would stall every on-air output in the channel. With it, the tap drops frames instead, which is the
fail-open behaviour. A fail-closed policy (decision Q8) would use a normal queue.

### 3.3 Decoded-source tap, CP1 (WS-03)

**Replaced: nothing.**

| Added | Notes |
| --- | --- |
| On each decoded input stream, after `decodebin`: `tee` → `queue` (leaky) → [`deinterlace`] → `videoconvert` → `capsfilter` (v210) → `mxlsink`; audio likewise to F32LE | Before the connector, so the media is **not** deinterlaced and **not** scaled to a fixed size. A playlist input changes size or rate between items, so the flow would need versioning or a fixed-format scaler. Build CP2 first. |

### 3.4 MXL input (WS-02)

**Replaced, for that input only:** the source element, the passthrough `tee` and `decodebin`
(demuxer, parser, decoder).

| Today | With `mxl://` input |
| --- | --- |
| `srtsrc` (or other) → passthrough `tee` → `decodebin` → raw video + raw audio | `mxlsrc video-flow-id=…` → raw v210; `mxlsrc audio-flow-id=…` → raw F32LE; [`mxlsrc data-flow-id=…` → ST 2038 ancillary data] |
| Everything from `input-selector` onward | **Unchanged.** The compositor's sink pad converts v210 → BGRA. |

NDI is the precedent: `ndisrc` already delivers raw media into the same graph (`input/single.cpp:509`,
`NdiInputTransportParameters`, parser `transport.ndi`). One difference is that `mxlsrc` reads exactly
one flow per element, so an MXL input has two or three source elements instead of one.

**Lost for an MXL input:**
- passthrough outputs (there is no compressed stream);
- in-band SCTE-35 (no TS sections), which need the separate event path (decision Q7);
- caption extraction from the bitstream, unless it is carried as ST 291 ancillary data;
- multi-programme selection.

### 3.5 Output moved to an external encoder worker (WS-05, Phase 3)

**Replaced, for each moved output only:** inside Nandi, that output's flowline and sink chain:
`videoconvert` → `capsfilter` → [`videorate`, `interlace`] → `encodebin` → mux → sink.

| Nandi keeps | Worker process (new) |
| --- | --- |
| The CP2 tap (3.2), shared by every moved output | `mxlsrc` (video) + `mxlsrc` (audio) → `videoconvert` (v210 → encoder format, e.g. I420) → [`interlace`] → encoder → mux → sink, the same elements Nandi uses today |

Outputs that are not moved keep their flowlines in Nandi. The encoding work does not go away; it
moves to the worker.

### 3.6 MXL Fabrics (WS-08, Phase 4)

**Replaced: nothing in Nandi.** A separate `mxl-fabrics` process replicates a flow from the local
domain to a remote host's domain. Remote workers use `mxlsrc` as in 3.5.

### 3.7 Unchanged in every phase

`clocksync`, `input-selector`, `deinterlace`, `fallbackswitch`, `compositor` and graphics,
`utplaudiomute`, the Dolby `dlbpat*` chain, and every flowline, mux and sink for outputs not moved
to a worker. Fill/key graphics from an external engine (a V210A `mxlsrc` as a compositor layer) are
deferred.

## 4. Performance impact

### 4.1 Frame sizes used

At 1920×1080 (ILLUSTRATIVE CALCULATION, payload only, no stride or allocator overhead):

| Format | Bytes per frame | MB/s at 25 | at 30000/1001 | at 50 | at 60 |
| --- | ---: | ---: | ---: | ---: | ---: |
| BGRA (compositor canvas) | 8,294,400 | 207.4 | 248.6 | 414.7 | 497.7 |
| V210 (MXL) | 5,529,600 | 138.2 | 165.7 | 276.5 | 331.8 |
| I420 (typical encoder input) | 3,110,400 | 77.8 | 93.2 | 155.5 | 186.6 |
| F32LE audio, 48 kHz, 2 ch / 6 ch | — | 0.38 / 1.15 | | | |

Audio is negligible next to video, so the figures below are video only, at 1080p60.

### 4.2 Change by change

**Measured CPU per 1080p frame** (single-threaded, from `tests/tests/mxl/sdk/09-conversion-cost`;
this host; see [41](41-mxl-integration.md#phase-1-results)). These replace the "CPU" estimates in the
table below:

| Operation | Measured | One core at 29.97 fps | At 59.94 fps |
| --- | ---: | ---: | ---: |
| BGRA → v210 (CP2 tap) | 7.1 ms | 21 % | 42 % |
| v210 → BGRA (MXL input into the compositor) | 8.1 ms | 24 % | 49 % |
| I420 → v210 (CP1 tap) | 0.6 ms | 2 % | 4 % |
| v210 → I420 (reader towards an encoder) | 0.6 ms | 2 % | 4 % |
| `mxlsink` write / `mxlsrc` read | 0.3 / 0.8–1.0 ms | 1 % / 3 % | 2 % / 5 % |

The write goes through memcpy-speed copies (0.3 ms for a 5.5 MB frame), so the "if `mxlsink` copies"
memory-traffic figures below should be taken as the copying case. Shared memory is **measured**:
27 MB for a 1080p29.97 flow, 60 MB for 1080p59.94, 12 MB for 720p29.97 (the plugin fixes the ring at
about 170–185 ms, and has no history setting).


| Change | What is added | Memory traffic added (1080p60) | CPU | Other effects |
| --- | --- | --- | --- | --- |
| **CP2 tap** (3.2) | BGRA → V210 conversion, then the write into the ring | Conversion: read 497.7 + write 331.8 = **829.5 MB/s**. If `mxlsink` copies into the ring (verify in Phase 1): +663.6 MB/s, about **1.5 GB/s** in total. | One extra conversion thread per tap: RGB → YUV matrix, 4:4:4 → 4:2:2, 8 → 10-bit packing. Unmeasured; may be a significant share of a core. | `/dev/shm`: 33.2 MB per 100 ms of history. Quality: 8-bit source in a 10-bit container gains no precision, chroma is halved horizontally, and the colour matrix must be set explicitly (BT.709, limited range) or colours shift. |
| **CP1 tap** (3.3) | Decoder output (I420) → V210 | Read 186.6 + write 331.8 = **518.4 MB/s**, plus a possible ring copy (+663.6) | Cheaper than CP2 (YUV → YUV), plus deinterlace if the source is interlaced | Same `/dev/shm` cost. |
| **MXL input** (3.4) | Ring read, then V210 → BGRA in the compositor | Compositor conversion: read 331.8 + write 497.7 = 829.5 MB/s, against 684.3 MB/s for today's I420 → BGRA; plus a possible copy out of the ring (+663.6) | **Saves the decode in this process** (H.264/HEVC decode, the heaviest step of ingest). The saving is real only if the raw source already exists on the host; otherwise the decode has moved to another process. | No network receive or jitter buffer for that input. |
| **Encoder moved to a worker** (3.5) | Nandi: CP2 tap. Worker: ring read and V210 → I420. | Today: BGRA → I420 = 684.3 MB/s. With MXL: 829.5 (tap) + 663.6 (ring copy, if any) + 518.4 (worker conversion) ≈ **2.0 GB/s**, about **3×** today | **Encoding CPU is unchanged; it moves.** Two conversions are added. | Gains: the encoder can crash or be upgraded without stopping Nandi. Loses: one process's GOP and PTS state; restart continuity needs work (Phase 3). |
| **Fabrics** (3.6) | Network replication | Per remote copy, network: **2.65 Gb/s** at 1080p60 (10.62 Gb/s at 2160p60) | libfabric progress threads; RDMA lowers CPU, TCP raises it | Does not fit 1 GbE. Usually loses to a compressed SRT feed unless the NIC budget is there. |

### 4.3 Latency

- **CP2 tap:** none added to the on-air path. The tap runs on its own `queue` thread, but it competes
  for cores and memory bandwidth.
- **Paths through MXL:** each adds at least the reader's chosen delay. MXL's reference
  `mxl-gst-sink` reads 40 ms behind by default (`--read-delay`). This is a hint, not a Nandi figure.
  Measure it in Phase 1 (T2) and Phase 2.

### 4.4 Where MXL saves resources

Only where a second process would otherwise ingest and decode the same source again: external QC, AI
or monitoring that today opens its own SRT/UDP connection. Inside one Nandi channel, decode is
already shared by `tee`s ([41](41-mxl-integration.md)), so MXL adds cost there and saves none. Its
value inside Nandi is process isolation and interoperability, which Gates B to D must show is worth
the cost in section 4.2.

## 5. Implementation plan

Each step lists the technical changes by layer. AGENTS.md treats parser, parameters, schema,
examples, API, UI and tests as one contract, so each feature step touches all of them.

Nandi creates GStreamer elements by name (`pipeline::Element::make("mxlsink")`). Using `gst-mxl-rs`
therefore needs **no C++ link against `libmxl`**: the plugin only has to be installed in the image.
Only the C API fallback ([section 6](#6-risks-that-change-this-plan)) would add a link dependency.

### Step 0: Repository and baseline (Phase 0, WS-00)

| Layer | Change |
| --- | --- |
| Repository | `git init`; initial commit of the tree as received. Each later step is its own commit, so any step can be reverted. |
| Baseline images | Build `utpl/base`, `utpl/devel`, `utpl/transcoder` and `utpl/transcoder-test` from this checkout **before** any MXL change, and tag them `:baseline`. After Step 1 every image contains MXL, so this is the only point where "today" can be captured. Run CTest. |
| Measurement | One reference channel (bars and tone → 1080p, 2 outputs). Record the DOT graph (`GST_DEBUG_DUMP_DOT_DIR`), CPU per thread, RSS, memory bandwidth if available, and glass-to-glass latency. This is test T0, and it is the "today" column in section 4. |
| Docs | Record the results in 41. |

### Step 1: MXL in the existing images, SDK qualification (Phase 1, WS-01) — DONE 2026-10-30

**Status of this step:**

| Item | State |
| --- | --- |
| `utpl/base` builds MXL, tools and plugins; all images inherit them | Done. Verified in `utpl/transcoder-test:mxl`. |
| Compose tmpfs domain volume | Done in `docker-compose.yml` (not yet used by any channel). |
| SDK tests 01–09 and the helper | Done. 9 of 9 pass in `utpl/transcoder-test:mxl`. |
| Cross-container, different-user, read-only and undersized-tmpfs checks | Done, as the host script `tests/manual/mxl/cross-container.sh` (needs Docker). |
| No regression in the engine | Shown with `tests/manual/mxl/engine-smoke.sh` on `:baseline` and `:mxl` (identical output). The existing suites do not run in this environment ([41](41-mxl-integration.md#environment-constraints)). |
| Conversion cost, ring size and memory | Measured ([41](41-mxl-integration.md#phase-1-results)). |
| `docker/build-docker.sh` build-argument pass-through | **Not done.** The script is unchanged; pass `--build-arg` to `docker build` directly. |
| Read the `gst-mxl-rs` source: how buffer PTS map to the MXL grain index, what `sync` does, behaviour on a caps change | **Not done.** The clock decision is still open. |
| Publish-to-read latency p50/p95/p99 | **Not done.** |
| Phase 0 baseline of the engine (T0/T1: graph, CPU, RAM, latency) | Done for one reference workload (1080p25, 1 and 2 outputs): [41, Phase 0 baseline](41-mxl-integration.md#phase-0-baseline). 50/60 fps, Dolby, graphics and long runs are not covered. |
| Gate A numbers agreed; section 7 defaults approved | **Not done.** These need the project owner. |

The table below is the original plan for the step; the status table above is what happened.

| Layer | Change |
| --- | --- |
| `docker/utpl-base/Dockerfile`, builder stage (`base-builder`) | Implemented as a separate `mxl-builder` stage (so the GStreamer packages are not rebuilt). `ARG MXL_TAG=v1.1.0`, `ARG ENABLE_MXL=1`, `MXL_RUST_TOOLCHAIN=1.90.0`, `MXL_JOBS=4`. Install MXL's apt build dependencies and vcpkg. Clone `dmf-mxl/mxl` at the tag; build the SDK and tools with the `Linux-GCC-Release` preset; `cmake --install` into a staging prefix under `/tmp/built/mxl`. Build `gst-mxl-rs` with the rustup toolchain the stage already installs for `dashsink2` (`DASH_RUST_TOOLCHAIN=1.99.1`, which meets MXL's minimum of 1.87), adding `libclang-dev` if `mxl-sys` uses bindgen. Stage `libgstmxl.so`. |
| `docker/utpl-base/Dockerfile`, runtime stage (`base`) | `COPY --from=base-builder` the staged MXL files: libraries and tools to `/usr/local`, `libgstmxl.so` to the GStreamer plugin directory; then `ldconfig`. Elements and tools then work without `LD_LIBRARY_PATH` or `GST_PLUGIN_PATH`. Install any shared runtime libraries MXL needs that vcpkg does not link statically (found during the build). |
| `utpl-devel`, `utpl-transcoder`, `utpl-transcoder-test` | No Dockerfile change expected: `utpl/devel` and the final `utpl/transcoder` stage are built `FROM utpl/base`, and `utpl/transcoder-test` from `utpl/transcoder`, so all of them inherit MXL. `utpl/transcoder-test` already has `gstreamer1.0-tools` for `gst-inspect-1.0`. |
| `docker/build-docker.sh` | Same image list. Pass `--build-arg MXL_TAG` and `ENABLE_MXL` through, so MXL can be switched off if it ever breaks the base build. |
| `docker-compose.yml` (and any other compose file that runs an MXL channel) | A named tmpfs volume for the MXL domain (for example `mxl-domain`, `driver_opts: {type: tmpfs, device: tmpfs, o: "size=256m"}`) mounted at `/run/mxl`. Other containers mount it read-only to consume flows. The size comes from Step 1 measurements. `/dev/shm` is left alone. |
| `tests/tests/mxl/sdk/<case>/` (new) | One `test.sh` per Phase 1 check (install, video, audio, A/V, frame rates, cross-container, ring size, restart, slow reader, read-only reader, cost), using `gst-launch-1.0` and the MXL tools inside `utpl/transcoder-test`. Flow-definition JSON fixtures (v210 1920×1080 30000/1001 BT.709 progressive; float32 48 kHz 2 and 6 ch, fixed UUIDs) sit beside each test. If the existing `test_execute` helper cannot run a non-transcoder test, add a small helper to `tests/framework/`. |
| `tests/README.md` | How to run the suite: `tests/run.sh mxl`. |
| Source reading | Read `gst-mxl-rs` at the pinned tag and record the answers in 41. How do buffer PTS map to the MXL grain index (TAI)? Does `mxlsink` copy into the ring or write in place? What do `sync` and `latency` do? What happens on a caps change? These answers settle the "if it copies" figures in section 4.2 and the clock decision. |
| Measurement | Conversion cost per format (`videotestsrc` BGRA and I420 → `videoconvert` → v210), ring read and write cost, publish-to-read latency p50/p95/p99. Compare base-image size and build time against `:baseline`. |
| Docs | 03 (images now contain MXL; build arguments), 19 (external plugin), 23, 41. |
| Exit | Gate A on reference flows; the defaults in [section 7](#7-proposed-defaults-for-the-open-decisions) are confirmed. |

### Step 2: Source tap, CP1 (Phase 2, WS-03) — DONE 2026-10-30

Built **before** the programme tap (agreed with the project owner): it is cheaper (a decoded source is
already YUV), independent of graphics, fallback and Dolby code, and proves the whole path
Nandi → MXL → another process. Settings, design, results and findings are on
[41, Source tap](41-mxl-integration.md#source-tap-step-2); the key is `inputs[n].mxlTap`
([05](05-configuration-reference.md#input-mxl-tap)).

Differences from the plan this page first made:

| Planned | Built |
| --- | --- |
| `type: "mxl"` output | `inputs[n].mxlTap`, because a source tap belongs to an input, not to an output. |
| `mxlsink` in the main pipeline behind a leaky queue | `mxlsink` in a **side pipeline** fed through `appsink` → `appsrc` (as the input preview works), so an error cannot reach the channel's bus. |
| A clock adapter | None: the plugin maps pipeline time to MXL time; the side pipeline shares the channel's clock and base time. |
| `videoconvert` only | `deinterlace` first (progressive flow), and `audiorate` in the audio path (contiguous timestamps). |
| A configurable ring length | Not possible: `mxlsink` fixes the ring. |

Measured cost on this host: +0.02 to +0.06 core on 720p25 and 1080i25 channels (41). The programme tap
(Step 3) reuses `MxlTap` with a different attach point and a BGRA → v210 conversion.

**Gaps closed 2026-11-02** (all in [41](41-mxl-integration.md#source-tap-step-2)): redundant two-URI inputs
(tapped after the switch), playlist inputs (one flow through every item), 1080p50, 720p59.94 and 1080p29.97,
AC-3 5.1 and stereo, a source with SCTE-35, a source that stops and returns, and the UI and API round trip.
Not closed: the Dolby SDK paths (integration gated), a channel with a second input, and a run of hours
(a soak script exists). Four faults surfaced and were fixed: the writer's strictly increasing slots (the
`MxlFrameGrid`), the 200 KB default of `appsrc`, sources that run ahead of the clock, and a tap that was
destroyed at every playlist item change.

### Step 3: CP2 programme tap (Phase 2, WS-03/WS-04) — DONE 2026-11-02

Settings, design, results and findings are on [41, Programme tap](41-mxl-integration.md#programme-tap-step-3);
the key is an output with `"type": "mxl"` ([05](05-configuration-reference.md#output-mxl)).

Differences from the plan this page first made:

| Planned | Built |
| --- | --- |
| New output type and parameters: domain, flow UUIDs, size, rate, channel layout, fail policy | `OutputType::MXL` and a `mxl` block (domain, `video.{flowId,width,height,rate}`, `audio.{flowId,channels}`). No fail policy: the tap is fail-open (the decision was assumed, not approved). |
| Add `PixelFormat::V210`, its caps mapping, parser value and schema enum | **Not needed.** The v210 conversion is done in the tap's side pipeline (`videoconvert`), so no pixel-format enum, caps mapping or parser value was added. |
| Register the tap as a connector consumer through `getPad(params)`; terminate the branch in `mxlsink` behind a leaky `queue` | The output joins the existing processors like any output, through a new `Codec::MXL` and a flowline that does not encode (`MxlFlowline`); the tap (leaky `queue`, `appsink`, side pipeline with `mxlsink`) is the output's stream sink. No connector code changed. |
| Output wiring in `output/` and `channel.cpp` | `MxlOutput` (`output/mxl.cpp`), created in `engine.cpp`; `channel.cpp` only passes the MXL parameters on; `application.cpp` validates the new type. |
| Clock: no adapter, both taps in the same pipeline | As planned. Each tap's side pipeline shares the channel's clock and base time. |
| Parser `type: "mxl"` next to `single`/`group` | As planned; the input's and the output's blocks share one validation helper. `rate` also accepts a fraction string. |
| API and UI: Pydantic models and a form | No API model (the config passes through). The UI does not show the output and keeps it through a save. |
| Metrics: frames written, dropped, conversion time, last write | A statistics line in the log every 30 s (frames written; frames dropped and moved by the grid). No metrics endpoint. |

Measured cost: +0.16 core on a 720p25 channel with a shared canvas ([41](41-mxl-integration.md#programme-tap-step-3)).
The 1080p cost of an own canvas, a 1080p50 programme flow and Dolby audio were not measured.

### Step 4: MXL input (Phase 2, WS-02) — DONE 2026-11-05

Settings, design, results and findings are on [41, MXL input](41-mxl-integration.md#mxl-input-step-4); the key is
`transport.mxl` next to an `mxl:///<domain>` uri ([05](05-configuration-reference.md#input-mxl)).

Differences from the plan this page first made:

| Planned | Built |
| --- | --- |
| `mxl.videoFlowId` / `audioFlowId` | `transport.mxl.video.flowId` and `audio.flowId`, the shape of the tap and the programme output. The domain is the uri (`mxl:///run/mxl`); a host in it is refused. |
| Raw pads as input streams, `decodebin` skipped | One `decodebin` demuxer per flow still exposes the raw pad, as `ndisrc` does, so the stream creation and the decode step after it are unchanged. |
| v210 → BGRA in the compositor | v210 → I420 at the reader, then the ordinary I420 → BGRA: 1.4 ms per 1080p frame against 7.7 ms (measured). |
| A watchdog (poll `Active`, or a no-data timeout) | A no-data timeout: 3 s, then the reader is stopped and started again, repeating until data returns. |
| — (not foreseen) | A leaky `queue` behind each reader and a dropped ALLOCATION query, because `mxlsrc` holds its state lock while it pushes caps and deadlocks a channel whose pads are blocked until it plays. |
| — (not foreseen) | One shift for the flows' timestamps (live time plus `transport.mxl.delayMs`, default 500 ms), because a writer's timestamps are in the past by its own processing delay. |
| `GET /inputs/probe` describes MXL inputs with `mxl-info` | The probe answers 422 for `mxl` (as for `ndi`): the API image has no MXL tools. |
| UI | The channel form has an MXL transport with a video and an audio flow id; the probe button is disabled for it. The built UI bundle under `api/src/transcoder_api/static/channel/` was rebuilt on 2026-11-06 and carries the MXL transport and the delay field. |

Not planned and not built: data flows (ST 2038), a redundant or playlist MXL input, several audio flows.
Measured cost: 720p25 0.24 core against 0.36 for an H.264 UDP input; 1080p25 0.63 against 0.77.

The table below is the original plan for the step.


| Layer | Change |
| --- | --- |
| Parameters (`include/utpl/transcoder/input.h`) | `Protocol::MXL` and `MxlInputTransportParameters` (domain, video/audio/data flow UUIDs), on the `NdiInputTransportParameters` pattern. |
| Media graph (`input/single.cpp`) | A `mxl` scheme branch next to `ndi` (line 509). Build one `mxlsrc` per flow, skip `decodebin`, expose the raw pads as input streams, and mark the input live. |
| Parser, schema, API, UI | `transport.mxl`, as for `transport.ndi`. `GET /inputs/probe` describes MXL inputs with `mxl-info` instead of `ffprobe`. |
| Tests | `tests/tests/mxl/input/<case>/` (T8): 30000/1001, writer restart and discontinuity; output checked with `ffprobe` and TSDuck. A loopback test feeds one channel's CP2 tap into a second channel's MXL input. |
| Docs | 07, 05, 02, 26. |

### Step 5: External consumers and encoder worker (Phases 2–3, WS-05 to WS-07)

| Layer | Change |
| --- | --- |
| Worker | A container from `utpl/transcoder` running `gst-launch-1.0` (or a second Nandi instance with an MXL input): `mxlsrc` → conversion → encoder → mux → sink. Docker restarts it; no orchestrator is needed. |
| Nandi config | Outputs move to the worker by replacing them with one `mxl` output in Nandi. |
| Events | SCTE-35 delivery to the worker, by the decision in section 7. |
| Registry | Flow UUID persistence, one writer per flow, stale-flow cleanup (`mxl-info -g`). |
| Compose | A worker service sharing the `mxl-domain` volume read-only. |
| Tests | T5–T7, T9, T10: consumer restart, slow reader, multi-audio, SCTE during restart. |

### Step 6: Fabrics (Phase 4, WS-08)

No Nandi media-graph change. Two hosts run `mxl-fabrics` over TCP and then Verbs/EFA, compared
against a compressed SRT feed under contention (T11). Fabrics is built into `utpl/base` in Step 1 if
its dependencies (libfabric) allow; otherwise it is added here. Target-info exchange gets a security
review. Gate E decides, per topology.

## 6. Risks that change this plan

| Risk | Effect | Found in |
| --- | --- | --- |
| `gst-mxl-rs` does not build or run against GStreamer 1.24.2 | Use the MXL C API from C++ (a custom element in `transcoder/plugins/gstreamer/`, or an `appsink`/`appsrc` bridge). This adds a link dependency and more work. | Step 1 |
| The MXL build breaks or slows the base image | Every image depends on `utpl/base`. `ENABLE_MXL=0` restores today's build while it is fixed. | Step 1 |
| BGRA → V210 conversion is too expensive | Move the tap, or change the canvas to a YUV format, which is a wider change affecting graphics | Step 1 measurement, Step 2 |
| `mxlsink` blocks or waits on the clock unexpectedly | On-air stall unless the leaky queue is in place | Step 1 source reading, Step 2 tests |
| A reader on a dead flow waits forever (measured, test 07) | A Nandi `mxl` input needs its own watchdog (poll `Active`, or a no-data timeout) or a lost writer freezes the input silently | Step 4 (done: a 3 s no-data timeout reattaches the reader) |
| A failing `mxlsink` posts a pipeline error (measured: tmpfs full → "No space left on device") | Done for the source tap: the tap runs in a side pipeline, so the error never reaches the channel | Step 2 (done); the same design applies to Step 3 |
| Only the first audio stream reaches outputs ([27](27-known-gaps-contradictions-and-remediation.md)) | Per-language audio flows are impossible until it is fixed | Before Step 3 audio |
| No audio during source loss ([27](27-known-gaps-contradictions-and-remediation.md)) | Gaps in the MXL audio flow | Before Step 3 audio |

## 7. Proposed defaults for the open decisions

These let implementation start without waiting for a workshop. **Each needs the project owner's
approval.** Each can be changed later, with the cost shown.

| Decision | Proposed default | Reason | Cost of changing later |
| --- | --- | --- | --- |
| MXL version | `v1.1.0`, as a build argument | Latest stable; includes `gst-mxl-rs` and Fabrics | Rebuild the base image |
| Fail open or closed (Q8) | **Open**: leaky queue, errors logged and counted, on-air outputs continue. A per-output `failPolicy: "closed"` comes later if needed. | The brief's design principle: optional consumers never take the main output down | Small: a queue setting |
| Clock | Keep the monotonic pipeline clock; `mxlsink` converts to TAI (confirmed in its source) | Changing the pipeline clock affects every output | Medium. **Open risk:** the offset is sampled once, so long-run drift is untested. |
| SCTE-35 timeline (Q7) | Stays in Nandi. MXL flows carry no SCTE through Step 4; the carrier to workers is decided at Step 5. | MXL has no SCTE-35 semantics; ST 291 is not SCTE-35 | None until Step 4 |
| Stop condition (Q18) | Stop or rethink if (a) neither `gst-mxl-rs` nor the C API works on GStreamer 1.24, or (b) the CP2 tap makes the on-air output regress (any TSDuck/`ffprobe` failure or dropped frames), or (c) the tap's CPU cost exceeds the limit. **CPU limit accepted by the project owner on 2026-10-30:** about 0.18 core extra for a CP2 tap at 1080p25 (about 19 % on a 1-output channel, measured baseline 0.95 core). Higher frame rates need a new decision (42 % of a core per 1080p59.94 tap). | Keeps the project reversible | — |
| Domain location | Named tmpfs volume at `/run/mxl` | Sized separately from `/dev/shm`; shareable read-only between containers | Small: compose only |
| Config shape | See below | Fits the existing output `type` enum and `transport.<protocol>` pattern | Medium once API/UI exist |

Proposed configuration. Field names follow the existing parser; this is a proposal, not a schema
yet. There is no history setting: `mxlsink` in v1.1.0 fixes the ring (about 170–185 ms).

```json
"outputs": [
  {
    "name": "programme-mxl",
    "type": "mxl",
    "transports": [
      { "uri": "mxl:///run/mxl", "mxl": { "failPolicy": "open" } }
    ],
    "streams": [
      { "type": "video", "codec": "raw", "video": { "width": 1920, "height": 1080 },
        "mxl": { "flowId": "5fbec3b1-1b0f-417d-9059-8b94a47197ed" } },
      { "type": "audio", "codec": "raw", "audio": { "rate": 48000, "channels": 2 },
        "mxl": { "flowId": "<audio-flow-uuid>" } }
    ]
  }
]
```

```json
"inputs": [
  { "transport": { "uri": "mxl:///run/mxl",
                   "mxl": { "video": { "flowId": "<uuid>" }, "audio": { "flowId": "<uuid>" } } } }
]
```

(The input as built uses `video.flowId` and `audio.flowId`, as the tap and the output do, not `videoFlowId` and `audioFlowId`.)

The frame rate follows the channel's existing video rate key. The `mxl://` scheme matches MXL's own
domain URI form (`mxl://[authority[:port]]/domain`).
