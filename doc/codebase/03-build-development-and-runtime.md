# Build, Development, and Runtime

## Build graph

`transcoder/CMakeLists.txt` requires CMake 3.14, C/C++, and C++17. Debug builds add AddressSanitizer; Release builds use `-O2 -s`. The default install prefix is `/opt/utpl` and the executable installs to `/opt/utpl/bin/transcoder`.

`lib/utpl/CMakeLists.txt` links GStreamer core/app/pbutils/controller/mpegts, FFmpeg libraries, TSDuck, PC/SC, spdlog, Boost URL, pugixml, and cpr. The application adds nlohmann/json, Boost program options, SQLiteCpp, gRPC, and Protobuf.

## Reproducible current-source build

```bash
docker run --rm -v "$PWD:/src:ro" --entrypoint bash utpl/devel:latest -lc \
  'cmake -S /src/transcoder -B /tmp/utpl-release -DCMAKE_BUILD_TYPE=Release && \
   cmake --build /tmp/utpl-release -j"$(nproc)" && \
   ctest --test-dir /tmp/utpl-release --output-on-failure'
```

Use a disposable build directory. Do not let a build write generated or installed output into this repository unless that is the intended change.

## Containers

- `utpl-base` installs patched GStreamer packages, TSDuck, cpr, and runtime libraries. It also builds and installs MXL (the `mxl-builder` stage): see [41](41-mxl-integration.md). Build arguments `ENABLE_MXL` (default `1`; `0` skips MXL and restores the previous image), `MXL_TAG` (default `v1.1.0`), `MXL_RUST_TOOLCHAIN` and `MXL_JOBS`. The stage clones MXL and vcpkg from GitHub, so the build needs internet access. MXL adds about 16 MB per image. Because every other image is built `FROM utpl/base`, they all contain MXL.
- `utpl-devel` adds compilers, headers, tooling, test dependencies, and Poetry.
- `utpl-transcoder` builds the C++ application, packages the API, installs it under `/opt/utpl`, and starts Supervisor.
- `utpl-transcoder-test` adds media-analysis tools and copies `tests/` into `/tests`.
- `utpl-gstreamer-test` is a standalone GStreamer-only image for isolating element/registry behavior outside the product build.
- `utpl-license-toolkit` packages license utilities.

`docker-compose.yml` is the default parameterised deployment: one `transcoder` service (optional `hls-lan` profile) whose channel file is `config/${CHANNEL:-rtmp_to_hls.json}`, with image, ports, `UI_BIND`, licence seed and playlist-bootstrap file taken from the environment or `.env` (template: `.env.example`). It creates `/runtime/hls` on start because the HLS writer does not, and shares the licence-seed behaviour of the playlist file below. Runtime-verified 2026-10-28 with `skandha-transcoder:rtmp-test`: an RTMP test publish produced HLS that `ffprobe` showed as H.264 1920x1080@25, AAC and SCTE-35. The older `skandha-transcoder:local` image hardcodes the licence path and exits 4 with it.

