import React from 'react';
import { Modal } from '../../../components/Modal/Modal';
import { Button } from '../../../components/Button/Button';
import { ProbeResponse } from '../types/view';
import { AlertTriangle, Video, Volume2, Radio, Film, XCircle } from 'lucide-react';

export interface ProbeResultModalProps {
  isOpen: boolean;
  onClose: () => void;
  probeData: ProbeResponse | null;
  /**
   * Why the probe failed, when it did. A failed probe opens this dialog too: the page's error
   * banner sits above the form, while the button that started the probe is far below the fold,
   * so posting the failure only to the banner reads as the button doing nothing at all.
   */
  probeError?: { message: string; status?: number } | null;
  onApplyToOutputs: (probe: ProbeResponse) => void;
}

/** ffprobe omits most per-stream numbers on a live MPEG-TS. Say so rather than printing "undefined". */
const UNKNOWN = '—';

function mbps(bps: number | null | undefined): string {
  return typeof bps === 'number' ? `${(bps / 1_000_000).toFixed(2)} Mbps` : UNKNOWN;
}

function pid(value: number | null | undefined): string {
  return typeof value === 'number' ? String(value) : UNKNOWN;
}

function text(value: string | null | undefined): string {
  return value || UNKNOWN;
}

/** What a given failure usually means here, in operator terms rather than ffmpeg's. */
function explain(status: number | undefined, message: string): string | null {
  if (status === 404) {
    return 'The transport connected but no media arrived before the timeout. The far end may be idle, or sending to a different address.';
  }
  if (status === 409) {
    return 'The source is already in use. Stop the engine to release it, then probe again.';
  }
  if (/Input\/output error|Connection .* failed|refused/i.test(message)) {
    return 'Nothing accepted the connection at that address. Check the host, the port, and that the sender is running.';
  }
  if (status === 422) {
    return 'The source was reached but could not be interpreted as media this engine can ingest.';
  }
  return null;
}

