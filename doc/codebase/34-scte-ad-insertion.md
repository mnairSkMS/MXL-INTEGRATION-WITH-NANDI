# SCTE-35 Ad Insertion

Replaces the programme with a second input for the duration of a signalled avail, and puts the
programme back when the break closes. The replacement source is normally a `PlaylistInput` holding
commercials.

- `AdBreakSwitcher` (`transcoder/adBreak.cpp`) decides when a break is on air.
- `ScteEventTracker` (`transcoder/scte/eventTracker.cpp`) turns cues into a resolved on/off state.
- `Channel::setCurrentInput` performs the switch, exactly as the slate event already did.
- `PlaylistInput::setPlaybackActive` freezes and resumes the replacement source.

## Configuration

Requires configuration version `1.5.0` or newer, which is also the first version that parses more
than one entry in `transcoder.inputs`. See `transcoder/config/example/scte_ad_insertion.json`.

```json
"inputs": [
    { "role": "primary", "transport": { "uri": "srt://..." } },
    { "role": "secondary", "offAir": "pause",
      "transport": { "uri": "pls+file:///opt/utpl/etc/commercials.m3u8" } }
],

"adInsertion": {
    "enable": true,
    "maxBreakDuration": 300000,
    "outputScte": "passthrough",
    "trigger": { "restriction": { "ignore": "NONE" }, "conditions": { ... } }
}
```

### `transcoder.inputs[]`

| field | values | default | meaning |
| --- | --- | --- | --- |
| `role` | `primary`, `secondary` | `primary` | `primary` is the programme; `secondary` is a replacement source that something else may put on air - today the ad switcher. Case-insensitive. |
| `offAir` | `drop`, `pause` | derived from `role`: `drop` for `primary`, `pause` for `secondary` | What the input does while it is not on air. |

Rules the parser enforces, each throwing `InvalidArgumentException`:

- `inputs[0]` must have `role: primary` - "transcoder.inputs[0] is the programme source and must
  have role 'primary'". It defines the output topology, and every alternate source is adapted to it
  by the connectors.
- An unknown `role` or `offAir` value is rejected rather than ignored.
- More than one entry in `inputs` at all requires version `1.5.0`; earlier versions parsed only
  `inputs[0]` and silently dropped the rest.

`offAir` in detail: `drop` keeps the source running and lets the selector discard its buffers, which
is the only correct choice for a live source because it is what makes the return land at the live
edge. `pause` is meant to freeze and retain position, for a source that should continue where it was
cut - **see the open defect below; it does not currently hold position.**

### `transcoder.adInsertion`

| field | type | default | meaning |
| --- | --- | --- | --- |
| `enable` | bool | `false` | Note the spelling: `enable`, not `enabled`. Absent or `false` leaves the channel behaving exactly as without ad insertion. |
| `maxBreakDuration` | ms | `600000` (10 min) | Backstop only, for a cue-in that never arrives. It does not set break length, and a late cue-in still overrides it. |
| `outputScte` | `passthrough`, `suppress` | `passthrough` | `passthrough` leaves the source's cues reaching outputs untouched, exactly as without ad insertion. `suppress` gates them for the duration of a break. Any other value is rejected. |
| `trigger` | blackout rule set | none | Which cues open and close a break. Has no `enable` of its own - it is enabled by being present. |

The whole block requires version `1.5.0`: "transcoder.adInsertion requires configuration version
1.5.0 or newer".

`trigger` is parsed by the same `_parseBlackout` used for `blanking`/`blackout`, so segmentation type
pairs, priorities and delivery restrictions are written identically - see 16. Two things worth
knowing:

- `matchWhenNoSegmentationDescriptors` is the neutrally named alias of
  `blankWhenNoSegmentationDescriptors`. A bare `splice_insert` carrying no descriptors is a trigger
  only when it is set, which is what the demo and the shell test rely on.
