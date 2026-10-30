# MCP Interface Evaluation

Recorded 2026-08-07 against source version `1.1.0-beta.2-skandha.3`.

> **Partly superseded.** [35](35-nimbus-mcp-implementation-report.md), written 2026-08-12, re-verified
> this ground against the source and corrects several statements here — most visibly the REST route
> list below, which omits the `/config` prefix that `routers/config.py` actually declares. Where the
> two pages disagree, 35 is the later evidence. This page remains the market survey and the
> tool-surface proposal.

This page evaluates adding a **Model Context Protocol (MCP)** server to this codebase, so operators
can ask an LLM questions about the running system and, eventually, have it perform work. It records
the state of the market as of the date above, a gap analysis against the current source tree, a
proposed tool surface, and the prerequisites that must land first.

Nothing on this page is implemented. There is no MCP code in the tree.

## Read this first

MCP is a JSON-RPC protocol that lets an AI client (Claude, an agent framework, an IDE) discover and
call **tools**, read **resources**, and use **prompts** that a server exposes. For this product it
would sit next to the existing FastAPI app and project the REST/gRPC surface into something an LLM
can drive.

**The single most important finding on this page is not about MCP at all.** It is that the control
plane has **no authentication** — no auth, no CORS, no middleware in
`api/src/transcoder_api/main.py`. Every credible implementation surveyed below derives its safety
from a permission model that already existed. This tree has none to inherit. A write-capable MCP
server built on the current API would be an unauthenticated remote control for a broadcast chain.

> **In plain terms:** an MCP server does not add security, and it does not remove any. It takes
> whatever the existing API allows and hands it to a language model. Right now the existing API
> allows anything to anyone who can reach the port. Fix that first, or ship read-only.

## Priority summary

| # | Item | Type | Blocking? | Effort |
| --- | --- | --- | --- | --- |
| 1 | No authentication or role model on the API | Prerequisite | Blocks all writes | Medium |
| 2 | `GET /restart-transcoder` is a destructive GET | Defect | Blocks any MCP exposure | Low |
| 3 | No metrics or telemetry of any kind | Prerequisite | Blocks diagnosis tools | Medium |
| 4 | Audit trail exists for rundowns only | Prerequisite | Blocks safe writes | Medium |
| 5 | Read-only tool surface over existing routes | Deliverable | — | Low |
| 6 | Config schema + effective-behaviour resources | Deliverable | — | Low |
| 7 | Runbook prompts | Deliverable | — | Low |
| 8 | Rundown-draft write surface | Deliverable | After 1 | Low |
| 9 | Input/output/on-air write surface | Deliverable | After 1, 4 | Medium |

---

## Part 1 — Market state as of 2026-08-07

### 1.1 Veset Nimbus MCP Server — the direct competitor

