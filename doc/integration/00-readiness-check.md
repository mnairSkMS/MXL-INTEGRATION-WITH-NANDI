# OpenLive / Strom integration: readiness check (work step 0)

Done on 2026-11-26. This is step 0 of the OpenLive / Strom work in
[project-status.md](../../project-status.md#after-step-5-openlive-and-strom-integration-one-host): re-check Step 5 in the code, pin the upstream
commits, and write down the version matrix and the risks. Nothing was built or run against Strom or OpenLive. The facts below are from reading
the code and from `git` and `apt` queries made on this host on that date.

**Verdict:** step 1 (a Strom image with MXL) can start. Nothing found blocks it. Two of the 2026-11-11 facts were wrong or incomplete and are
corrected below (the upstream checkouts and the mixer's output format). The Step 5 items still open do not block step 1, but they block
the acceptance demo (step 7), because the programme encode goes through Step 5 pieces.

## Version matrix (pinned)

| Component | Pin | Date | Since the 2026-11-11 pin | Notes |
| --- | --- | --- | --- | --- |
| Strom | `0df2e4b` (release 0.8.4) | 2026-11-18 | 11 commits after `f125401` | SRT relays (`srt+relay://`, a breaking `feat(srt)!`), SRT fixes, MPEG-TS retiming, `GET /api/capabilities`. No MXL. |
| OpenLive backend | `31fe60c` | 2026-11-22 | 4 commits after `157cde6` | SRT relay addresses, audio streams found from the live flow's pad caps, deleted-source fix. No MXL. `flow-generator.ts` changed (37 lines). |
| OpenLive Studio | `b42e886` | 2026-11-26 | 4 commits after `cad8bb1` | SRT relay UI, slot-label fixes. No MXL. `SourcesPanel.tsx` still in place. |
| MXL (libmxl + `gst-mxl-rs`) | `v1.1.0` (`8f3f109`) | — | still the newest tag | main is at `21944b2` (2026-11-26), 297 commits past the tag; see risk 4. |
| GStreamer, Nandi | 1.24 (Ubuntu 24.04) | — | unchanged | `utpl/base:mxl`. |
| GStreamer, Strom | **1.28.2** (Ubuntu 26.04 apt: `libgstreamer1.0-0 1.28.2-1ubuntu0.1`, `plugins-bad 1.28.2-1ubuntu1.2`) | — | — | Strom's `Dockerfile` installs from apt, so the version is whatever 26.04 ships on build day. |
| Node (OpenLive) | `node:22-alpine` | — | unchanged | — |
| Studio | React ^19.1, Vite ^6.3 | — | unchanged | — |

Recommendation: build Strom's `gst-mxl-rs` from **the same v1.1.0 tag as Nandi, with patch 010** (risk 3), not from MXL main. Nandi and
Strom share one domain, and one libmxl version on both sides is the case that has been tested here. Moving both to a later MXL is a
separate change, to be made on both images at once.

## Corrections to the 2026-11-11 facts

1. **The upstream checkouts were gone.** `../strom`, `../open-live` and `../open-live-studio` did not exist anywhere on the host, and
   there was no Strom image. They were cloned again from `github.com/Eyevinn/` into `~/Projects/` at the pins above (on upstream `main`,
   with no local changes).
2. **Strom's mixer has a CPU compositor as well as the GPU one** (`vision_mixer/builder/pipeline_cpu.rs`, in the tree since 2026-10-28).
   Its `output_format` property can be set to `v210`. The mixer then blends in `A422_10LE` and converts to `v210` once on the way out
   (`builder/mod.rs`, `alpha_blend_format`). Only the GPU path emits RGBA in GL memory, which a consumer has to download and convert.
   The 2026-11-11 note ("an MXL output must download and convert to v210") is true of the GPU path only. Shader FX (wipes, looks) is GPU
   only.

## Step 5 re-checked in the code

| Item | State on 2026-11-26 | Blocks |
| --- | --- | --- |
| Flow registry, `GET /mxl/flows` | Built (`api/src/transcoder_api/routers/mxl.py`, tests in `api/tests/mxl_test.py`). | — |
| Encoder worker | Built; 8 test cases in `tests/tests/mxl/worker/`. On the installed images 6 of 8 passed (`05-worker-slow` and `07-cue-worker-restart` failed). | Step 7 (programme encode) |
| Gate A | CPU limit missed in 1 of 5 runs at 720p25; the 1-hour run and the two-worker run not done. | Step 8 pass or fail |
| MXL URI with a host | Refused by the parser (`transcoder/app/src/application/config/parser/json/parser.cpp:180`), as expected for one host. | — (Mode A only) |
| SCTE-35 | Travels in an MXL data flow between Nandi engines (`mxltap.cpp`). Strom does not carry it. | Must be refused through Strom |
| WP6 (multi-audio) | Dropped by the owner on 2026-11-25: the engine has no multi-audio even without MXL. **Not yet recorded in `project-status.md`**, which still says "not started". | One audio track per camera |
| Mixer failure policy on a lost source | Not decided (owner). | Step 7 fault tests |

## Strom and OpenLive reuse points, re-checked

All the reuse points listed on 2026-11-11 still exist at the new pins:

- Strom NDI block `backend/src/blocks/builtin/ndi.rs` (`get_external_pads`, `build`, `mode`). It is registered in `builtin/mod.rs`
  (`builtin.ndi_input` / `builtin.ndi_output`).
- `convert_front` in `backend/src/gst/download_front.rs`.
- `pipeline_lifecycle_test`: in `backend/tests/it/main.rs`.
- OpenLive `activateStromFlow` and `readStromBlocks` in `src/lib/flow-generator.ts`, and `pgm_out` / `main_out` in `src/lib/default-flow.ts`.
- OpenLive zod enums: sources `['srt', 'efp', 'whip', 'html', 'clip']` (`src/routes/sources.ts:129`). The test sources listed on
  2026-11-11 are not in this enum. Outputs `['mpegtssrt', 'efpsrt', 'whep', 'recording', 'rtmp']` (`src/routes/outputs.ts:117`).
- New: Strom's `GET /api/capabilities` (`backend/src/api/capabilities.rs`) reports network and protocol capabilities, not blocks. Use
  `readStromBlocks` to detect the MXL blocks; capabilities could carry MXL domain reachability later.

## Risks

| # | Risk | Effect | What to do |
| --- | --- | --- | --- |
| 1 | **RAM.** 15 GB total; at the check 12 GB was in use and 2 GB available, mostly desktop applications (editor, browsers). | A Rust release build of Strom and the full stack (3 Nandi engines, Strom, OpenLive, CouchDB, Studio) may not fit beside them. MXL flows are small by comparison (a 1080p25 v210 flow is about 28 MB: 5 slots × 5.5 MB). | Close the desktop apps for builds and measured runs; record free RAM with every benchmark. |
| 2 | **GStreamer 1.24 vs 1.28.** `gst-mxl-rs` is built twice, against two GStreamer versions. | Caps or behaviour differences in the plugin between the images. | Step 1's cross-container round trips (Nandi → Strom, Strom → Nandi) are the check. Pin the 26.04 apt versions in the Strom image. |
| 3 | **The `mxlsrc` deadlock fix is not upstream.** `transcoder/patches/mxl/010-mxlsrc-release-locks-before-set-caps.patch` still applies cleanly to v1.1.0 **and** to MXL main `21944b2`. | A stock `mxlsrc` in Strom can hang a pipeline that blocks its pads (see page 41). | Apply patch 010 in the Strom image too. Reporting it upstream is still pending. |
| 4 | **MXL main is moving fast** in `gst-mxl-rs`: colorimetry mapping, `v210a` (fill and key as `A422_10LE`), audio channel-mask and channel-position changes (2026-11-16 to 2026-11-26). | Not in v1.1.0. The audio changes may matter to OpenLive, which now finds audio streams from pad caps (`8008eb5`). | Stay on v1.1.0. In step 3, check what OpenLive sees on an MXL audio pad. `v210a` is worth a look later for keyed graphics. |
| 5 | **The mixer's output path is a choice.** GPU: RGBA in GL memory, then download and convert (measured cost of a similar conversion in Nandi: 7.75 ms per 1080p frame). CPU: `output_format = v210`, no download, but blending on the CPU and no Shader FX. | CPU per process and latency differ between the two. | Measure both in step 8. Make the MXL output block accept either. |
| 6 | **Upstream drift.** Strom made 4 releases (0.8.1 to 0.8.4) in two weeks, one with a breaking change. | Local patches go stale. | Work on local branches from the pins above; rebase only on purpose. |
| 7 | **Strom's build can use an external compiler cache** (sccache with a MinIO bucket). | It is used only when the cache credentials are given (`Dockerfile` lines 48 on), so a local build does not use it. | Build without those secrets. |
| 8 | **Step 5 is not finished** (see the table above). | The acceptance demo's encode stage rests on the worker, which has two failing tests. | Fix or explain `worker/05` and `worker/07` before step 7. Use a Nandi MXL-input channel for the programme if the worker is not ready. |
| 9 | **The mixer failure policy is not decided.** | Fault tests in step 7 cannot be passed or failed. | Owner decision. Record behaviour without judging it until then. |
| 10 | **GPU in containers.** Strom's GPU mixer needs the NVIDIA runtime. | — | Present on this host (Docker 29.10.1, `nvidia` runtime, RTX 5060 Ti 8 GB, driver 595.104.02, about 1.1 GB in use). Strom fixed finding the driver under `/usr/local/nvidia` (`ca98b53`). |

## Next step

Done on 2026-11-26: see [01-strom-image.md](01-strom-image.md). Step 1 as planned: a Strom image from `0df2e4b` with MXL v1.1.0, `gst-mxl-rs` built against GStreamer 1.28 with patch 010. Then `gst-inspect-1.0`
checks of `mxlsrc` / `mxlsink` in it, and the cross-container round trips with `utpl/transcoder:mxl`: Nandi → Strom, Strom → Nandi, the
reader started before the writer, and a writer restart.
