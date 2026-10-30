Yes. For your coding agent, the useful target is **not “copy Veset Nimbus”** but:

> **Determine whether our existing control plane already contains the primitives that Veset exposed through MCP, and estimate how much work is required to add an MCP adapter safely.**

One important caveat: Veset has **not publicly published the complete MCP tool manifest or source code**. What is publicly described is the capability surface. The August 2026 material captured in the research says Nimbus MCP connects Claude to Nimbus and gives permissioned access to channel health, schedules, assets, ingest/audit information, playlist/configuration changes and manual event-to-air actions. 

So below I’ll separate **confirmed MCP capability** from **my inferred implementation**.

---

# 1. Channel / playout health inspection

### Confirmed capability

The MCP can inspect **channel/playout health**. 

Conceptually the user can ask things like:

```text
What's wrong with Channel 4?

Are all channels healthy?

Which channels currently have warnings?

Is the backup playout running?
```

This is primarily a **read-only operational tool**.

### What the MCP probably exposes

The exact Veset tool names are private, but conceptually:

```text
list_channels()

get_channel_status(channel_id)

get_playout_status(channel_id)

get_channel_alarms(channel_id)

get_output_status(channel_id)
```

Potential response:

```json
{
  "channel": "sports-uk",
  "state": "ON_AIR",
  "playout": "healthy",
  "active_source": "primary",
  "schedule_loaded_until": "2026-08-13T06:00:00Z",
  "warnings": [],
  "output": {
    "status": "connected"
  }
}
```

### What your codebase needs

Your agent should search for:

```text
Channel
Playout
Session
Pipeline
Output
Stream
Health
Status
Monitor
Alarm
Metrics
Heartbeat
```

and answer:

* Is there a single method/API for getting the current state?
* Is state machine based or reconstructed from logs?
* Can you determine `RUNNING / FAILED / STARTING / STOPPED`?
* Can you determine active input/output?
* Are alarms machine-readable?
* Are timestamps available?
* Can health be queried without shell access?

If yes, this feature is easy to MCP-enable.

---

# 2. Schedule inspection

### Confirmed capability

The MCP exposes Nimbus schedules/playlists to the agent. 

Nimbus itself already has advanced scheduling, last-minute editing, gap/overlap correction, missing-content identification, secondary events and playlist imports.

This allows conversations like:

```text
What's playing at 7 PM?

Show tomorrow's schedule.

What is currently on air?

What's next?

Are there any schedule gaps?

Which events are missing assets?
```

### Likely internal tool surface

```text
get_schedule(
    channel_id,
    from,
    to
)

get_current_event(channel_id)

get_next_event(channel_id)

find_schedule_issues(channel_id)

get_playlist(playlist_id)
```

The important part is not MCP.

It's whether your system has a clean **schedule domain model**.

Something like:

```text
Schedule
 └── Event[]
      ├── id
      ├── asset
      ├── scheduled_start
      ├── duration
      ├── event_type
      ├── timing_mode
      ├── secondary_events[]
      └── state
```

### Replication requirement

Your coding agent should determine:

```text
Can the code answer:

"what is happening?"
"what happened?"
"what will happen?"
"what should happen?"
```

without scraping a UI.

If the answer is yes, this is MCP-ready.

---

# 3. Asset / media-library access

### Confirmed capability

Nimbus MCP exposes information about **assets/media**. 

Nimbus itself provides media ingestion and media-library functionality and integrates with external MAM systems through REST APIs.

An operator could therefore ask:

```text
Do we have Episode 6?

Where is the master for programme X?

Which assets are unavailable?

Is this file ready for playout?

What audio/subtitle tracks does it contain?
```

### Likely MCP abstraction

```text
find_asset(query)

get_asset(asset_id)

get_asset_media_info(asset_id)

get_asset_readiness(asset_id)
```

For a transcoder-oriented platform, this becomes extremely valuable.

Your domain model might be:

```text
Asset
 ├── id
 ├── source URI
 ├── media metadata
 ├── duration
 ├── video streams
 ├── audio streams
 ├── subtitle streams
 ├── renditions
 └── processing state
```

### What your agent should inspect

Look for:

```text
AssetRepository
MediaRepository
MediaInfo
Probe
FFprobe
Source
Input
MediaMetadata
MediaFile
```

and ask:

* Can assets be searched?
* Is metadata normalized?
* Can readiness be determined?
* Can existing renditions be enumerated?
* Can source availability be checked?
* Can asset IDs be mapped to jobs/schedules?

