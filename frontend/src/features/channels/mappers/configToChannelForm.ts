/**
 * Maps Engine Config DTO to Channel Form Model
 * Reference: API-CREATE-CHANNEL.md Section 05
 */

import { ChannelFormValues, FormInputItem, FormOutputItem, FrameFormat, RateControlMode, InputTransportType } from '../types/form';
import { EngineConfigDto, VideoStreamDto, AudioStreamDto, VideoStreamProperties } from '../types/api';
import { audioBitrateKbpsFrom } from '../constants/schema';

/** Mirrors the engine: an http(s) input whose path ends in .mpd is DASH even without a dash block. */
function isDashManifestUri(uri: string): boolean {
  try {
    return new URL(uri).pathname.toLowerCase().endsWith('.mpd');
  } catch {
    return false;
  }
}

export function configToChannelForm(config: EngineConfigDto): ChannelFormValues {
  const inputs: FormInputItem[] = (config.transcoder?.inputs || []).map((inDto, idx) => {
    const uri = inDto.transport?.uri || '';
    let transportType: InputTransportType = 'srt';
    if (uri.startsWith('udp://')) transportType = 'udp';
    else if ((uri.startsWith('http://') || uri.startsWith('https://')) && (inDto.transport?.dash || isDashManifestUri(uri))) transportType = 'dash';
    else if (uri.startsWith('hls+file:') || uri.startsWith('http://') || uri.startsWith('https://')) transportType = 'hls';
    else if (uri.startsWith('pls+file:')) transportType = 'playlist';
    else if (uri.startsWith('file://')) transportType = 'file';
    else if (uri.startsWith('ndi://')) transportType = 'ndi';
    else if (uri.startsWith('rtmp://') || uri.startsWith('rtmps://')) transportType = 'rtmp';
    else if (uri.startsWith('mxl://')) transportType = 'mxl';

    const srtMode = inDto.transport?.srt?.mode || (uri.includes('mode=listener') ? 'listener' : 'caller');
    const srtLatency = inDto.transport?.srt?.latency || 120;
    const udpInterface = inDto.transport?.udp?.multicast?.interfaces?.[0];

    return {
      id: `in-${idx + 1}`,
      name: `Input ${idx + 1}`,
      transportType,
      uri,
      srtMode,
      srtLatency,
      udpInterface,
      programSelector: inDto.selector?.program,
      mxlVideoFlowId: inDto.transport?.mxl?.video?.flowId,
      mxlAudioFlowId: inDto.transport?.mxl?.audio?.flowId,
    };
  });

  // An output of type 'mxl' (the programme tap) has no form fields; it is kept in the config, not shown here.
  const outputs: FormOutputItem[] = (config.transcoder?.outputs || []).filter((outDto) => outDto.type !== 'mxl').map((outDto, idx) => {
    const videoStream = outDto.streams?.find((s) => s.type === 'video') as VideoStreamDto | undefined;
    const audioStream = outDto.streams?.find((s) => s.type === 'audio') as AudioStreamDto | undefined;
    const transport = outDto.transports?.[0];
    const uri = transport?.uri || '';

    let outputType: 'hls' | 'dash' | 'srt' | 'udp' | 'file' = 'hls';
    if (uri.startsWith('dash+')) outputType = 'dash';
    else if (uri.startsWith('srt://')) outputType = 'srt';
    else if (uri.startsWith('udp://')) outputType = 'udp';
    else if (uri.startsWith('file://')) outputType = 'file';

    const h264Props = videoStream?.h264?.properties || {};
    const bitrateKbps = parseInt(h264Props['bitrate'] || '6000', 10);
    const keyIntMax = parseInt(h264Props['key-int-max'] || '50', 10);
    const bFrames = parseInt(h264Props['bframes'] ?? '0', 10);
    const rateControlMode: RateControlMode =
      (['cbr', 'qual', 'quant'] as RateControlMode[]).includes(h264Props['pass'] as RateControlMode)
        ? (h264Props['pass'] as RateControlMode)
        : 'cbr';

    const frameFormat: FrameFormat = videoStream?.video?.interlace === 'interleaved'
      ? (videoStream.video.fieldOrder === 'bff' ? 'bff' : 'tff')
      : 'progressive';

    const audioCodec = (audioStream?.codec as 'aac' | 'mp2' | 'ac3') || 'aac';
    const audioConfig = audioStream?.[audioCodec] as VideoStreamProperties | undefined;

    return {
      id: `out-${idx + 1}`,
      name: outDto.name || `output-${idx + 1}`,
      container: (outDto.container as 'mpegts' | 'fmp4') || 'mpegts',
      outputType,
      destinationUri: uri,
      ...(outputType === 'srt'
        ? {
            srtMode:
              transport?.srt?.mode || (uri.includes('mode=listener') ? 'listener' : 'caller'),
            srtLatency: transport?.srt?.latency || 120,
          }
        : {}),
      video: {
        codec: (videoStream?.codec as 'h264' | 'h265') || 'h264',
        width: videoStream?.video?.width || 1920,
        height: videoStream?.video?.height || 1080,
        frameRate: videoStream?.video?.rate || 25,
        bitrateKbps: isNaN(bitrateKbps) ? 6000 : bitrateKbps,
        profile: (videoStream?.h264?.caps?.profile as 'baseline' | 'main' | 'high') || 'high',
        speedPreset: h264Props['speed-preset'] || 'veryfast',
        tune: h264Props['tune'] || 'zerolatency',
        keyIntMax: isNaN(keyIntMax) ? 50 : keyIntMax,
        frameFormat,
        rateControlMode,
        bFrames: isNaN(bFrames) ? 0 : bFrames,
      },
      audio: {
        codec: audioCodec,
        sampleRateHz: audioStream?.audio?.rate || 48000,
        bitrateKbps: audioBitrateKbpsFrom(audioConfig?.properties?.['bitrate']) ?? 128,
        channels: audioStream?.audio?.channels || 2,
      },
      scte: {
        enabled: !!outDto.scte,
        mode: outDto.scte?.mode || 'passthrough',
        pid: outDto.scte?.pid || 4269,
        nullInterval: outDto.scte?.nullInterval || 0,
      },
    };
  });

  let readOnlyReason: string | undefined;

  // Check input complexity
  if (config.transcoder?.inputs && config.transcoder.inputs.length > 1) {
    readOnlyReason = "Configuration contains multiple inputs. This UI only supports modifying single-input channels.";
  }

  return {
    channelName: 'Live Channel',
    templatePreset: 'custom',
    inputs: inputs.length > 0 ? inputs : [
      {
        id: 'in-1',
        name: 'Primary Ingest',
        transportType: 'srt',
        uri: 'srt://103.248.72.80:8000?mode=caller',
        srtMode: 'caller',
        srtLatency: 120,
      },
    ],
    outputs: outputs.length > 0 ? outputs : [
      {
        id: 'out-1',
        name: 'hls-output',
        container: 'mpegts',
        outputType: 'hls',
        destinationUri: 'hls+file:/runtime/hls',
        video: {
          codec: 'h264',
          width: 1920,
          height: 1080,
          frameRate: 25,
          bitrateKbps: 6000,
          profile: 'high',
          speedPreset: 'veryfast',
          tune: 'zerolatency',
          keyIntMax: 50,
          frameFormat: 'progressive',
          rateControlMode: 'cbr',
          bFrames: 0,
        },
        audio: {
          codec: 'aac',
          sampleRateHz: 48000,
          bitrateKbps: 128,
          channels: 2,
        },
        scte: {
          enabled: true,
          mode: 'passthrough',
          pid: 4269,
          nullInterval: 0,
        },
      },
    ],
    readOnlyReason,
  };
}