- Configuring `blanking`/`blackout` and `adInsertion` for the same segmentation types blanks the
  channel while the ad is on air. The engine warns at startup rather than refusing.

### Not yet reflected elsewhere

`transcoder/config/doc/openapi.yaml` and `api/api.yaml` still describe a single input. No new
endpoints are needed - `GET /inputs` already enumerates both - but the second input's meaning is
undocumented there.

## Break lifecycle

1. A cue-out - `splice_insert` with `out_of_network`, or a `time_signal` carrying a configured
   segmentation start type - creates an event. The switch is scheduled for the splice point, not
   for the moment the cue arrived.
2. At the splice point the ad source is resumed and `setCurrentInput` puts it on air.
3. A cue-in closes the break and the programme returns; the ad source is frozen again.
4. **Break length comes from the cues alone.** A signalled `break_duration` is not used to close a
   break (`ScteEventTracker::setIgnoreSignalledDuration`); `maxBreakDuration` is the only backstop
   against a lost cue-in, and a late cue-in still overrides it.

Because the switcher subscribes where `Blanker` does - after conditioning in
`Channel::injectScte35Event` - it inherits POIS semantics: a cue the POIS answers `DELETE` never
reaches it, and `REPLACE` makes it act on the conditioned section.

## Output markers

`outputScte` defaults to `passthrough`, which leaves `sendScteToOutputs` behaving exactly as it
does without ad insertion: the source's cues still reach the SCTE PID and HLS manifests while the
replacement is on air, so a downstream splicer may replace the content again. `suppress` gates
`putScteData` for the duration of the break instead.

## Constraints

- **Transcoded tracks only.** Switching happens inside the transcoding connectors, so a channel
  that passes video through has no selector to switch. The switcher logs once and stays inert,
  the same guard the slate event applies.
- **One live input.** The connectors insert a `clocksync` for every non-live branch feeding a live
  output; two independently live sources cannot share one output timeline. `Channel::prepare`
  asserts this.
- **Alternate captions are dropped.** Only the programme input contributes caption streams to the
  output topology, and the programme's caption processor keeps forwarding during a break.
- **Overlapping rule sets.** Configuring `blanking`/`blackout` and `adInsertion` for the same
  segmentation types blanks the channel while the ad is on air. The engine warns at startup.

## Black video across an item rollover - open defect

Observed on 2026-08-17 with `tests/manual/ad-insertion`, which replaces the shell test's synthetic
solid-colour spots with real 1080p H.264 commercials and runs six breaks over five minutes.

With an ad playlist of four 14 s spots, breaks that overlap a playlist item rollover render black
video for the whole overlap. The rollovers are the anomaly: after the first break, `Committed
playlist item ...` lines arrive 27-42 s apart for items that are 14 s long, so the playlist is dry
for most of that window and anything switching to it sees nothing.

With a single reel long enough that the playlist never reaches the end of an item, **all six breaks
render**, in both `drop` and `pause` off-air modes. That is what the demo ships with, and it is the
sharpest available statement of the defect: it is not the switch, and not the off-air mode.

Ruled out: GOP length (a 10 s GOP and a 1 s GOP behave identically) and the media (every spot decodes
correctly on its own). The pattern reproduces across runs.

One earlier 3-break run in `pause` mode is not fully explained by the dry-playlist account: two
breaks were black while output audio was still the spots' own (-9.8 dB, against the programme's
-24.1 dB tone), so in that run the video path failed while the audio path did not. Whether that is
the same defect or a second one is unresolved.

None of this appears in `tests/tests/ad_insertion`, whose spots are short solid-colour clips that
never roll over mid-break - worth knowing about the coverage that test actually provides.

**It is not an ad-insertion defect.** Running the operator's own rundown as the *primary* input on
2026-08-18 (`PRIMARY=playlist`, three clips of 10-16 s from `config/channel.yml`) showed the same
black across every item boundary, on air, with no break in progress - 3-6 s at 11 s, 49 s, 62 s and
repeatedly from 99 s in a 117 s recording, while both breaks in that run switched and returned
correctly. The failing path is `PlaylistInput`'s item transition itself. That configuration is also
the cheapest reproduction: about 30 seconds, and no SCTE involved.

