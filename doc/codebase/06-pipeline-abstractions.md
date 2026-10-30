# Pipeline Abstractions

The UTPL pipeline layer is a narrow C++ wrapper over the GStreamer C API. It is not a declarative pipeline DSL; source modules create elements, configure properties, request pads, and handle callbacks directly.

| Wrapper | Source | Role |
| --- | --- | --- |
| `pipeline::Element` | `pipeline/element.*` | Creates elements, gets/request pads, changes state, and tracks pending state callbacks. |
| `pipeline::Bin` | `pipeline/bin.*` | Owns child elements, adds ghost pads, and exposes traversal/link helpers. |
| `pipeline::Pad` / `GhostPad` | `pipeline/pad.*` | Links pads, handles probes/events, and represents request/ghost pads. |
| `pipeline::Caps` | `pipeline/caps.*` | Maps UTPL media/codec parameters to GStreamer caps. |
| `pipeline::Pipe` | `pipeline/pipe.*` | Owns a pipeline and translates bus state/error/EOS/buffering/tag messages. |
| `pipeline::utils` | `pipeline/utils.*` | Common queues, identity elements, and Dolby compatibility helpers. |

## Rules for changes

- Match GStreamer ownership rules: obtain/unref object references and release request pads deliberately.
- Treat dynamic-pad callbacks as asynchronous; link only after checking the negotiated caps and graph state.
- Do not use `gst_parse_launch` as a substitute for the existing typed construction path unless a new boundary is justified. `Pipe::parse` exists, but production media graphs mostly use `Element::make`.
- Preserve bus error/EOS handling. An unhandled bus error can leave the engine state machine waiting indefinitely.
- Record the factory origin for every new element: standard GStreamer, UTPL plugin, patched upstream, Rust plugin, or vendor SDK.

The most important consumers are inputs, connectors, processors, outputs, and the HLS subsystem. Their exact graphs are documented in pages 07–14.
