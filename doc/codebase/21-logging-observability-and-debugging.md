# Logging, Observability, and Debugging

## Logging

The JSON debug parser configures separate UTPL/transcoder and GStreamer logging. It validates enabled file outputs have paths. Application and library code use project debug/log macros and spdlog-backed facilities; GStreamer debug output is configured through UTPL debug helpers.

Start by collecting the source version, command line, config hash with secrets removed, plugin registry, image digest, host media/GPU facts, and logs from both API and transcoder processes.

## As-run event log

Every scheduled event writes one `FIRED` line when it takes effect on air and one `ENDED` line when it stops, on the `schedule` logger at the normal (`log`) level, so they print with `application.logger.transcoder.level` at `log` or finer. The format is fixed `key=value` text:

```text
FIRED id=4 type=scte35_timesignal at=2026-09-24T07:18:14.229Z running_time=0:01:12.320 segmentation=3001:0x34 duration_ms=6000 preroll_ms=4000 section=built
ENDED id=4 type=scte35_timesignal at=2026-09-24T07:18:20.229Z running_time=0:01:18.320 reason=segmentation_duration_elapsed
```

`id` is the `GET /scheduled_events` id and `type` its REST type name. `at` is the UTC wall clock of the on-air moment, `running_time` the channel running time (`h:mm:ss.mmm`, `-` when the channel has no video compositor). The remaining keys depend on the type.

| Type | `FIRED` | `ENDED` |
| --- | --- | --- |
| `image_overlay` | The graphic's first buffer reaches the compositor (the pad probe in `overlay.cpp`) | The fade-out starts; `fade_out_ms` says when it is fully gone |
| `squeeze` | The window starts moving (`fade_in_ms` to reach the target) | The window starts returning to full frame (`fade_out_ms`) |
| `slate` | The channel switches to the slate input; `video`/`audio` say what the slate replaced (`muted` for audio-less media) | The original input is restored |
| `scte35_spliceinsert` | The section is injected; `at` and `running_time` are the splice point, `preroll_ms` after injection | At splice + `duration_ms`, only when `auto_return=yes`. Without auto-return the break ends on the matching `scte35_returntonetwork` |
| `scte35_timesignal` | As splice insert; `segmentation` lists `event_id:type_id` per descriptor | At splice + the longest `segmentation_duration`, when one is present |
| `scte35_returntonetwork` | As splice insert; this line is the end of the break it closes | — |

A `MISSED` line at warning level reports an event that never went on air, with a `reason`: an overlay image that did not load in time or a channel without a video compositor, a slate whose media could not be downloaded or has no playable stream, or a `squeeze_name` missing from the engine config. A cancelled event that had not started logs neither line.

The SCTE `ENDED` lines are computed from the signalled duration at injection, not observed: the scheduler's own stop timer never runs for SCTE events, which are constructed with a base duration of 0.

The HLS writer adds one line the first time each `EXT-X-CUE-OUT` or `EXT-X-CUE-IN` lands on a segment, on the debug logger at the normal level:

```text
HLS EXT-X-CUE-OUT placed playlist=variant_0.m3u8 segment=20 uri=variant_0_section_20.ts program_date_time=2026-09-24T07:18:16Z splice_running_time=0:01:12.320 segment_running_time=0:01:12.320 command=time_signal event_id=3001 duration_ms=6000 auto_return=yes
```

`program_date_time` is the segment's `EXT-X-PROGRAM-DATE-TIME`, the cue's wall clock for a player of that playlist. `event_id` is the `splice_event_id`, or the first `segmentation_event_id` of a `time_signal`. The playlist is rebuilt on every refresh, so `MediaPlaylist` remembers which placements it has logged and forgets them as segments leave the window; `EXT-X-CUE-OUT-CONT` is not logged. The line appears when the segment enters the playlist, a segment or more after the splice.

To read these lines on a live channel, set `application.logger.gstreamer.level` to `warning`: at `log` or finer, GStreamer prints thousands of lines a second around them.

## Media debugging workflow

1. Validate configuration parse and application validation before starting media flow.
2. Use `gst-inspect-1.0` for every nonstandard factory in the graph.
3. Capture GStreamer debug selectively by category; avoid unbounded all-category logs in live systems.
4. Enable DOT dumps for a reproduction where supported by the engine source and inspect linked pads/caps.
5. Use `ffprobe` for stream/container/timebase facts.
6. Use TSDuck for MPEG-TS PID, PMT, continuity, SCTE, and SRT inspection.
7. Decode/extract frames for overlay/squeeze/slate assertions; analyze audio separately.

## Common failure categories

- Missing/factory-not-found plugin: deployment/path/packaging issue.
- Pad link/caps failure: wrong media type, insufficient converter, or encoder caps mismatch.
- HLS upload failure: endpoint/auth/WebDAV/retry issue.
- Licensing failure: key/license/server/session validation issue.
- API error: request validation, database state, gRPC reachability, or transcoder state issue.
- SCTE mismatch: input acquisition, running-time mapping, muxer/patch behavior, or assertion issue.

Do not turn a warning into an ignored condition without recording whether it affects output correctness.