## Resume accuracy - the freeze does not freeze

**An interrupted spot does not resume where it left off, because the ad source keeps playing while
it is frozen.** Measured on 2026-08-17 with a single 330 s reel and six breaks:

| | reel position |
| --- | --- |
| frozen at the end of break 1 | 34.48 s |
| resumed at the start of break 2, 30 s later | 65.28 s |

The same holds for every subsequent break: +30.84 s over a 30 s freeze, +28.72 over 28 s, +28.80
over 28 s, +27.68 over 27 s. Position advances essentially 1:1 with wall-clock time **while the
input is frozen**, and `setPlaybackActive(false)` has already reported `bin PAUSED, item PAUSED`.

This is not a reporting artifact. The reel carries a burnt-in position counter, so what aired can be
read off the picture: break 1's last frame reads `00:00:33.760` and break 2's first frame reads
`00:01:06.619`. 32.9 s of commercial was consumed, unseen, during a 30 s freeze.

That settles the two hypotheses previously recorded here: it is the branch continuing to run, not
queue depth ahead of the encoder - no queue holds 30 s. The bin and the current item both report
`PAUSED`, so the next step is to find what still pulls data on that branch despite the reported
state, rather than to add a compensating seek.

Separately, the source also over-consumes while on air: 0.8 s -> 34.48 s of position across a 15 s
break. That surplus is a second, smaller effect and is not yet accounted for.

## Threading

`setCurrentInput` runs on the channel's timed-callback thread while a playlist rolling to its next
item runs on its own streaming thread. Both reach the connectors, so
`Video`/`Audio`/`DolbyComplexConnector` guard stream membership and the active-input switch with a
recursive mutex. Without it the two interleave inside `_setActiveInput` and corrupt the dummy
source chain; the observed failure was an exception from `videotestsrc -> capsfilter` linking,
which terminated the process.

The connectors' `input-selector` elements are created with `sync-streams=false`. With the default,
an inactive pad parks its streaming thread inside `gst_input_selector_wait_running_time` until the
active pad advances; when the active pad is a frozen source that never happens, and the next state
change on the parked branch waits on that thread forever. The observed failure was
`_setActiveInput` blocking permanently in `gst_element_set_state(videotestsrc, NULL)`.

## Verification

`tests/tests/ad_insertion/splice_insert/mpegts` drives two breaks from a TSDuck-injected cue
sequence and identifies the on-air source by frame colour (grey test pattern for the programme,
saturated colour for a spot), then checks that the source markers are still passed through.

Runtime-verified on 2026-08-11: both breaks switched and returned, each 6.0 s as signalled, the
programme restored within ~460 ms of the cue-in, and no crash or stall across the run.

Regression status with all of this in place (2026-08-12): CTest 4/4, `blackout` 2/2, `scte` 9/9,
`ad_insertion` 1/1. The `proto` suite reports 41/4, which is **identical to the untouched
baseline** - the four failures are pre-existing HLS-output analysis failures in the offline
harness, not caused by the connector locking or `sync-streams=false`.

## Design decisions

Taken with the requester when the feature was specified, and load-bearing for how the code behaves:

- **A break ends on SCTE cues alone.** A signalled `break_duration` never closes a break;
  `maxBreakDuration` is only a stuck-channel backstop, and a late cue-in still overrides it. This is
  why `ScteEventTracker::setIgnoreSignalledDuration(true)` is set by `AdBreakSwitcher::configure`,
  and why an operator-driven cue-out requires a matching cue-in.
- **Ad underrun loops**, reusing the existing `PlaylistInput` wrap from last item to first.
- **Output markers stay as they were by default.** `outputScte` defaults to `passthrough`, so every
  pre-existing SCTE, POIS and blanking test keeps its original path.
