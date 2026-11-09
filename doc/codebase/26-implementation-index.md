# Implementation Index

This index maps common concepts to their primary implementation. It is intentionally an index, not a substitute for source review.

## Application and control plane

| Concept | Primary source | Guide |
| --- | --- | --- |
| CLI/license/bootstrap | `transcoder/app/src/main.cpp` | 04, 22 |
| Process exit status | `main.cpp` return; `Application::Pimpl::onTranscoderStopped` in `app/src/application.cpp`; `utpl::Exception::getCode` in `lib/common/include/utpl/common/types/exception.h` | 04 |
| Application lifecycle/config validation | `transcoder/app/src/application.cpp` | 04, 05 |
| JSON parsing | `transcoder/app/src/application/config/parser/json/parser.cpp` | 05 |
| Interlaced output | parser `video.interlace`/`fieldOrder`; `processor/transcoder/video.cpp` canvas rate; `processor/transcoder/flowline.cpp` `interlace` element | 05, 10 |
| Encoder key unit requests stay at the encoder | `processor/transcoder/flowline.cpp` `_dropUpstreamKeyUnitRequest` probe on `encodebin` sink pads | 10, 27 |
| Encoder sharing between outputs | `StreamParameters::operator==` / `GstEncoderParameters::operator==` in `include/utpl/transcoder/stream.h`; `Flowline::compatible`; `utpl_stream_parameters_test` | 10, 27 |
| Worker/gRPC server | `transcoder/app/src/application/transcoder*.cpp` | 04, 17 |
| SQLite events | `transcoder/app/src/application/event/provider/sqlite*` | 15, 17 |
| REST service | `api/src/transcoder_api` | 17 |
| Channel configuration UI | `frontend/src/features/channels` | 17, 05 |
| Engine process control | `api/src/transcoder_api/routers/config.py` | 17 |
| Resource utilization (`GET /stats`) | `api/src/transcoder_api/services/stats.py`, `routers/stats.py` | 17 |
| Engine license management | `api/src/transcoder_api/routers/license.py`, `services/license.py`, `--license-info` in `transcoder/app/src/main.cpp` | 17, 22 |
| Probe safety guard | `frontend/src/features/channels/utils/sourceIdentity.ts` | 17 |
| Channel UI ↔ engine config mapping | `frontend/src/features/channels/mappers` | 05, 17 |
| Channel UI API client | `frontend/src/services/apiClient.ts`, `features/channels/api/channelApi.ts` | 17 |

## Media engine

