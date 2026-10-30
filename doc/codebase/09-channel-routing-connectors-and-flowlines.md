# Channel Routing, Connectors, and Flowlines

`Channel` is the graph-building boundary between discovered input streams and requested output streams. It decides whether a stream can remain passthrough or requires a complex connector and processor.

## Selection model

`Processor::create` selects a passthrough processor, video transcoding processor, Dolby transcoding processor, or standard audio transcoding processor from output stream parameters. `Channel` creates compatible connectors and shares processors/flowlines where outputs can reuse work.

| Path | Main classes | Core purpose |
| --- | --- | --- |
| Passthrough | `PasstroughConnector`, `PassthroughProcessor` | Direct media flow with tee/splitter behavior; captions can receive `utplcaptionmute`. |
| Complex video | `VideoComplexConnector`, `VideoTranscodingProcessor` | Normalize/select video, feed compositor and encoding flowline. |
| Complex audio | `AudioComplexConnector`, `AudioTranscodingProcessor` | Normalize audio, select fallback/mute paths, feed encoding. |
| Dolby | `DolbyComplexConnector`, `DolbyTranscodingProcessor` | Decode/convert/metadata/encode Dolby-capable paths. |

## Flowlines and sharing

`Flowline` creates an `encodebin` and can insert conversion, rate, and caps filters before it. It tees encoded output into queues for downstream outputs. This sharing is why a change to one output may affect another output with compatible requirements; inspect flowline matching before adding a per-output element.

## Media graph shape

```mermaid
flowchart LR
  I[Input stream] --> C[Connector]
  C --> P[Processor]
  P --> F[Flowline / tee]
  F --> O1[Single output]
  F --> O2[Single output]
  O1 --> G[Optional group output]
  O2 --> G
```

## Important constraints

Connectors create request pads, fallback sources, and clock synchronization elements. Do not replace their linking with a simple element chain: live output and missing-stream handling rely on these branches. Verify media type, codec, caps, live status, and output container before assuming a path is reusable.