- **`Blanker` was refactored onto a shared tracker** rather than copied, so blackout and ad
  insertion cannot drift apart.

## Regression evidence

Recorded 2026-08-12, with the full feature in place, against a RelWithDebInfo build:

| suite | result |
| --- | --- |
| CTest | 4/4 |
| `blackout` | 2/2 |
| `scte` | 9/9 |
| `ad_insertion` | 1/1 |
| `proto` | 41 passed / 4 failed |

The four `proto` failures were A/B'd against a build of the untouched pre-feature snapshot and are
**identical on both**: `standard/hls/hls`, `standard/srt-caller/hls`, `standard/srt-listener/hls`,
`standard/udp/hls`. All four are HLS *outputs* failing at the same point - `analyze-out` runs
`ffprobe` on the output `master.m3u8` and it exits non-zero. Pre-existing harness/HLS-analysis
behaviour in the offline environment, not a regression from this work.

## Two defects fixed while building this

Both were pre-existing fragilities in shared code that only a second input exposes:

- **Connector data race.** `setCurrentInput` (channel timed-callback thread) and a playlist rolling
  to its next item (its own streaming thread) both entered `_setActiveInput`, which rebuilds the
  shared dummy-source chain. They interleaved and the process died with
  `Error linking elements 'videotestsrc4' -> 'capsfilter26'`. Fixed with a recursive mutex in the
  three complex connectors.
- **Deadlock on switch-back.** With the default `sync-streams=true`, an inactive `input-selector`
  pad parks its streaming thread in `gst_input_selector_wait_running_time` until the active pad
  advances. When the active pad is a frozen source that never happens, so `_setActiveInput` blocked
  forever in `gst_element_set_state(videotestsrc, NULL)` - confirmed by gdb backtrace. Fixed with
  `sync-streams=false` on the selectors.

Also fixed during the same work: `Channel::addInput` was called from each input's probe-completion
callback, so a fast local playlist registered before a slow SRT caller and **became the programme**.
Inputs are now added in configuration order once every probe has finished.

## Driving breaks without cues in the stream

A file playlist carries no SCTE-35, so a channel whose programme is a local rundown has nothing to
trigger on. Scheduled events fill that gap and reach the switcher through the same path as embedded
cues: `ScheduledSpliceInsertEvent`/`ScheduledReturnToNetworkEvent` build a real splice section and
call `Channel::injectScte35Event`, behind POIS conditioning.

Rows go in the `events` table of the database passed with `-b`, which is the same file the REST API
writes (`DB_PATH`). The operator UI's SSAI panel already emits exactly the right pair:

| button | splice mode | time-signal mode |
| --- | --- | --- |
| Cue Out | `scte35_spliceinsert` + `splice_event_id` | `scte35_timesignal`, `segmentation_type_id` 52 (0x34) |
| Cue In | `scte35_returntonetwork`, same `splice_event_id` | `scte35_timesignal`, `segmentation_type_id` 53 (0x35) |

