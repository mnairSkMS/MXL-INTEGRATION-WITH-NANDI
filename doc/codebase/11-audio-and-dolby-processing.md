# Audio and Dolby Processing

## Standard audio path

`AudioComplexConnector` uses `input-selector`, `audioconvert`, `audioresample`, `capsfilter`, `fallbackswitch`, `audiotestsrc`, `clocksync`, `tee`, queues, and `utplaudiomute`. It normalizes channel/rate requirements, supplies silence for fallback cases, and fans out to requested output processing.

`AudioTranscodingProcessor` creates output flowlines and adds muting support. The JSON parser recognizes MP1, MP2, MP3, AAC, AC-3, and E-AC-3 in addition to passthrough where applicable. Audio parameters carry channels and rate; `audio.bitrate` is parsed but never used, so bitrate reaches the encoder only as a `bitrate` property of a named encoder. Actual encoder support comes from the installed GStreamer registry.

AAC is encoded by `fdkaacenc` (rank primary; `avenc_aac` and `voaacenc` are also installed). It produces MPEG-4 AAC-LC only: its output caps carry `mpegversion=4` and `base-profile=lc`, so a `caps.profile` override is dropped, and the installed plugin comes from Ubuntu's `gstreamer1.0-fdkaac` 1.20 package rather than the 1.24 build and rejects HE-AAC. For MP1/MP2/MP3 the MPEG version is set by the sample rate: 32/44.1/48 kHz gives MPEG-1, 16/22.05/24 kHz gives MPEG-2 (`avenc_mp2` at 24 kHz emits MPEG-2 Layer II).

## Dolby path

`DolbyComplexConnector` and `DolbyTranscodingProcessor` provide a distinct path for Dolby-compatible parameters. The source can instantiate `dlbpatdec`, `dlbpatmx`, `dlbpatloud`, and `dlbpatenc` elements, alongside decode/convert/fallback/mute and clock branches. The Dolby processor explicitly uses `dlbpatenc` followed by a caps filter.

The four elements are compiled from `transcoder/plugins/gstreamer/dolbypat`; their SDK headers and static archives are checked into `ext/`. Build success proves that these targets link in the development environment. It does not prove every DD/DD+/Atmos/AC-4 conversion on a deployed device.

### Source-following mixer

Every input reaches the `input-selector` as 16-channel Dolby PCM with metadata: Dolby-decodable inputs through `dlbpatdec`, all others through `audioconvert ! audioresample ! capsfilter ! dlbpatloud mode=off`. Each PCM block's `DlbPatMetadata` buffer meta also carries the **source channel count**: `dlbpatloud` stamps it from its raw input caps and `dlbpatdec` from its sink caps (set by the AC-3/E-AC-3 parser and updated when acmod changes). The connector's dummy and fallback silence sources set `dlbpatloud source-channels=0` (unknown), so silence never reconfigures a mixer. Because the tag travels with the samples, it is correct whichever input, playlist item, switch or fallback put the block on air.

On the raw input side, `_followInputLayout` (a CAPS probe on the converter) retargets the capsfilter to 48 kHz and 2, 6 or 10 channels for each new input layout. Mono is widened to stereo, because PADMX does not upmix mono, and 7.1 is folded to 5.1. A new playlist item or a live layout change therefore renegotiates, instead of being folded into the first stream's layout. The input side of a Dolby decoder is `identity`: the blocks keep the decoder's layout.

Every Dolby output with 2-10 channels gets `dlbpatmx out-channels=N|2_auto in-channels=source ! dlbpatloud`. With `in-channels=source`, `dlbpatmx` reads the tag of each block and reconfigures at the first block whose tag differs. It drains the library first, so no samples are lost, and logs the switch.

| Source channels | Output 2.0 | Output > 2 channels |
| --- | --- | --- |
| 0 (unknown) | keeps the current configuration | keeps the current configuration |
| 2 | metadata, downmix (pass-through) | 2.0 input without metadata, **upmix** |
| more than 2, fewer than the output | metadata, downmix | metadata, upmix |
| the output's count or more | metadata, downmix (render) | metadata, downmix (render) |

PADMX passes metadata-described stereo through with C, LFE, Ls and Rs silent (the "audio pass-through" row of the SDK upmix table). That is why stereo is handed over as plain 2.0 without its metadata.