**Announced 2026-08-06**, one day before this page was written, at
[veset.tv/news](https://www.veset.tv/news) under the headline *"Introducing the Veset Nimbus MCP
Server"*, described as the **first release**.

Veset Nimbus is a SaaS cloud playout platform — the closest commercial analog to this product.
This is the first published MCP interface from a playout vendor.

> "a new integration that connects Claude, Anthropic's AI assistant, directly to the Nimbus playout
> platform using the Model Context Protocol (MCP)"

**Announced capabilities**, in the order Veset lists them, which tracks the risk gradient exactly:

| Capability | Veset's wording |
| --- | --- |
| Monitoring | "Monitor live channels and playout health" |
| Query | "schedules, playlists, and media assets without digging through separate interfaces" |
| Forensics | "ingest and audit logs to trace what happened and when" |
| Mutation | "playlist editing and configuration changes" |
| On-air | "Take events live on air when it's time to act" |

**The security claim is the whole architecture:**

> "every action Claude can take runs through the same permissions, guardrails, and audit trail that
> Nimbus customers already rely on."

That sentence is only sayable because the permission model predates the MCP server. The
[Nimbus API docs](https://apidocs.veset.tv/) show what it inherits: JWT bearer auth, tokens minted
in the Nimbus UI under Settings as *Organization tokens* or *Service provider tokens*, with
published guidance to assign "a role with least privileges required", use short expiry, and issue a
separate token per integration. The MCP server almost certainly holds one scoped service token, so
blast radius is configured by the customer in the existing RBAC UI and the agent cannot exceed it.

**Distribution:** "reach out to your account team to get set up." No self-serve, no pricing, no
public technical documentation, no repository. This is a design-partner rollout, not GA.

**Strategic arc worth copying.** Veset already shipped a ChatGPT-based chatbot inside the Nimbus UI
earlier in 2026. The MCP server supersedes it. The chatbot could only answer questions inside their
own interface; the MCP server lets a customer's own agent compose Nimbus with everything else it
can reach. **Skip the embedded-chatbot stage entirely.**

**Sourcing caveat.** The announcement was one day old when this page was written. Search indexes had
not picked it up; there is no third-party coverage and no technical documentation. Everything above
comes from Veset's own announcement page and public API docs. **Re-check this section before
relying on it** — capability lists in a first-release announcement are marketing summaries, not
tool manifests.

### 1.2 Structural analogs — stateful control planes wrapped in MCP

These are the projects worth studying, because their shape matches this product.

**[obs-mcp](https://github.com/royshil/obs-mcp)** — closest structural match. Exposes **148 tools
covering the whole obs-websocket v5 protocol**: scenes, sources, scene items, the full audio mixer,
filters, transitions, streaming, recording, virtual camera, replay buffer, media playback, studio
mode, and output stats. Runs entirely locally as a WebSocket client to OBS.

The design decision to note: they did **not** hand-curate a friendly subset. They mapped an existing
RPC protocol 1:1 and let the model compose. At least three competing implementations exist
([agentic-obs](https://github.com/ironystock/agentic-obs),
[sbroenne/mcp-server-obs](https://github.com/sbroenne/mcp-server-obs)), which indicates real demand.

This tree's surface is ~35 REST routes and 4 gRPC RPCs — an order of magnitude smaller than OBS's
protocol. Full coverage is days, not a project.

**[grafana/mcp-grafana](https://github.com/grafana/mcp-grafana)** — the best model for the
*operator-doubt-solving* use case, and the one to steal from hardest. Two decisions matter:

1. **It exposes investigations, not just data.** Beyond "query Loki", it has `find error patterns in
   logs` and `find slow requests`; the server starts a Sift investigation and returns a
   *conclusion*. That is the difference between a tool that answers "what is the bitrate" and one
   that answers "why does output 2 look wrong."
2. **Permissions bind to existing roles.** Viewer for read tools, Editor for anything that creates.
   The MCP server does not invent an auth model. Same lesson as Veset.

**[containers/kubernetes-mcp-server](https://github.com/containers/kubernetes-mcp-server)** — the
source of the safety vocabulary. Ships `--read-only` and `--disable-destructive` flags, uses the MCP
`DestructiveHint` annotation so clients know to prompt, and supports approval gates at two levels:
tool-name matching that fires once before the handler, and per-API-call matching on verb / kind /
group / version / name / namespace that fires during handler execution.

That two-level model maps directly onto this product: "may this agent touch outputs at all" versus
"may it touch *this* output while it is on air."

### 1.3 Industry direction

**AWS Agentic Intelligent Operations for Streaming Media** (NAB 2026, with Hydrolix as the data
layer) segments specialised agents by delivery-chain layer — **contribution, transcoding,
distribution, viewer experience** — monitoring and resolving streaming quality failures. MCP is used
for the *observability query path*, providing sub-second natural-language query over full-fidelity
telemetry.

Two things to take from it: MCP was applied to observability first, and the agents were scoped per
pipeline stage rather than being one omniscient bot. Per-stage scoping is also a natural
least-privilege boundary.

**[Bitmovin Stream Lab MCP Server](https://www.newscaststudio.com/2026/01/06/ces-2026-bitmovins-stream-lab-mcp-server-enables-llm-based-video-testing/)**
(CES 2026) — a shipped enterprise product in this vertical: agentic test creation, execution, and
analysis across 30+ physical devices (Samsung / LG / Vizio TVs, browsers, consoles) "without
requiring traditional software integration." The framing is instructive — MCP replaced the
*integration work*, not the testing.

**Other playout vendors.** Amagi, Harmonic, Grass Valley, Imagine, and Ross all market "agentic
operations" — Amagi has a paper titled *"From manual to agentic operations"* — but as of this date
none has published an MCP interface. Veset is the sole exception, by one day.

### 1.4 The low-value tier — do not build this

There are at least a dozen FFmpeg MCP servers
([ffmpeg-mcp](https://github.com/AmolDerickSoans/ffmpeg-mcp),
[video-audio-mcp](https://github.com/misbahsy/video-audio-mcp),
[ffmpeg-mcp-comp](https://github.com/video-dev/ffmpeg-mcp-comp),
[mcp-ffmpeg](https://pypi.org/project/mcp-ffmpeg/)). Nearly all are the same thing: the LLM writes
an FFmpeg command line and the server shells out.

That pattern is worthless here. This product's value is not in constructing a command — it is in the
state of a running pipeline. The one exception,
[ffmpeg-micro-mcp](https://github.com/javidjamae/ffmpeg-micro-mcp), wraps a **REST job API** so
agents can "create, monitor, and download" transcodes. That is the wrap-a-service pattern, and it is
the correct one.

> **In plain terms:** putting an AI in front of a command-line tool is a party trick. Putting one in
> front of a live system that is currently broadcasting is a product. This codebase is the second
> thing.

### 1.5 Security precedent

[CVE-2026-46519](https://www.manifold.security/blog/mcp-server-kubernetes-readonly-bypass) is a
high-severity access-control bypass in `mcp-server-kubernetes` where any client could invoke
restricted tools despite read-only mode being enabled; fixed in v3.6.0.

**Implication:** if a read-only flag is implemented, enforce it at the dispatcher, not per-tool. A
per-tool check is one forgotten decorator away from being no check at all.

---

## Part 2 — Gap analysis against this source tree

Veset organises its API into 11 resource groups. Measuring this tree against that taxonomy is the
most useful output of the survey, because it is a competitor's own product decomposition.

| Veset group | This codebase | State |
| --- | --- | --- |
| Ingest | `/get-inputs`, `/get-input/{id}`, `/set-input/{id}`, `/set-inputs`; gRPC `getInputs`, `getInput`, `setInputStream` | Present; only `inputs[0]` is consumed by the parser |
| Library | `/media`, `/media/import`, `DELETE /media/{id}` | Present |
| Channel | `/get-outputs`, `/get-output/{id}`, `/set-output/{id}`, `/set-outputs` | Present |
| Schedule | `/scheduled_events`, the `/rundowns/*` family | Present; strongest surface in the tree |
| Playout | managed queue, gRPC `applyPlaylistSnapshot`, `runtime/main.m3u8` | Present |
| Audit | `/rundowns/{id}/audit`, `/rundowns/{id}/executions` | Present **for rundowns only** |
| Configuration | `/get-config`, the four `set-*` routes | Present; write routes are broken — see [32](32-performance-and-tooling-findings.md) finding 4 |
| Monitoring | `/check-transcoder` | Liveness ping only |
| **Accounts** | — | **Absent.** No auth, no roles, no tokens |
| **Metrics** | — | **Absent.** Zero telemetry — see [32](32-performance-and-tooling-findings.md) |
| **Reporting** | — | Absent |

**7 of 11 groups already exist.** The four missing ones are precisely the four that make Veset's
safety guarantee possible.

### Gap 1 — Accounts (blocking)

`api/src/transcoder_api/main.py` has no auth, no CORS, and no middleware. Veset's entire claim is
"the same permissions customers already rely on." There are no permissions here to inherit.

Their token model is a cheap, proven template: organisation-scoped tokens, per-integration issuance,
least-privilege role assignment, short expiry, stored outside version control.

**This blocks every write tool.** It does not block read-only tools on a trusted network, but it
does mean a read-only server must be enforced at the dispatcher (see §1.5).

### Gap 2 — Metrics (blocking for the interesting features)

Veset can advertise *monitor playout health* because a Metrics API exists. `/check-transcoder`
answers up-or-down and nothing else. Per [32](32-performance-and-tooling-findings.md), this product
has no metrics, no tracing, and unused GStreamer tracers.

This is the difference between an MCP server that reads values back and one that diagnoses. It is
also the same conclusion page 32 already reached by a different route — the missing telemetry layer
is now also the missing MCP layer. **MCP is a useful forcing function here:** build the tracer and
metrics layer *because* it is the thing the agent reads.

### Gap 3 — Audit scope

[30](30-transactional-rundown-service.md) already provides versioned, immutable, audited mutation —
but only for rundowns. Veset applies that model to everything.

Generalising the existing audit model across inputs, outputs, and config is smaller than building
one from scratch, and it converts the riskiest tools from irreversible into recoverable.

### Gap 4 — `GET /restart-transcoder` (defect, fix regardless)

`/restart-transcoder` is registered as a **GET**. That places an on-air-killing side effect behind a
verb that every convention, crawler, and cache treats as safe. An agent that speculatively probes
GET endpoints to orient itself will take the channel off air.

**Fix this before any MCP server sees the API**, and independently of whether MCP is ever built.

---

## Part 3 — Proposed tool surface

Tiered by blast radius. Tier 0 carries roughly 80% of the operator value and 5% of the risk.

### Tier 0 — read-only

Direct wraps of existing routes:

| Tool | Backed by |
| --- | --- |
| `get_inputs`, `get_input` | `/get-inputs`, `/get-input/{id}` |
| `get_outputs`, `get_output` | `/get-outputs`, `/get-output/{id}` |
| `get_config` | `/get-config` |
| `health` | `/check-transcoder` |
| `list_media` | `/media` |
| `list_graphics_cues`, `graphics_catalog` | `/graphics/cues`, `/graphics/catalog` |
| `list_scheduled_events` | `/scheduled_events` |
| `rundown_status`, `get_rundown` | `/rundowns/status`, `/rundowns/{id}` |
| `rundown_audit`, `rundown_executions`, `rundown_occurrences` | the `/rundowns/{id}/*` GETs |
| `get_playlist` | `/playlists/{id}` |

Reads with **no REST endpoint today**, which are where the diagnostic value actually is:

- `tail_manifest` — read `runtime/main.m3u8`: segment count, discontinuities, target duration,
  media-sequence drift.
- `query_playlist_db` — bounded read of `runtime/managed-playlist.sqlite`: current item, queue
  depth, last `applyPlaylistSnapshot` result and revision.
- `query_events_db` — SCTE-35 and blackout event history.
- `tail_logs` — Supervisor and GStreamer stderr from the container.
- `describe_capabilities` — which SDK-gated features **this build** actually has (Dolby PAT, NDI,
  hardware encode), read from the CMake configuration rather than from documentation.

`describe_capabilities` alone answers a recurring class of operator doubt — "does this box do X" —
that currently requires reading [02](02-feature-catalog.md) and trusting it.

### Tier 1 — mutating, reversible

`set_input` / `set_inputs` (source switching), `add_graphics_cue`, `delete_graphics_cue`,
`import_media`, and the rundown draft path: `put_draft`, `validate_draft`, `resolve_time`.

**The rundown draft surface is the single best write target in the tree, and it is not close.** Per
[30](30-transactional-rundown-service.md) the draft is a single mutable row that never reaches air,
audited, validated on every write, and requires a matching `expectedRevision` so a stale write returns
409 rather than clobbering; publishing is a separate, immutable, human-gated step, and the whole
service is disabled by default. An LLM writing a draft that is then validated and human-published is a
genuinely safe loop: the model cannot put anything to air by mistake, and every edit carries an
author trail.

**Start writes here, not on inputs.**

### Tier 2 — destructive / on-air

`set_output` / `set_outputs`, `restart_transcoder`, `apply_playlist_snapshot`, `delete_media`,
`publish_rundown`.

Requirements for this tier:

- Annotate with MCP `DestructiveHint` so clients know to prompt.
- Use **elicitation** to force a confirm round-trip. The 2026-07-28 MCP spec's multi-round-trip
  pattern (`InputRequiredResult` with `inputRequests` plus an opaque `requestState` the client echoes
  back) is designed for exactly this.
- Log the **initiating operator and the prompt**, not merely the resulting API call.

Veset shipped "take events live on air" in a first release. They can absorb that risk because a
customer misconfiguring an RBAC role is contractually the customer's problem. That defence is not
available here until Gap 1 is closed.

### Resources — the underrated half

Expose as MCP **resources**, not tools: the JSON config schema, `config-documentation.yaml`, the
current effective config, the live manifest, and selected handbook pages.

This matters disproportionately here because of the parser. Per [32](32-performance-and-tooling-findings.md)
finding 9 and the traps catalogued in [31](31-code-reading-roadmap.md), the 1,282-line config parser
silently ignores several documented-looking fields:

- only `inputs[0]` is consumed;
- `video.interlace` sits inside `#if 0`;
- the `adobe` and `enhanced` ad markers sit inside `#if 0`;
- `scte.prerollTime` is parsed but never read.

An LLM handed only the OpenAPI schema will confidently write config using every one of those and
report success while nothing happens. A resource stating the **effective** schema, plus a
`validate_config` tool, is what turns MCP config authoring from actively dangerous into useful.

> **In plain terms:** the configuration file has fields that look real, are accepted without
> complaint, and do nothing. A human learns this the hard way once. A language model will make the
> same mistake every single time unless it is told, in writing, which fields are dead.

### Prompts

Ship canned MCP prompts encoding existing runbooks:

- *diagnose black or frozen output*
- *why did rundown item N fail*
- *pre-flight this rundown against media availability*
- *prepare channel for an ad break*
- *explain the current config in plain English and flag settings that are silently ignored*

---

## Part 4 — Future features MCP enables

These are not "the API in English"; they are things that become possible only once an agent can
compose the surface.

**Diagnosis instead of readout.** The Grafana/Sift lesson. A `diagnose_output` tool correlating
manifest stall, last SCTE event, engine state, and encoder CPU is worth more than ten getters.
Requires Gap 2.

**Dry-run config authoring.** "Give me a 1080p50 HLS output with 6-second segments" → generated
config → validated → diffed against current → human applies. Requires a `--validate-only` mode in
the parser, which does not exist today; it is small and self-contained, and it makes the whole write
path safe.

**Natural-language scheduling** on the transactional rundown service — "move the 18:00 promo to
18:30 and push everything after it" — resolved through `POST /rundowns/{id}/time/resolve` and
`/draft/validate` before commit.

**Post-incident narration.** `events.sqlite` + rundown audit + executions history → "at 14:32 the
SCTE-35 splice arrived 400 ms late; item 3 was cut short by 12 frames." A report nobody currently
writes by hand.

**Agentic QC harness** — the Bitmovin play. Drive the existing 75 shell / 45 proto / 12 audio tests
([23](23-testing-and-validation.md)) and have the agent interpret failures rather than a human
reading raw output.

**Multi-agent by pipeline stage** — the AWS play. Separate ingest, transcode, and delivery agents,
each scoped to its own tool subset. This doubles as a least-privilege boundary.

---

## Part 5 — Recommendation and sequencing

**Build it. Ship v1 read-only.**

| Phase | Contents | Gate |
| --- | --- | --- |
| 0 | Fix `GET /restart-transcoder` → POST | None; do this regardless |
| 1 | Tier 0 tools + config/schema resources + runbook prompts. Read-only enforced at the dispatcher. | None |
| 2 | Auth: tokens, roles, least-privilege issuance | — |
| 3 | Metrics/telemetry layer; then `diagnose_*` tools | After 2 |
| 4 | Rundown-draft writes | After 2 |
| 5 | Generalise audit beyond rundowns | After 2 |
| 6 | Input/output/on-air writes with `DestructiveHint` + elicitation | After 4 and 5 |

Phase 1 is roughly 400 lines of Python beside the existing FastAPI app — FastAPI to MCP is close to
mechanical — carries essentially no on-air risk, and answers the majority of what operators actually
ask.

**One implementation note:** do not naively tool-wrap `_getChannel()`. Per
[32](32-performance-and-tooling-findings.md) finding 1, `api/src/transcoder_api/services/transcoder.py`
builds a fresh gRPC channel per call. An agent firing twenty exploratory calls creates twenty
channels.

**Timing.** The playout category was one day into having this feature when this page was written.
That is the earliest possible moment to be a fast follower, and Veset has usefully published the
taxonomy to build against. Re-check §1.1 before acting on it.

## Related

- [17](17-rest-grpc-and-sqlite-control-plane.md) — the REST/gRPC/SQLite surface an MCP server wraps.
- [30](30-transactional-rundown-service.md) — the safest write surface in the tree.
- [32](32-performance-and-tooling-findings.md) — the missing telemetry, the broken config write
  routes, and the gRPC channel-per-call finding.
- [31](31-code-reading-roadmap.md) — where the silently-ignored config fields are documented.
- [22](22-licensing-crypto-and-security.md) — existing security posture.

## References

Retrieved 2026-08-07. Vendor pages are marketing sources and are labelled as such.

| Source | Type |
| --- | --- |
| [Introducing the Veset Nimbus MCP Server](https://www.veset.tv/news/introducing-the-veset-nimbus-mcp-server) | Vendor announcement, 2026-08-06 |
| [Veset Nimbus API documentation](https://apidocs.veset.tv/) | Vendor technical docs |
| [grafana/mcp-grafana](https://github.com/grafana/mcp-grafana) | Open source |
| [Grafana MCP introduction](https://grafana.com/docs/grafana/latest/developer-resources/mcp/introduction/) | Vendor technical docs |
| [containers/kubernetes-mcp-server](https://github.com/containers/kubernetes-mcp-server) | Open source |
| [Red Hat: Kubernetes MCP server](https://developers.redhat.com/articles/2025/09/25/kubernetes-mcp-server-ai-powered-cluster-management) | Technical article |
| [CVE-2026-46519 read-only bypass](https://www.manifold.security/blog/mcp-server-kubernetes-readonly-bypass) | Security advisory |
| [royshil/obs-mcp](https://github.com/royshil/obs-mcp) | Open source |
| [ironystock/agentic-obs](https://github.com/ironystock/agentic-obs) | Open source |
| [AWS Agentic Intelligent Operations, NAB 2026](https://www.storagenewsletter.com/2026/04/27/nab-show-2026-hydrolix-powers-aws-agentic-intelligent-operations-for-streaming-media/) | Trade press |
| [Bitmovin Stream Lab MCP Server, CES 2026](https://www.newscaststudio.com/2026/01/06/ces-2026-bitmovins-stream-lab-mcp-server-enables-llm-based-video-testing/) | Trade press |
| [MCP 2026-07-28 spec changes](https://stacktr.ee/blog/mcp-2026-spec-changes) | Technical article |
| [MCP elicitation and sampling](https://stacktr.ee/blog/what-is-mcp-elicitation) | Technical article |
| [WorkOS MCP features guide](https://workos.com/blog/mcp-features-guide) | Technical article |
