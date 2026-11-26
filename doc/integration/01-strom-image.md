# OpenLive / Strom integration: the Strom image with MXL (work step 1)

Done on 2026-11-26. Step 1 of the plan in
[project-status.md](../../project-status.md#after-step-5-openlive-and-strom-integration-one-host): a Strom image with MXL v1.1.0 and
`gst-mxl-rs` built against GStreamer 1.28, standalone `gst-inspect-1.0` checks, and cross-container round trips with the Nandi image.
The pins are the ones in [00-readiness-check.md](00-readiness-check.md).

**Result:** the image builds, Strom starts in it, and MXL works both ways between Nandi (GStreamer 1.24) and Strom (GStreamer 1.28).
Pictures are bit-exact and audio is unchanged. All checks passed in two runs. **One requirement for step 2:** a plain `mxlsrc` that is attached
when its writer dies does not pick up the restarted writer, so Strom's MXL input block needs a watchdog and a reattach, like Nandi's MXL input.

## How the image is built

Strom is not changed. Its image is built from the pinned upstream commit with its own `Dockerfile`, and MXL is added on top by
[docker/strom-mxl/Dockerfile](../../docker/strom-mxl/Dockerfile) in this repository:

```sh
(cd ../strom && git checkout 0df2e4b && \
  docker build --build-arg GIT_HASH=0df2e4bb --build-arg GIT_TAG=v0.8.4 --build-arg GIT_BRANCH=main -t strom:0df2e4b .)
docker build -f docker/strom-mxl/Dockerfile -t strom-mxl:0df2e4b-mxl1.1.0 .      # from this repository's root
```

- The builder stage is Ubuntu 26.04. It uses the same MXL recipe as the `mxl-builder` stage of `docker/utpl-base/Dockerfile`: tag v1.1.0,
  vcpkg, Rust 1.90.0, the `mxl-not-built` feature, and every patch in `transcoder/patches/mxl` (today only 010, which applied cleanly).
- The final stage is `strom:0df2e4b` plus libmxl and the tools in `/usr/local`, and `libgstmxl.so` and `libgstavsynctest.so` in the system
  GStreamer plugin directory. It also adds the `libmxl.so` path that the plugin recorded at build time, as in the Nandi image.
- The build prints the GStreamer it compiled against next to the one in the runtime, and fails if `mxlsrc` or `mxlsink` do not load. On
  2026-11-26 both were `libgstreamer1.0 1.28.2-1ubuntu0.1` and `plugins-base 1.28.2-1ubuntu0.3`.
- Strom's optional compiler cache (sccache with a remote bucket) was not used: no cache credentials were given.
- Build times on this host: Strom 7 to 8 minutes (the Rust backend 6 min), the MXL builder about 4 minutes, the final layer seconds. Peak RAM
  did not run out with about 7.5 GB available. Strom's image is 1.65 GB.

## Checks

Run with [tests/manual/mxl/strom-roundtrip.sh](../../tests/manual/mxl/strom-roundtrip.sh)
(`tests/manual/mxl/strom-roundtrip.sh [nandi image] [strom image]`, defaults `utpl/transcoder-test:mxl` and
`strom-mxl:0df2e4b-mxl1.1.0`). The two containers share only a tmpfs volume as the domain. Nandi runs as uid 1000, Strom as root (its default),
so every exchange is also between two users. Pictures are compared as **raw v210 bytes** with a reference frame made in the writer's own
image. A comparison after `videoconvert` would mix GStreamer 1.24 and 1.28 rounding into the result.

| # | Check | Result (both runs) |
| --- | --- | --- |
| 0 | `mxlsrc` / `mxlsink` load in the Strom image; `libgstmxl`, `libmxl` and `mxl-info` resolve | ok. GStreamer 1.28.2, plugin `0.1.0-8f3f109+` |
| 0 | Pad templates and properties: Nandi image vs Strom image | Identical, once 1.28's `gst-inspect` type annotations (`v210 (gchararray)`) are removed |
| 1 | Video Nandi → Strom, 1080p25 v210, 50 frames | 50 frames, all byte-identical to the writer's reference |
| 2 | Video Strom → Nandi, same | 50 frames, all byte-identical |
| 3 | Audio both ways, 48 kHz Float32 stereo, -6 dBFS 1 kHz on channel 1 | 48000 Hz, 2 channels, `pcm_f32le`; channel 1 at -6.0 dB, channel 0 at -91 dB, both directions |
| 4 | Reader (Strom) started 3 s before its writer (Nandi) | The reader waited, then got all 50 frames, byte-identical |
| 5 | Writer (Strom) killed, then started again under the same flow id | Flow shows `Active: false` after the kill; the new writer takes the flow id without `mxl-info -g` (2.2 s to `Active`, including the container start); a new reader gets 50 identical frames |
| 5 | A reader **attached across** that restart | **Did not resume**: 77 and 72 of 200 frames (before the kill), then still waiting at 40 s |

Strom itself was started from the image (`/app/strom`, port 8080 on 127.0.0.1). `/api/version` reports 0.8.4 at `0df2e4bb`, GStreamer 1.28.2,
and `/api/elements` lists `mxlsrc` and `mxlsink`. The only warnings were that the GPU was not passed in (`--gpus` not given) and that
authentication is off, which is Strom's default. The stack in step 6 keeps its ports on 127.0.0.1.

## What this means for the next steps

- **Step 2, `builtin.mxl_input`:** it needs its own watchdog and reattach when a flow's writer goes away. The plain element waits for ever
  (check 5, and SDK test `07-writer-kill-and-restart` on the Nandi side). Nandi's MXL input does this already; see
  [page 41](../codebase/41-mxl-integration.md).
- **Patch 010 is in the image.** The deadlock it fixes was proved in Nandi's channel, whose pads are blocked until it plays. Whether a Strom
  pipeline can reach the same state was not tested here. The patch stays, because it is not upstream (readiness check, risk 3), and the
  check comes in step 2 with the real block.
- **GPU:** these checks ran without the GPU. The mixer's GPU path (RGBA in GL memory) is first used in step 2.
- The plugin is the same build as Nandi's (`8f3f109` plus patch 010), so behaviour found in Nandi's SDK tests carries over, apart from
  GStreamer-version effects. None showed up here.
