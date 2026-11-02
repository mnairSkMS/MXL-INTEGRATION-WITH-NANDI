# Outputs, Muxing, and Delivery

## Output model

`Engine` creates all `SingleOutput` objects first and then resolves `GroupOutput` children by output name. A group is a logical collection of single outputs, commonly for multi-rendition HLS. Link stream definitions can refer to another named output with an optional name modifier.

## Single output

`SingleOutput` owns a bin, output pad streams, optional synchronization, and a muxer. Source construction supports MPEG-TS through `MpegTsMuxer` and elementary streams without a muxer. A stream tee can feed multiple transports where supported. The application rejects multiple streams in ES and rejects ungrouped single outputs without transport.

`MpegTsMuxer` wraps `mpegtsmux`, creates a tee, and exposes output pads. The project patches GStreamer mux behavior for DVB AC-3/E-AC-3 and SCTE-35 use cases; see 19.

### Stream PIDs

`mpegtsmux` takes an elementary stream's PID from its request-pad name: `sink_256` puts the stream on PID 256, and `sink_%d` lets the muxer number it from 0x41 upward. An output stream's optional `pid` (see 05) travels in `StreamParameters` through the channel's output flow analysis and `SingleOutput::prepare` to the muxer pad request.

- `MpegTsMuxer::link` requests all configured-PID pads before any automatic ones. The muxer's automatic numbering skips PIDs already in use, but a fixed PID requested after an automatic pad that already took it fails.
- `HlsOutputSink` cannot name the muxer pads itself, because `splitmuxsink` requests them. It names its own `video`/`audio_N`/`caption_N` pads and passes `muxer-pad-map` (`audio_0=sink_1001`, …), again fixed PIDs first.
- A pad request that fails throws `InvalidArgumentException` instead of linking a null pad.
- `fmp4`, `es` and the S3 sink ignore the field.

Runtime check (scratch build, 2026-10-24): file, SRT and HLS outputs carried video 0x3E8, audio 0x3E9 and SCTE 0x1F4 as configured. With only audio fixed at 0x41, video moved to 0x42. With no `pid`, numbering was unchanged (0x41/0x42).

## MXL output

An output of `"type": "mxl"` is created as `MxlOutput` (`output/mxl.cpp`) next to the `SingleOutput`s. It has no muxer or sink: each stream's pad ends in an `MxlTap` that converts the raw media in a side pipeline and writes an MXL flow, and a flowline that does not encode (`MxlFlowline`, see [09](09-channel-routing-connectors-and-flowlines.md)) delivers the processor's picture and audio to it. It is the programme tap of [41](41-mxl-integration.md#programme-tap-step-3); settings are in [05](05-configuration-reference.md#output-mxl). It is not part of a group.

## Sink selection

`OutputSink::create` dispatches by URI protocol:

| Protocol | Sink implementation |
| --- | --- |
| file or no protocol | `FileOutputSink` / `filesink` |
| UDP | `UdpOutputSink` / `udpsink` |
| SRT | `SrtOutputSink` / `srtsink` |
| HLS or HTTP | `HlsOutputSink` |
| DASH (`dash+file`, `dash+http(s)`) | `DashOutputSink` / `dashsink2` (see 40) |
| S3 | `S3OutputSink` |

SRT mode, UDP multicast/unicast settings, live synchronization, and file behavior belong to their transport parameter classes and sink implementations. Do not assume an input transport option applies unchanged to output.

The SRT sink keeps a downstream-leaky queue so a slow or absent peer cannot stall the whole channel,
but its backlog is capped at 250 ms. `chopmydata` still emits 1316-byte MPEG-TS/SRT payloads, and
`srtsink` remains clock-synchronized. SRT's configured peer latency remains the network-loss recovery
budget; the leaky queue is only a short graph-isolation window and drops old media when it fills.
Listener outputs set `wait-for-connection=false`, so an absent receiver cannot pin one old buffer
outside that queue; caller outputs retain their existing connection behavior.

## Containers

The parser accepts `mpegts`, `es`, and `fmp4`; output runtime support depends on sink/container pairing. HLS source branches show MPEG-TS and ISO fMP4 muxing paths. Validate actual media output and manifests before claiming fMP4 support in a deployment. A DASH output requires `fmp4` and produces CMAF (one track per segment stream); see [40](40-cmaf-dash-output.md).

## Verification

For each output, inspect streams, PIDs, bitrate, timing, connection mode, and packet loss/reconnect behavior. For live paths, test a controlled source interruption and shutdown. For TS, use TSDuck in addition to `ffprobe`.