| Concept | Primary source | Guide |
| --- | --- | --- |
| Engine state machine | `lib/utpl/src/utpl/transcoder/engine.cpp` | 04 |
| Channel graph | `lib/utpl/src/utpl/transcoder/channel.cpp` | 09 |
| Generic pipeline wrappers | `lib/utpl/src/utpl/pipeline` | 06 |
| Inputs | `lib/utpl/src/utpl/transcoder/input` | 07, 08 |
| RTMP input and reconnect | `SingleInput::prepare` RTMP branch in `input/single.cpp`; `Protocol::RTMP` in `lib/common` `types.h`/`types.cpp` | 07 |
| MPEG-DASH input | `SingleInput::prepare` DASH branch, `_linkAdaptiveDemuxer`, `_startAdaptiveStreams`, `_onDashElementAdded`, `_onDashSegmentData`, `_injectDashScte35` in `input/single.cpp`; `input/dash/emsg.{h,cpp}` (test `lib/utpl/test/src/dashEmsg.cpp`); `DashInputTransportParameters` in `input.h`; `transport.dash` in the JSON parser; `Protocol::DASH`; API `InputTransportType.dash` and `services/probe.py`; frontend `channelSchema.ts`, `InputSection.tsx`, mappers | 07 |
| RTMP ingest relay | `docker/utpl-transcoder/mediamtx.yml`, `rtmp-relay-fetcher` stage in `docker/utpl-transcoder/Dockerfile`, `[program:rtmp_relay]` in `supervisord.conf` | 07, 22 |
| Streams/caps | `lib/utpl/src/utpl/transcoder/stream*`, `pipeline/caps.cpp` | 06, 09 |
| Connectors/processors | `channel/connector`, `channel/processor` | 09–12 |
| Compositor | `channel/compositor.cpp` | 10, 15 |
| Outputs/muxers/sinks | `transcoder/output` | 13, 14 |
| Event scheduler | `transcoder/event*` | 15, 16 |
| SCTE event state machine | `transcoder/scte/eventTracker.cpp` | 16, 34 |
| As-run event log (`FIRED`/`ENDED`/`MISSED`, HLS cue placement) | `event/scheduler.cpp` (`logFired`, `logEnded`, `logMissed`), each `event/scheduler/*.cpp`, `hls/playlist.cpp` (`logCue`) | 21 |
| SCTE blackout/blanking | `transcoder/blanker.cpp` | 16 |
| SCTE ad insertion | `transcoder/adBreak.cpp` | 34 |
| Input roles (`primary`/`secondary`) and off-air behaviour | `transcoder/input.h` (`InputRole`, `InputOffAirBehavior`), `config/parser/json/parser.cpp` | 34 |
| Shared SCTE event state machine | `transcoder/scte/eventTracker.cpp` | 16, 34 |
| SCTE section build/insert | `transcoder/event/scheduler/scte.cpp` | 16 |
| SCTE auto-return / cue-in pairing | `services/schedule.py` (`find_open_break`), `event/scheduler/spliceInsert.cpp` | 17 |
| SCTE splice_insert segmentation descriptor | `event/scheduler/spliceInsert.cpp`, `EventSpliceInsert::Segmentation` in `transcoder/event.h` | 17 |
| Slate audio blanking (refcounted) | `transcoder/channel/audioBlankState.h`, `Channel::AudioBlankHold` in `transcoder/channel.cpp`, `event/scheduler/slate.cpp` | 15 |
| Slate catalog and media | `services/slates.py`, `routers/slates.py`, `assets/slates_catalog.json` | 17 |
| SCTE segmentation_type_id (Table 23) | `static/operator.js` (`SEGMENTATION_TYPES` combobox, `SEGMENTATION_TABLE_ROWS` reference panel), `services/schedule.py` (break start/end sets), `hls/types.h` | 38 |
| HLS SCTE ad markers + base64 | `hls/playlist.cpp`, `hls/types.h` | 37 |
| SCTE splice-time resolution | `transcoder/types.h` (`getSpliceTime`), `hls/streamer.cpp` | 37 |
| HLS streamer | `lib/utpl/src/utpl/hls` | 14 |
| CMAF DASH output | `output/sink/dash.{h,cpp}` (`DashOutputSink`, `UtplDashCaptureStream`), `output/dash/presentation.{h,cpp}` (`dash::Presentation`, `mergeManifests`; test `lib/utpl/test/src/dashPresentation.cpp`), `OutputSink::createDash`/`createDashPresentation` in `output/sink.cpp`, `GroupOutput::prepareDash`; `DashOutputTransportParameters` in `output/parameters.h`; `dash` URI scheme in `common/types.cpp`; output `dash` block in the JSON parser; DASH validation in `app/src/application.cpp`; live detection in `channel.cpp`; `transcoder/patches/gst-plugins-rs/`; base image `docker/utpl-base/Dockerfile`; API `/dash` mount in `main.py`; frontend `channelSchema.ts`, `OutputSection.tsx`, mappers | 40 |
| HLS master playlist `RESOLUTION`/`FRAME-RATE` | `transcoder/output/utils.cpp` (`createHlsVariant`), `hls::Variant` in `hls/playlist.h`, `MasterPlaylist::serialize` in `hls/playlist.cpp` | 14 |
| ESAM | `lib/utpl/src/utpl/esam` | 16 |

## GStreamer element index