export const ProbeResultModal: React.FC<ProbeResultModalProps> = ({
  isOpen,
  onClose,
  probeData,
  probeError,
  onApplyToOutputs,
}) => {
  if (probeError) {
    const hint = explain(probeError.status, probeError.message);
    return (
      <Modal
        isOpen={isOpen}
        onClose={onClose}
        title="Source could not be probed"
        footer={
          <div style={{ display: 'flex', width: '100%', justifyContent: 'flex-end' }}>
            <Button variant="secondary" onClick={onClose}>
              Dismiss
            </Button>
          </div>
        }
      >
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
          <div className="alert-banner alert-danger" style={{ margin: 0 }}>
            <XCircle size={24} className="flex-shrink-0" />
            <div>
              <div style={{ fontWeight: 700, marginBottom: '0.25rem' }}>
                No media was read from this source
              </div>
              <div>Nothing has been changed. The output ladder is untouched.</div>
            </div>
          </div>

          {hint && (
            <p style={{ fontSize: '0.875rem', color: 'var(--color-text-secondary)', margin: 0 }}>
              {hint}
            </p>
          )}

          <div>
            <div style={{ fontSize: '0.75rem', color: 'var(--color-text-muted)', marginBottom: '0.25rem' }}>
              Reported by the appliance{typeof probeError.status === 'number' ? ` (HTTP ${probeError.status})` : ''}:
            </div>
            <pre
              style={{
                margin: 0,
                padding: '0.75rem',
                background: 'var(--color-bg-subtle)',
                borderRadius: 'var(--radius-sm)',
                fontSize: '0.75rem',
                whiteSpace: 'pre-wrap',
                wordBreak: 'break-word',
              }}
            >
              {probeError.message}
            </pre>
          </div>
        </div>
      </Modal>
    );
  }

  if (!probeData) return null;

  // Nothing can be copied to the ladder from an audio-only source with no usable sample rate.
  const canApply =
    !!probeData.video ||
    (probeData.audio.length > 0 && typeof probeData.audio[0].sampleRateHz === 'number');

  return (
    <Modal
      isOpen={isOpen}
      onClose={onClose}
      title="Source Stream Inspection Details"
      footer={
        <div style={{ display: 'flex', gap: '0.75rem', width: '100%', justifyContent: 'space-between', alignItems: 'center' }}>
          <div style={{ fontSize: '0.75rem', color: 'var(--color-text-muted)' }}>
            Inspected with ffprobe via <code>GET /inputs/probe</code>
          </div>
          <div style={{ display: 'flex', gap: '0.5rem' }}>
            <Button variant="secondary" onClick={onClose}>
              Dismiss
            </Button>
            <Button
              variant="primary"
              disabled={!canApply}
              onClick={() => {
                onApplyToOutputs(probeData);
                onClose();
              }}
            >
              Apply to Output Ladder
            </Button>
          </div>
        </div>
      }
    >
      <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
        {/* Container summary */}
        <div className="card" style={{ margin: 0, padding: '1rem' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontWeight: 600, marginBottom: '0.5rem' }}>
            <Film size={16} color="var(--color-primary)" />
            <span>Source</span>
          </div>
          <div className="grid-3" style={{ fontSize: '0.8125rem' }}>
            <div><strong>Transport:</strong> {probeData.transport.toUpperCase()}</div>
            <div><strong>Container:</strong> {text(probeData.container)}</div>
            <div><strong>Mux Rate:</strong> {mbps(probeData.bitrateBps)}</div>
            <div><strong>Mode:</strong> {probeData.isLive ? 'Live' : 'File'}</div>
            <div>
              <strong>Duration:</strong>{' '}
              {typeof probeData.durationSeconds === 'number'
                ? `${probeData.durationSeconds.toFixed(1)} s`
                : UNKNOWN}
            </div>
            {probeData.waitedForCaller && <div><strong>SRT:</strong> waited for caller</div>}
          </div>
          {probeData.probedUri !== probeData.uri && (
            <div style={{ fontSize: '0.75rem', color: 'var(--color-text-muted)', marginTop: '0.5rem' }}>
              Probed <code>{probeData.probedUri}</code>
            </div>
          )}
        </div>

        {/* Video stream */}
        {probeData.video ? (
          <div className="card" style={{ margin: 0, padding: '1rem' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontWeight: 600, marginBottom: '0.5rem' }}>
              <Video size={16} color="var(--color-primary)" />
              <span>Video Stream (PID: {pid(probeData.video.pid)})</span>
            </div>
            <div className="grid-3" style={{ fontSize: '0.8125rem' }}>
              <div><strong>Codec:</strong> {probeData.video.codec.toUpperCase()}</div>
              <div><strong>Profile:</strong> {text(probeData.video.profile)}</div>
              <div><strong>Resolution:</strong> {probeData.video.width} × {probeData.video.height}</div>
              <div>
                <strong>Frame Rate:</strong>{' '}
                {typeof probeData.video.frameRate === 'number'
                  ? `${probeData.video.frameRate} fps`
                  : UNKNOWN}
                {probeData.video.frameRateExact && ` (${probeData.video.frameRateExact})`}
              </div>
              <div><strong>Scan Type:</strong> {probeData.video.scanType}</div>
              <div><strong>Stream Bitrate:</strong> {mbps(probeData.video.bitrateBps)}</div>
              <div><strong>Aspect:</strong> {text(probeData.video.displayAspectRatio)}</div>
              <div><strong>Pixel Format:</strong> {text(probeData.video.pixelFormat)}</div>
            </div>
          </div>
        ) : (
          <div className="alert-banner alert-info" style={{ margin: 0 }}>
            No video stream found. Audio-only source.
          </div>
        )}

        {/* Audio streams */}
        {probeData.audio.length > 0 && (
          <div className="card" style={{ margin: 0, padding: '1rem' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontWeight: 600, marginBottom: '0.5rem' }}>
              <Volume2 size={16} color="var(--color-primary)" />
              <span>Audio Streams ({probeData.audio.length})</span>
            </div>
            {probeData.audio.map((a) => (
              <div key={a.index} className="grid-3" style={{ fontSize: '0.8125rem', marginBottom: '0.25rem' }}>
                <div><strong>PID {pid(a.pid)}:</strong> {a.codec.toUpperCase()}</div>
                <div>
                  <strong>Sampling:</strong>{' '}
                  {typeof a.sampleRateHz === 'number' ? `${a.sampleRateHz} Hz` : UNKNOWN}
                </div>
                <div>
                  <strong>Layout:</strong>{' '}
                  {a.channelLayout || (a.channels === 2 ? 'Stereo (2ch)' : `${a.channels} ch`)}
                </div>
                {a.language && <div><strong>Language:</strong> {a.language}</div>}
              </div>
            ))}
          </div>
        )}

        {/* Data & SCTE */}
        {probeData.data.length > 0 && (
          <div className="card" style={{ margin: 0, padding: '1rem' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontWeight: 600, marginBottom: '0.5rem' }}>
              <Radio size={16} color="var(--color-primary)" />
              <span>Signalling &amp; SCTE-35</span>
            </div>
            {probeData.data.map((d) => (
              <div key={d.index} style={{ fontSize: '0.8125rem' }}>
                <strong>PID {pid(d.pid)}:</strong> {d.codec}
              </div>
            ))}
          </div>
        )}

        {/* Programs */}
        {probeData.programs.length > 0 && (
          <div className="card" style={{ margin: 0, padding: '1rem' }}>
            <div style={{ fontWeight: 600, marginBottom: '0.5rem' }}>
              Programs ({probeData.programs.length})
            </div>
            {probeData.programs.map((p) => (
              <div key={p.programNumber} className="grid-3" style={{ fontSize: '0.8125rem', marginBottom: '0.25rem' }}>
                <div><strong>Program {p.programNumber}</strong>{p.serviceName ? ` — ${p.serviceName}` : ''}</div>
                <div><strong>PMT:</strong> {pid(p.pmtPid)}</div>
                <div><strong>PCR:</strong> {pid(p.pcrPid)}</div>
              </div>
            ))}
          </div>
        )}

        {/* Warnings. These are the probe's own non-fatal observations, so they read as cautions. */}
        {probeData.warnings.length > 0 && (
          <div className="alert-banner alert-warning" style={{ padding: '0.75rem 1rem', margin: 0 }}>
            <AlertTriangle size={18} className="flex-shrink-0" />
            <div style={{ fontSize: '0.8125rem' }}>
              {probeData.warnings.map((w, i) => (
                <div key={i}>{w}</div>
              ))}
            </div>
          </div>
        )}
      </div>
    </Modal>
  );
};
