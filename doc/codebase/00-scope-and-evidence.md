# Scope and Evidence

## Repository boundary

The product root is the directory containing `AGENTS.md`. It contains `transcoder/`, `api/`, `config/`, `tests/`, `docker/`, `doc/`, and `media/`. The top-level README calls the project UTPL, the Universal Transcoding Pipeline Library.

`transcoder/` is the production C/C++ source tree. `api/` is the Python FastAPI control plane. `tests/` is the current integration framework. `config/` holds every configuration the deployment runs from. The former `sandbox/` R&D tree was removed on 2026-10-16.

## Evidence hierarchy

1. A reachable source path plus its CMake/Docker wiring.
2. Build and runtime observations made from this checkout.
3. Test code whose fixture and assertion have been inspected.
4. Parser schemas, examples, old documentation, and test names.

A prebuilt image is not evidence for this checkout unless its source and dependency provenance match. The locally available `utpl/transcoder:latest` reported `1.1.0-beta.3`, which differs from this source version, so it is intentionally excluded from current-source claims.

## Reference build environment

A full multi-stage image build from this checkout succeeds with GCC 13.3.0, CMake 3.28.3,
GStreamer 1.24.2, FFmpeg 6.1 libraries, and TSDuck 3.37.3670. Release CTest passes its four suites;
the compiler emits deprecated GLib `GValueArray` warnings but no errors. The final runtime image
does not package `gst-inspect-1.0` or `ffprobe`, so registry inspection and media capture need the
development image or host tools.

## Claim format

Each feature entry should state its user behavior, entry point, source path, pipeline elements, tests, runtime result, dependencies, and restrictions. Use **integration gated** for NDI, GPU, proprietary encoders, cloud/S3, Dolby runtime dependencies, licensing servers, and POIS endpoints unless the exact environment was exercised.

## Cross-transcoder rule

Do not copy configuration fields, GStreamer element names, defaults, caps, or runtime rules from another device/transcoder. If external code is later supplied for comparison, keep its provenance separate and prove every claimed match in this tree.