A live input that changes codec on the same PID (AAC to E-AC-3 and back, with a new PMT stream type) is followed in two places. `SingleInputStream::replacePads` rebinds the stream to the decoder group's new pads, matched by stream-id (see [07](07-inputs-and-ingest.md)). In the connector, a CAPS probe on the input pad rebuilds the `decodebin` when the media type changes (`_replaceDecoder`). A codec that `dlbpatdec` does not take (AAC) leaves that `decodebin` as raw PCM and goes through the same raw transformer as above (`InputContext::adapter`).

**Timestamps.** `dlbpatdec`, `dlbpatmx` and `dlbpatloud` stamp their output from sample counts against the input anchor (`common/timeline.c`), not by copying the last input timestamps. They drop the library's priming samples and hold serialized events until the sample they arrived at (`common/queue.c`). `dlbpatenc` corrects GstAudioEncoder's timestamps in `pre_push`: PAEC holds input back, so `finish_frame` stamped every frame after the first 64-68 ms late. The frame size comes from the AC-3/E-AC-3 header. Measured A/V offset by frame PTS: single files +5/-3 ms; playlist items within the detector's resolution (about ±30 ms, one AC-3 block).

**Live outputs with heavy video.** The live muxer holds audio until the video encoder delivers the same running time, and x264 at 1080p can hold more than a second. The queue after `dlbpatenc` (`DolbyTranscodingProcessor`) is therefore limited by time, at 3 s, rather than by buffer count. It only fills while the muxer waits. With a few buffers of room, the audio path stalled back to the connector's `fallbackswitch`. That timed out after 0.5 s and dropped about half the real audio.

Verified on a scratch build (2026-10-12): SRT live E-AC-3/AAC 5.1 ↔ 2.0 ↔ mono ↔ 7.1 layout changes land at the change with constant A/V; AAC ↔ E-AC-3 codec changes on one PID work in both directions, starting from either codec; a mixed playlist (stereo/5.1 AAC, AC-3, MP2, mono) upmixes every stereo item; a 1080p25 x264 playlist into an SRT output keeps its audio. There is no automated test case yet.

`audio.inputLayout: stereo` (older spelling `audio.stereoSource: true`) marks a source whose header declares more channels than it carries (for example E-AC-3 5.1 with only L/R audible). The input is then treated as 2.0 in the same way, instead of being re-encoded 5.1 → 5.1 with four silent channels. It applies all the time, so a source that also carries real 5.1 loses that centre/surround content. A downmix-then-upmix chain (`2_loro` followed by `mode=upmix`) does not work: the encoder still receives 2.0.

**Content-detected layout (`audio.inputLayout: auto`).** For a feed that keeps a constant 5.1 header but alternates real 5.1 with stereo carried in it (for example promos inside a 5.1 programme), the output mixer runs as `dlbpatmx in-channels=source-auto`. For a 2.0, 5.1 or 5.1.4 source and a 5.1 or 5.1.4 output it opens PADMX in upmix mode with `input_speaker_cfg=AUTO` and feeds the blocks without their Dolby metadata, because with metadata Auto behaves exactly like metadata (Dolby PAT development guide v4.5, "Input channel configuration", section 10.9.3). PADMX then decides from the audio: stereo is upmixed, real 5.1 is passed through. One PADMX instance covers both, upmix mode has one latency (1505 samples) for every input configuration, and the switch lands on the content change (the library starts it about 5 ms early, inside its look-ahead). Other source layouts (mono, 7.1, ...) keep the `in-channels=source` behaviour.

Library behaviour measured with the vendored PADMX (2026-10-16, plain 6-channel PCM): C/LFE/Ls/Rs at digital zero, -110 dBFS or -90 dBFS count as stereo; 5.1 whose C and surrounds are at -80 dBFS also counts as stereo, at -60 dBFS as 5.1 - the threshold is fixed inside the library and not configurable. 3.0 and L/R+surround-without-centre are passed through, not completed. Real 5.1 passes through at lag 0 with other differences at about -75 dBFS; the LFE is low-pass filtered.

Trade-offs: the source's Dolby metadata (declared dialnorm, downmix coefficients) does not reach the mixer on such outputs - the mixer's own metadata is used and `dlbpatloud` re-levels after it, so set `dialnorm` on the encoder. Every source on the output goes through upmix mode, so real 5.1 sees the 1505-sample PADMX latency instead of the downmix-mode 64 samples; the timeline compensates it (no A/V change measured).