For the migrated local playlist, `docker-compose.playlist.yml` is the runnable operator setup. It intentionally mounts its configuration, license, media directory, and SQLite state rather than baking them into an image. The license is copied from a read-only seed mount into `/runtime/license/` on the first start only, so `PUT /license` and `DELETE /license` can replace and remove it; the compose file therefore needs an image whose `supervisord.conf` reads `TRANSCODER_LICENSE_PATH`. See [the playlist guide](../playlist/README.md#local-docker-compose-playlist) for start, access, and manual-update steps.

The base Dockerfile builds Rust `fallbackswitch`, fMP4, AWS, and NDI plugins, but packaging/discovery must be checked in the exact derived runtime image. The local `utpl/transcoder:latest` image is version-mismatched and must not stand in for a fresh build.

## Runtime layout

Supervisor starts database preload before `transcoder_api` and `transcoder`. The engine's `-c` and `-l` come from `TRANSCODER_CONFIG_PATH` and `TRANSCODER_LICENSE_PATH`, which the API also reads; with the image defaults its command is equivalent to:

```text
/opt/utpl/bin/transcoder -c /opt/utpl/etc/config.json -l /opt/utpl/etc/transcoder.lic -b /opt/utpl/events.sqlite
```

`LD_LIBRARY_PATH` includes `/opt/utpl/lib` and an optional `/opt/ndi` SDK location. `GST_PLUGIN_PATH` may be needed for vendor encoders; see 19.

## Build-type traps

Recorded 2026-08-11 while running shell suites against a fresh build.

- A `Debug` build links AddressSanitizer **and** embeds the development license keys. The runtime
  test image (`utpl/transcoder-test`) ships neither `libasan` nor a matching license, so Debug
  binaries will not start there. Do not mount a development license over `/tmp/transcoder.lic` to
  work around it.
- Any non-Debug type embeds the production keys, which match the license the test image already
  ships. `RelWithDebInfo` is the build to use when a backtrace is needed; `Release` adds `-s` and is
  stripped.
- Cap build parallelism on a developer workstation. `cmake --build -j20` exhausted a 15 GB machine
  and the kernel OOM-killer terminated the desktop session rather than the compiler, because the
  editor process carried a higher `oom_score_adj`. `-j4` is safe on that machine.

To run a shell suite against a fresh build without rebuilding the image, mount the three artifacts
over the test image:

```bash
docker run --rm -e TEST_DISABLE_CLEANUP=1 -v <tests copy>:/tests \
  -v <build>/app/transcoder:/opt/utpl/bin/transcoder:ro \
  -v <build>/lib/utpl/libutpl.so:/opt/utpl/lib/libutpl.so:ro \
  -v <build>/lib/common/libcommon.so:/opt/utpl/lib/libcommon.so:ro \
  utpl/transcoder-test:latest bash run.sh <suite>
```

To attach a debugger to a stuck run, start the scenario detached and share its PID namespace:

```bash
docker run --rm --pid=container:<name> --cap-add=SYS_PTRACE <same lib mounts> \
  utpl/devel:latest bash -lc \
  'gdb -batch -ex "set sysroot /" -ex "file /opt/utpl/bin/transcoder" \
       -ex "attach <pid>" -ex "thread apply all bt"'
```

The Big Buck Bunny fixture 404s upstream and the framework only checks that the file *exists*, so a
0-byte `bbb.mov` left by a failed download makes every media test fail with an ffmpeg error. Delete
it and substitute `test_prepare_src -t image` **in a scratch copy of `tests/`**, never in the tree.

## Verification checklist

Record the compiler, image digest, source version, `gst-inspect-1.0` output, linked libraries, and enabled plugin paths for every runtime result. A missing plugin is a packaging/discovery observation, not proof that its corresponding source implementation is absent.

## Expected build result

A Release build completes all production targets with only deprecated GLib `GValueArray` warnings.
CTest passes 4/4 (common library, crypto, license, playlist parser). The API build generates the
REST/protobuf models and passes `pytest`. `x264enc` and `fallbackswitch` are present in the
development registry. The final runtime image carries `ffmpeg` and `ffprobe` (the probe route
needs them) but **not** `gst-inspect-1.0`, so use the development image for element inspection.

**Engine-only rebuild.** The `transcoder` target also installs the API wheel from PyPI, and a PyPI
timeout fails the whole build. For an engine change, build `--target transcoder-builder` and copy its
`/tmp/transcoder_dist` over the previous runtime image:

```dockerfile
FROM skandha-transcoder:<engine-tag> AS b
FROM skandha-transcoder:<previous-runtime-tag>
COPY --from=b /tmp/transcoder_dist /tmp/transcoder_dist
RUN cp -ar /tmp/transcoder_dist/* / && rm -rf /tmp/transcoder_dist
```

## Compose operator profile

`docker-compose.playlist.yml` builds the current source, mounts the migrated media/configuration,
starts Supervisor through its dependency listener, and forwards the transcoder's loopback UDP to
the Docker host with an FFmpeg stream-copy relay. The channel it runs is the
`TRANSCODER_CONFIG_PATH` entry in the compose file (a file under the `./config` mount). To switch
channels, change that entry and run `docker compose -f docker-compose.playlist.yml up -d`; the
compose file pins its image tag, so retag a new image to that tag rather than starting it with
`docker run`, which loses the compose environment and leaves the engine `FATAL` (exit 4). Steady container load in this environment is roughly 2-4% CPU with memory settling around
300 MiB after several playlist loops. See the
[playlist guide](../playlist/README.md#local-docker-compose-playlist).
