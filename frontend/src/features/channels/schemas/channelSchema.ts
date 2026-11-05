import { z } from 'zod';
import { DOLBY_AC3_BITRATES_KBPS, DOLBY_SAMPLE_RATE_HZ } from '../constants/schema';

const FLOW_ID = /^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/;

// The fields of an input. `inputItemSchema` is this plus the checks that look at more than one field.
export const inputItemObject = z.object({
  id: z.string(),
  name: z.string().min(1, 'Input name is required'),
  transportType: z.enum(['srt', 'udp', 'hls', 'dash', 'playlist', 'file', 'ndi', 'rtmp', 'mxl']),
  uri: z.string().min(1, 'Source URI is required').refine((uri) => {
    // Scheme check
    return uri.includes('://') || uri.startsWith('hls+file:') || uri.startsWith('pls+file:');
  }, 'URI must include a valid protocol scheme (e.g. srt://, udp://, hls+file:)'),
  srtMode: z.enum(['caller', 'listener']),
  srtLatency: z.number().min(20, 'Latency must be at least 20ms'),
  udpInterface: z.string().optional(),
  programSelector: z.number().optional(),
  mxlVideoFlowId: z.string().optional(),
  mxlAudioFlowId: z.string().optional(),
});

export const inputItemSchema = inputItemObject.superRefine((input, ctx) => {
  if (input.transportType !== 'mxl') return;

  // The engine refuses these at start; saying so here keeps the operator from finding out then.
  if (!input.uri.startsWith('mxl:///') || input.uri.replace(/^mxl:\/+/, '').length === 0) {
    ctx.addIssue({ code: 'custom', path: ['uri'], message: 'An MXL input is a domain directory: mxl:///run/mxl' });
  }

  const video = (input.mxlVideoFlowId || '').trim();
  const audio = (input.mxlAudioFlowId || '').trim();

  if (!video && !audio) {
    ctx.addIssue({ code: 'custom', path: ['mxlVideoFlowId'], message: 'Name a video flow or an audio flow' });
  }
  if (video && !FLOW_ID.test(video)) {
    ctx.addIssue({ code: 'custom', path: ['mxlVideoFlowId'], message: 'A flow id is a UUID' });
  }
  if (audio && !FLOW_ID.test(audio)) {
    ctx.addIssue({ code: 'custom', path: ['mxlAudioFlowId'], message: 'A flow id is a UUID' });
  }
  if (video && video === audio) {
    ctx.addIssue({ code: 'custom', path: ['mxlAudioFlowId'], message: 'Video and audio need different flows' });
  }
});

export const videoLadderSchema = z.object({
  codec: z.enum(['h264', 'h265']),
  width: z.number().min(160).max(3840),
  height: z.number().min(120).max(2160),
  frameRate: z.number().min(1).max(120),
  bitrateKbps: z.number().min(100).max(50000),
  profile: z.enum(['baseline', 'main', 'high']),
  speedPreset: z.string().min(1),
  tune: z.string().min(1),
  keyIntMax: z.number().min(1).max(300),
  frameFormat: z.enum(['progressive', 'tff', 'bff']),
  /** x264enc `pass` property — H.264 only. */
  rateControlMode: z.enum(['cbr', 'qual', 'quant']),
  /** x264enc `bframes` property: 0 = disabled. */
  bFrames: z.number().int().min(0).max(16),
});


export const audioLadderSchema = z.object({
  codec: z.enum(['aac', 'mp2', 'ac3']),
  sampleRateHz: z.number().refine((val) => [32000, 44100, 48000].includes(val), 'Supported rates: 32000, 44100, 48000'),
  bitrateKbps: z.number().min(32).max(640),
  channels: z.number().refine((val) => [1, 2, 6].includes(val), 'Channels must be 1 (mono), 2 (stereo), or 6 (5.1)'),
});

export const scteConfigSchema = z.object({
  enabled: z.boolean(),
  mode: z.enum(['passthrough', 'removal']),
  pid: z.number().min(0).max(8192),
  nullInterval: z.number().min(0),
});

export const outputItemSchema = z.object({
  id: z.string(),
  name: z.string().min(1, 'Output slug name is required').regex(/^[a-zA-Z0-9_-]+$/, 'Output name must be alphanumeric with dashes or underscores'),
  container: z.enum(['mpegts', 'fmp4']),
  outputType: z.enum(['hls', 'dash', 'srt', 'udp', 'file']),
  destinationUri: z.string().min(1, 'Destination URI is required'),
  srtMode: z.enum(['caller', 'listener']).optional(),
  srtLatency: z.number().min(20, 'Latency must be at least 20ms').optional(),
  video: videoLadderSchema,
  audio: audioLadderSchema,
  scte: scteConfigSchema,
}).superRefine((output, ctx) => {
  // The engine refuses a DASH output that is not CMAF (fmp4) or whose audio is not AAC.
  if (output.outputType === 'dash') {
    if (output.container !== 'fmp4') {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        path: ['container'],
        message: 'DASH output is CMAF: the container must be Fragmented MP4',
      });
    }

    if (output.audio.codec !== 'aac') {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        path: ['audio', 'codec'],
        message: 'DASH output supports AAC audio only',
      });
    }
  }

  // The engine refuses interleaved H.265 at startup: x265enc does not field-code.
  if (output.video.frameFormat !== 'progressive' && output.video.codec !== 'h264') {
    ctx.addIssue({
      code: z.ZodIssueCode.custom,
      path: ['video', 'frameFormat'],
      message: 'Interlaced output is supported for H.264 only',
    });
  }

  // Dolby AC-3 accepts only its fixed rates; any other value is dropped by the encoder.
  if (
    output.audio.codec === 'ac3' &&
    output.audio.sampleRateHz === DOLBY_SAMPLE_RATE_HZ &&
    !(DOLBY_AC3_BITRATES_KBPS as readonly number[]).includes(output.audio.bitrateKbps)
  ) {
    ctx.addIssue({
      code: z.ZodIssueCode.custom,
      path: ['audio', 'bitrateKbps'],
      message: `AC-3 bitrate must be one of ${DOLBY_AC3_BITRATES_KBPS.join(', ')} kbps`,
    });
  }
});

export const channelFormSchema = z.object({
  channelName: z.string().min(1, 'Channel name is required'),
  templatePreset: z.string().optional(),
  inputs: z.array(inputItemSchema).min(1, 'At least one input source is required'),
  outputs: z.array(outputItemSchema).min(1, 'At least one output ladder is required'),
});

export type ChannelFormSchemaType = z.infer<typeof channelFormSchema>;
