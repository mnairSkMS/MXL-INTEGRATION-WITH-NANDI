# Testing and Validation

## Test layers

| Layer | Location | Current evidence |
| --- | --- | --- |
| C++ unit tests | `transcoder/lib/*/test` | Release CTest: 7/7 passed on 2026-10-22 after the dev/main merge (`utpl/devel`, `-j3`). |
| ESAM app test | `transcoder/test/esam` | Build target present; runtime endpoint/message behavior needs focused execution. |
| Python API tests | `api/tests` | Full suite observed passing on 2026-10-24: `338 passed, 1 skipped` in the `utpl/devel` image via `poetry install && poetry run pytest -q`, run with `doc/` beside `api/` (two tests read `38-scte35-segmentation-type-id-table.md` and skip without it). The remaining skip is a directory-mode check that root bypasses. |
| Product integration | `tests/tests` | 76 shell-driven cases across protocol/audio/SCTE/event/ad-insertion families. |
| RTMP input | Manual, runtime image | Publish `ffmpeg -re -f lavfi -i testsrc2=size=1920x1080:rate=25 -f lavfi -i sine -c:v libx264 -g 50 -c:a aac -f flv rtmp://<host>:1935/live/<key>` to a channel whose input is `rtmp://127.0.0.1:1935/live/<key>`. Cover engine-first and encoder-first start, `kill -9` and SIGINT of the publisher, and an outage over a minute; after each, a new HLS segment must appear and the concatenated segments must decode with `ffmpeg -v error -f null`. Also check the picture: `ffmpeg -i <segment> -vf signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=y.txt -an -f null -` must not give a constant `YAVG=16` (black), and the log must not repeat "Input is running out of data" every few seconds; either means video is reaching the live `fallbackswitch` late and its black fallback is on air while audio still plays. Repeat with an external pull URI. The relay's "reader is too slow" warning means the input stalled. |
| Channel UI unit tests | `frontend/tests` | 50 tests in 7 files under Vitest + jsdom. `configMapper` covers the form↔engine-config round trip; `probeContract` pins the `GET /inputs/probe` query string and the handling of a response whose optional fields are all null; `CreateChannelPage` and `pollingAndConfirmation` cover the validate→save→restart→poll sequence and the on-air restart confirmation; `sourceIdentity` pins the probe safety guard's URI comparison, which must block the endpoint the engine holds without blocking every other source; `enginePower` pins that a stop is confirmed separately from a restart and never promises the channel comes back on its own; `probeFailure` pins that a failed probe opens a dialog next to the button, shows the appliance's message verbatim, tells the operator to stop the engine when the source is held, and clears on the next success. The component tests stub `fetch` or the `channelApi` module - none reaches a live appliance. Run in `node:20-alpine`; the `frontend-builder` Docker stage runs them, so a failure breaks the image build. |
| Resource stats tests | `api/tests/stats_test.py` | Build a fake `/proc` and `/sys` per test and assert exact figures: CPU cores and percent against a quota or cpuset, host CPU from `/proc/stat`, memory without inactive page cache, writes counted once across a `sda`→`dm-0`→`dm-1` stack, the one-minute write average and the sampler's history pruning, network in bits with loopback excluded, engine discovery by binary path, null rates after an engine restart or with cgroup files missing, and the route. |
| License route tests | `api/tests/license_test.py` | Drive `GET/PUT/DELETE /license` against a stand-in engine binary run through the real subprocess path and the stateful supervisor fake from `config_test.py`. They pin that a refused upload (invalid, expired, not yet valid, online without a license server, empty, oversized) leaves the installed file and the engine untouched, that an accepted upload restarts the engine, and that delete stops the engine before removing the file and does nothing when there is no file. The engine side of `--license-info` needs a real build; see [22](22-licensing-crypto-and-security.md#verification). |
| Channel UI route tests | `api/tests/channel_ui_test.py` | 5 tests pinning the `/channel` index and asset mount: `no-store` on the index, `immutable` on the hashed assets, the `/channel/` base prefix in the emitted HTML, no SPA fallback on a stale asset URL, and that `/operator` is unaffected. They skip when `frontend/` has not been built. |
| Operator UI smoke test | jsdom harness (`drive.mjs`) | Loads the real `operator.html`, `operator.js` and `rundown.js` under jsdom with `fetch` stubbed against **captured live API responses**, in four modes: `enabled` (52 assertions), `anchor` (54), `wallclock` (5), `disabled` (7). Run in `node:20-alpine`; see `api/tests/ui/README.md`. Proven to fail: each regression it exists to catch produces exit 1. `anchor` pins the last program on air because otherwise the next-cycle branch of the projection is never reached and its assertion passes vacuously. |

The channel UI tests cover the client's side of the `/config` and `/inputs/probe` contracts only. Nothing yet drives the built SPA in a browser against a running engine, so the screen is API verified rather than runtime verified; the operator UI's jsdom harness has no equivalent here.

The existing Python suite does not cover `/inputs` against a live gRPC engine or the handwritten `/config` routes. A manual runtime check fills a narrow part of that gap for playlist enumeration/switching and exposes the broken config setters; it does not replace automated regression tests.

The SRT queue latency check uses a controlled, real-time MPEG-TS source with deterministic audio,
an SRT listener input, and an SRT listener output. It records a direct source reference and the
transcoded output to a common stop time, cross-correlates the audio, checks `ffprobe` stream shape,
and inspects logs for input prebuffer transitions, discontinuities, and socket errors. This validates
the local graph and SRT plugin path; it does not model the packet-loss distribution of a production
WAN peer. On 2026-09-02, stream discovery to engine-start fell from about 3.08 s to 57 ms; the
transcoded steady path measured about 0.96 s. Disconnecting a receiver for three seconds and joining
again produced a second decodable H.264/AAC capture without restarting the channel.

The rebuilt Compose service was also exercised against its configured `srt://103.248.72.39:18000`
input and listener output on port 9000. A late-join capture contained H.264 1920x1080/25, AAC 48 kHz
stereo, and the data stream. TSDuck reported 32,553 TS packets, zero invalid-sync or transport-error
packets, and zero expected/unexpected discontinuities on PAT, SDT, audio, and video; decoding after
the first joined GOP produced no errors. The live startup logged no generic input-buffer cycle.

## Load-bearing regression guards

A few tests exist to hold a specific design property that is easy to lose silently.

| Test | Property held |
| --- | --- |
| `rundown_test.test_the_sponsor_swap_that_separate_writes_cannot_express` | The reason the draft could be removed. L-band and Aston share one squeeze resource, so swapping which owns a slot is rejected as three separate applies and accepted as one `ops` list, because validation runs once on the merged result. |
| `bootstrap_test.test_v2_to_v3_drops_only_the_drafts_table_and_keeps_history` | Removing the drafts table must not take the v1 reset path, which would drop ten tables to remove one. Asserts publish, occurrence and audit rows survive the migration. |
| `rundown_test.test_a_document_that_breaks_timing_constraints_cannot_be_applied` | Every write path validates the timeline. The deleted YAML routes did not, and `publish` would promote what they accepted. |
| `bootstrap_test.test_publish_writes_the_channel_file_and_the_result_round_trips` | `lower(lift(doc))` reproduces the canonical document, so the start after a publish reports `unchanged`. Without it, writeback would mint a version on every restart. |
| `bootstrap_test.test_writeback_preserves_the_original_ownership_and_mode` | `os.replace` installs a new inode; the operator must not be left needing `sudo` to edit their own file. |
| `bootstrap_test.test_publish_refuses_before_the_queue_moves_when_the_file_cannot_be_written` | An unpersistable publish is refused before anything goes to air, rather than airing a change the next restart reverts. |
| `rundown_test.test_an_elapsed_wall_clock_trigger_does_not_lock_the_schedule` | A published version stays readable and forkable after its wall-clock triggers fire. The future-window check belongs on writes; enforcing it on reads locked operators out of their own live schedule. |
| `rundown_test.test_publish_keeps_on_air_item_as_one_cycle_bridge_then_cleans_it` | The engine rule that an on-air stable ID cannot disappear from a live snapshot. |
| `rundown_test.test_clock_counts_a_cycle_per_completed_loop` | `cycle` is how the grid header shows the loop advancing, and it is a division by a loop length that can legitimately be zero. |
| `rundown_test.test_clock_with_unprobed_media_reports_a_zero_loop_and_does_not_divide_by_zero` | An unprobed clip is a normal state, not an error; the clock must degrade rather than 500. |
| UI: *rows above the on-air item show their NEXT airing, one loop later* | The grid re-anchors on the observed current item instead of projecting forever from a fixed epoch. Measured clip durations differ from probed values by up to 300 ms, so a fixed anchor drifts. |
| UI: *a rejected apply shows the server's reason in the panel* | Removing the Validate button removed the only writer of the error status. Apply is now the sole write path, so a rejection must surface where the operator is looking, not only in the activity log. |
| UI: *an impossible offset is refused without a request* | Clip-fit is checked in the browser first; a local refusal must land in the same place on screen as a server one. |
| UI: *an unchanged poll does not rebuild the table* | The grid polls at 1 Hz and holds expansion state and controls. A full `replaceChildren`, which is what the old queue table did, would flicker it and collapse every open row under the operator. |

## Integration families

`tests/tests/ad_insertion` covers SCTE-triggered ad insertion: a TSDuck-injected cue sequence drives two breaks and the on-air source is identified by frame colour. Its spots are short solid-colour clips that never roll over inside a break, so it does not exercise the two open defects recorded in 34 - use `tests/manual/ad-insertion` for those - it holds media tooling only (`prepare.sh`, `verify.sh`), because the runtime it used to carry is now the Compose stack itself.

`tests/manual/` holds harnesses that `run.sh` does not collect (it scans `tests/tests` for `test.sh`). They are run by hand, generate their media into a scratch directory outside the tree, and are the fastest way to reproduce runtime behaviour that the automated suites do not assert.

The production shell suite includes roughly 45 protocol matrix cases across `single` and `standard` inputs with file/HLS/SRT/UDP outputs; 12 audio cases; SCTE, blackout, overlay, squeeze, AV-sync, JIRA regression, and POIS cases. The test framework provides process setup, URI/config helpers, media generation, assertions, and cleanup.

`tests/tests/mxl/sdk/` holds nine MXL SDK tests (plugin present, video and 6-channel audio round trips, A/V pair, frame rates, ring and memory, writer kill and restart, stalled reader, conversion cost). They need no transcoder, so they use `tests/framework/mxl.sh` instead of `test.sh`, and run with `tests/run.sh mxl` in `utpl/transcoder-test`. Run the container with `--user $(id -u):$(id -g)` so a failed run does not leave root-owned `tmp/` directories in the tree. `tests/manual/mxl/` has three host-level scripts: `cross-container.sh` (separate containers, different user, read-only mount, undersized tmpfs), `engine-smoke.sh` (engine regression on any image) and `baseline.sh` (CPU, memory, latency and graph links of a UDP-in channel with 1 or 2 outputs; results in `doc/mxl/baseline-2026-10-30/`). The rest of MXL tests T0–T15 are planned, not written. [41](41-mxl-integration.md) maps each one
to where it will live and what it depends on.

## Evidence discipline for tests

Read `test.sh` and framework functions before describing a test as coverage. A case can only establish the exact conditions it asserts. Record input fixture, config, required tools/services, start/stop process topology, timeout, and assertions. A test that only observes process exit is not a media-correctness test.

## Required media assertions

- `ffprobe`: codec, stream count, channels, sample rate, dimensions, framerate, bitrate, duration/timebase.
- TSDuck: TS packet/PID/PMT integrity, SCTE sections, descriptors, continuity, and transport details.
- HLS parser/player checks: master/media playlists, tags, segment availability, duration/window, and playback.
- Image comparison: overlay/squeeze/slate geometry and timing at controlled PTS frames.
- Audio analysis: mute/silence, channel mapping, rate, and A/V synchronization.
- Control plane: REST body/status, gRPC status, SQLite row/read-time changes, and restart behavior.

## Test execution policy

Start with config/parser and unit checks, then one representative scenario per feature family, then the full suite. Run all media tests in a clean environment with versioned tools. Classify failures as product defect, test fixture/expectation defect, timing flake, missing dependency, unavailable external integration, or harness defect; do not blindly update expected output.

The playlist/API smoke procedure, deterministic fixture, exact REST assertions, and output-media checks are recorded in [28](28-playlist-input-api-runtime-verification.md). Reuse that behavior in automated tests: compare effective input versus file-config counts, wait for asynchronous `currentStreamId`, capture output, and decode a frame from each selected source.

## Adding a test

Make fixtures deterministic, use isolated ports/files, enforce cleanup, assert media results, retain debug artifacts on failure, and add rows to [the feature catalog](02-feature-catalog.md) and [implementation index](26-implementation-index.md).