---

# 4. Ingest status and troubleshooting

### Confirmed capability

Nimbus MCP exposes **ingest information/logs**. 

This is more important than it first appears.

The operator can potentially ask:

```text
Why isn't tonight's programme available?

Did episode 15 ingest?

Which ingests failed today?

Why did asset X fail ingest?
```

The agent can correlate:

```text
schedule
   +
asset
   +
ingest job
   +
logs
```

instead of requiring the operator to manually visit multiple dashboards.

### Likely tools

```text
get_ingest_status(asset_id)

list_ingest_jobs(filters)

get_ingest_errors(job_id)

retry_ingest(job_id)
```

The first three could safely be v1 read-only tools.

The last is a write action.

### For your codebase

Search for:

```text
ingest
input
download
fetch
transfer
upload
source acquisition
job
queue
worker
```

You ideally need a durable structure like:

```text
IngestJob
{
    id
    asset_id
    source
    state
    progress
    started_at
    completed_at
    error_code
    error_message
}
```

If ingest only exists as console logs, an MCP implementation will be much weaker.

---

# 5. Audit-log interrogation

### Confirmed capability

Audit logs are explicitly part of the agent-accessible Nimbus surface. 

This is one of the more interesting features.

It enables questions such as:

```text
Who changed the schedule?

Why did Channel 2 go off air?

What changed in the last hour?

Who switched the live source?

What configuration changed before this incident?
```

That transforms the agent into something resembling an operations investigator.

### Useful MCP tools

```text
search_audit_events(
    channel_id?,
    user?,
    action?,
    from?,
    to?
)

get_recent_changes(resource_id)

get_action_history(resource_id)
```

### Your codebase should ideally have

```text
AuditEvent
{
    timestamp
    actor
    action
    resource_type
    resource_id
    previous_state
    new_state
    request_id
    result
}
```

This is significantly more valuable to an AI than unstructured text logs.

---

# 6. Playlist modification

Now we cross from **observability** into **control**.

### Confirmed capability

Nimbus MCP can make playlist changes subject to existing permissions/guardrails. 

Nimbus itself allows authorized operators to modify both offline and online playlists, including last-minute changes, skip events and manage live sources.

Potential user commands:

```text
Move this programme after the news.

Remove the 14:30 promo.

Replace asset X with asset Y.

Insert this commercial before the live event.

Extend this programme by two minutes.
```

### Likely MCP tools

Not necessarily these exact names:

```text
insert_event()

update_event()

move_event()

delete_event()

replace_event_asset()

skip_event()
```

### But here's the important architecture

It should NOT be:

```text
LLM
 ↓
write directly to database
```

It should be:

```text
LLM
 ↓
MCP
 ↓
ScheduleService
 ↓
validation
 ↓
schedule engine
 ↓
database
```

The same validation should run whether the request comes from:

```text
UI
REST API
MCP
```

### Your coding agent needs to find

A service layer resembling:

```text
ScheduleService
PlaylistService
EventService
```

rather than only:

```text
database repository
```

Then check whether it validates:

```text
gaps
overlaps
locked events
start times
duration
missing assets
timing modes
on-air event constraints
```

If that domain logic already exists, MCP replication is mostly an adapter problem.

---

# 7. Configuration changes

### Confirmed capability

The MCP release is described as allowing configuration changes under existing permissions and guardrails. 

This is potentially broad.

It could mean things such as:

```text
channel configuration
inputs
outputs
playout parameters
graphics configuration
live-source configuration
```

but Veset has **not published enough detail for me to claim exactly which config objects are exposed**.

Treat this category carefully in your coding-agent comparison.

### Generic implementation

Rather than:

```text
update_config(key, value)
```

I would expose constrained operations:

```text
configure_output()

configure_live_input()

update_channel_setting()

enable_feature()
```

And every configuration mutation should ideally support:

```text
validate
dry-run
apply
rollback
```

For example:

```text
propose_channel_config()
        ↓
validate_channel_config()
        ↓
human confirmation
        ↓
apply_channel_config()
```

That is safer than generic mutable JSON.

---

# 8. Manual event-to-air control

This is probably the most distinctive Nimbus MCP feature.

### Confirmed capability

The research specifically identifies **manual event-to-air actions** as part of Nimbus MCP's exposed capabilities. 

