# Video Processing

## Reachable graph components

The video connector and processor use standard GStreamer elements including `input-selector`, `deinterlace`, `fallbackswitch`, `videotestsrc`, `capsfilter`, `videoconvertscale`, `videorate`, `clocksync`, `tee`, `queue`, `compositor`, and `encodebin`. The exact encoder factory normally comes from stream `h264`/`h265` configuration rather than being hardcoded to one vendor.

## Fixed output canvas and static graphics

The processor registers the requested downstream video parameters before connector preparation.
The complex video connector therefore scales the selected source, dummy source, and fallback source
to the configured compositor canvas. The supplied Compose profile requests 1920×1080 at 25 fps;
decoded runtime output retained that format while switching between 320×180/10 fps inputs.
The active Compose playlist was subsequently upgraded to two native 1920×1080/25 fps H.264
sources; decoded transitions remained 1080p25 and eliminated the obvious proxy-upscaling artifacts.

Static overlay `appsrc` buffers are timestamped on the active compositor running-time timeline and
repeated at the event cadence (40 ms for the supplied 25 fps catalog cues). The image path uses an
`identity` passthrough rather than `videorate`: `videorate` reset the first buffer to PTS zero, which
made a long-running compositor discard a newly scheduled graphic as late. Live decoded captures
verified repeated L-band overlay/squeeze cycles. Video/MOV overlays still require the bounded-decoder
work described in the migration record.

## VLC-compatible Compose preview

The engine currently needs x264 `tune=zerolatency`; disabling sliced threads introduces encoder
buffering that stops host output at the first playlist boundary. VLC 3.0.20 reports
`decode_slice_header` failures and displays vertical fragments for that multi-slice stream. The
Compose relay performs a preview-only decode and single-slice libx264 re-encode with zero
B-frames/lookahead, while copying the MPEG-TS data stream. This workaround costs approximately one
additional CPU core and is not a substitute for fixing buffered-encoder EOS handling in the core.
Its decoder and encoder are capped at four threads; a verified cross-item run used about 219 MiB
for the relay instead of the roughly 1 GiB observed with FFmpeg's automatic thread count.

## Connector responsibilities

`VideoComplexConnector` handles selection of input video, deinterlacing, normalization of dimensions/rate/caps, fallback generation, synchronization, and tee fan-out. It creates converter bins when a processor needs a particular size, framerate, or format.

## Processor responsibilities

`VideoTranscodingProcessor` feeds its flowlines through an identity filter; it never builds a per-output `Compositor`, so compositing happens only in the connector-level compositor. `Compositor` builds the underlying `compositor`, output `videoconvertscale`, and output caps filter; its layers can create crop and conversion sub-bins.

Events such as image overlay, slate, and squeeze alter the compositor graph or its pad properties. Their timing belongs to the scheduler, not the static video processor alone.

`TranscodingFlowline` (`processor/transcoder/flowline.cpp`) puts a probe on each `encodebin` sink pad that drops upstream `GstForceKeyUnit` events. Segmenting sinks (`splitmuxsink` for HLS, `dashsink2` for DASH) request a keyframe at every boundary; the encoder inside `encodebin` receives that request, but `encodebin` also forwarded it through its passthrough branch up the raw chain to the input's parser. The parser then requested a key unit downstream at the next input keyframe, so every encoder emitted a second IDR mid-segment (see 27). Requests for a keyframe must therefore come from downstream of the encoder; nothing upstream of `encodebin` can see one.

Outputs share one encoder (one `TranscodingFlowline`, likewise Dolby and passthrough flowlines) when their `StreamParameters` compare equal. Equality includes the encoder block: element name, every property value and every caps override (`GstEncoderParameters::operator==`, `stream.h`). Until 2026-10-27 it compared only the size, rate and channel parameters and the property *names*, so a ladder whose renditions differed only in encoder settings (for example AAC at 128 and 96 kbit/s) ran one encoder with the first output's settings for all of them (27).

## Configuration and caps

Video stream parameters include width, height, integer rate, format, interlace, field order, codec, and optional encoder factory/properties/caps. `pipeline::Caps` maps UTPL H.264/H.265 parameters to GStreamer caps.

Rate control and B-frames are encoder properties, not engine parameters ([05](05-configuration-reference.md), [configuration-reference.md](../configuration-reference.md)). For `x264enc`, the default `pass=cbr` caps the bitrate but does not hold it: a static picture at `bitrate=4000` encodes at about 30 kbps. `option-string: nal-hrd=cbr` adds filler and holds the video at the target. `tune=zerolatency` disables B-frames unless `bframes` is set explicitly. The TS muxer is never given a bitrate, so the transport stream itself is not constant-rate.

## Interlaced output

The connector deinterlaces every input and the compositor canvas is always progressive. An output with `video.interlace: interleaved` is re-interlaced in its flowline:

1. `VideoTranscodingProcessor::createFlowLine` raises the processor's canvas rate to twice the output `rate`, so a 1080i25 output asks for a 50 fps canvas and each field carries its own moment of motion. Outputs with the same size and rate share a processor whatever their scan type, so this is done per flowline: a progressive output listed before an interlaced one still gets a 50 fps canvas and its own `videorate` brings it back to 25.
2. `TranscodingFlowline` converts to that progressive rate, then an `interlace` element (`field-pattern` 1:1, `top-field-first` from `video.fieldOrder`) weaves each pair of frames into one frame of two fields.
3. `x264enc` switches to field coding from its interlaced input caps; no encoder property is needed.

H.265 is refused at parse time: `x265enc` accepts the woven frames but does not field-code them, and its output reports `field_order=unknown`.

## Runtime-dependent behavior

Hardware acceleration and external encoders are not built into the core selection model. A configured factory succeeds only if its plugin is discoverable and its sink/src caps intersect with the generated graph. Verify with `gst-inspect-1.0`, a current-source run, pipeline DOT graph, and `ffprobe` output. GPU, MainConcept, and Visionular support are integration gated unless the exact factory has run.

## Change checklist

Update parser/config docs, caps mapping, connector conversion rules, compositor/event behavior, test media assertions, and encoder plugin deployment together. Test progressive/interleaved input detection, scaling, frame-rate conversion, fallback, and at least one multi-output graph. For interlace changes, check `field_order` with `ffprobe` and real field motion with ffmpeg's `idet` filter on TFF, BFF, and progressive outputs of the same run.
