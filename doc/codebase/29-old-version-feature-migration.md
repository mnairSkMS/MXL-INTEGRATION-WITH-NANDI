# Old-Version Feature Migration

This page records the migration from the three older transcoder worktrees. They are evidence and
design input, not production source. The current tree owns all implementation decisions.

## Implemented in this revision

- Same-origin operator pages at `/operator` and `/operator/tools/squeeze-box`.
- Catalog-driven graphics cue REST surface and persisted cue/event ownership.
- Overlay z-order, including a programme layer at 100 and foreground graphics at 101+.
- REST/SQLite propagation of squeeze `fade_in` and `fade_out` and overlay `z_order`.
- Managed media catalog, atomic upload/import, queue persistence, queue revisions, duplicate queue
  entries, reorder/delete/skip/start/pause desired-state endpoints.
- The old four-column graphics workflow, API status/clock, upload-and-enqueue, media library,
  now-configured input list, actual Take/Next switching, queue controls, output hint, and SSAI cue
  controls are consolidated in the current same-origin `/operator` page.
- Logo, Bug, six static L-band assets, static Aston, and two shortened QTRLE/ARGB MOV assets were
  migrated with their old 1920×1080 geometry and per-asset squeeze windows. The Compose profile
  mounts them read-only at `/media/graphics`; MOV remains hidden behind its acceptance flag.
- `GET /graphics/cues` restores cue visibility after a page reload and makes Stop/cancel usable;
  the old frontend's Stop button did not have reliable ownership of all paired events.
- Graphic-layer cancellation now erases the released layer from the compositor's layer registry.
  Without that cleanup, later playlist-driven window updates retained memory and attempted
  transitions on an orphan GStreamer pad.

## Deliberately gated

- Animated video graphics remain disabled in the default catalog. The bounded 96 MiB decoder ring
  is implemented and build verified, but alpha, repeated teardown, and RSS acceptance are not complete.
- Empty-queue fallback, Start/Pause and one-shot consumption remain deliberately unsupported. The
  managed queue is a continuous loop and requires at least one item.
- Nested playlist items and browser-side recurring SCTE automation are not supported.

## Rejected old approaches

- The standalone Python SRT feeder hard-coded H.264/AAC and double-encoded media.
- The old monolithic input rewrite fixed media parameters to 1080p25 and did not safely reconcile
  all queue reorders.
- The old frontend proxy and unescaped filename rendering are not used. The former 15-second REST
  lead-time was removed in current source with explicit immediate/current/future API tests instead
  of applying the old Docker-time source patch at container startup.

## Required evidence before enabling the queue runtime

The live snapshot path is enabled. Retain three deterministic local clips (A/V, video-only, and
differing format) to extend the remaining evidence: decoded frames, audio, PTS continuity, restart
recovery, and SRT/UDP output. HTTP 202 alone is still insufficient evidence.

## 2026-08-04 local Compose verification

The three old `channel/flowline.cpp` copies use the same GStreamer `encodebin` profile/property
approach as the current `TranscodingFlowline`; no missing old encoder implementation was copied.
The migrated originals instead exposed a deployment/media mismatch: they changed resolution and
frame rate between items, and one was 3840x2160 AV1. The active M3U8 now uses fixed-format
320x180, 10 fps H.264 proxies for the two suitable clips. The originals and AV1 asset remain in the
media catalog, but the AV1 asset is not in the static on-air list.

`docker-compose.playlist.yml` was built and run from the current checkout. The transcoder sends to
container loopback and an FFmpeg copy relay forwards the MPEG-TS to the Docker host. Host
`ffprobe` observed H.264 at 320x180 and 10 fps, the API exposed one engine input with two
switchable streams, and switching was accepted in both directions. Operator, OpenAPI,
squeeze-tool, graphics, media, and playlist routes returned HTTP 200. Three steady resource
samples were 2.34-3.45% CPU; memory settled at approximately 298-300 MiB after several playlist
loops. API tests passed 14/14 and the
current Release CMake build/CTest command exited successfully.

