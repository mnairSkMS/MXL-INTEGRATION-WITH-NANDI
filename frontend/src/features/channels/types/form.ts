/**
 * Form Model for Create Channel
 * Optimized for React Hook Form, user inputs, and Figma screen representations.
 */

// Re-exported rather than redeclared: the transports this form offers must be exactly the
// ones `InputTransportType` in the API accepts, and two independent copies of the union drift
// the moment the engine gains or loses one.
export type { InputTransportType } from './view';
import type { InputTransportType } from './view';

export interface FormInputItem {
  id: string;
  name: string;
  transportType: InputTransportType;
  uri: string;
  srtMode: 'caller' | 'listener';
  srtLatency: number;
  udpInterface?: string;
  programSelector?: number;
}

export interface FormVideoLadder {
  codec: 'h264' | 'h265';
  width: number;
  height: number;
  frameRate: number;
  bitrateKbps: number;
  profile: 'baseline' | 'main' | 'high';
  speedPreset: string;
  tune: string;
  keyIntMax: number;
  /** Scan type. Interlaced is supported for H.264 only; see constants/schema.ts. */
  frameFormat: FrameFormat;
  /**
   * Rate-control mode for x264enc `pass` property.
   * cbr = bitrate ceiling (default for live), qual = CRF constant quality.
   * H.264 only; ignored for H.265 (use option-string instead).
   */
  rateControlMode: RateControlMode;
  /**
   * Number of B-frames between reference frames (x264enc `bframes` property).
   * 0 = no B-frames (lowest latency). tune:zerolatency already implies 0;
   * an explicit value here overrides the tune setting.
   */
  bFrames: number;
}

export type FrameFormat = 'progressive' | 'tff' | 'bff';

/**
 * Maps to x264enc `pass` property (H.264 only).
 * cbr  = bitrate ceiling — default for live streaming.
 * qual = constant quality / CRF (set `quantizer` via option-string for target quality).
 * quant = fixed QP, no bitrate bound — not recommended for live.
 */
export type RateControlMode = 'cbr' | 'qual' | 'quant';


export interface FormAudioLadder {
  codec: 'aac' | 'mp2' | 'ac3';
  sampleRateHz: number;
  bitrateKbps: number;
  channels: number;
}

export interface FormScteConfig {
  enabled: boolean;
  mode: 'passthrough' | 'removal';
  pid: number;
  nullInterval: number;
}

export interface FormOutputItem {
  id: string;
  name: string;
  container: 'mpegts' | 'fmp4';
  outputType: 'hls' | 'dash' | 'srt' | 'udp' | 'file';
  destinationUri: string;
  /** SRT outputs only. Absent on other types; the mapper then keeps whatever the config has. */
  srtMode?: 'caller' | 'listener';
  srtLatency?: number;
  // Video parameters
  video: FormVideoLadder;
  // Audio parameters
  audio: FormAudioLadder;
  // SCTE-35 parameters
  scte: FormScteConfig;
}

export interface ChannelFormValues {
  channelName: string;
  templatePreset: string;
  inputs: FormInputItem[];
  outputs: FormOutputItem[];
  readOnlyReason?: string;
}