Nimbus already supports scheduled and manually controlled live events; operators can use Skip/Take controls and SCTE-35-driven switching in the regular product.

Conceptually an operator could say:

```text
Take the live event now.

Take the backup feed.

Skip the current item.

End the live segment.

Move to the next event.
```

### Likely flow

A safe implementation should be:

```text
Operator:
"Take Event 412 live"

        ↓

Agent:
resolve channel + event

        ↓

get_current_playout_state()

        ↓

validate_take_action()

        ↓

authorization check

        ↓

possibly require confirmation

        ↓

take_event_to_air()

        ↓

deterministic playout service

        ↓

verify resulting state
```

The LLM should **not perform the frame-critical switching**.

It requests the operation.

The existing playout system performs it.

### Your codebase check

Search for things like:

```text
Take
Skip
Cue
Next
Play
Live
Switch
Transition
Trigger
OnAir
PlayoutController
```

The critical question is:

> Does your code already expose an atomic command such as `take(event)`?

If yes:

```text
MCP implementation = relatively straightforward
```

If instead the UI manually manipulates lots of internal state to create a take, replicating it safely will be much harder.

---

# 9. Permission-aware AI actions

This is not a flashy feature, but technically it might be the most important one.

Veset's reported approach is that MCP actions go through the existing Nimbus permission/guardrail/audit model rather than giving Claude a new unrestricted administrative identity. 

You want:

```text
User
 ↓
AI client
 ↓
MCP
 ↓
authenticated user context
 ↓
existing authorization
 ↓
operation
```

Not:

```text
User
 ↓
AI
 ↓
MASTER_ADMIN_API_KEY
 ↓
everything
```

Your coding agent should identify:

```text
AuthenticationService
AuthorizationService
RBAC
Permissions
Roles
Policies
Claims
JWT
```

and determine whether an MCP request could preserve an actual user identity.

Example:

```text
user: alice

permissions:
    channel.read
    schedule.read
    schedule.modify

denied:
    playout.take
    channel.delete
```

Then MCP automatically has the same restrictions.

---

# 10. Every agent action is auditable

Closely related, but separate.

If Claude says:

```text
"Replace programme X with programme Y."
```

the audit log should not merely record:

```text
schedule changed
```

Ideally it records:

```text
actor = alice
interface = MCP
agent_session = abc123
tool = replace_event_asset
resource = schedule_event_918
old_asset = 15
new_asset = 22
timestamp = ...
result = successful
```

This matters enormously once agents make real operational changes.

Your coding agent should check whether existing mutations already produce structured audit events.

If they do, **reuse them**.

Don't create an MCP-specific audit universe.

---

# 11. Natural-language cross-system reasoning

This is not technically an MCP tool.

It emerges because the model can combine multiple tools.

This is where Nimbus becomes more useful than merely saying:

```text
"click button X using chat."
```

Example:

> Why won't tonight's film play?

Agent:

```text
get_schedule()
      ↓
find asset ID
      ↓
get_asset()
      ↓
get_ingest_status()
      ↓
get_audit_log()
```

Then:

```text
The film is scheduled correctly,
but the replacement master uploaded at 09:42
failed ingest because its audio layout was rejected.
```

Nothing special needs to exist in the backend.

What matters is that MCP exposes **small composable primitives**.

That is something your coding agent should evaluate too.

Ask:

> Can our existing APIs be combined to answer operational questions without requiring hidden UI state?

---

# 12. Monitoring multiple channels

Nimbus itself supports centralized multi-channel management and monitoring.

Even if the MCP announcement doesn't give us the exact tool schema, a channel-health interface naturally enables queries such as:

```text
Show unhealthy channels.

Which channels have missing content?

Which channels have schedule problems in the next hour?
```

For your architecture this suggests that tools should support **batch queries**.

Avoid forcing the model to call:

```text
get_channel_status()
```

500 times.

Provide:

```text
list_channel_health(
    state="warning"
)
```

or:

```text
find_channels_at_risk(
    horizon_minutes=60
)
```

This will matter significantly at scale.

---

# 13. Live source visibility/control

Nimbus supports multiple live feeds, operator-controlled live events and SCTE-35-driven live switching in the base platform.

We know manual event-to-air is exposed through MCP, but **we do not have public evidence that every low-level live-source control is an MCP tool**.

So classify this for your coding agent as:

