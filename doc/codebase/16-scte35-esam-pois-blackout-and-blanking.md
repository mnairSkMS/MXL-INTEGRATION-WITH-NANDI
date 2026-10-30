# SCTE-35, ESAM/POIS, Blackout, and Blanking

## Source-backed components

- `ScteEventTracker` (`transcoder/scte/eventTracker.cpp`) owns the SCTE event state machine:
  descriptor parsing, scheduling a start at the splice point and a stop after the signalled
  duration, cancellation, duration updates, and priority resolution across overlapping events.
  It reports one resolved on/off state to its subclass.
- `Blanker` is that tracker with blanking as the action. Blanking and blackout are two rule sets
  fed into one tracker, so they share a single event list and priority resolution, which is what
  the merged implementation has always done.
- `AdBreakSwitcher` is the same tracker with a programme switch as the action; see 34.
- `Channel::onScteData` is additive: blanking and ad insertion both subscribe, and registering one
  must not displace the other.
- `Blanker` monitors SCTE splice sections and applies blackout/blanking policy.
- `SingleInput` can acquire SCTE from MPEG-TS/HLS-oriented input paths.
- `MpegTsMuxer` and HLS paths handle SCTE output-related flow.
- The event scheduler creates splice-insert, time-signal, and return-to-network actions.
- `utpl::esam::Client` and model classes handle external conditioning requests.
- `Channel` handles POIS conditioning responses: NOOP, DELETE, and REPLACE.

## Output modes and patches

Output SCTE parameters expose a PID, a mode, and a null interval. The parser recognizes `passthrough`; other behavior must be traced to parameter defaults and muxer logic. The Docker build applies patches that add mpegtsmux SCTE-null control and concurrent SCTE behavior. The application also patches DVB AC-3/E-AC-3 and H.26x-over-SRT behavior at dependency build time.

## Base64 section insertion

The three SCTE event types (`scte35_spliceinsert`, `scte35_timesignal`,
`scte35_returntonetwork`) accept an optional `scte35_base64` field carrying a complete,
pre-authored `splice_info_section`. When present it supplies the signalling payload instead
of the field-by-field builder.

The section is not re-serialised. `scheduler::sectionFromBase64`
(`transcoder/event/scheduler/scte.cpp`) shifts the whole section onto this output's timeline
by setting `pts_adjustment`, which a compliant receiver adds to every splice time, and
recomputes the CRC_32. Every other byte the operator supplied reaches the wire untouched -
segmentation descriptors, UPIDs, break durations, event IDs and restriction flags included.
Rewriting the splice time instead would mean a round trip through GStreamer's SIT model,
which silently drops anything that model does not represent.

Only `splice_insert` and `time_signal` are accepted, matching the restriction
`Channel::injectScte35Event` and `hls::Streamer::putScteData` already impose. The REST layer
(`validate_scte35_base64` in `api/src/transcoder_api/services/schedule.py`) rejects anything
else - bad base64, wrong `table_id`, inconsistent `section_length`, encrypted sections, a
command that does not match the event type, or an `out_of_network_indicator` that contradicts
it - so an operator gets a 400 rather than a marker that silently never appears.

A section requesting an immediate splice carries no time to re-base and is passed through
unshifted; it is muxed at the scheduled moment regardless.

Note that for an already-packetized section, mpegtsmux does **not** re-time from the
`running-time-map`. It only adjusts `pts_adjustment` and recomputes the CRC; the map is used
solely to request key frames at the splice points. The splice times written into the section
are what reaches the wire.

## Conditioning

POIS configuration provides enablement, server URI, acquisition point ID, and timeout. ESAM/POIS are external integrations: a source path does not prove an endpoint contract or production result. The test tree has mock POIS processors and NOOP/DELETE/REPLACE scenarios that should be run with inspected assertions before raising their status.

## Blackout and blanking

The JSON parser accepts both `blanking` and `blackout` sections and delegates to blackout parsing. `Blanker` evaluates splice commands and segmentation descriptors, restriction flags, and configured ignore modes. Source explicitly skips or warns on several unsupported descriptor/event shapes. Document those exact conditions rather than simplifying them to “SCTE blackout supported.”

## Verification matrix

Use TSDuck to inspect PID/PMT/SCTE sections and descriptors, then inspect output media and blackout/slate frames. Cover disabled, passthrough, removal/filtering, null insertion, HLS markers, API-created events, POIS NOOP/DELETE/REPLACE, restriction honoring/ignore modes, splice-out/in, time signal, and return-to-network.

## Base64 encoding

`utpl::utils::toBase64` / `fromBase64` (`lib/common/src/utpl/common/utils.cpp`) are the single
implementation, used by the ESAM client, the HLS `OATCLS-SCTE35` tags, and the SCTE-35 log
lines. `fromBase64` resizes to the actual decoded length, which matters wherever the result
is fed to a length-sensitive consumer such as `gst_mpegts_section_new`.

## Known limitation

`esam/model.cpp` rejects `SCTE35PointDescriptor`; that is an explicit unsupported model feature, not a missing test.