| Factory family | Origin | Main guide |
| --- | --- | --- |
| `filesrc`, `udpsrc`, `srtsrc`, `souphttpsrc`, `decodebin`, `hlsdemux`, `dashdemux2` | GStreamer | 07 |
| `input-selector`, `fallbackswitch`, `clocksync`, `tee`, `queue` | GStreamer/Rust | 07, 09 |
| `compositor`, `videoconvertscale`, `videorate`, `deinterlace`, `encodebin` | GStreamer | 10 |
| `audioconvert`, `audioresample`, `audiotestsrc` | GStreamer | 11 |
| `mpegtsmux`, `splitmuxsink`, `isofmp4mux`, parsers | GStreamer/Rust/patches | 13, 14, 19 |
| `dashsink2`, `cmafmux` (fmp4 0.14.5) | gst-plugins-rs + patches | 40, 19 |
| `utplaudiomute`, `utplcaptionmute` | In-tree | 18 |
| `dlbpatdec`, `dlbpatmx`, `dlbpatloud`, `dlbpatenc` | In-tree + Dolby SDK | 11, 18 |
| Source-following Dolby mixer (per-block source channel count) | `DolbyComplexConnector::_createDolbyTransformer`, `_followInputLayout`; `common/metadata.c` (`sourceChannels`); `dlbpatmx.c` `_dlbpatmx_submit_input_buffer`; `dlbpatloud.c`, `dlbpatdec.c` stamping | 11, 18 |
| Dolby PCM/encoder timestamps | `common/timeline.c`, `common/queue.c`; `dlbpatenc.c` `_dlbpatenc_pre_push` | 11, 18 |
| Input preview | `include/utpl/transcoder/preview.h`, `src/utpl/transcoder/preview.cpp` (`PreviewTap`, `PreviewHub`); `stream/input/base.cpp` `linkSrcPad`, `_createPreview`; gRPC `setInputPreview` (`transcoder.proto`, `app/.../api/grpc.cpp`); `api/.../services/preview.py`, `routers/input.py` | 07, 17 |
| Media thumbnails | `services/media.py` `thumbnail`, `_thumbnail_root`; `routers/playout.py` `get_media_thumbnail` | 17 |
| Graphics upload frontend guide | `doc/codebase/39-graphics-upload-frontend-guide.md` | 39, 15, 17 |
| Multi-file media upload (`POST /media`) | `routers/playout.py` `upload_media`, `MediaBatchResult`; `services/media.py` `store_upload`, `MAX_UPLOAD_FILES` | 17, 05 |
| Output stream `pid` | JSON parser `_parseStreamPid` / `_checkOutputPids`; `StreamParameters` `_pid` (`stream.h`); copied in `ChannelImpl` output flow analysis (`channel.cpp`); `SingleOutput::prepare` splitters (`output/single.cpp`); `MpegTsMuxer::link` (`sink_<pid>` pads, fixed first); `HlsOutputSink::link` (`muxer-pad-map`) | 05, 13 |
| Codec change on one live PID | `SingleInputStream::replacePads` (`input/single.cpp`); `DolbyComplexConnector::_replaceDecoder`, `_createDecoder` | 07, 11 |
| Playlist legacy commit start after the filler | `PlaylistInput` commit, `PlaylistStream::startNoEarlierThan`, `PLAYLIST_FILLER_MARGIN` (`input/playlist.cpp`) | 08 |
| `audio.inputLayout` (`metadata`/`auto`/`stereo`) and its older spelling `audio.stereoSource` | `AudioInputLayout` / `AudioParameters` in `transcoder/types.h`, JSON parser, `DolbyComplexConnector::_createDolbyTransformer`, `dlbpatmx` `_dlbpatmx_source_config` (`in-channels=source-auto`) | 05, 11, 18 |
| `ndisrc`, `awss3hlssink` | Rust/external availability | 07, 14, 19 |

## Configuration/API index

