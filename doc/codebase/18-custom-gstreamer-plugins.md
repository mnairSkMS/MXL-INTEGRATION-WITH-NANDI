# Custom GStreamer Plugins

The current Release build compiles these in-tree plugin targets. Compilation establishes build wiring, not runtime registry installation.

| Element | Target | Purpose | Integration sites |
| --- | --- | --- | --- |
| `utplaudiomute` | `utplaudiomute` | Audio muting with pending/current mute state. | Audio and Dolby connectors/processors. |
| `utplcaptionmute` | `utplcaptionmute` | Caption muting with pending/current mute state. | Caption passthrough processor. |
| `dlbpatdec` | `gstdlbpatdec` | Dolby PAT decode to PCM. Tags blocks with the source channel count from its sink caps, drops decoder priming, emits one buffer per input frame. | Dolby connector. |
| `dlbpatmx` | `gstdlbpatmx` | Dolby PAT mix/upmix/downmix processing. `in-channels=source` (transcoder-only value, never passed to PADMX) reconfigures per block from the source channel count in `DlbPatMetadata`, draining before a reopen; an explicit `in-channels=2` uses only front left/right (used by `audio.inputLayout: stereo`). `in-channels=source-auto` (transcoder-only, used by `audio.inputLayout: auto`) is `source` except that a 2.0/5.1/5.1.4 source for a 5.1/5.1.4 target runs PADMX in upmix mode with `input_speaker_cfg=AUTO` and without input metadata, so the library detects stereo carried in a wider header from the audio. Output is stamped from sample counts (`common/timeline.c`). | Dolby connector output side. |
| `dlbpatloud` | `gstdlbpatloud` | Dolby loudness processing. `source-channels` (-1 = from input caps or input blocks, 0 = unknown) sets the source channel count stamped on its output blocks; drains on a caps change. | Dolby path. |
| `dlbpatenc` | `gstdlbpatenc` | Dolby PAT encoding. `pre_push` re-stamps each frame from consumed minus emitted samples (PAEC holds input, so the base class stamped frames late). | Dolby transcoding processor. |

## Mute elements

The mute elements are C GStreamer elements with registered pad templates, property handling, and a pending-to-current state update mechanism. Their source uses a property named `mute`; verify caps, event behavior, and exact property timing with `gst-inspect-1.0` and a buffer-level test before changing muting semantics.

## Dolby PAT elements

The Dolby elements use the checked-in SDK headers/static archives under `plugins/gstreamer/dolbypat/ext`. They are not generic upstream GStreamer elements. Their code exposes error paths for unsupported codecs, data rates, channel counts, layouts, and metadata configurations.

The individual plugin documents should always cover:

1. Base class, pad templates, accepted caps, and produced caps.
2. Construct-only and runtime properties, defaults, and validation.
3. SDK context allocation/teardown and metadata ownership.
4. Frame conversion/latency behavior and EOS/flush state.
5. Call sites in connector/processor graph construction.
6. A versioned `gst-inspect-1.0` capture from the built deployment.

## Registry verification

After install, set `GST_PLUGIN_PATH` if the install location is not in GStreamer's default scan path, clear only a disposable registry cache when needed, and run `gst-inspect-1.0 <element>`. A prebuilt image where a plugin is absent may have packaging/path drift; it does not negate the compiled source target.
