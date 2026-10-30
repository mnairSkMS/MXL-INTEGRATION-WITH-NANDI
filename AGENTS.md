# UTPL / Skandha Transcoder Agent Guide

This checkout has no Git metadata. Treat the directory containing this file as the repository root. The former `sandbox/` experiments tree was removed on 2026-10-16; handbook pages that still describe it are historical.

## Purpose

This checkout exists to test integrating MXL (Media eXchange Layer) and MXL Fabrics with Nandi (this
engine). Before any MXL work, read [the MXL integration page](doc/codebase/41-mxl-integration.md). The
planning brief in `doc/mxl/` describes Nandi from project discussion, not from the code; where the two
disagree, page 41 and the source win. Nothing MXL-related is implemented: label MXL paths
**integration gated** until they are built and run here.

## Experiments

When asked to try a skill/MCP/tool just to produce an artifact (architecture diagram, brag video, etc.), write all output to `../experiments/` (shared parent folder), never in this checkout. Afterwards, tell the user what was created and where, so they can ask for it to be undone.

## Source of truth

Use the current production source before samples, tests, schemas, or old documents:

1. Reachable code under `transcoder/` and its CMake/Docker wiring.
2. A build and runtime observation produced from this checkout.
3. Independently understood automated tests.
4. Configuration examples, OpenAPI files, and existing documentation.

Never copy a behavior from another device or transcoder without proving it exists here. Mark unavailable SDK, GPU, cloud, license, or service paths as **integration gated**, not supported.

## Where changes belong

- Core media graph and transcoder behavior: `transcoder/lib/utpl/`.
- CLI, JSON configuration, lifecycle, gRPC server, and SQLite provider: `transcoder/app/`.
- REST control plane: `api/`.
- In-tree GStreamer elements: `transcoder/plugins/gstreamer/`.
- Required upstream changes: `transcoder/patches/`.
- Container dependency and packaging behavior: `docker/`.
- Product integration tests: `tests/`.
- Configuration that anything runs from: top-level `config/` (see below).

## Configuration placement

A configuration that is in use lives in the top-level `config/` folder, never under `transcoder/`. "In use" means a compose file, Dockerfile, supervisor or launch script, or the benchmark kit mounts, copies, or loads it.

- `transcoder/config/` holds examples and the schema only. Nothing may mount or load a file from there.
- To start using an example, **move** it (do not copy) into `config/`. In the same change, update every reference to the old path: compose files, scripts, `README.md`, and handbook pages. Grep for the old path to confirm none are left.
- A test's own fixture config stays beside its test (for example `tests/tests/<suite>/<case>/src/config.json`). It is not a deployment config.
- `config/` is bind-mounted writable at `/opt/utpl/channel`, and Publish writes back into it. Never point `PLAYLIST_BOOTSTRAP_FILES` at a transcoder JSON.

Read the relevant page in [the codebase handbook](doc/codebase/README.md) before changing a subsystem. To learn the tree by reading source, follow [the code-reading roadmap](doc/codebase/31-code-reading-roadmap.md).

## Required validation

- Preserve the JSON parser, parameter classes, configuration schema/examples, API models, and tests as a single contract; verify all affected layers.
- Build in a disposable directory or `utpl/devel` container. The repository CMake project is `transcoder/` and needs GStreamer 1.24-era dependencies.
- For media code changes, inspect actual output with `ffprobe`, TSDuck, manifests, and/or image/audio assertions. A zero process exit alone is not sufficient.
- For config-only or deployment changes (channel JSON, compose files), a quick check is enough: the engine starts without errors and `ffprobe` shows each output's expected streams. Do not run long decode or soak tests unless asked.
- For custom elements, check `gst-inspect-1.0` in the built environment, including pad caps and properties.
- Do not overwrite expected media, sample config, license, or generated artifacts to make a test pass without explicit approval.

## Documentation rule

When a feature is added or changed, update the subsystem handbook page, `02-feature-catalog.md`, `26-implementation-index.md`, and the appropriate test/config/API documentation. Record unresolved mismatches in `27-known-gaps-contradictions-and-remediation.md`.