| Contract | Authoritative implementation | Detailed guide |
| --- | --- | --- |
| CLI arguments and startup validation | `transcoder/app/src/main.cpp`, `application.cpp` | [05 configuration reference](05-configuration-reference.md) |
| JSON syntax, enums, and version handling | `transcoder/app/src/application/config/parser/json/parser.cpp` | [05 configuration reference](05-configuration-reference.md) |
| Parameter defaults | `transcoder/lib/utpl/include/utpl/transcoder/{input.h,types.h,output/parameters.h}` | [05 configuration reference](05-configuration-reference.md) |
| REST route assembly/error handling | `api/src/transcoder_api/main.py`, `routers/` | [17 control-plane reference](17-rest-grpc-and-sqlite-control-plane.md) |
| REST scheduled-event validation/models | `api/api.yaml`, generated `model/rest_api.py`, `services/schedule.py` | [17 control-plane reference](17-rest-grpc-and-sqlite-control-plane.md) |
| REST-to-gRPC input mapping | `api/src/transcoder_api/services/transcoder.py` | [17 control-plane reference](17-rest-grpc-and-sqlite-control-plane.md) |
| Setup-time input probing | `api/api.yaml` `/inputs/probe`, `api/src/transcoder_api/services/probe.py`, `routers/input.py` | [07 inputs](07-inputs-and-ingest.md), [17 control-plane reference](17-rest-grpc-and-sqlite-control-plane.md) |
| Input count, switching, and playlist API guide | REST `/inputs*`, configuration inspection routes, and source-backed limitations | [API README](../api/README.md) |
| Primary/backup switch demo page and channel | `api/src/transcoder_api/static/switch.html`, `main.py` (`/switch`), `services/media.py` (`live_entries`, `reconcile`), `preload_db.py`, `config/srt_playlist_switch{.json,_seed.m3u8}`, `docker-compose.srt-playlist-switch.yml` | [08 playlist design](08-playlists-buffering-and-switching.md) |
| C++ gRPC contract/server | `app/grpc/proto/transcoder.proto`, `app/src/application/transcoder/api/grpc.cpp` | [17 control-plane reference](17-rest-grpc-and-sqlite-control-plane.md) |
| M3U8 tag/attribute tokenizer (shared by the playlist parser and HLS) | `lib/common/src/utpl/common/utils/m3u8.cpp` | [08 playlist design](08-playlists-buffering-and-switching.md) |
| Playlist file parser/runtime switching, live-item rebase | `lib/utpl/src/utpl/playlist/parser.cpp`, `lib/utpl/src/utpl/transcoder/input/playlist.cpp` (`LiveTimeline`, `_rebaseLiveTimeline`, `_commitPending`) | [08 playlist design](08-playlists-buffering-and-switching.md) |
| Make-before-break playlist switch | `lib/utpl/src/utpl/transcoder/input/playlist.cpp` (`_canArmPending`, `_armPending`, `_holdUntilCut`/`ArmGate`, `_cutOver`, `_scheduleCut`, `_abortArm`; `PlaylistStream::armInputStream`, `connectArmedStream`, `alignToArrival`); `include/utpl/transcoder/input/playlist.h` (`Arm`) | [08 make-before-break](08-playlists-buffering-and-switching.md#make-before-break-switching) |
| Live playlist item played at its arrival (mux-delay trim) | `lib/utpl/src/utpl/transcoder/input/playlist.cpp` (`_liveTrim`, `_clockRunningTime`, `_liveArrivalMargin`, `_measureArrival`, `_armTrim`, `ArmGate::itemTrim`, `PlaylistStream::_rebaseLiveTimeline`); env `UTPL_PLAYLIST_LIVE_MARGIN_MS` | [08 arrival](08-playlists-buffering-and-switching.md#playing-a-live-item-at-its-arrival) |
| SRT low-latency input/output queues | `lib/utpl/src/utpl/transcoder/input/single.cpp`, `lib/utpl/src/utpl/transcoder/output/sink/srt.cpp` | [07 inputs](07-inputs-and-ingest.md), [13 outputs](13-outputs-muxing-and-delivery.md) |
| Local high-resolution Compose playlist/assets | `media/queue/playlist.m3u8`, `media/queue/{air-asia-billboard,one-cent-call-1080p}.mp4` | [playlist operator guide](../playlist/README.md) |
| Declarative channels to managed playlists (one file per channel, input order binds them) | `services/bootstrap.py` (`bootstrap_paths`, `channel_files`, `run_all`), `preload_db.py`, `services/media.py` (`managed_playlist_order`, `_live_input`) | [30 rundown service](30-transactional-rundown-service.md), [34 ad insertion](34-scte-ad-insertion.md) |
| Managed persistent playlist | SQLite queue, API reconciler, private `applyPlaylistSnapshot` gRPC and `PlaylistInput` stable IDs | [08 playlist design](08-playlists-buffering-and-switching.md), [REST/gRPC control plane](17-rest-grpc-and-sqlite-control-plane.md) |
| Optional transactional rundown and the operator rundown grid | `api/src/transcoder_api/services/rundown.py`, `services/graphics.py`, `routers/rundown.py`, `static/rundown.js`, additive ORM tables; includes the atomic programs-only reset (`POST /rundowns/{id}/reset`) and one rundown per declared channel | [30 transactional rundown service](30-transactional-rundown-service.md) |
| Declarative channel bootstrap file and publish writeback | `api/src/transcoder_api/services/bootstrap.py` (`lower_to_rundown_v1`, `lift_from_rundown_v1`, `stage_channel_file`, `commit_channel_file`), `preload_db.py`, `services/rundown.publish_bootstrap`, `config/channel.yml` | [30 transactional rundown service](30-transactional-rundown-service.md), [05 configuration reference](05-configuration-reference.md) |
| Rundown endpoint and Postman guide | All `/rundowns/*` REST routes | [Rundown API Guide](../api/RUNDOWN_API_GUIDE.md) |
| Default parameterised compose deployment | `docker-compose.yml`, `.env.example` (`CHANNEL`, `UI_BIND`, `TRANSCODER_IMAGE`, `LICENSE_FILE`, `PLAYLIST_BOOTSTRAP_FILE`, `*_HOST_PORT`) | [03 build and runtime](03-build-development-and-runtime.md) |
| VLC-compatible host preview relay | `docker-compose.ad-insertion.yml` FFmpeg single-slice video re-encode with AAC/data passthrough | [10 video processing](10-video-processing.md), [playlist operator guide](../playlist/README.md) |
| Input/config REST runtime evidence | Current-source image, deterministic M3U8/TS fixtures, decoded UDP captures | [28 runtime verification](28-playlist-input-api-runtime-verification.md) |
| SQLite ORM/provider/DAO | `api/src/transcoder_api/model/db.py`, `app/src/application/event/provider/sqlite*` | [15 events](15-events-overlays-slates-and-squeeze.md), [17 control plane](17-rest-grpc-and-sqlite-control-plane.md) |
| Operator dashboard and static previews | `api/src/transcoder_api/static/operator.{html,css,js}`, `static/graphics/` | [17 control plane](17-rest-grpc-and-sqlite-control-plane.md) |
| Graphics catalog, cue ownership, managed media queue | `api/src/transcoder_api/assets/graphics_catalog.json`, `routers/{graphics,playout}.py`, `services/{graphics,media}.py` | [15 events/overlays](15-events-overlays-slates-and-squeeze.md), [08 playlist design](08-playlists-buffering-and-switching.md) |
| Event time validation and immediate graphics cues | `api/src/transcoder_api/services/schedule.py`, `services/graphics.py`, `routers/graphics.py` | [17 REST/control plane](17-rest-grpc-and-sqlite-control-plane.md) |
| Static-image cadence and timeline alignment | `lib/utpl/src/utpl/transcoder/stream/input/image.cpp`, `event/scheduler/overlay.cpp`, `channel/compositor.cpp` | [10 video processing](10-video-processing.md), [15 events/overlays](15-events-overlays-slates-and-squeeze.md) |
| Bounded animated graphic ring and runtime gate | `lib/utpl/src/utpl/transcoder/stream/input/image.cpp`, `services/graphics.py`, `docker-compose.playlist.yml` | [15 events/overlays](15-events-overlays-slates-and-squeeze.md) |
| Capability/status matrix | Production CMake graph and owning sources above | [02 feature catalog](02-feature-catalog.md) |
| MXL build and SDK tests (implemented) | `docker/utpl-base/Dockerfile` (`mxl-builder`), `docker-compose.yml` (`mxl-domain`), `tests/framework/mxl.sh`, `tests/tests/mxl/sdk/`, `tests/manual/mxl/` | [41 MXL integration](41-mxl-integration.md), [19](19-gstreamer-patches-and-external-plugins.md) |
| MXL source tap (implemented) | `lib/utpl/include/utpl/transcoder/mxltap.h`, `mxlframegrid.h`, `mxlparameters.h`; `src/utpl/transcoder/mxltap.cpp`, `mxlframegrid.cpp`; `BaseInputStream::_createMxlTap` and `linkSrcPad` in `src/utpl/transcoder/stream/input/base.cpp`; hooks in `input/single.cpp`, `input/standard.cpp` (tap after the redundancy switch) and `input/playlist.cpp` (tap kept through item changes); parser block `mxlTap` and helper `_parseMxlFlows` in `transcoder/app/src/application/config/parser/json/parser.cpp`; unit tests `lib/utpl/test/src/mxlTapParameters.cpp` and `mxlFrameGrid.cpp`; tests `tests/tests/mxl/tap/`; UI keep-through test in `frontend/tests/configMapper.test.ts`; API test in `api/tests/config_test.py` | [41](41-mxl-integration.md#source-tap-step-2), [05](05-configuration-reference.md#input-mxl-tap), [07](07-inputs-and-ingest.md) |
| MXL programme output (implemented) | `lib/utpl/include/utpl/transcoder/output/mxl.h`, `src/utpl/transcoder/output/mxl.cpp` (`MxlOutput`); `src/utpl/transcoder/channel/processor/transcoder/mxlflowline.{h,cpp}` (`MxlFlowline`) and the `Codec::MXL` branch in `video.cpp` and `audio.cpp` `createFlowLine`; `OutputType::MXL` in `output/parameters.h`; `Codec::MXL` in `lib/common`; engine hook in `engine.cpp`, output validation in `transcoder/app/src/application.cpp`; parser `type: "mxl"` branch and `_parseMxlVideo` in `parser.cpp`; schema `MxlOutputParameters` in `transcoder/config/doc/openapi.yaml`; UI carry-over in `frontend/src/features/channels/mappers/`; tests `tests/tests/mxl/programme/` | [41](41-mxl-integration.md#programme-tap-step-3), [05](05-configuration-reference.md#output-mxl), [13](13-outputs-muxing-and-delivery.md) |
| MXL input (implemented) | `Protocol::MXL` in `lib/common/include/utpl/common/types.h` and `types.cpp`; `MxlInputTransportParameters` in `lib/utpl/include/utpl/transcoder/input.h`; the `mxl` scheme branch, `SingleInput::_createMxlFlows`, `MxlWatch` (watchdog and time shift, learning and relearn) and the MXL cases of the buffer, back-off and buffered-state code in `src/utpl/transcoder/input/single.cpp` and `include/utpl/transcoder/input/single.h`; `async-handling` case in `input/standard.cpp`; parser `_parseMxlInput`, `_mxlDomainOfUri` and the `transport.mxl` block in `transcoder/app/src/application/config/parser/json/parser.cpp`; schema `MxlInputTransportParameters` in `transcoder/config/doc/openapi.yaml`; API `InputTransportType.mxl` (`api/api.yaml`, `model/rest_api.py`, `services/probe.py`); channel form: `frontend/src/features/channels/` (`types/`, `schemas/channelSchema.ts`, `mappers/`, `components/InputSection.tsx`, `pages/CreateChannelPage.tsx`); unit tests in `lib/utpl/test/src/mxlTapParameters.cpp`; tests `tests/tests/mxl/input/`, soak `tests/manual/mxl/input-soak.sh`, delay measurement `tests/manual/mxl/input-jitter.sh`; the plugin patch `transcoder/patches/mxl/010-mxlsrc-release-locks-before-set-caps.patch`, applied in `docker/utpl-base/Dockerfile` (stage `mxl-builder`); the gst-libav patch `transcoder/patches/gst-libav1.0/010-avviddec-free-the-av-packet.patch` (H.264 decoder memory leak), rebuilt in `docker/utpl-base/Dockerfile` (stage `base-builder`); memory measurement `tests/manual/mxl/writer-memory.sh`; relearn test `tests/tests/mxl/input/12-relearn/` | [41 MXL input](41-mxl-integration.md#mxl-input-step-4), [05](05-configuration-reference.md#input-mxl), [07](07-inputs-and-ingest.md) |
| MXL integration points in the engine for the rest (planned, nothing implemented) | Event path for SCTE-35 next to the flows; external encoder worker reading `mxlsrc` (no engine change planned); `fallbackswitch` timeouts; `shm_size` in `docker-compose.yml` | [41 MXL integration](41-mxl-integration.md) | [41 MXL integration](41-mxl-integration.md) |

The parser, application validation, route/service code, and reachable media graph—not this index or either OpenAPI file—decide effective behavior.
