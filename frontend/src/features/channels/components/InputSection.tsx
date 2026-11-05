import React from 'react';
import { UseFormReturn, useFieldArray } from 'react-hook-form';
import { ChannelFormValues } from '../types/form';
import { FormSection } from '../../../components/FormSection/FormSection';
import { Input } from '../../../components/Input/Input';
import { Select } from '../../../components/Select/Select';
import { Button } from '../../../components/Button/Button';
import { Plus, Trash2, Search, Radio, Copy, RefreshCw } from 'lucide-react';

// The appliance's built-in RTMP relay: encoders publish to it, the engine reads from loopback.
const RTMP_RELAY_URI = /^rtmp:\/\/(?:127\.0\.0\.1|localhost):1935\/live\/([A-Za-z0-9_-]{16,})$/;

const newStreamKey = (): string => {
  const bytes = crypto.getRandomValues(new Uint8Array(24));
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
};

const rtmpRelayUri = (key: string) => `rtmp://127.0.0.1:1935/live/${key}`;

export interface InputSectionProps {
  form: UseFormReturn<ChannelFormValues>;
  /** Probes the input at this index. The handler reads transport and mode off the form. */
  onProbeSource: (index: number) => void;
  isProbing: boolean;
  probeCountdown: number;
  /**
   * Why input `index` must not be probed, or null when it may be. A probe opens the source
   * itself, so one aimed at the endpoint the engine is already holding can steal or duplicate
   * the live subscription and take the channel off air. Scoped per input: probing a different
   * URI while a channel runs is safe.
   */
  probeBlockedReason?: (index: number) => string | null;
}

