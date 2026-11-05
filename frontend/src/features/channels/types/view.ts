/**
 * View Models and Probe Response Types.
 *
 * The probe shapes below mirror the `InputProbe` family in
 * `api/src/transcoder_api/model/rest_api.py` exactly. Optionality is copied from the pydantic
 * models rather than assumed: on a live MPEG-TS most per-stream numbers are genuinely absent -
 * ffprobe reports no per-stream `bit_rate`, and `pid` exists only when the container is
 * MPEG-TS - so anything typed non-optional here would be a lie the UI then renders as
 * "undefined".
 */

export type InputScanType = 'progressive' | 'interlaced' | 'unknown';

export type InputTransportType = 'srt' | 'udp' | 'hls' | 'dash' | 'file' | 'playlist' | 'ndi' | 'rtmp' | 'mxl';

export interface ProbeVideoStream {
  index: number;
  codec: string;
  codecLongName?: string | null;
  profile?: string | null;
  width: number;
  height: number;
  displayAspectRatio?: string | null;
  pixelFormat?: string | null;
  /** From avg_frame_rate, falling back to r_frame_rate. Null if unusable. */
  frameRate?: number | null;
  /** The unrounded ffprobe fraction, for example 30000/1001. */
  frameRateExact?: string | null;
  scanType: InputScanType;
  /** Raw ffprobe field_order - progressive, tt, bb, tb or bt. */
  fieldOrder?: string | null;
  /** Per-stream bit_rate. Usually absent on a live MPEG-TS. */
  bitrateBps?: number | null;
  /** MPEG-TS packet identifier. Only when the container is MPEG-TS. */
  pid?: number | null;
}

export interface ProbeAudioStream {
  index: number;
  codec: string;
  codecLongName?: string | null;
  profile?: string | null;
  channels: number;
  channelLayout?: string | null;
  sampleRateHz?: number | null;
  bitrateBps?: number | null;
  /** ISO 639 code from tags.language. Omitted when absent or und. */
  language?: string | null;
  isDefault?: boolean | null;
  pid?: number | null;
}

export interface ProbeDataStream {
  index: number;
  /** For example scte_35 for a splice information PID, or bin_data. */
  codec: string;
  pid?: number | null;
}

export interface ProbeProgram {
  programNumber: number;
  programId?: number | null;
  serviceName?: string | null;
  serviceProvider?: string | null;
  pmtPid?: number | null;
  pcrPid?: number | null;
  streamIndexes: number[];
}

export interface ProbeResponse {
  /** Exactly what the client sent. */
  uri: string;
  transport: InputTransportType;
  /** The URI ffprobe was given. For playlist, the first media entry. */
  probedUri: string;
  /** ffprobe format.format_name, for example mpegts. */
  container: string;
  /** True for srt, udp and hls; false for file and playlist. */
  isLive?: boolean | null;
  /** Null for live transports. */
  durationSeconds?: number | null;
  /** format.bit_rate. On a live MPEG-TS this is the mux rate. */
  bitrateBps?: number | null;
  /** True when the probe bound an SRT listener socket and waited for a caller. */
  waitedForCaller?: boolean | null;
  video?: ProbeVideoStream | null;
  audio: ProbeAudioStream[];
  data: ProbeDataStream[];
  /** Empty for any container that is not MPEG-TS. */
  programs: ProbeProgram[];
  /** Non-fatal observations about the probe. */
  warnings: string[];
}

export type SupervisorState = 'RUNNING' | 'STARTING' | 'STOPPED' | 'BACKOFF' | 'FATAL' | 'UNKNOWN';

export interface ChannelStatusViewModel {
  supervisorState: SupervisorState;
  isConfigured: boolean;
  configPath?: string;
  channelName?: string;
  inputCount: number;
  outputCount: number;
  restartRequired: boolean;
}
