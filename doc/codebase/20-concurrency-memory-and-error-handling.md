# Concurrency, Memory, and Error Handling

## Execution contexts

| Context | Main owner | Responsibility |
| --- | --- | --- |
| GLib main loop | `Application` | Signals, application lifecycle, GStreamer watch dispatch. |
| Transcoder worker | `app::Transcoder` | Runs/owns engine work. |
| GStreamer streaming threads | GStreamer elements | Pad callbacks, probes, data flow, dynamic pads. |
| Event scheduler | `EventScheduler` | Timed event dispatch to channel/compositor/output paths. |
| HLS uploader worker | `BaseUploader` | Serialized upload/removal/retry tasks. |
| Image/overlay preload/feed workers | image/overlay input classes | Asset retrieval/feeding. |
| FastAPI server | Uvicorn/FastAPI | REST request handling and gRPC client calls. |

## Ownership rules

UTPL wraps GObject/GStreamer references in pointer helpers and uses C++ Pimpl plus smart pointers. GStreamer request pads, static pads, bus objects, caps, tag lists, and callback-owned data still follow GStreamer ownership rules; do not assume `shared_ptr` makes C references safe.

`Element` tracks pending asynchronous state changes, and `Pipe` maps bus errors/EOS to engine callbacks. `Engine` performs ordered teardown through its state machine. Bypassing those teardown paths risks dangling callbacks, request pads, and graph elements.

## Failure behavior

The application validates many configuration errors before building a pipeline. Runtime errors arrive through the GStreamer bus and stop/interrupt the engine. Sink/upload/licensing/HTTP paths have their own error reporting and retry logic. Record the originating layer in failures: parser, application validation, graph construction, caps negotiation, element bus error, external service, or test harness.

## Test changes

For every async change, test EOS, SIGINT/stop, pipeline error, source loss, callback after teardown, and retry/cancellation. Use ASAN Debug builds for ownership-sensitive changes and retain a DOT graph/log artifact for failures.
