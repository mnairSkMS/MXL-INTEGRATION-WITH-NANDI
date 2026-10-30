# Repository Map

| Area | Status | Purpose |
| --- | --- | --- |
| `transcoder/lib/common` | Production | Shared logger, time, utility, and common type library. |
| `transcoder/lib/crypto` | Production | OpenSSL-backed cryptographic primitives and tests. |
| `transcoder/lib/license` | Production | License model, serialization, signing, key embedding, and tooling support. |
| `transcoder/lib/utpl` | Production | GStreamer wrappers and the media-engine implementation. |
| `transcoder/app` | Production | CLI executable, JSON config parser, lifecycle, SQLite provider, and gRPC server. |
| `transcoder/plugins/gstreamer` | Production | In-tree mute and Dolby PAT GStreamer elements. |
| `transcoder/patches` | Production dependency | Five patches applied to `gst-plugins-bad` by the Docker base image. |
| `transcoder/config` | Examples/schema | Example JSON, playlist samples, and a configuration OpenAPI description. Nothing runs from here. |
| `config` | Deployment configuration | Every configuration a deployment runs from: `migrated-media-playlist.json` (channel topology, mounted at `/opt/utpl/etc/config.json`) and the declarative channel files `channel.yml` and `ads.yml`. An example that goes into use is moved here; see `AGENTS.md`. |
| `api` | Production control plane | FastAPI REST service, generated gRPC client use, persistence models, and tests. |
| `frontend` | Production control plane UI | Vite/React source for the channel configuration screen served at `/channel`. Builds into `api/src/transcoder_api/static/channel`, which is committed. |
| `tests` | Product integration | Shell framework and protocol/audio/SCTE/event/ad-insertion test suites under `tests/tests`. |
| `tests/manual` | Manual verification | Operator-driven tooling that is not part of `run.sh`: scripts only, with all generated media written outside the repository. Not a runtime - the channel itself runs from `docker-compose.playlist.yml`. |
| `docker` | Production packaging | Base, development, runtime, test, and license-toolkit Docker definitions. |
| `config` | Deployment | Channel configurations bind-mounted into the engine by the root compose files. |
| `license-server` | Separate service | Standalone online-licence session service. |
| `doc` | Existing docs | HLD/LLD, the configuration, graphics and create-channel guides, and `doc/reference/` (SCTE-35 standard, API document export); audit them rather than assuming correctness. |
| `doc/mxl` | Planning material | Nandi + MXL + MXL Fabrics presentation brief and the review deck generated from it. Its Nandi description is discussion-derived; [41](41-mxl-integration.md) holds the source-verified version. |
| `sandbox` | Experimental | Standalone samples, prototypes, helper tools, and legacy-style tests. |
| `media` | Fixture/assets | Development media files and image assets. |

## Target map

The root `transcoder/CMakeLists.txt` sets C++17 and adds `lib`, `license`, `app`, `test`, and `plugins`. `lib/utpl` builds shared `utpl`; `app` builds the `transcoder` executable and a generated `transcoderGrpcApi` library. Plugin subdirectories build two mute plugins and four Dolby PAT plugins. License tools are separate install targets.

## Duplicate and ambiguous material

The former `sandbox/` tree, which held a non-identical Dolby plugin copy and a second test harness, was removed on 2026-10-16. Nothing in the production CMake graph, Docker build, or `tests/` consumed it. [24](24-sandbox-prototypes-and-artifact-status.md) is kept as a historical record.

## Existing-document audit starting points

`doc/hld.md` and `doc/lld.md` are useful orientation material but predate current-source verification. For example, their broad “always reencodes” description conflicts with reachable `PassthroughProcessor` code and passthrough scenarios. See [known gaps](27-known-gaps-contradictions-and-remediation.md).
