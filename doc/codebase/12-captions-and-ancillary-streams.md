# Captions and Ancillary Streams

Caption handling is narrower than audio/video handling and must be described conservatively. `Stream::create` recognizes caption media, `PassthroughProcessor` can insert the in-tree `utplcaptionmute` element, and the custom plugin exposes a mute property that is applied at a safe stream boundary.

`pipeline::Caps` maps DVB subtitle and teletext codecs to `subpicture/x-dvb` and `subpicture/x-teletext`-style GStreamer caps paths as implemented by the source mapping. Caption behavior ultimately depends on demuxer/parser/muxer support and on whether the stream remains passthrough.

The code contains explicit caption/subtitle TODOs in channel construction. Therefore neither generic subtitle transcoding nor complete ancillary-stream support should be advertised as runtime verified. The tests include subtitle fixtures in selected scenarios; inspect their assertions before treating them as coverage.

When changing captions, document input discovery, language/tag preservation, mute timing, muxer request pads, output container limitations, and test media. Use TSDuck/ffprobe stream inspection rather than visual playback alone.
