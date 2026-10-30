# Sandbox Prototypes and Artifact Status

> **Historical.** `sandbox/` was deleted from this checkout on 2026-10-16. Nothing in the production CMake graph, Docker build, or `tests/` consumed it. This page records what it contained; none of the paths below exist any more.

`sandbox/` is intentionally outside the production CMake target graph. It contains prototypes and supporting tools that may be useful for investigation but are not proof of product behavior.

| Area | Status | Notes |
| --- | --- | --- |
| `app/src/main.cpp`, `main2.cpp` | Experimental | Standalone GStreamer pipeline examples with hardcoded sources/parameters and disabled branches. |
| `hls/`, `playlist/`, `ddmeta/` | Experimental | Independent utilities/prototypes, not the `lib/utpl` production implementation. |
| `plugins/dlbpatenc` | Experimental/duplicate | Similar Dolby encoder material but not the production plugin target. |
| `pois/`, `tsduck/`, `utils/videoExtractor` | Tooling | Helpers for conditioning, SCTE, and analysis. |
| `sandbox/tests` | Experimental tests | Older/parallel test harness and configs. |
| `crypto/AesGcmTest.java` | Utility | Standalone experiment, not linked to C++ crypto library. |

## Safe reuse rule

An idea, pipeline, test, or config discovered under `sandbox/` may be reused only after its assumptions are reproduced against `transcoder/` code and current build/runtime evidence. Never move a sandbox plugin or config into production based on name similarity.

## Useful investigative value

The sandbox retains simple examples of `decodebin`, input selectors, compositor, x264/AC-3 encoding, file/SRT/UDP transport, POIS mocks, and SCTE injection. These are helpful to isolate GStreamer behavior outside UTPL, but they do not model the production engine's state machine, connectors, scheduler, or packaging.

When documenting a sandbox artifact, label it **experimental**, link its production analogue if one exists, and identify any divergence rather than trying to make the copies appear equivalent.