Verified 2026-08-17 by inserting both rows while a channel was running: the scheduler picked the
cue up 122 ms after the insert, the switch happened 4.0 s later (that gap is `scte.prerollTime`,
which the section's splice time is built from), and the cue-in returned the programme. Both UI modes
work against the shipped trigger configuration, because 0x34/0x35 are listed in it and
`matchWhenNoSegmentationDescriptors` covers the bare splice-insert path.

Nothing is scheduled automatically. A channel started this way waits, indefinitely, until something
sends a cue.

## One runtime, several playlists

Ad insertion is not a separate runtime: it is the same image and services as every other channel,
and configuration decides how much of it is used. `docker-compose.ad-insertion.yml` runs this
channel; `docker-compose.playlist.yml` is the managed-playlist operator deployment.

A channel is one managed playlist declared by one YAML file, and a runtime holds as many as its
transcoder configuration has inputs. `PLAYLIST_BOOTSTRAP_FILES` lists the files **in the order the
transcoder configuration lists its inputs**: the first is the programme on the primary input, the
second is the break reel on the secondary. That ordering is the whole binding, and it is what
`media.managed_playlist_order()` resolves an operator request through.

```
config/channel.yml  --+                              +-- /runtime/main.m3u8 --> input 0  primary
                      |  preload_db -> bootstrap ->  |
config/ads.yml      --+   (one managed playlist      +-- /runtime/ads.m3u8  --> input 1  secondary
                           per declared channel)
```

Each file is lowered into the canonical rundown document and validated identically, so the break
reel inherits catalog validation, stable item identity and the publish/writeback path for free. A
publish writes back to the file that declares that channel, never to whichever file is listed first.

To turn the feature off, remove `ads.yml` from `PLAYLIST_BOOTSTRAP_FILES` and set
`adInsertion.enable` to `false`. The runtime, the image and the startup path are unchanged; the
channel is an ordinary single-playlist channel again.

| Concern | Where it is decided |
| --- | --- |
| Which media is the programme | `config/channel.yml` |
| Which media is the break reel | `config/ads.yml` |
| Which playlists exist | `PLAYLIST_BOOTSTRAP_FILES` |
| Which input each playlist drives | the order of that list, against `transcoder.inputs` |
| Whether breaks happen at all | `transcoder.adInsertion.enable` |
| What ends a break | a cue-in, or `maxBreakDuration` |

A broken channel file does not take the runtime down: that channel keeps whatever queue it was last
given, every other channel applies normally, and the failure is recorded against the file that
caused it rather than against the default channel. The same rule means a channel *removed* from the
list keeps its rows and keeps having its M3U8 refreshed - it simply stops being applied, and nothing
reads the file once its input is gone. Drop the rows to be rid of it entirely.

Verified 2026-08-18, both directions:

| configuration | result |
| --- | --- |
| two files, `adInsertion.enable: true`, two inputs | two managed playlists, stable item IDs on both, `POST /scheduled_events` cue pair switched to the reel and returned |
| one file, `adInsertion.enable: false`, one input | one input, no switcher, no other change - same image, same startup path |

`tests/manual/ad-insertion/` holds no runtime any more - only `prepare.sh`, which builds synthetic
labelled media and a cue-carrying transport stream, and `verify.sh`, which reports which source was
on air from a recording.

## Change set

Files this feature added or changed, for anyone diffing it against the pre-feature snapshot kept at
`../skandha-transcoder-dev.baseline-2026-08-11` (read-only; the checkout has no Git metadata).

New: `transcoder/scte/eventTracker.{h,cpp}`, `transcoder/adBreak.{h,cpp}`,
`transcoder/config/example/scte_ad_insertion.json`,
`tests/tests/ad_insertion/splice_insert/mpegts/`, `tests/manual/ad-insertion/`, this page.

Changed: `input.h` (`InputRole`, `InputOffAirBehavior`, `setPlaybackActive`), `types.h`
(`AdInsertionParameters`), `engine.{h,cpp}` (multi-input parameters, ordered `addInput` after all
probes, switcher binding, off-air freeze), `channel.{h,cpp}` (honour `setActive`, attach alternate
inputs, additive `onScteData`, SCTE suppression predicate, current-input guard), `input/playlist.*`
(`setPlaybackActive`, position-watch suspend/resume, `PlaylistStream::synchronizeClock`, freeze and
resume logging), `blanker.*` (now a thin `ScteEventTracker` subclass, public API unchanged),
`connector/complex/{video,audio,dolby}.*` (recursive mutex, `sync-streams=false`), `parser.cpp`
(whole `inputs` array, `role`/`offAir`, `adInsertion`, `VERSION_MAX` 1.4.2 to 1.5.0,
`matchWhenNoSegmentationDescriptors` alias).