```text
CONFIRMED:
manual event-to-air

LIKELY RELATED BACKEND CAPABILITY:
live input state
live input selection
skip/end live event

NOT PUBLICLY CONFIRMED AS INDIVIDUAL MCP TOOLS:
every possible source/transport setting
```

That's an important distinction.

---

# 14. What Nimbus MCP does NOT replace

Your coding agent should understand this too.

Nimbus MCP is not:

```text
video decoder
encoder
frame scheduler
SCTE engine
transport engine
graphics renderer
media storage
```

Nimbus itself continues doing those deterministic broadcast jobs. The MCP layer provides an agent-facing control surface over them. Veset's base platform remains responsible for scheduling, graphics, encoding, live switching, SCTE-35 and actual playout.

Think:

```text
                   AI layer
                      ↓
                 Nimbus MCP
                      ↓
       existing Nimbus APIs/services
                      ↓
             actual playout engine
```

For your codebase:

> **Do not ask whether the transcoder itself understands MCP.**

Ask:

> **Does the transcoder have enough structured control APIs for a thin MCP service to expose?**

---

# The replication checklist I'd give your coding agent

Have it score each capability as:

```text
READY
PARTIALLY READY
MISSING
UNSAFE TO EXPOSE
```

using this matrix:

| Capability               | Backend prerequisite                |
| ------------------------ | ----------------------------------- |
| List channels/services   | Channel/service repository          |
| Channel health           | Structured health/state API         |
| Current playout state    | Runtime state API                   |
| Schedule query           | Schedule/event domain model         |
| Current/next event       | Timeline query                      |
| Schedule validation      | Deterministic validation service    |
| Asset lookup             | Asset repository                    |
| Media metadata           | Probe/metadata service              |
| Asset readiness          | Asset processing state              |
| Ingest status            | Durable ingest-job state            |
| Ingest errors            | Structured failure reporting        |
| Audit search             | Structured audit service            |
| Modify playlist          | Schedule mutation service           |
| Insert/remove/move event | Event mutation API                  |
| Configuration change     | Validated configuration service     |
| Take event to air        | Atomic playout-control command      |
| Skip/end event           | Atomic playout command              |
| Permissions              | Existing RBAC/policy system         |
| Agent auditing           | Audit context propagation           |
| Multi-channel queries    | Batch/filter APIs                   |
| Long-running jobs        | Durable job IDs/state               |
| Safe agent writes        | validation + approval + idempotency |

---

# What I would have your coding agent search for specifically

You can tell it:

```text
Do not implement anything yet.

Analyze this repository and determine whether its existing architecture
could support a Veset Nimbus-style MCP control layer.

For each capability below, locate the exact classes, services, APIs,
controllers, repositories, state machines and data models that could
implement it:

1. List channels/services/pipelines.
2. Get runtime health and current state.
3. Get active/current operation.
4. Query scheduled/upcoming work.
5. Inspect assets/media inputs.
6. Inspect media metadata.
7. Inspect ingest/input-processing state.
8. Query job/error logs.
9. Query structured audit/history.
10. Modify scheduled jobs/events.
11. Change validated configuration.
12. Trigger an immediate operational action analogous to "take to air".
13. Retry/skip/cancel/end an operation.
14. Enforce existing user permissions for each operation.
15. Produce an immutable audit record for an AI-triggered operation.
16. Support batch/fleet health queries.
17. Support long-running asynchronous operations through durable job IDs.

For each item return:

- READY / PARTIAL / MISSING
- relevant files/classes/functions
- existing API endpoint if any
- data model involved
- whether operation is read or write
- whether it is safe for an LLM to invoke
- missing abstraction needed
- estimated implementation complexity
- recommended MCP tool schema

Also identify places where the UI directly manipulates state instead of
calling a reusable service/API. Flag these because they would need to be
refactored before safe MCP exposure.

Do NOT propose letting the LLM directly access SQL, shell commands,
FFmpeg command strings, internal mutable state or unrestricted generic
REST calls.

Assume architecture should be:

AI client
→ MCP server
→ authentication/RBAC
→ domain service/control API
→ existing deterministic processing engine.

The MCP layer must not become part of the media-processing/data path.
```

That analysis will tell you something much more useful than simply **“can we add MCP?”** It will tell you whether your codebase already has a sufficiently clean **control plane** to expose to an agent.

If you want, the next step can be even more concrete: we can take **your transcoder architecture/code-reading roadmap** and map these ~17 Nimbus-style capabilities onto the actual components/classes in your codebase one by one.
