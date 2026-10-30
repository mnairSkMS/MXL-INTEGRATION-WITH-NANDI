/**
 * Standard Presets for Skandha Transcoder Channel Creation
 * Mirrors config/config.json and config/srt_to_hls_srt.json
 */
import { ChannelFormValues } from '../types/form';

export const CHANNEL_PRESETS: Record<string, { label: string; description: string; values: ChannelFormValues }> = {
  'srt-to-hls': {
    label: 'SRT Caller → HLS (MPEG-TS)',
    description: 'Live broadcast intake via SRT Caller, transcoded to rolling HLS (MPEG-TS) with SCTE-35 ad markers.',
    values: {
      channelName: 'Live-Channel-HLS',
      templatePreset: 'srt-to-hls',
      inputs: [
        {
          id: 'in-1',
          name: 'Primary SRT Caller',
          transportType: 'srt',
          uri: 'srt://103.248.72.80:8000?mode=caller',
          srtMode: 'caller',
          srtLatency: 120,
        },
      ],
      outputs: [
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
    },
  },

  'srt-to-udp': {
    label: 'SRT Listener → UDP/TS',
    description: 'Binds local SRT listener for incoming contribution, outputs multicast/unicast UDP MPEG-TS.',
    values: {
      channelName: 'Live-Channel-UDP',
      templatePreset: 'srt-to-udp',
      inputs: [
        {
          id: 'in-1',
          name: 'Local SRT Listener',
          transportType: 'srt',
          uri: 'srt://0.0.0.0:8000?mode=listener',
          srtMode: 'listener',
          srtLatency: 120,
        },
      ],
      outputs: [
        {
          id: 'out-1',
          name: 'udp-ts-output',
          container: 'mpegts',
          outputType: 'udp',
          destinationUri: 'udp://239.255.0.1:5000',
          video: {
            codec: 'h264',
            width: 1920,
            height: 1080,
            frameRate: 25,
            bitrateKbps: 8000,
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
            bitrateKbps: 192,
            channels: 2,
          },
          scte: {
            enabled: true,
            mode: 'passthrough',
            pid: 500,
            nullInterval: 0,
          },
        },
      ],
    },
  },

  'srt-to-srt': {
    label: 'SRT Caller → SRT Pass-through Relay',
    description: 'Direct point-to-point SRT transcode and relay with low latency.',
    values: {
      channelName: 'SRT-Relay-Channel',
      templatePreset: 'srt-to-srt',
      inputs: [
        {
          id: 'in-1',
          name: 'SRT Ingest Source',
          transportType: 'srt',
          uri: 'srt://source.broadcast.net:9000?mode=caller',
          srtMode: 'caller',
          srtLatency: 120,
        },
      ],
      outputs: [
        {
          id: 'out-1',
          name: 'srt-destination',
          container: 'mpegts',
          outputType: 'srt',
          destinationUri: 'srt://dest.broadcast.net:9001?mode=caller',
          video: {
            codec: 'h264',
            width: 1920,
            height: 1080,
            frameRate: 25,
            bitrateKbps: 5000,
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
            enabled: false,
            mode: 'removal',
            pid: 500,
            nullInterval: 0,
          },
        },
      ],
    },
  },
};
