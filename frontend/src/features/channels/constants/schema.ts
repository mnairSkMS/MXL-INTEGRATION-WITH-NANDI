/**
 * Engine configuration schema version.
 *
 * Authority is `transcoder/app/src/application/config/parser/json/parser.cpp`, which accepts
 * VERSION_MIN 1.0.0 through VERSION_MAX 1.4.2 and refuses to parse anything outside that
 * range. The value is not cosmetic - it selects the parser dialect:
 *
 *   < 1.1.0  different stream/output handling
 *   < 1.4.0  `outputs[].container` is mandatory
 *
 * A new configuration is therefore written at the newest version this engine supports, which
 * is also what config/config.json in this repository carries. An existing document keeps
 * whatever version it already has - upgrading a live config's dialect is not this screen's
 * decision to make.
 */
export const DEFAULT_CONFIG_VERSION = '1.4.2';

/** Below this, `outputs[].container` is mandatory. See CONFIG-REFERENCE.md. */
export const CONTAINER_REQUIRED_BELOW = '1.4.0';

/**
 * Audio bitrate reaches the engine only as a property of a named encoder: the parser reads
 * `audio.bitrate` but nothing uses it, and codec-block `properties` apply only when `encoder`
 * is set. The encoders below are the ones the engine picks for each codec in utpl/devel.
 *
 * AC-3 at 48 kHz goes through the Dolby processor (DolbyTranscodingProcessor::isCompatible),
 * whose `dlbpatenc` takes an enum of fixed rates written as "<kbps>k". Any other AC-3 rate falls
 * back to the generic flowline, where `avenc_ac3` takes bits/s like the others.
 */
export const DOLBY_SAMPLE_RATE_HZ = 48000;
// 640 is labelled DD+ only in gst-inspect, but dlbpatenc produces 640 kbps AC-3 from it.
export const DOLBY_AC3_BITRATES_KBPS = [32, 56, 64, 96, 128, 192, 256, 384, 448, 640] as const;

export type AudioEncoderChoice = { encoder: string; bitrate: string };

export function audioEncoderFor(codec: string, sampleRateHz: number, bitrateKbps: number, existingEncoder?: string): AudioEncoderChoice | null {
  switch (codec) {
    case 'aac':
      return { encoder: existingEncoder || 'fdkaacenc', bitrate: String(bitrateKbps * 1000) };
    case 'mp2':
      return { encoder: existingEncoder || 'avenc_mp2', bitrate: String(bitrateKbps * 1000) };
    case 'ac3':
      return sampleRateHz === DOLBY_SAMPLE_RATE_HZ
        ? { encoder: 'dlbpatenc', bitrate: `${bitrateKbps}k` }
        : { encoder: 'avenc_ac3', bitrate: String(bitrateKbps * 1000) };
    default:
      return null;
  }
}

/** An MXL input plays its flows this long after they arrive unless told otherwise, and at most this long (the engine refuses more). */
export const MXL_DEFAULT_DELAY_MS = 500;
export const MXL_MAX_DELAY_MS = 2000;

/** Reads back what audioEncoderFor wrote: "<kbps>k" for dlbpatenc, bits/s otherwise. */
export function audioBitrateKbpsFrom(bitrate: string | undefined): number | undefined {
  if (!bitrate) return undefined;
  const value = parseInt(bitrate, 10);
  if (isNaN(value) || value <= 0) return undefined;
  return bitrate.trim().endsWith('k') ? value : Math.round(value / 1000);
}