Verified on a scratch build (2026-10-16): E-AC-3 5.1 file and live SRT (caller, jio-derived config) feeds alternating real 5.1 and stereo-in-5.1 come out with all six channels active throughout, where `metadata` leaves C/LFE/Ls/Rs at about -100 to -120 dBFS in the stereo stretches; a per-speaker tone test gives identical centre/LFE/surround levels in `auto` and `metadata` (within 0.1 dB); a flash-and-beep source measures 0.1 ms A/V in both modes in every section, with no jump at the switches; an AAC 5.1 source behaves the same; a replay of a recorded 646 s jio output has no second with C/Ls/Rs silent (267 before) and its real-5.1 seconds stay within about 1 dB of the input per channel. Not yet tested: 5.1.4 outputs, playlist inputs, a long soak. There is no automated test case yet.

The transcoder, `libutpl` and `dlbpatmx` must come from the same build; mixing one `dlbpatmx` build with another build's `dlbpatcommon`/`dlbpatdec`/`dlbpatenc` silently produces 2.0 output. The flag participates in `AudioParameters` equality, so outputs with and without it do not share a processor.

### Encoder properties

`DolbyTranscodingProcessor` is used when the output's `encoder` is empty or `dlbpatenc`, and applies every entry of `ac3.properties` / `eac3.properties` to its `dlbpatenc`. Each string value is deserialized into the property's own GLib type, so enum nicks such as `"bitrate": "384k"` work; an unknown property name logs a warning and a value that cannot be deserialized logs an error. The `dlbpatmx` and `dlbpatloud` properties from the same object are passed through the connector's pipeline string.

`dialnorm` does not set the bitstream's dialogue normalization on its own. `dlbpatloud` defaults to the ATSC regulation type, which fixes the loudness target at -24 LKFS and writes dialnorm -24 into the metadata the encoder follows; PALOUD uses the configured `dialnorm` only with the `manual` regulation type.

### Verifying against the Dolby SDK test kit

Upmix output can be checked against the Dolby Pro Audio Transcoder SDK library-integration tests. A03H_AF14_XX01 (2.0 upmixed to Dolby Digital 5.1 at 384 kbps, MPEG-2 TS output) matches the product's AC-3 output; A02H_AG14_XX01 and A01H_AH14_XX01 are the DD+ and DD+ Atmos variants. The procedure:

1. Carry the kit's `AA_2ch_upmix_test_60s_-24LKFS_ws.wav` into an MPEG-TS the input accepts, as AAC or AC-3 stereo. PCM as SMPTE 302M is decoded by `avdec_s302m` but does not link into the audio chain, and the output is silent.
2. Configure one `ac3` output with `"audio": {"channels": 6, "rate": 48000}` and `"ac3": {"encoder": "dlbpatenc", "properties": {"bitrate": "384k"}}`, play the input in real time, and capture about 40 seconds that start after the output is valid and end before the input does.
3. Take the capture window by slicing the output TS on 188-byte packet boundaries. A remux (for example `ffmpeg -c copy`) rewrites the PMT, and the kit's TsDemux then rejects the stream type.
4. Run `mpeg_2_ts_validator -if <capture> -if_pid <audio PID>`, then `dolby_bitstream_inspector -if <demuxed .ac3> -ir AF_0143_2ch_upmix_test_60s_-24LKFS_ws_configuration.xml`. The inspector checks data rate, channel mode, dialnorm, per-channel tone routing and integrated loudness.

The kit's Python tools need 32-bit `libstdc++6`/`libgcc1`; on a host without i386 support, run them in a container.

## Constraints and limits

The plugin sources contain explicit unsupported channel/layout/bitrate branches, and `doc/misc/mix-capabilities.md` contains a useful matrix. Treat that matrix as a candidate until a path is tied to the current plugin property behavior and runtime media test. In particular, source comments flag 5.1.2 as unsupported in some encoder/mixer cases and document metadata cropping implications.

## Evidence-oriented tests

The production suite has twelve audio cases covering AAC, MP3, AC-3, E-AC-3, rates, channels, and bitrate. Extend verification with `ffprobe`, decoded-channel analysis, and media fixtures for every claimed Dolby matrix cell. Missing license/SDK/media dependencies make a case integration gated, not absent.
