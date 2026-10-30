/**
 * Maps the channel form onto an engine configuration document.
 * GUARANTEE: Deeply preserves unknown and unedited fields from baseConfig.
 * Reference: CONFIG-REFERENCE.md, whose authority is the JSON parser, not the OpenAPI schema.
 */

import { ChannelFormValues, FormInputItem, FormOutputItem } from '../types/form';
import { EngineConfigDto, InputItemDto, OutputItemDto, VideoStreamDto, AudioStreamDto, VideoStreamProperties } from '../types/api';
import { DEFAULT_CONFIG_VERSION, audioEncoderFor } from '../constants/schema';

export function channelFormToConfig(
  form: ChannelFormValues,
  baseConfig?: EngineConfigDto | null
): EngineConfigDto {
  // Deep clone baseConfig to avoid mutating referenced state
  const result: EngineConfigDto = baseConfig
    ? JSON.parse(JSON.stringify(baseConfig))
    : {
        version: DEFAULT_CONFIG_VERSION,
        transcoder: {
          inputs: [],
          outputs: [],
        },
      };

  // Only a document that has no version at all gets one. An existing config keeps its own:
  // the parser gates behaviour on this field, so silently re-stamping a live 1.2.0 channel as
  // 1.4.2 would change how the engine reads the rest of the file.
  result.version = result.version || DEFAULT_CONFIG_VERSION;

  if (!result.transcoder) {
    result.transcoder = { inputs: [], outputs: [] };
  }

  // 1. Map Inputs
  const existingInputs = result.transcoder.inputs || [];
  result.transcoder.inputs = form.inputs.map((inputItem: FormInputItem, index: number) => {
    // Retain any existing input object properties
    const existingInput = existingInputs[index] || {};
    const inputDto: InputItemDto = {
      ...existingInput,
      transport: {
        ...(existingInput.transport || {}),
        uri: inputItem.uri,
      },
    };

    // Clean old transport blocks to prevent mismatch
    if (inputItem.transportType !== 'srt') delete inputDto.transport.srt;
    if (inputItem.transportType !== 'udp') delete inputDto.transport.udp;
    if (inputItem.transportType !== 'hls') delete inputDto.transport.hls;
    if (inputItem.transportType !== 'dash') delete inputDto.transport.dash;
    if (inputItem.transportType !== 'file') delete inputDto.transport.file;
    if (inputItem.transportType !== 'playlist') delete inputDto.transport.playlist;
    if (inputItem.transportType !== 'ndi') delete inputDto.transport.ndi;

    // Attach transport block matching scheme
    switch (inputItem.transportType) {
      case 'srt':
        inputDto.transport.srt = {
          ...(existingInput.transport?.srt || {}),
          mode: inputItem.srtMode,
          latency: inputItem.srtLatency,
        };
        break;
      case 'udp':
        inputDto.transport.udp = {
          ...(existingInput.transport?.udp || {}),
          multicast: inputItem.udpInterface ? { interfaces: [inputItem.udpInterface] } : undefined,
        };
        break;
      case 'hls':
        inputDto.transport.hls = {
          // Keep parser bandwith spelling
          bandwith: 6000000,
          scte: { source: 'segments' },
          ...(existingInput.transport?.hls || {}),
        };
        break;
      case 'dash':
        // The block itself is what routes an http(s) URI to the DASH demuxer, so it is always
        // written, even empty: an MPD served without a .mpd path would otherwise be read as HLS.
        inputDto.transport.dash = {
          ...(existingInput.transport?.dash || {}),
        };
        break;
      case 'file':
        inputDto.transport.file = {
          ...(existingInput.transport?.file || {}),
        };
        break;
      case 'playlist':
        inputDto.transport.playlist = {
          ...(existingInput.transport?.playlist || {}),
        };
        break;
      case 'ndi':
        inputDto.transport.ndi = {
          ...(existingInput.transport?.ndi || {}),
          ndiName: inputItem.name,
        };
        break;
    }

    if (inputItem.programSelector !== undefined) {
      inputDto.selector = {
        ...(existingInput.selector || {}),
        program: inputItem.programSelector,
      };
    }

    return inputDto;
  });

  // 2. Map Outputs
  const existingOutputs = result.transcoder.outputs || [];
  result.transcoder.outputs = form.outputs.map((outItem: FormOutputItem, index: number) => {
    const existingOutput = existingOutputs.find((o) => o.name === outItem.name) || existingOutputs[index] || {};
    const existingVideoStream = (existingOutput.streams?.find((s) => s.type === 'video') || {}) as Partial<VideoStreamDto>;
    const existingAudioStream = (existingOutput.streams?.find((s) => s.type === 'audio') || {}) as Partial<AudioStreamDto>;
    const extraStreams = existingOutput.streams?.filter((s) => s !== existingVideoStream && s !== existingAudioStream) || [];

    const codec = outItem.video.codec as 'h264' | 'h265';
    const existingCodecConfig = (existingVideoStream[codec] as VideoStreamProperties) || {};

    // Construct video stream with string property values per API-CREATE-CHANNEL.md
    const videoStream: VideoStreamDto = {
      ...existingVideoStream,
      type: 'video',
      codec: codec,
      video: {
        ...(existingVideoStream.video || {}),
        width: outItem.video.width,
        height: outItem.video.height,
        rate: outItem.video.frameRate,
        format: 'I420',
      },
      [codec]: {
        ...existingCodecConfig,
        encoder: codec === 'h264' ? 'x264enc' : 'x265enc',
        properties: {
          ...(existingCodecConfig.properties || {}),
          'key-int-max': String(outItem.video.keyIntMax),
          tune: outItem.video.tune,
          'speed-preset': outItem.video.speedPreset,
          bitrate: String(outItem.video.bitrateKbps),
          // Rate-control: omit for the engine default (cbr) to keep the config minimal.
          ...(codec === 'h264' && outItem.video.rateControlMode !== 'cbr'
            ? { pass: outItem.video.rateControlMode }
            : {}),
          // B-frames: omit when 0 — tune:zerolatency already implies it.
          ...(codec === 'h264' && outItem.video.bFrames > 0
            ? { bframes: String(outItem.video.bFrames) }
            : {}),
        },
        caps: {
          ...(existingCodecConfig.caps || {}),
          profile: outItem.video.profile,
        },
      },
    };

    // Clean old codec blocks if changed
    if (codec === 'h264') delete videoStream['h265'];
    if (codec === 'h265') delete videoStream['h264'];

    // Progressive is the engine default, so it is written by leaving both keys out.
    if (outItem.video.frameFormat === 'tff' || outItem.video.frameFormat === 'bff') {
      videoStream.video = { ...videoStream.video, interlace: 'interleaved', fieldOrder: outItem.video.frameFormat };
    } else if (videoStream.video) {
      delete videoStream.video.interlace;
      delete videoStream.video.fieldOrder;
    }

    // Construct audio stream
    const audioStream: AudioStreamDto = {
      ...existingAudioStream,
      type: 'audio',
      codec: outItem.audio.codec as 'aac' | 'mp2' | 'ac3' | 'passthrough',
      audio: {
        ...(existingAudioStream.audio || {}),
        rate: outItem.audio.sampleRateHz,
        channels: outItem.audio.channels,
      },
    };

    // The bitrate only takes effect as a property of a named encoder (see audioEncoderFor).
    // Properties written for a different encoder are dropped rather than carried across.
    {
      const audioCodec = outItem.audio.codec;
      for (const other of ['aac', 'mp2', 'ac3'] as const) {
        if (other !== audioCodec) delete audioStream[other];
      }

      const existingAudioConfig = (existingAudioStream[audioCodec] as VideoStreamProperties | undefined) || undefined;
      const choice = audioEncoderFor(audioCodec, outItem.audio.sampleRateHz, outItem.audio.bitrateKbps, existingAudioConfig?.encoder);
      if (choice) {
        const sameEncoder = existingAudioConfig?.encoder === choice.encoder;
        audioStream[audioCodec] = {
          ...(sameEncoder ? existingAudioConfig : {}),
          encoder: choice.encoder,
          properties: {
            ...(sameEncoder ? existingAudioConfig?.properties || {} : {}),
            bitrate: choice.bitrate,
          },
        };
      }
    }

    // Output transports block
    const existingPrimaryTransport = existingOutput.transports?.[0] || {};
    const extraTransports = existingOutput.transports?.slice(1) || [];

    const primaryTransport = {
      ...existingPrimaryTransport,
      uri: outItem.destinationUri,
      ...(outItem.outputType === 'hls'
        ? {
            hls: {
              numSegments: 6,
              segmentLength: 4,
              singleDirectory: true,
              vodMode: false,
              adMarkers: ['enhanced'],
              ...(existingPrimaryTransport.hls || {}),
            },
          }
        : {}),
      ...(outItem.outputType === 'dash'
        ? {
            dash: {
              segmentDuration: 2000,
              numSegments: 30,
              ...(existingPrimaryTransport.dash || {}),
            },
          }
        : {}),
      ...(outItem.outputType === 'srt'
        ? {
            srt: {
              mode: 'caller' as const,
              latency: 120,
              ...(existingPrimaryTransport.srt || {}),
              // The form's choice wins over the carried-over block: the engine reads this
              // block, not the `mode=` query in the URI, so a stale 'caller' here makes a
              // listener URI dial out instead of binding.
              ...(outItem.srtMode ? { mode: outItem.srtMode } : {}),
              ...(outItem.srtLatency ? { latency: outItem.srtLatency } : {}),
            },
          }
        : {}),
    };

    // Clean old protocol blocks if type changed
    if (outItem.outputType !== 'hls') delete primaryTransport.hls;
    if (outItem.outputType !== 'dash') delete primaryTransport.dash;
    if (outItem.outputType !== 'srt') delete primaryTransport.srt;

    const outputTransports = [primaryTransport, ...extraTransports];

    const outputDto: OutputItemDto = {
      ...existingOutput,
      name: outItem.name,
      type: 'single',
      container: outItem.container || 'mpegts',
      transports: outputTransports,
      streams: [videoStream, audioStream, ...extraStreams],
      scte: outItem.scte.enabled
        ? {
            mode: outItem.scte.mode,
            pid: outItem.scte.pid,
            nullInterval: outItem.scte.nullInterval,
          }
        : undefined,
    };

    return outputDto;
  });

  return result;
}
