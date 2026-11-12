/**
 * Engine Configuration DTO (API level).
 * Mirrors the document the JSON parser reads; see CONFIG-REFERENCE.md.
 */

export interface SrtTransportConfig {
  mode: 'caller' | 'listener';
  latency: number;
  bufferSize?: number;
}

export interface UdpTransportConfig {
  multicast?: {
    interfaces?: string[];
  };
}

export interface HlsTransportConfig {
  bandwith?: number; // Note: intentional misspelling per engine parser
  numSegments?: number;
  segmentLength?: number;
  singleDirectory?: boolean;
  vodMode?: boolean;
  adMarkers?: string[];
  credentials?: {
    username?: string;
    password?: string;
  };
  scte?: {
    source?: 'segments' | 'manifest';
  };
}

/** MPEG-DASH input; see DashInputTransportParameters in the engine schema. */
export interface DashTransportConfig {
  /** Bits per second: the highest representation not above it is used for the whole session. */
  bandwidth?: number;
  /** Milliseconds behind the live edge when the MPD sets no suggestedPresentationDelay. */
  presentationDelay?: number;
  scte?: {
    source?: 'segments';
  };
}

/** CMAF DASH output block (dash+file: / dash+http(s): URIs). Durations in milliseconds. */
export interface DashOutputTransportConfig {
  segmentDuration?: number;
  numSegments?: number;
  vodMode?: boolean;
  presentationDelay?: number;
  minBufferTime?: number;
  minimumUpdatePeriod?: number;
  utcTimingUrl?: string;
  baseFileName?: string;
  [key: string]: unknown;
}

export interface InputTransportDto {
  uri: string;
  srt?: SrtTransportConfig;
  udp?: UdpTransportConfig;
  hls?: HlsTransportConfig;
  dash?: DashTransportConfig;
  file?: Record<string, unknown>;
  playlist?: Record<string, unknown>;
  ndi?: {
    ndiName?: string;
    urlAddress?: string;
  };
  /** An input whose uri is an MXL domain (`mxl:///run/mxl`) names its flows here. */
  mxl?: {
    video?: { flowId: string };
    audio?: { flowId: string };
    /** A data flow carrying SCTE-35, written by a Nandi programme output's `mxl.data`. */
    data?: { flowId: string };
    /** Milliseconds the channel plays the flows after they arrive, for the jitter of their writer. Engine default 300. */
    delayMs?: number;
  };
}

export interface InputItemDto {
  transport: InputTransportDto;
  selector?: {
    program?: number;
  };
  // Allow unknown properties for preservation
  [key: string]: unknown;
}

export interface VideoStreamProperties {
  encoder: string;
  properties?: Record<string, string>; // Note: values must be strings per API-CREATE-CHANNEL.md
  caps?: Record<string, string>;
}

export interface VideoStreamDto {
  type: 'video';
  codec: 'h264' | 'h265' | 'passthrough';
  video?: {
    width?: number;
    height?: number;
    rate?: number;
    format?: string;
    interlace?: 'progressive' | 'interleaved';
    fieldOrder?: 'tff' | 'bff';
  };
  h264?: VideoStreamProperties;
  h265?: VideoStreamProperties;
  [key: string]: unknown;
}

export interface AudioStreamDto {
  type: 'audio';
  codec: 'aac' | 'mp2' | 'ac3' | 'passthrough';
  audio?: {
    rate?: number;
    channels?: number;
  };
  aac?: VideoStreamProperties;
  mp2?: VideoStreamProperties;
  ac3?: VideoStreamProperties;
  [key: string]: unknown;
}

export interface UnknownStreamDto {
  type: string;
  [key: string]: unknown;
}

export type StreamDto = VideoStreamDto | AudioStreamDto | UnknownStreamDto;

export interface OutputItemDto {
  name: string;
  /** 'mxl' publishes the programme to MXL flows; the channel form has no fields for it (see channelFormToConfig). */
  type: 'single' | 'group' | 'mxl';
  /** Mandatory below schema 1.4.0; always emitted, which is valid at every version. */
  container: 'mpegts' | 'es' | 'fmp4';
  transports: Array<{
    uri: string;
    hls?: HlsTransportConfig;
    dash?: DashOutputTransportConfig;
    srt?: SrtTransportConfig;
    udp?: UdpTransportConfig;
    file?: Record<string, unknown>;
    [key: string]: unknown;
  }>;
  streams: StreamDto[];
  scte?: {
    mode: 'passthrough' | 'removal';
    pid: number;
    nullInterval?: number;
  };
  [key: string]: unknown;
}

export interface TranscoderDto {
  inputs: InputItemDto[];
  outputs: OutputItemDto[];
  [key: string]: unknown;
}

export interface ApplicationDto {
  logger?: {
    level?: string;
    console?: boolean;
    file?: boolean;
    path?: string;
  };
  gstreamer?: {
    plugins?: Array<{
      name: string;
      rank: number;
    }>;
  };
  licenseServer?: {
    uri?: string;
  };
  [key: string]: unknown;
}

export interface EngineConfigDto {
  /** MAJOR.MINOR.PATCH. This engine's parser accepts 1.0.0 through 1.4.2. */
  version: string;
  application?: ApplicationDto;
  transcoder: TranscoderDto;
  // Deep-preserves any root-level metadata or comments
  [key: string]: unknown;
}
