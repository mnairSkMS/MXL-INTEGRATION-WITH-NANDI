# UTPL Codebase Handbook

This handbook describes the current source tree, not an assumed sibling transcoder or a prebuilt
image. The checked-out version in `version.txt` is `1.1.0-beta.2-skandha.3`; the tree has no Git
metadata.

## Purpose of this checkout

This checkout is the test bed for integrating MXL and MXL Fabrics with Nandi. Read
[41](41-mxl-integration.md) first for MXL work: it holds the source-verified integration points, the
test plan and the open decisions. The planning brief and review deck are in [`doc/mxl/`](../mxl/).
Nothing MXL-related is implemented yet.

## Read this first

If your goal is to understand the codebase by reading the source rather than to look up one
subsystem, start with [the code-reading roadmap](31-code-reading-roadmap.md). It gives a staged
file-by-file order, with the handbook pages below used as cross-checks after each stage.

1. [Scope and evidence](00-scope-and-evidence.md) explains what each confidence label means.
2. [Repository map](01-repository-map.md) separates product code from tests, Docker assets, and deployment configuration in `config/`.
3. [Feature catalog](02-feature-catalog.md) is the complete capability/status matrix and the quickest way to find implementation evidence.
4. [Build and runtime](03-build-development-and-runtime.md) gives the reproducible environment.
5. Follow the subsystem pages from [the implementation index](26-implementation-index.md).

## Contracts

- [JSON configuration reference](05-configuration-reference.md)
- [REST/gRPC/SQLite control plane](17-rest-grpc-and-sqlite-control-plane.md)
- [Input and playlist REST API guide](../api/README.md)
- [Transactional rundown service](30-transactional-rundown-service.md) — optional versioned YAML
  layer; does not replace the M3U8 or managed-queue execution path.
- [HLS SCTE-35 ad markers and the base64 payload](37-hls-scte-ad-markers-and-base64.md) — the
  `hls.adMarkers` contract and where to cut in to change the encoding.
- [SCTE-35 segmentation_type_id reference](38-scte35-segmentation-type-id-table.md) — Table 23 of
  ANSI/SCTE 35 2023r1 verbatim, with the start/end ids the cue pairing logic keys off.
- [Graphics upload: frontend design guide](39-graphics-upload-frontend-guide.md) — stage,
  draw, commit and cue sponsor graphics: the API calls in order, screens, states and errors.
- [CMAF DASH output](40-cmaf-dash-output.md) — `dash+file`/`dash+http` outputs: dashsink2 and its
  patches, segment grid and timing, object names, MPD merging for GROUP outputs, SCTE-35
  EventStream, validation and verification.

These record effective behavior and known schema differences rather than treating the OpenAPI files
as authoritative.
The current-source build and end-to-end playlist input/API evidence is recorded in [the 2026-08-03 runtime verification](28-playlist-input-api-runtime-verification.md).

The old-worktree migration record, including which operator and managed-playlist features are still
runtime-gated, is [29](29-old-version-feature-migration.md).

The optional versioned YAML rundown layer is documented in
[30](30-transactional-rundown-service.md). It is disabled by default and does not replace the
existing M3U8 or managed-queue execution path.

Static performance, efficiency, and tooling observations — each with a plain-language explanation —
are in [32](32-performance-and-tooling-findings.md). None of them are profiled, and none compete with
video encoding cost; read its opening section before prioritising from it.

The evaluation of adding an MCP (Model Context Protocol) server, so operators can drive the system
through an LLM, is [33](33-mcp-interface-evaluation.md). It is a forward-looking design note —
nothing in it is implemented — and it records a market survey that was one day old when written.
[35](35-nimbus-mcp-implementation-report.md) is the later, source-verified assessment of the same
question, with a capability-by-capability scorecard and an effort estimate; it supersedes 33 where
they disagree.

SCTE-35 ad insertion — replacing the programme with a second input for the duration of an avail —
is [34](34-scte-ad-insertion.md), including its configuration reference and two open defects.

The plan for deriving minimum and recommended Ubuntu EC2 hardware (playout, HLS ABR ladder, and
heavier-codec profiles; CPU encoders; many channels per instance) is
[36](36-ec2-hardware-sizing-plan.md). It is a test plan only; no EC2 results are recorded yet.
Integrating MXL (local shared-memory media exchange) and MXL Fabrics (inter-host replication) is
[41](41-mxl-integration.md). It is a plan backed by source reading; every MXL capability is
integration gated. [42](42-mxl-implementation-plan.md) is the implementation plan: which existing
elements MXL replaces or adds to, the illustrative performance cost of each change, and the
per-layer change list.

Open mismatches and their remediation are tracked in
[27](27-known-gaps-contradictions-and-remediation.md).

## Suggested routes

- **New C++/GStreamer developer:** follow [31](31-code-reading-roadmap.md); it sequences 03, 04, 06, 07, 09 and the media-specific pages against the source.
- **Config/API developer:** 05, 17, 15, 16, and 23.
- **Release or integration engineer:** 03, 13, 14, 18, 19, 21, and 22.
- **Coding agent:** start with the root `AGENTS.md`, then 25 and the owning subsystem page.
- **MXL integration engineer:** 41, then 07, 10, 11, 13 and 03, then the brief in `doc/mxl/`.

## Evidence labels

| Label | Meaning |
| --- | --- |
| Runtime verified | Current-source code was built and a meaningful runtime check passed. |
| Build verified | Current source compiles and the path is wired into a production target. |
| Statically implemented | Reachable source exists; runtime proof is absent. |
| Integration gated | Runtime requires an unavailable SDK, GPU, cloud service, license, or peer service. |
| Experimental | Lives only in a non-production path (not wired into a CMake target, image, or `config/`). |
| Claim only | A test, example, schema, or document says it exists, but production evidence is missing. |
| Contradicted/broken | Sources of evidence disagree or a required check fails. |

The evidence inventory is intentionally conservative. Absence of a runtime test does not prove absence of implementation.
