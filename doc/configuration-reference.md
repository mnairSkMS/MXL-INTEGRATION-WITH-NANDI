# Transcoder JSON config — full parameter reference

Every key the engine reads from `config.json`, with a short meaning and who should
decide the value.

Authority is `transcoder/app/src/application/config/parser/json/parser.cpp`, not the
OpenAPI schema — where they disagree, the parser wins and the difference is noted.
**Unknown keys are silently ignored.** A typo is not an error; it is a setting that
never applies.

## Legend — `Decide` column

| Value | Meaning |
|---|---|
| `HUMAN` | A fact about the facility, the contract, or the business. Cannot be inferred. Ask. |
| `DERIVED` | Compute it from another value already chosen. The rule is in the `Rule` column. |
| `LLM` | Safe default. Use the stated value unless the user raises the topic. |

`Type` shows `(default)` where the engine has one. `req` marks a required key.

## Shape

```
version                       string, req
transcoder                    object, req
  inputs[]                    req, >=1
    transport                 req   { uri, srt|udp|hls|dash|file|playlist|ndi|mxl }
    selector                        { program }
    mxlTap                          { domain, video.flowId, audio.flowId, audio.channels }
  outputs[]                   req, >=1
    type, name, container, scte
    mxl                             { domain, video.{flowId,width,height,rate}, audio.{flowId,channels} }   (type "mxl" only)
    transports[]                    { uri, srt|udp|hls|s3|file }
    streams[]                 req, >=1
  scte                              { pois }
  blanking / blackout               { enable, restriction, conditions }
  squeeze[]                         { name, windows }
application                   object, optional
  logger, gstreamer, licenseServer
```

---

## Top level

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `version` | string, req | Config format `MAJOR.MINOR.PATCH`. Gates parser behaviour. | `LLM` | `"1.4.2"`. Below `1.4.0` the parser makes `outputs[].container` mandatory. |
| `transcoder` | object, req | The pipeline. | — | — |
| `application` | object | Logging, plugin ranks, licensing. Omit entirely for defaults. | — | — |

---

## `transcoder.inputs[]`

