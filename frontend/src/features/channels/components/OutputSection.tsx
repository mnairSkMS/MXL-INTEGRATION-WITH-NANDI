import React from 'react';
import { UseFormReturn, useFieldArray } from 'react-hook-form';
import { ChannelFormValues } from '../types/form';
import { FormSection } from '../../../components/FormSection/FormSection';
import { Input } from '../../../components/Input/Input';
import { Select } from '../../../components/Select/Select';
import { Button } from '../../../components/Button/Button';
import { Plus, Trash2, Sliders, Layers } from 'lucide-react';

/** Euclidean GCD — used to reduce aspect ratio to its simplest form. */
function gcd(a: number, b: number): number {
  return b === 0 ? a : gcd(b, a % b);
}

/** Returns a human-readable aspect ratio string like "16:9", or "–" for invalid inputs. */
function aspectRatioLabel(width: number, height: number): string {
  if (!width || !height || width <= 0 || height <= 0) return '–';
  const d = gcd(width, height);
  return `${width / d}:${height / d}`;
}

const RESOLUTION_PRESETS = [
  { label: 'Custom', value: '' },
  { label: '3840 × 2160 — 4K UHD', value: '3840x2160' },
  { label: '1920 × 1080 — 1080p FHD', value: '1920x1080' },
  { label: '1280 × 720 — 720p HD', value: '1280x720' },
  { label: '1024 × 576 — 576p PAL wide', value: '1024x576' },
  { label: '720 × 576 — 576i PAL', value: '720x576' },
  { label: '720 × 480 — 480i NTSC', value: '720x480' },
  { label: '640 × 360 — 360p', value: '640x360' },
];

export interface OutputSectionProps {
  form: UseFormReturn<ChannelFormValues>;
}

