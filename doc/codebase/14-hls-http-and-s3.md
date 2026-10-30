# HLS, HTTP, and S3

## Internal HLS output

`HlsOutputSink` receives output pads and creates a segmenting path. MPEG-TS uses `splitmuxsink` with `mpegtsmux`; ISO fragmented MP4 uses `isofmp4mux`. It feeds an `appsink` into UTPL's `hls::Streamer`, which maintains segments and writes media/master playlists through a storage and uploader abstraction.

`hls::Streamer` uses in-memory segment storage. `FileUploader` and `HttpUploader` derive from `BaseUploader`, whose worker thread serializes upload/removal/directory work and retry behavior. HTTP upload supports WebDAV operations such as `MKCOL` and `PROPFIND` in the source.

`FileUploader` writes each object to `<name>.tmp` and renames it into place, so a web server serving the output directory never returns a truncated playlist or segment while it is rewritten. (Without it a DASH client read an empty MPD during a rewrite and `dashdemux2` aborted; see 40.) The CMAF DASH output reuses these uploaders.

`splitmuxsink` requests a keyframe from the encoder at each segment boundary. Before 2026-10-27 that request also leaked past the encoder into the input and caused a second IDR one frame (or a fraction of a second) into each segment of a GROUP output; see the flowline note in [10](10-video-processing.md) and 27.

## Master playlist

A `group` output writes one master playlist, `<baseFileName>.m3u8` (or `master.m3u8`), with one `EXT-X-STREAM-INF` per linked output, in link order. `createHlsVariant` in `transcoder/output/utils.cpp` copies each linked output's configured video `width`/`height` and `rate` into the `hls::Variant`. `MasterPlaylist::serialize` in `hls/playlist.cpp` then writes:

- `BANDWIDTH`: the measured average of that rendition's first segments, not the configured bitrate. The streamer holds the master back until every variant has one (up to three segments) and uploads it once per run.
- `RESOLUTION=<width>x<height>`: only when both are configured. Players label their quality menu from this. Without it they show 0x0.
- `FRAME-RATE`: only when the output sets `rate`.

`CODECS` is not written; see 27.

## HLS configuration

The parser recognizes HLS transfer mode/framing, timeout/retry parameters, authentication, base filename, ad marker selections, segment duration/window parameters, and credentials. The application validates minimum segment duration and window size. This is source-backed parsing, but every selected combination remains subject to runtime plugin/caps/endpoint validation.

## Input HLS

HLS input is a different feature: `SingleInput` uses HTTP source and `hlsdemux`, and playlist input may contain HLS entries. Patch `050-hlsdemux-add-playlist-update-signal.patch` is relevant to application callback behavior; see 19.

## S3

`S3OutputSink` contains both parser/queue branches and a path using `awss3hlssink`/`hlssink` factories. The generic HLS uploader path also recognizes HTTP/WebDAV-style targets. Which branch is production-ready for a particular S3 endpoint must be demonstrated with current plugins, credentials, and a bucket; label all unrun S3 claims integration gated.

## Required tests

Validate segment count/window cleanup, target duration, sequence progression, discontinuity and SCTE tags, media/master playlist correctness, upload retry behavior, and fMP4 versus TS output. A generated playlist alone is not sufficient proof that the segments are playable.