One entry per source. Multiple entries are alternative sources for failover.

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `transport` | object, req | The source endpoint. Missing → parse error. | — | — |
| `transport.uri` | string \| string[], req | Source endpoint. An array is an ordered failover list. | `HUMAN` | Never invent a host, port or path. |
| `selector.program` | int | MPEG-TS program number to pick from a multi-program stream. | `HUMAN` | Only for multi-program TS; omit otherwise. |
| `transport.mxl` | object | The flows of an input whose uri is `mxl:///<domain directory>`: `video.flowId` and/or `audio.flowId`, and optionally `delayMs` (0–2000, default 500). | `HUMAN` | Required with an `mxl://` uri, forbidden without. UUIDs, different; one uri only; no host after `mxl://`. Not for playlist items. Details: [codebase/05](codebase/05-configuration-reference.md#input-mxl). |
| `mxlTap` | object | Copies the decoded source to MXL flows (other processes read them). Fail-open: a broken tap never affects the channel. | `HUMAN` | Needs the MXL plugin in the image and a shared tmpfs domain. `domain` and at least one `video.flowId`/`audio.flowId` (UUIDs, different) are required; `audio.channels` 1–8, default 2. Details: [codebase/05](codebase/05-configuration-reference.md#input-mxl-tap). |

**URI schemes:** `file://` `udp://` `srt://` `http://` `https://` (HLS, or DASH for `.mpd` / `transport.dash`) `hls+file://`
`pls+file://` `ndi://` `rtmp://` `rtmps://` `s3://` `mxl:///<domain directory>`

`rtmp://` and `rtmps://` take no sub-block. For an encoder pushing to this appliance use
`rtmp://127.0.0.1:1935/live/<key>` (key: 16+ URL-safe characters) and give the encoder
`rtmp://<host>:1935/live` plus the key. To pull from an external RTMP server or CDN, put its
playback URL in the URI unchanged (`rtmp://203.0.113.10:1935/live/<stream>`); the relay is not
involved. Only H.264 video with AAC or MP3 audio is accepted. `config/rtmp_to_srt_hls.json` is a
pull channel to an SRT listener plus HLS.

The sub-block you add under `transport` must match the scheme; its **presence** is what
selects the transport type.

### `transport.srt`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `srt.mode` | `listener` \| `caller` | Who dials. | `DERIVED` | `listener` when the URI host is `0.0.0.0` or a local address; `caller` when it is a remote host. |
| `srt.latency` | int, ms | Receive buffer depth (`SRTO_RCVLATENCY`). | `LLM` | `120`. On a lossy WAN use ~4× RTT. |
| `srt.bufferSize` | int, bytes | `SO_RCVBUF`. | `LLM` | Omit. Only raise alongside a high `latency`. |

### `transport.udp`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `udp.multicast.interfaces` | string[] | Interfaces to join the group on. Multiple allowed. | `HUMAN` | Interface names are facility facts (`eth0`, `ens1f0`). |

### `transport.hls`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `hls.bandwith` | int, bps (`4294967000`) | Pick the variant whose `BANDWIDTH` is closest. | `LLM` | Omit → highest variant. **Note the spelling**: the parser reads `bandwith`; the schema's `bandwidth` is ignored. |
| `hls.credentials.username` | string | Source auth. | `HUMAN` | — |
| `hls.credentials.password` | string | Source auth. | `HUMAN` | — |
| `hls.scte.source` | `segments` \| `manifest` | Where SCTE-35 is read from. The other is ignored. | `HUMAN` | Depends on how the upstream signals ads. |

### `transport.dash`

An `http(s)://` input is MPEG-DASH when this block is present (even `{}`) or the URI path ends in `.mpd`; otherwise it is HLS.

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `dash.bandwidth` | int, bps (`4294967295`) | The highest representation not above it, kept for the whole session. | `LLM` | Omit → highest representation. 1000–4294967295, else rejected. |
| `dash.presentationDelay` | int, ms (10 s) | How far behind the live edge to play when the MPD has no `suggestedPresentationDelay`. | `LLM` | Lower → less latency, less margin for late segments. |
| `dash.scte.source` | `segments` | SCTE-35 from in-band `emsg` boxes. | `HUMAN` | `manifest` (MPD `EventStream`) is not supported and is rejected. |

### `transport.file` / `transport.playlist` / `transport.ndi`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `file` | `{}` | Marker only, no fields. | `LLM` | `{}` with a `file://` URI. |
| `playlist` | `{}` | Marker selecting the playlist input, used with `pls+file://`. | `LLM` | `{}`. **`mode` and `id` are not read by the parser** — shipped configs contain them but they do nothing. |
| `ndi.ndiName` | string | NDI source name to discover. | `HUMAN` | — |
| `ndi.urlAddress` | string | NDI source address, bypassing discovery. | `HUMAN` | — |

---

## `transcoder.outputs[]`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `type` | `SINGLE` \| `GROUP` \| `MXL` (`SINGLE`) | `GROUP` is a multi-variant HLS parent. Only HLS supports it. `MXL` writes the finished programme to MXL flows; it takes an `mxl` object instead of `transports`, `container` and `streams`, is fail-open and needs the MXL plugin in the image. | `HUMAN` | `SINGLE` unless building an HLS ladder or publishing to MXL. Details: [codebase/05](codebase/05-configuration-reference.md#output-mxl). |
| `name` | string | Logical id used to link a `SINGLE` into a `GROUP`. | `DERIVED` | Only needed when grouping. Use a stable slug. |
| `container` | `mpegts` \| `es` \| `fmp4` | Output container. | `LLM` | `mpegts` for TS/SRT/UDP/HLS-TS; `fmp4` for HLS only; `es` for raw. Required when `version` < 1.4.0. |
| `streams[]` | array, req | Output streams, ≥1. Missing → parse error. | — | — |
| `transports[]` | array | Destinations. Required for a standalone `SINGLE` and for a `GROUP`; **ignored** for a `SINGLE` inside a group. | — | — |
| `scte.mode` | `passthrough` \| `removal` | Keep or strip SCTE-35 on this output. | `LLM` | `passthrough` when the input carries SCTE-35 and downstream needs ad markers; else `removal`. |
| `scte.pid` | int, 0–8192 | Output PID carrying SCTE-35. | `DERIVED` | Reuse the input's SCTE PID; `500` if unknown. |
| `scte.nullInterval` | int, ms (`0`) | Interval for injecting SCTE-35 null packets. `0` = none. | `LLM` | `0`. Set ~`100` only when a downstream splicer needs a heartbeat. |

A `GROUP` output holds only `type`, `name` and `transports`. Each variant is a separate
`SINGLE` output carrying a `link` stream that names the group (see `streams[].link`).
`container` and `scte` are inherited from the group and may be overridden per variant.

### `outputs[].transports[]`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `uri` | string, req | Destination endpoint. | `HUMAN` | Never invent a host, port, bucket or path. |

#### `.srt`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `srt.mode` | `listener` \| `caller` | Who dials. | `DERIVED` | `listener` when the URI host is `0.0.0.0` (players connect in); `caller` when pushing to a remote. |
| `srt.latency` | int, ms | `SRTO_PEERLATENCY`. | `LLM` | `120`; ~4× RTT on a lossy WAN. |
| `srt.bufferSize` | int, bytes | `SO_SNDBUF`. | `LLM` | Omit. |

#### `.udp`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `udp.multicast.interface` | string | Egress interface for the group. | `HUMAN` | — |
| `udp.multicast.ttl` | int (`1`) | Multicast TTL. `0` is coerced to the default. | `LLM` | `1` on one subnet; raise only to cross routers. |
| `udp.unicast.ttl` | int (`64`) | Unicast TTL. `0` is coerced to the default. | `LLM` | `64`. |

Note: the schema spells this block `mulitcast`. The parser reads `multicast`.

#### `.hls`

The whole HLS transport block must be present for HLS parameters to apply.

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `hls.numSegments` | int (`10`) | Segments kept in the playlist. | `LLM` | `6`. Window = `numSegments × segmentLength` seconds of DVR. |
| `hls.segmentLength` | int, s (`10`) | Max segment length. | `DERIVED` | Must be a whole multiple of the GOP: `key-int-max ÷ video.rate`. Player latency ≈ 3 × this. |
| `hls.singleDirectory` | bool (`true`) | Playlists and segments in one directory vs. a subdirectory per variant. File and WebDAV only. | `LLM` | `true` single-variant, `false` for a group. |
| `hls.vodMode` | bool (`false`) | Emit one file containing all segments. | `LLM` | `false` for live. |
| `hls.transferMode` | `http_put` \| `http_dav` | How files are pushed. | `DERIVED` | From the URI scheme. Omit for `hls+file://`. **The schema's `file` value is rejected by the parser.** |
| `hls.transferFraming` | `chunked` \| `fixed` (`fixed`) | HTTP transfer encoding. | `LLM` | `fixed`. |
| `hls.timeout` | int, ms (`2000`) | Upload timeout. | `LLM` | `2000`. |
| `hls.retry.count` | int (`0`) | Upload retries. `-1` = unlimited. | `LLM` | `3` for a remote push, `0` for local. |
| `hls.retry.interval` | int, ms (`1000`) | Delay between retries. | `LLM` | `1000`. |
| `hls.adMarkers` | array of `elemental` \| `enhanced` | Ad-marker tags written into the media playlist. | `LLM` | `["elemental"]`. Add `enhanced` when downstream needs the base64 SCTE section. Unknown values warn and are dropped; `adobe` is not implemented. |
| `hls.authentication` | `basic` \| `digest` | HTTP auth scheme. | `HUMAN` | Set with `credentials`. |
| `hls.credentials.username` | string | Destination auth. | `HUMAN` | — |
| `hls.credentials.password` | string | Destination auth. | `HUMAN` | — |
| `hls.baseFileName` | string | Renames outputs to `<base>.m3u8`, `<base>_variant.m3u8`, `<base>_variant_section_0.ts`. | `LLM` | Omit → `master.m3u8` / `variant.m3u8`. |

#### `.dash`

CMAF DASH output; the URI is `dash+file://<dir>`, `dash+http://` or `dash+https://`. The output (and each member of a DASH `group`) needs `container: fmp4`, one `h264`/`h265` video stream and `aac` audio. Without the block, defaults apply. See [the DASH output page](codebase/40-cmaf-dash-output.md).

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `dash.segmentDuration` | int, ms (`2000`) | Segment length; every stream is cut on this grid. | `DERIVED` | A whole number of frames: `2000` at 25/50 fps; `3200` (or any multiple of 640) also makes 48 kHz AAC segments exact. |
| `dash.numSegments` | int (`30`) | Live window (`timeShiftBufferDepth`). | `LLM` | `30` (60 s DVR at 2 s). |
| `dash.vodMode` | bool (`false`) | Keep every segment, static MPD on stop. | `LLM` | `false` for live. |
| `dash.presentationDelay` | int, ms (`0` = 3 segments) | `suggestedPresentationDelay`: how far behind live players start. | `LLM` | Omit. |
| `dash.minBufferTime` | int, ms (`0` = 2 segments) | MPD `minBufferTime`. | `LLM` | Omit. |
| `dash.minimumUpdatePeriod` | int, ms (`0` = 1 segment) | How often players reload the MPD. | `LLM` | Omit. |
| `dash.utcTimingUrl` | string | `UTCTiming` clock source for players. | `HUMAN` | Set for public players, e.g. an `?iso` time endpoint; omit on a closed LAN. |
| `dash.baseFileName` | string | `<base>.mpd` and a prefix on every object. | `LLM` | Omit → `manifest.mpd`. Set when several outputs share a directory. |
| `dash.transferMode`, `dash.transferFraming`, `dash.timeout`, `dash.retry.*`, `dash.authentication`, `dash.credentials.*` | as `.hls` | Upload settings for `dash+http(s)`. | as `.hls` | Omit for `dash+file://`. |

#### `.s3`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `s3.bucket` | string | Target bucket. | `HUMAN` | — |
| `s3.region` | string | Bucket region. | `HUMAN` | — |
| `s3.accessKey` | string | Credential. | `HUMAN` | Secret — never generate or guess. |
| `s3.secretAccessKey` | string | Credential. | `HUMAN` | Secret — never generate or guess. |
| `s3.keyPrefix` | string | Key prefix for uploaded objects. | `DERIVED` | From the channel name. |

#### `.file`

`{}` — marker only, no fields.

### `outputs[].streams[]`

At least one. Every stream needs `type`.

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `type` | `audio` \| `video` \| `link`, req | What the stream is. Unknown value → parse error. | — | — |
| `codec` | `passthrough` \| `es` \| `mp1` \| `mp2` \| `mp3` \| `aac` \| `ac3` \| `eac3` \| `h264` \| `h265` | Output codec. `passthrough` skips transcoding entirely; `es` is raw PCM / raw frames. | `LLM` | `h264` + `aac` unless the delivery target dictates otherwise. Prefer `passthrough` when no transform is needed — it is by far the cheapest. |
| `pid` | int, 16–8190 | MPEG-TS PID for this audio/video stream (mpegts and HLS-TS only). Omit for automatic numbering from 0x41. Must be unique in the output and differ from `scte.pid`. | `HUMAN` | Set only when the receiver expects fixed PIDs; copy them from its spec. |
| `audio.bitrate` | int, kbps | **Parsed but unused.** The engine never reads it back, so the encoder runs at its own default (`fdkaacenc`: about 320 kbps for 48 kHz stereo). Set the bitrate as the encoder's `bitrate` property instead: bits/s for `fdkaacenc`, `avenc_mp2` and `avenc_ac3`, an enum nick such as `"192k"` for `dlbpatenc`. | `LLM` | Leave unset. |
| `audio.channels` | int | Channel count. | `LLM` | `2`. Omit to inherit the source. |
| `audio.inputLayout` | string (`metadata`) | `metadata`: trust the source's declared layout. `auto`: detect from the audio - stereo inside a 5.1 header (silent C/LFE/surrounds) is upmixed, real 5.1 passes through, switching per segment; needs `channels` 6 or 10. `stereo`: always use front L/R only and upmix. Dolby PAT path (`eac3`/`ac3` + `dlbpatenc`) only. | `HUMAN` | `auto` for feeds that alternate real 5.1 and stereo-in-5.1 (e.g. promos); `stereo` only for a feed that is always stereo-in-5.1. |
| `audio.stereoSource` | bool (`false`), deprecated | Older spelling of `inputLayout: stereo`. Source audio is stereo inside a wider declared layout; use only front L/R and upmix to `audio.channels`. Dolby PAT path with `channels` > 2 only. | `HUMAN` | Use `inputLayout` instead. |
| `audio.rate` | int, Hz | Sample rate. | `LLM` | `48000`. |
| `video.width` | int, px | Frame width. | `HUMAN` | The channel's delivery resolution. Omit to inherit the source. |
| `video.height` | int, px | Frame height. | `HUMAN` | As above. |
| `video.rate` | int, fps | Frame rate. For interleaved video this is the frame rate, so `25` is 1080i25 (50 fields/s). | `DERIVED` | Match the source. Never resample unless asked. |
| `video.interlace` | `progressive` \| `interleaved` | Output scan type. Every input is deinterlaced, so output is progressive unless this is `interleaved`. `interleaved` needs `video.rate` and codec `h264`, or the engine refuses to start. | `HUMAN` | Omit (progressive) unless the delivery spec requires interlaced. |
| `video.fieldOrder` | `tff` \| `bff` (`tff`) | Field order of interleaved video. | `HUMAN` | `tff` for HD. |
| `video.format` | `I420` \| `YV12` \| `I422` \| `I444` \| `UYVY` \| `YUY2` \| `YVYU` \| `NV12` \| `NV21` (`I420`) | Pixel format. | `LLM` | `I420`. |
| `link.type` | `OUTPUT`, req | Marks this stream as a group membership link. | `LLM` | `OUTPUT`. |
| `link.output.name` | string, req | Name of the `GROUP` output to join. | `DERIVED` | Must match that group's `name`. |
| `link.output.modifier` | string | Suffix distinguishing this variant's files, e.g. `_hd`. | `DERIVED` | From the variant's resolution or bitrate. |

Any unset `audio.*` / `video.*` value is taken from the input stream.

### `outputs[].streams[].<codec>` — encoder block

Key it by the codec name: `mp1` `mp2` `mp3` `aac` `ac3` `eac3` `h264` `h265`.

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `encoder` | string | GStreamer element name. `properties` and `caps` are applied only when it is set. | `LLM` | `x264enc` (h264), `x265enc` (h265), `fdkaacenc` (aac), `avenc_mp2` (mp2), `dlbpatenc` (ac3/eac3 at 48 kHz, licensed), `avenc_ac3` (ac3 at other rates). |
| `properties` | object of **string→string** | Element properties, passed straight through. Values must be strings, including numbers. | see below | — |
| `caps` | object of **string→string** | Caps constraints on the encoder output. Only fields the encoder's output template already has are applied; others log a warning and are ignored. | `LLM` | `{"profile": "high"}` for h264. Nothing for aac — see below. |

Values are opaque to the config parser — a wrong key silently does nothing.

**x264enc properties in use in this tree:**

| Property | Meaning | Decide | Rule |
|---|---|---|---|
| `bitrate` | Target bitrate in kbps. | `HUMAN` | A quality/cost decision. ~`6000` for 1080p25, ~`3500` for 720p. |
| `key-int-max` | Max frames between keyframes (the GOP). | `DERIVED` | `video.rate × desired GOP seconds`. Sets HLS segment alignment and SRT join time — halving it halves join time and costs bitrate. |
| `tune` | Encoder tuning. | `LLM` | `zerolatency` for live. |
| `speed-preset` | Speed/quality trade-off. | `LLM` | `veryfast`. `ultrafast` if CPU-bound, `medium` for file output. |
| `bframes` | B-frames between reference frames. | `LLM` | `tune: zerolatency` makes it 0; an explicit value overrides the tune. Omitted without `zerolatency`, x264 uses 3. Each B-frame adds a frame of latency. |
| `threads` | Encoder threads. | `LLM` | Omit → auto. |
| `pass` | Rate-control mode: `cbr` (default), `qual`, `quant`. | `LLM` | Omit for live. `cbr` is a bitrate **ceiling**, not constant bitrate: easy content drops far below `bitrate`. `qual` is constant quality (CRF) with `quantizer`; `quant` is a fixed quantizer with no bitrate bound — never use it live. |
| `quantizer` | CRF or QP value for `qual` / `quant`. | `LLM` | `23`. |
| `vbv-buf-capacity` | Rate-control buffer, milliseconds (`600`). | `DERIVED` | Larger smooths the rate over a longer window. |
| `option-string` | Raw x264 options, `key=value:key=value`. An element property of the same name wins. | `LLM` | `nal-hrd=cbr` for true CBR (filler keeps the video at `bitrate`); `vbv-maxrate=…:vbv-bufsize=…` to cap `qual`. |

**x265enc** exposes only `bitrate`, `qp`, `speed-preset`, `tune` and `key-int-max`; B-frames,
CRF and VBV go in `option-string` (`bframes=2`, `crf=23`, `vbv-maxrate=…:vbv-bufsize=…`).

A constant **transport-stream** rate (null-packet padding across video, audio and tables) is not
configurable: the muxer supports it but the parser has no key for it.

**aac (`fdkaacenc`)** produces MPEG-4 AAC-LC only. `bitrate` is in bits/s, `0` or unset lets the
encoder choose (about 320 kbps for 48 kHz stereo). There is no HE-AAC: `caps.profile` is ignored,
and `caps.mpegversion` other than `4` crashes the engine. For **mp2/mp3** the MPEG version follows
`audio.rate`: 32/44.1/48 kHz is MPEG-1, 16/22.05/24 kHz is MPEG-2.

**ac3 (`dlbpatenc`) properties in use:** `dialnorm` (`-24`), `drc-line`, `drc-rf`,
`surround-trim`, `height-trim` (all `auto`), and `bitrate` as an enum nick. For AC-3 the
accepted nicks are `32k`, `56k`, `64k`, `96k`, `128k`, `192k`, `256k`, `384k`, `448k`, `640k`,
or `auto`; any other value is refused by the element and it keeps its default. `HUMAN` — loudness compliance is a contractual matter.
Under the default ATSC loudness regulation the stream carries dialnorm -24 whatever
`dialnorm` is set to; see 11-audio-and-dolby-processing.md.

---

## `transcoder.scte`

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `pois.enable` | bool (`false`) | Enable ESAM/POIS signal conditioning. | `HUMAN` | `false` unless a POIS server exists. |
| `pois.server.uri` | string, req | POIS endpoint URL. | `HUMAN` | — |
| `pois.server.acquisitionPointId` | string, req | Identity of this acquisition point. | `HUMAN` | Assigned by the POIS operator. |
| `pois.timeout` | int, ms (`200`) | POIS request timeout. | `LLM` | `200`. **Read from `scte.pois.timeout`, not `scte.pois.server.timeout`** as the schema shows — and only when `server` is present. |

---

## `transcoder.blanking` and `transcoder.blackout`

Identical shape, two separate engine policies. `blanking` covers programme blanking;
`blackout` covers regional blackout. Both are evaluated against SCTE-35 splice commands.

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `enable` | bool (`false`) | Turn the policy on. | `HUMAN` | — |
| `restriction.ignore` | `NONE` \| `NO_REGIONAL_BLACKOUT` \| `WEB_DELIVERY_ALLOWED` | Which restriction bit in the segmentation descriptor to disregard. | `HUMAN` | Rights decision. `NONE` when unsure. |
| `conditions.spliceInsert` | object, req | Policy for `splice_insert` commands. | — | — |
| `conditions.timeSignal` | object, req | Policy for `time_signal` commands. | — | — |
| `conditions.*.blankWhenNoSegmentationDescriptors` | bool (`false`) | Whether a segment carrying no segmentation descriptors can still trigger blanking — the usual case for `time_signal`. | `HUMAN` | — |
| `conditions.*.segments[]` | array, req | Which segment types trigger blanking. | `HUMAN` | — |
| `segments[].start` | string, req | `segmentation_type_id` that starts blanking. Hex (`0x30`), decimal or octal. | `HUMAN` | — |
| `segments[].stop` | string, req | `segmentation_type_id` that ends it. | `HUMAN` | Usually `start + 1`. |
| `segments[].name` | string | Label for logs. | `LLM` | Derive from the type id, e.g. `provider-ad`. |
| `segments[].priority` | int, 1–16 (`16`) | Precedence when segments overlap. 1 is highest. | `LLM` | `16` unless overlaps are expected. |

---

## `transcoder.squeeze[]`

Named crop/scale presets, applied at runtime by the control plane.

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `name` | string | Preset name referenced when triggering the squeeze. | `LLM` | A stable slug, e.g. `l-bar`. |
| `windows.input` | `{x, y, width, height}` | Region of the source frame to take, as fractions of the frame (0..1). | `HUMAN` | A creative/layout decision. |
| `windows.output` | `{x, y, width, height}` | Where it lands in the output frame, as fractions of the frame (0..1). | `HUMAN` | As above. |

Windows are normalized: `1.0` is the full frame width or height, and the engine resolves them
against the channel's real output raster, so one preset is correct at any resolution.

`-1` in any dimension means copy from the source frame, or compute it keeping aspect ratio.

---

## `application`

Omit the whole block for defaults.

| Path | Type | Meaning | Decide | Rule |
|---|---|---|---|---|
| `logger.transcoder` | object | Engine logging. | — | — |
| `logger.gstreamer` | object | GStreamer logging. | — | — |
| `logger.*.level` | `trace` \| `debug` \| `log` \| `warning` \| `error` \| `fatal` | Verbosity. | `LLM` | `log` in production, `debug` while diagnosing. `trace` is very heavy. |
| `logger.*.console.enable` | bool | Log to the console. | `LLM` | `true`. |
| `logger.*.console.colors` | bool | ANSI colour. | `LLM` | `false` when logs are captured to a file or journal. |
| `logger.*.console.stderr` | bool | Write to stderr instead of stdout. | `LLM` | `false`. |
| `logger.*.file.enable` | bool | Log to a file. | `LLM` | `false` under Docker — the container captures stdout. |
| `logger.*.file.colors` | bool | ANSI colour in the file. | `LLM` | `false`. |
| `logger.*.file.path` | string | Log file path. | `DERIVED` | Required when `file.enable` is true. |
| `gstreamer.plugins[].name` | string, req | GStreamer element name. | `HUMAN` | Only set to resolve a known plugin conflict. |
| `gstreamer.plugins[].rank` | `NONE` \| `MARGINAL` \| `SECONDARY` \| `PRIMARY`, req | Autoplug priority. `NONE` disables the element. | `HUMAN` | As above. Invalid value → parse error. |
| `licenseServer.uri` | string, req | Network license server endpoint. | `HUMAN` | — |

---

## Documented but inert

Present in the OpenAPI schema; the parser never reads them. Setting them has no effect.

| Path | Note |
|---|---|
| `transcoder.scte.prerollTime` | Runtime always uses the built-in `4000` ms. |
| `transcoder.inputs[].transport.playlist.mode` | Not read. Shipped configs set `"managed"`; it does nothing. |
| `transcoder.inputs[].transport.playlist.id` | Not read. Shipped configs set `"main"`; it does nothing. |
| `transports[].hls.transferMode: "file"` | Documented value, rejected by the parser. |
| `transports[].udp.mulitcast` | Schema typo. Use `multicast`. |
| `transport.hls.bandwidth` | Schema spelling. The parser reads `bandwith`. |
| `transcoder.scte.pois.server.timeout` | Schema nesting. The parser reads `scte.pois.timeout`. |
