# MXL and MXL Fabrics Integration

**Status: nothing is implemented.** No MXL input, output, flow registry or Fabrics code exists in
`transcoder/`, `api/`, `docker/` or `config/`. Every MXL capability is **integration gated**.

This checkout exists to test integrating the [Media eXchange Layer](https://dmf-mxl.github.io/mxl/main/)
(MXL) and MXL Fabrics with Nandi (this UTPL engine; "Nandi Transcoder" is the product name in the
operator UI). This page records what the source says about the integration points. It also lists
corrections to the planning material, the test plan, and the decisions still open.

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
Mapping between them needs an explicit monotonic → realtime → TAI step in the adapter. Switching the
whole pipeline to a realtime/TAI clock would affect every output, so treat it as a separate decision
(see [open decisions](#open-decisions)).

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
| `/dev/shm` is Docker's default 64 MiB (67.1 MB) | No `shm_size`, `ipc` or tmpfs mount in `docker-compose.yml` | CP1 + CP2 at 1080p60 V210 with 100 ms of history is 66.36 MB of video payload alone (2 × 33.18 MB, brief section 7.2). That leaves under 1 MB for audio flows, writer-reserved grains and allocation overhead, so any more history or a third flow does not fit. Set an explicit `shm_size` or a dedicated tmpfs domain mount, sized from measurement. |
| GStreamer 1.24.2 (image and host) | [00](00-scope-and-evidence.md); `gst-launch-1.0 --version` on the host | It is unknown whether `gst-mxl-rs` builds against GStreamer 1.24 bindings. The first WS-01 build will tell us. |
| Rust toolchain | `docker/utpl-base/Dockerfile:65` pins `DASH_RUST_TOOLCHAIN=1.99.1` through rustup for `dashsink2`. The other plugins use Ubuntu's cargo. | `gst-mxl-rs` can use the same rustup pattern if it needs a newer Rust than Ubuntu's. |
| Dolby SDK | Static libraries vendored under `transcoder/plugins/gstreamer/dolbypat/ext/*/lib/linux/x86_64/` | The transcoder build is self-contained. Keep Dolby processing inside the licensed engine (brief section 6.3). |
| Images | `utpl/base`, `utpl/devel` and `skandha-transcoder` tagged `:dash-out` exist on the development host (built 2026-10-27). `transcoder/` and `docker/` are byte-identical to the sibling `skandha-transcoder-dev` checkout. | Under [00](00-scope-and-evidence.md) a prebuilt image is not evidence for this checkout until its provenance is proven. Rebuild, or prove the source match, before you record baseline results. |
| No Git metadata | `AGENTS.md` | Initialise a repository before any source change so that MXL work can be reviewed and reverted. |

## Configuration contract

[AGENTS.md](../../AGENTS.md) treats the JSON parser, parameter classes, schema and examples, API
models and tests as one contract. A raw MXL output does not fit the current output model, which goes
codec → `encodebin` → mux → sink ([13](13-outputs-muxing-and-delivery.md)): it has no codec and no
mux. Agree its shape before writing code. For example, decide whether it is a new output protocol
that bypasses the flowline, or a connector-level tap configured next to the outputs. The same
applies to an `mxl` input.

## Test plan

These are the brief's tests (section 10.1), placed in this repository. All of them are planned and
none have been run.

| ID | Test | Where it will live | Depends on |
| --- | --- | --- | --- |
| T0 | Baseline channel: DOT graph, CPU, RAM, latency | `tests/` plus a config in `config/` | Image built from this checkout |
| T1 | Audited decode sharing / `tee` topology | DOT export from T0 | T0 |
| T2 | Standalone `mxlsink` → `mxlsrc`, V210 and Float32, 30000/1001, checked with `ffprobe` | `tests/tests/mxl/sdk/`, run in `utpl/transcoder-test`. MXL is built into `utpl/base`; there is no separate image. | MXL v1.1.0 + `gst-mxl-rs` build |
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

1. **Phase 0 / WS-00:** `git init`; build and tag `:baseline` images **before** any MXL change; baseline
   channel, DOT graphs, measured CPU/RAM/latency (T0, T1).
2. **WS-01:** MXL built into `utpl/base` (inherited by every image); compose tmpfs domain; SDK
   qualification tests in `tests/tests/mxl/sdk/` (T2).
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