export const OutputSection: React.FC<OutputSectionProps> = ({ form }) => {
  const { register, watch, setValue, formState: { errors } } = form;
  const { fields, append, remove } = useFieldArray({
    control: form.control,
    name: 'outputs',
  });

  const outputs = watch('outputs');

  return (
    <FormSection
      title="Output & Encoding Ladders"
      badge={<span className="badge badge-neutral">{fields.length} Output{fields.length > 1 ? 's' : ''}</span>}
      action={
        <Button
          type="button"
          variant="secondary"
          style={{ padding: '0.25rem 0.5rem', fontSize: '0.75rem' }}
          icon={<Plus size={14} />}
          onClick={() =>
            append({
              id: `out-${Date.now()}`,
              name: `output-${fields.length + 1}`,
              container: 'mpegts',
              outputType: 'udp',
              destinationUri: 'udp://239.255.0.2:5000',
              video: {
                codec: 'h264',
                width: 1280,
                height: 720,
                frameRate: 25,
                bitrateKbps: 3500,
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
            })
          }
        >
          Add Output Destination
        </Button>
      }
    >
      {fields.map((field, index) => {
        const scteEnabled = outputs?.[index]?.scte?.enabled ?? true;

        return (
          <div
            key={field.id}
            style={{
              padding: '1.25rem',
              border: '1px solid var(--color-border)',
              borderRadius: 'var(--radius-sm)',
              marginBottom: index < fields.length - 1 ? '1.5rem' : 0,
              background: 'var(--color-bg-surface)',
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontWeight: 700, fontSize: '0.9375rem' }}>
                <Layers size={16} color="var(--color-primary)" />
                <span>Output {index + 1}: {outputs?.[index]?.name || 'output'}</span>
              </div>
              {fields.length > 1 && (
                <Button
                  type="button"
                  variant="danger"
                  style={{ padding: '0.25rem 0.5rem', fontSize: '0.75rem' }}
                  icon={<Trash2 size={14} />}
                  onClick={() => remove(index)}
                >
                  Remove
                </Button>
              )}
            </div>

            {/* General Output Properties */}
            <div className="grid-3">
              <Input
                label="Output Slug Name"
                required
                {...register(`outputs.${index}.name` as const)}
                error={errors.outputs?.[index]?.name?.message}
                placeholder="e.g. hls-master-1080p"
                helperText="Identifier slug used in logs and playout"
              />

              <Select
                label="Output Type"
                required
                options={[
                  { value: 'hls', label: 'HLS Live Manifest (MPEG-TS/fMP4)' },
                  { value: 'dash', label: 'DASH Live Manifest (CMAF)' },
                  { value: 'srt', label: 'SRT Stream' },
                  { value: 'udp', label: 'UDP Stream (MPEG-TS)' },
                  { value: 'file', label: 'Local File / Archive' },
                ]}
                {...register(`outputs.${index}.outputType` as const)}
                onChange={(e) => {
                  const val = e.target.value as any;
                  setValue(`outputs.${index}.outputType`, val);
                  if (val === 'hls') setValue(`outputs.${index}.destinationUri`, 'hls+file:/runtime/hls');
                  else if (val === 'dash') {
                    // CMAF only, AAC only: the engine refuses anything else for DASH.
                    setValue(`outputs.${index}.destinationUri`, 'dash+file:/runtime/dash');
                    setValue(`outputs.${index}.container`, 'fmp4');
                    setValue(`outputs.${index}.audio.codec`, 'aac');
                  }
                  else if (val === 'srt') {
                    setValue(`outputs.${index}.destinationUri`, 'srt://dest.broadcast.net:9000');
                    setValue(`outputs.${index}.srtMode`, outputs?.[index]?.srtMode ?? 'caller');
                    setValue(`outputs.${index}.srtLatency`, outputs?.[index]?.srtLatency ?? 120);
                  }
                  else if (val === 'udp') setValue(`outputs.${index}.destinationUri`, 'udp://239.255.0.1:5000');
                  else if (val === 'file') setValue(`outputs.${index}.destinationUri`, 'file:///media/output.ts');
                }}
              />

              <Select
                label="Container Format"
                required
                options={[
                  { value: 'mpegts', label: 'MPEG-TS (Standard Broadcast)' },
                  { value: 'fmp4', label: 'Fragmented MP4 (HLS, DASH)' },
                ]}
                {...register(`outputs.${index}.container` as const)}
                helperText="Required by the engine below schema 1.4.0; always written"
              />
            </div>

            <div className="form-group">
              <Input
                label="Destination URI"
                required
                {...register(`outputs.${index}.destinationUri` as const)}
                error={errors.outputs?.[index]?.destinationUri?.message}
                placeholder="hls+file:/runtime/hls, dash+file:/runtime/dash, srt://host:port or udp://ip:port"
              />
            </div>

            {outputs?.[index]?.outputType === 'srt' && (
              <div className="grid-2" style={{ marginTop: '1rem', padding: '0.75rem', background: 'var(--color-bg-subtle)', borderRadius: 'var(--radius-sm)' }}>
                <Select
                  label="SRT Mode"
                  options={[
                    { value: 'caller', label: 'Caller (Transcoder pushes to the remote host)' },
                    { value: 'listener', label: 'Listener (Transcoder binds the port; receivers pull)' },
                  ]}
                  {...register(`outputs.${index}.srtMode` as const)}
                  helperText="The engine uses this, not the mode= query in the URI"
                />
                <Input
                  label="SRT Latency (ms)"
                  type="number"
                  {...register(`outputs.${index}.srtLatency` as const, { valueAsNumber: true })}
                  error={errors.outputs?.[index]?.srtLatency?.message}
                  helperText="Default 120ms"
                />
              </div>
            )}

            {/* Video Parameters Sub-card */}
            <div style={{ marginTop: '1.25rem', padding: '1rem', border: '1px solid var(--color-border-subtle)', borderRadius: 'var(--radius-sm)', background: 'var(--color-bg-subtle)' }}>
              <div style={{ fontWeight: 600, fontSize: '0.875rem', marginBottom: '0.75rem', display: 'flex', alignItems: 'center', gap: '0.375rem', color: 'var(--color-primary)' }}>
                <Sliders size={14} />
                <span>Video Encoder Parameters</span>
              </div>

              <div className="grid-4">
                <Select
                  label="Codec"
                  options={[
                    { value: 'h264', label: 'H.264 / AVC (x264enc)' },
                    { value: 'h265', label: 'H.265 / HEVC (x265enc)' },
                  ]}
                  {...register(`outputs.${index}.video.codec` as const)}
                />

                {/* Resolution preset quick-pick */}
                <Select
                  label="Resolution Preset"
                  options={RESOLUTION_PRESETS}
                  value={(() => {
                    const w = outputs?.[index]?.video?.width;
                    const h = outputs?.[index]?.video?.height;
                    const match = RESOLUTION_PRESETS.find(p => p.value === `${w}x${h}`);
                    return match ? match.value : '';
                  })()}
                  onChange={(e) => {
                    if (!e.target.value) return;
                    const [w, h] = e.target.value.split('x').map(Number);
                    setValue(`outputs.${index}.video.width`, w);
                    setValue(`outputs.${index}.video.height`, h);
                  }}
                />

                <Input
                  label="Bitrate (kbps)"
                  type="number"
                  required
                  {...register(`outputs.${index}.video.bitrateKbps` as const, { valueAsNumber: true })}
                  error={errors.outputs?.[index]?.video?.bitrateKbps?.message}
                  placeholder="6000"
                />

                <Input
                  label="Frame Rate (fps)"
                  type="number"
                  required
                  {...register(`outputs.${index}.video.frameRate` as const, { valueAsNumber: true })}
                  error={errors.outputs?.[index]?.video?.frameRate?.message}
                  placeholder="25"
                />
              </div>

              {/* Width × Height + live aspect ratio — grid-4 so each cell = same width as Codec above; 4th slot intentionally empty */}
              <div className="grid-4" style={{ marginTop: '0.5rem' }}>
                <Input
                  label="Width (px)"
                  type="number"
                  required
                  {...register(`outputs.${index}.video.width` as const, { valueAsNumber: true })}
                  error={errors.outputs?.[index]?.video?.width?.message}
                  placeholder="1920"
                />

                <Input
                  label="Height (px)"
                  type="number"
                  required
                  {...register(`outputs.${index}.video.height` as const, { valueAsNumber: true })}
                  error={errors.outputs?.[index]?.video?.height?.message}
                  placeholder="1080"
                />

                {/* Aspect ratio — same form-group structure as Input; explicit height matches native <input> */}
                <div className="form-group">
                  <label className="form-label">Aspect Ratio</label>
                  <div
                    className="form-input"
                    style={{
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'center',
                      height: 'calc(1.25rem + 1rem)',  /* matches input: line-height ~1.25rem + vertical padding 0.5rem×2 */
                      fontFamily: 'JetBrains Mono, monospace',
                      fontWeight: 700,
                      fontSize: '1rem',
                      color: 'var(--color-primary)',
                      letterSpacing: '0.05em',
                      cursor: 'default',
                      userSelect: 'none',
                    }}
                  >
                    {aspectRatioLabel(
                      outputs?.[index]?.video?.width ?? 0,
                      outputs?.[index]?.video?.height ?? 0,
                    )}
                  </div>
                </div>

                {/* 4th column intentionally empty — keeps row from spanning full width */}
              </div>

              <div className="grid-4" style={{ marginTop: '0.5rem' }}>
                <Select
                  label="H.264 Profile"
                  options={[
                    { value: 'high', label: 'High' },
                    { value: 'main', label: 'Main' },
                    { value: 'baseline', label: 'Baseline' },
                  ]}
                  {...register(`outputs.${index}.video.profile` as const)}
                />

                <Select
                  label="Speed Preset"
                  options={[
                    { value: 'veryfast', label: 'veryfast (Recommended)' },
                    { value: 'fast', label: 'fast' },
                    { value: 'medium', label: 'medium' },
                    { value: 'ultrafast', label: 'ultrafast' },
                  ]}
                  {...register(`outputs.${index}.video.speedPreset` as const)}
                />

                <Select
                  label="Tune"
                  options={[
                    { value: 'zerolatency', label: 'zerolatency' },
                    { value: 'film', label: 'film' },
                    { value: 'animation', label: 'animation' },
                  ]}
                  {...register(`outputs.${index}.video.tune` as const)}
                />

                <Input
                  label="Keyframe Interval (GOP)"
                  type="number"
                  {...register(`outputs.${index}.video.keyIntMax` as const, { valueAsNumber: true })}
                  placeholder="50"
                  helperText="50 frames = 2s at 25 fps"
                />
              </div>

              <div className="grid-4" style={{ marginTop: '0.5rem' }}>
                <Select
                  label="Frame Format"
                  options={[
                    { value: 'progressive', label: 'Progressive' },
                    { value: 'tff', label: 'Interlaced, top field first' },
                    { value: 'bff', label: 'Interlaced, bottom field first' },
                  ]}
                  {...register(`outputs.${index}.video.frameFormat` as const)}
                  error={errors.outputs?.[index]?.video?.frameFormat?.message}
                  helperText={
                    outputs?.[index]?.video?.frameFormat !== 'progressive'
                      ? `Frame rate is frames per second: ${outputs?.[index]?.video?.frameRate || 25} gives ${outputs?.[index]?.video?.frameRate || 25}i`
                      : undefined
                  }
                />

                <Select
                  label="Rate Control"
                  options={[
                    { value: 'cbr', label: 'CBR (Constant Bitrate)' },
                    { value: 'qual', label: 'CRF (Constant Quality)' },
                    { value: 'quant', label: 'CQP (Fixed Quantizer)' },
                  ]}
                  {...register(`outputs.${index}.video.rateControlMode` as const)}
                  error={errors.outputs?.[index]?.video?.rateControlMode?.message}
                  disabled={outputs?.[index]?.video?.codec !== 'h264'}
                  helperText={outputs?.[index]?.video?.codec !== 'h264' ? 'H.264 only (x264enc)' : undefined}
                />

                <Input
                  label="No. of B-Frames"
                  type="number"
                  {...register(`outputs.${index}.video.bFrames` as const, { valueAsNumber: true })}
                  error={errors.outputs?.[index]?.video?.bFrames?.message}
                  placeholder="0"
                  disabled={outputs?.[index]?.video?.codec !== 'h264'}
                  helperText={
                    outputs?.[index]?.video?.codec !== 'h264'
                      ? 'H.264 only (x264enc)'
                      : outputs?.[index]?.video?.tune === 'zerolatency'
                      ? '0 forced by zerolatency tune'
                      : '0 = disabled (lowest latency)'
                  }
                />
              </div>
            </div>

            {/* Audio & SCTE Parameters */}
            <div className="grid-2" style={{ marginTop: '1rem' }}>
              {/* Audio Sub-card */}
              <div style={{ padding: '1rem', border: '1px solid var(--color-border-subtle)', borderRadius: 'var(--radius-sm)', background: 'var(--color-bg-subtle)' }}>
                <div style={{ fontWeight: 600, fontSize: '0.875rem', marginBottom: '0.75rem', color: 'var(--color-primary)' }}>
                  Audio Encoder Parameters
                </div>
                <div className="grid-2">
                  <Select
                    label="Audio Codec"
                    options={[
                      { value: 'aac', label: 'AAC-LC' },
                      { value: 'mp2', label: 'MPEG-1 Layer 2' },
                      { value: 'ac3', label: 'Dolby AC-3' },
                    ]}
                    {...register(`outputs.${index}.audio.codec` as const)}
                  />

                  <Select
                    label="Sampling Rate"
                    options={[
                      { value: '48000', label: '48000 Hz (Broadcast standard)' },
                      { value: '44100', label: '44100 Hz' },
                      { value: '32000', label: '32000 Hz' },
                    ]}
                    {...register(`outputs.${index}.audio.sampleRateHz` as const, { valueAsNumber: true })}
                  />

                  <Input
                    label="Bitrate (kbps)"
                    type="number"
                    {...register(`outputs.${index}.audio.bitrateKbps` as const, { valueAsNumber: true })}
                    error={errors.outputs?.[index]?.audio?.bitrateKbps?.message}
                    placeholder="128"
                  />

                  <Select
                    label="Channels"
                    options={[
                      { value: '2', label: '2 Channels (Stereo)' },
                      { value: '1', label: '1 Channel (Mono)' },
                      { value: '6', label: '6 Channels (5.1 Surround)' },
                    ]}
                    {...register(`outputs.${index}.audio.channels` as const, { valueAsNumber: true })}
                  />
                </div>
              </div>

              {/* SCTE-35 Sub-card */}
              <div style={{ padding: '1rem', border: '1px solid var(--color-border-subtle)', borderRadius: 'var(--radius-sm)', background: 'var(--color-bg-subtle)' }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.75rem' }}>
                  <div style={{ fontWeight: 600, fontSize: '0.875rem', color: 'var(--color-primary)' }}>
                    SCTE-35 Ad Cueing Policy
                  </div>
                  <label style={{ display: 'flex', alignItems: 'center', gap: '0.375rem', fontSize: '0.75rem', cursor: 'pointer' }}>
                    <input
                      type="checkbox"
                      {...register(`outputs.${index}.scte.enabled` as const)}
                    />
                    <span>Enable SCTE</span>
                  </label>
                </div>

                {scteEnabled && (
                  <div className="grid-2">
                    <Select
                      label="Mode"
                      options={[
                        { value: 'passthrough', label: 'Passthrough (Preserve markers)' },
                        { value: 'removal', label: 'Removal (Strip SCTE stream)' },
                      ]}
                      {...register(`outputs.${index}.scte.mode` as const)}
                    />

                    <Input
                      label="SCTE PID"
                      type="number"
                      {...register(`outputs.${index}.scte.pid` as const, { valueAsNumber: true })}
                      placeholder="4269"
                      helperText="Default 4269 or 500"
                    />
                  </div>
                )}
              </div>
            </div>
          </div>
        );
      })}
    </FormSection>
  );
};