This verifies the static M3U8 path only. The SQLite managed queue still reported
`appliedRevision: 0`; live reconciliation remains gated as described above.

## 2026-08-04 frontend and asset migration verification

The current implementation refers to the richest old dashboard and playout pages for layout and
behavior, but uses the current FastAPI, scheduler, gRPC input, and media-store contracts. It does
not copy the old port-8090 proxy, Python SRT feeder, unsafe filename HTML, or browser-only SCTE
timer. JavaScript syntax checking passed, and the Python 3.12 API suite passed 15/15 after adding
operator-static, default-catalog, cue-list, and cancellation assertions. Live Compose confirmed
all seven HTTP previews and media paths, cue creation/list/cancellation, and input switching in
both directions.

The Compose output is 320x180, so the catalog scales the old 1920x1080 rectangles to the active
canvas while keeping the source PNGs at their original resolution. A continuous MPEG-TS capture
proved the VoltEdge overlay and squeezed programme in decoded frames; a capture that joined UDP
mid-GOP was discarded as invalid evidence. Cue cancellation removed both events. The same run
identified and fixed stale compositor-layer retention described above.

After rebuilding image `sha256:a1d3049d101f7a549555848f07bce1cdfb2771e297887623aa2183be86f4f35d`,
an Aston cue was allowed to start and was then cancelled. The engine reported zero subsequent
`GStreamer-CRITICAL`/invalid-running-time messages across later playlist transitions, and both the
cue and scheduled-event stores were empty. Three post-test CPU samples were 1.82-2.99%; memory was
approximately 369-371 MiB. The disposable GCC 13.3/GStreamer 1.24.2 Release build and all four
CTest targets passed, as did the 15-test API suite and final H.264 320x180/10 fps UDP probe.
A second full AquaPure cue/cleanup cycle also produced zero critical messages. Container memory
briefly fell to 377 MiB and later samples ranged from 409-411 MiB. CPU was normally 2.05-2.42%
with one 14.66% playlist-transition spike. This short run does not replace the planned
long-duration repeated-cue memory regression gate.

## 2026-08-05 fixed-1080p and packaged graphics acceptance

The old Nandi Graphics/SSAI and Playout designs are served same-origin at `/operator` and
`/operator/playout`. The current configuration fixes the downstream compositor and encoder at
1920×1080/25 fps; two 320×180/10 fps sources were decoded from the UDP output at that target.

Live probes isolated the missing-static-graphic failure to the image stream's internal `videorate`:
the application pushed a first buffer on the current pipeline timeline, but `videorate` emitted it
at PTS zero. Replacing that element with `identity` preserves the scheduler-aligned PTS while the
application continues to generate each repeated frame and duration. Decoded captures then showed
VoltEdge and, in a separate repeated cycle, AquaPure together with the squeezed programme. The
programme returned after the event. The final Compose image
`sha256:cde2508699117149b6aa55c8d74b0149b1d0fd20fd43448f2cc1a9a2cd00d73f` was rebuilt and
deployed; its output again probed as H.264 1920×1080/25 fps. The API suite passed 15/15 and the
disposable Release build's four CTest targets passed. A decoded pixel/timestamp test should still
be automated to replace this manual acceptance evidence. Immediately after the final software-x264
cue run, `docker stats` reported 173.75% CPU (about 1.74 host cores) and 834 MiB resident memory for
the combined transcoder/API/relay container. This is materially above the old 320×180 profile and
must be included in capacity planning; long-duration repeated-cue memory testing remains open.

### High-resolution media replacement

The initial softness came from scaling the diagnostic 320×180 proxy, while the repeating vertical
fragments persisted at native resolution and were separately traced to VLC decoding the engine's
multi-slice H.264. The active M3U8 now uses the old-version Air Asia and One Cent Call H.264/AAC
originals, both 1920×1080/25 fps. A continuous decoded capture covered both directions of the loop
and retained 1080p25 output. The migrated 1080p24 H.264 Coke file is present and registered in
`/media`, but remains outside the authoritative two-item on-air queue. A temporary live third-item
insert, reorder, and remove completed with acknowledged revisions and no construction crash.