export const InputSection: React.FC<InputSectionProps> = ({
  form,
  onProbeSource,
  isProbing,
  probeCountdown,
  probeBlockedReason,
}) => {
  const { register, watch, setValue, formState: { errors } } = form;
  const { fields, append, remove } = useFieldArray({
    control: form.control,
    name: 'inputs',
  });

  const inputs = watch('inputs');

  const transportOptions = [
    { value: 'srt', label: 'SRT (Secure Reliable Transport)' },
    { value: 'udp', label: 'UDP (MPEG-TS Multicast / Unicast)' },
    { value: 'hls', label: 'HLS / HTTP Live Stream' },
    { value: 'dash', label: 'MPEG-DASH / CMAF (.mpd)' },
    { value: 'playlist', label: 'Playlist (pls+file://)' },
    { value: 'file', label: 'Local File (file://)' },
    { value: 'ndi', label: 'NDI Network Video' },
    { value: 'rtmp', label: 'RTMP / RTMPS (Encoder Push or Pull)' },
    { value: 'mxl', label: 'MXL flows (Media eXchange Layer, same host)' },
  ];

  return (
    <FormSection
      title="Input Specifications"
      badge={<span className="badge badge-neutral">{fields.length} Source{fields.length > 1 ? 's' : ''}</span>}
      action={
        <Button
          type="button"
          variant="secondary"
          style={{ padding: '0.25rem 0.5rem', fontSize: '0.75rem' }}
          icon={<Plus size={14} />}
          onClick={() =>
            append({
              id: `in-${Date.now()}`,
              name: `Failover Input ${fields.length + 1}`,
              transportType: 'srt',
              uri: 'srt://source.backup.net:8000?mode=caller',
              srtMode: 'caller',
              srtLatency: 120,
            })
          }
        >
          Add Input Failover
        </Button>
      }
    >
      {fields.map((field, index) => {
        const currentType = inputs?.[index]?.transportType || 'srt';
        const currentUri = inputs?.[index]?.uri || '';
        const blockedReason = probeBlockedReason?.(index) ?? null;

        return (
          <div
            key={field.id}
            style={{
              padding: '1.25rem',
              border: '1px solid var(--color-border)',
              borderRadius: 'var(--radius-sm)',
              marginBottom: index < fields.length - 1 ? '1.25rem' : 0,
              background: index === 0 ? 'var(--color-bg-surface)' : 'var(--color-bg-subtle)',
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontWeight: 700, fontSize: '0.9375rem' }}>
                <Radio size={16} color="var(--color-primary)" />
                <span>{index === 0 ? 'Primary Source (Input 1)' : `Failover Source ${index + 1}`}</span>
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

            <div className="grid-3">
              <Input
                label="Input Label"
                required
                {...register(`inputs.${index}.name` as const)}
                error={errors.inputs?.[index]?.name?.message}
                placeholder="e.g. Main Studio Feed"
              />

              <Select
                label="Transport Type"
                required
                options={transportOptions}
                {...register(`inputs.${index}.transportType` as const)}
                onChange={(e) => {
                  const val = e.target.value as any;
                  setValue(`inputs.${index}.transportType`, val);
                  // Default URI scheme
                  if (val === 'srt') setValue(`inputs.${index}.uri`, 'srt://103.248.72.80:8000?mode=caller');
                  else if (val === 'udp') setValue(`inputs.${index}.uri`, 'udp://239.255.0.1:5000');
                  else if (val === 'hls') setValue(`inputs.${index}.uri`, 'https://stream.broadcast.net/live.m3u8');
                  else if (val === 'dash') setValue(`inputs.${index}.uri`, 'https://stream.broadcast.net/live/manifest.mpd');
                  else if (val === 'playlist') setValue(`inputs.${index}.uri`, 'pls+file:///media/queue/playlist.m3u8');
                  else if (val === 'file') setValue(`inputs.${index}.uri`, 'file:///media/clip.mp4');
                  else if (val === 'ndi') setValue(`inputs.${index}.uri`, 'ndi://STUDIO-A');
                  else if (val === 'rtmp') setValue(`inputs.${index}.uri`, rtmpRelayUri(newStreamKey()));
                  else if (val === 'mxl') setValue(`inputs.${index}.uri`, 'mxl:///run/mxl');
                }}
              />

              <div className="form-group">
                <label className="form-label">Probe Source</label>
                <Button
                  type="button"
                  variant="outline"
                  style={{ width: '100%' }}
                  icon={<Search size={14} />}
                  isLoading={isProbing}
                  disabled={!currentUri || isProbing || !!blockedReason}
                  title={blockedReason || undefined}
                  onClick={() => onProbeSource(index)}
                >
                  {isProbing ? `Probing (${probeCountdown}s)...` : 'Detect Source'}
                </Button>
                {blockedReason && <span className="form-helper">{blockedReason}</span>}
              </div>
            </div>

            <div className="form-group">
              <Input
                label="Source URI Endpoint"
                required
                {...register(`inputs.${index}.uri` as const)}
                error={errors.inputs?.[index]?.uri?.message}
                placeholder="srt://hostname:port?mode=caller or udp://interface:port"
                helperText="Must match scheme (srt://, udp://, hls+file:, pls+file:, file://, ndi://, rtmp://, rtmps://, mxl:///<domain directory>)"
              />
            </div>

            {/* Protocol-Specific Sub-Blocks */}
            {currentType === 'srt' && (
              <div className="grid-2" style={{ padding: '0.75rem', background: 'var(--color-bg-subtle)', borderRadius: 'var(--radius-sm)' }}>
                <Select
                  label="SRT Mode"
                  options={[
                    { value: 'caller', label: 'Caller (Transcoder initiates connection to remote)' },
                    { value: 'listener', label: 'Listener (Transcoder binds local port and waits)' },
                  ]}
                  {...register(`inputs.${index}.srtMode` as const)}
                />
                <Input
                  label="SRT Latency (ms)"
                  type="number"
                  {...register(`inputs.${index}.srtLatency` as const, { valueAsNumber: true })}
                  error={errors.inputs?.[index]?.srtLatency?.message}
                  helperText="Default 120ms (~4× RTT on lossy network)"
                />
              </div>
            )}

            {currentType === 'rtmp' && (() => {
              const relayKey = RTMP_RELAY_URI.exec(currentUri)?.[1];
              const serverUrl = `rtmp://${window.location.hostname}:1935/live`;
              const copy = (text: string) => void navigator.clipboard?.writeText(text);

              return (
                <div style={{ padding: '0.75rem', background: 'var(--color-bg-subtle)', borderRadius: 'var(--radius-sm)' }}>
                  {relayKey ? (
                    <div className="grid-2">
                      <Input
                        label="Encoder Server URL"
                        readOnly
                        value={serverUrl}
                        helperText="Enter this as the server / URL in OBS, vMix or a hardware encoder"
                      />
                      <Input label="Stream Key" readOnly value={relayKey} helperText="Treat as a password" />
                      <div style={{ display: 'flex', gap: '0.5rem' }}>
                        <Button type="button" variant="outline" icon={<Copy size={14} />} onClick={() => copy(serverUrl)}>
                          Copy URL
                        </Button>
                        <Button type="button" variant="outline" icon={<Copy size={14} />} onClick={() => copy(relayKey)}>
                          Copy Key
                        </Button>
                        <Button
                          type="button"
                          variant="outline"
                          icon={<RefreshCw size={14} />}
                          onClick={() => setValue(`inputs.${index}.uri`, rtmpRelayUri(newStreamKey()), { shouldDirty: true })}
                        >
                          New Key
                        </Button>
                      </div>
                    </div>
                  ) : (
                    <span className="form-helper">
                      Pull mode: the transcoder connects out to this RTMP server. Use rtmps:// for TLS.
                    </span>
                  )}
                </div>
              );
            })()}

            {currentType === 'mxl' && (
              <div className="grid-2" style={{ padding: '0.75rem', background: 'var(--color-bg-subtle)', borderRadius: 'var(--radius-sm)' }}>
                <Input
                  label="Video flow id"
                  placeholder="5fbec3b1-1b0f-417d-9059-8b94a47197ed"
                  {...register(`inputs.${index}.mxlVideoFlowId` as const)}
                  error={errors.inputs?.[index]?.mxlVideoFlowId?.message}
                  helperText="The UUID of the v210 video flow. Leave empty for an audio-only input"
                />
                <Input
                  label="Audio flow id"
                  placeholder="b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10"
                  {...register(`inputs.${index}.mxlAudioFlowId` as const)}
                  error={errors.inputs?.[index]?.mxlAudioFlowId?.message}
                  helperText="The UUID of the Float32 audio flow. Leave empty for a video-only input"
                />
              </div>
            )}

            {currentType === 'udp' && (
              <div style={{ padding: '0.75rem', background: 'var(--color-bg-subtle)', borderRadius: 'var(--radius-sm)' }}>
                <Input
                  label="Multicast Network Interface"
                  placeholder="e.g. eth0 or ens1f0"
                  {...register(`inputs.${index}.udpInterface` as const)}
                  helperText="Operator supplied interface name (never guessed)"
                />
              </div>
            )}
          </div>
        );
      })}
    </FormSection>
  );
};
