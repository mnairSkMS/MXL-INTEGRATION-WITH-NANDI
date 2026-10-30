# Agent Workflows and Change Recipes

## Before every change

1. Identify the feature row in 02 and owning subsystem in 26.
2. Read the source path, parser/API contract, graph, plugin origin, and current tests.
3. State whether the change affects source, config, API, database, GStreamer caps, Docker packaging, external runtime dependencies, or documentation.
4. Do not copy from another transcoder without direct proof in this tree.

## Common recipes

### Add a config field

Update the JSON parser and parameter model/defaults; add application validation; update `transcoder/config/doc/openapi.yaml` only after parser behavior is established; add a minimal valid example and invalid tests; document it in 05 and 26. Trace the field through to a GStreamer property or internal behavior—parsing alone is insufficient.

### Add a media transform

Start at `Channel` processor/connector selection. Define caps and dynamic/request-pad ownership, live/fallback behavior, graph teardown, output sharing, event interactions, and error propagation. Add a focused current-source media test that validates content, then update 09 and its media-specific guide.

### Add an input or output protocol

Add transport parameter parsing and validation, URI dispatch, source/sink construction, Docker/runtime dependency provisioning, reconnection/error behavior, and protocol tests. Update 07 or 13/14; do not claim support until a real peer is exercised.

### Add a scheduled event

Update REST model/validation, database representation, C++ provider parsing, `Event` type, scheduler subclass, channel/compositor/output actions, cancellation/cleanup, and API/media tests. Update 15–17 and make timing/clock units explicit.

### Add or update a GStreamer plugin

Update CMake/install behavior, element registration, pad templates/caps, properties, source call sites, Docker plugin path, `gst-inspect` validation, and buffer-level tests. Document in 18/19.

## Review checklist

- Input/output caps and container compatibility checked.
- Live clocking, queueing, EOS, error, and shutdown checked.
- API/config/schema/examples/test contract synchronized.
- Generated/prebuilt artifacts and sandbox code not mistaken for production source.
- External dependencies/credentials/license requirements labeled correctly.
- New or changed docs include evidence status and an observable validation command.