### VLC artifact and Playout UI audit

VLC 3.0.20 reproduced vertical corruption and logged `decode_slice_header` failures from a saved
engine TS. The fault follows x264's zero-latency sliced output, not the UDP command or source asset.
Non-sliced experiments removed VLC errors but stopped at the first playlist boundary; OpenH264 did
not negotiate. The deployed Compose relay now emits a single-slice compatibility stream on host
ports 5555/5556 and retains the data PID. Its 24.36-second capture crossed both items at H.264
1920×1080/25 fps with zero FFmpeg or VLC decoder errors. After capping the relay decoder and encoder
to four threads, an 18.8-second cross-item capture again decoded cleanly; the measured container
load was about 264% CPU and 776 MiB, with the relay accounting for about 219 MiB.

## 2026-08-05 managed playlist acceptance

The production path now uses `ApplyPlaylistSnapshot`: the API persists an ordered stable-ID queue,
then reconciles the complete snapshot into the running `PlaylistInput`. In a live 1080p25 run,
inserting Coke created revision 7, reordering it created revision 8, and removing it created
revision 9; every revision reached `appliedRevision` with no `applyError`. The original Air Asia +
One Cent Call queue was restored afterwards. The unified `/operator` page was rendered headlessly
with working insert, move, remove, Skip, graphics, SSAI, activity and applying/error panels.

The current preview stream probes as H.264 1920×1080/25 fps. Joining an MPEG-TS UDP stream mid-GOP
can produce initial PPS warnings before the next keyframe; this is not evidence of a persistent
decode failure. Long-duration PTS/audio and mixed-rate transition tests remain open.

## 2026-08-05 exact old-logic rollout acceptance

The old unified page remains the visual and interaction reference, but every action is adapted to
the current same-origin API. The deployed page has four catalog-backed Logo/L-band/Bug/Aston
columns, compact queue actions, selectable media rows, local/UTC clocks, SSAI state, cue countdowns,
reload recovery, and an Activity panel. A browser probe confirmed that a scheduled L-band disables
both L-band and Aston Start buttons while leaving only the owning Stop enabled. A simultaneous Aston
request returned HTTP 409. Splice Cue Out/Return to Network reused one event ID; Time Signal Cue Out
and Cue In reused one event ID with segmentation types 0x34 and 0x35.

The migrated Logo and Bug files are byte-for-byte identical to the old `run/ui/media` PNGs. A
continuous output capture visibly showed Logo at the old top-left geometry and its removal after
Stop. Animated MOV catalog entries remain disabled because the bounded-decoder 20-cycle, alpha,
teardown, and memory gates have not been completed.

The ready-pending playlist defect found during acceptance was fixed in the current `PlaylistInput`.
A 52-second continuous transport-stream capture visibly decoded Air Asia, Coke, the newly inserted
One Cent item, and the following loop; engine logs recorded each stable-ID commit. The temporary
item was then removed and the original two stable IDs/order were restored at revision 19. A later
Skip changed `currentItemId` and `skipSequence` while leaving revision 19 unchanged, and its decoded
capture changed programme content.

The VLC compatibility relay now preserves audio as well as SCTE/data. A 20.006-second probe found
H.264 1920×1080/25, AAC 48 kHz stereo, and a data stream. All 904 audio packets and 500 video
packets had monotonic PTS, with end timestamps within about 25 ms. The final source image is
`sha256:78359b2f2d0cb9db7eff0105ea217ed7dfeef576e3f1e38162933f0de9e526e3`;
the Compose command/config changes are mounted from this checkout. The image build ran 18 API tests,
the disposable Release build passed, and all four available CTest targets passed. A steady sample
after the 1080p relay was 278.67% CPU and 668.4 MiB for the combined container.
