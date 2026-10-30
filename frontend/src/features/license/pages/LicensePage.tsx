import React, { useState, useRef, useCallback, useEffect } from 'react';
import {
  ShieldCheck,
  ShieldAlert,
  ShieldX,
  Shield,
  UploadCloud,
  Trash2,
  RefreshCw,
  FileCheck2,
  Clock,
  Key,
  Hash,
  Wifi,
  WifiOff,
  Users,
  CalendarClock,
  AlertTriangle,
  CheckCircle2,
  XCircle,
  Info,
  ChevronRight,
} from 'lucide-react';
import {
  fetchLicense,
  uploadLicense,
  deleteLicense,
} from '../api/licenseApi';
import {
  GetLicenseResponse,
  LicenseStatus,
} from '../types/api';
import { ApiError } from '../../../services/apiError';

/* ─── helpers ─────────────────────────────────────────────────────────────── */

function formatTs(ms: number | undefined): string {
  if (ms === undefined) return '—';
  return new Date(ms).toLocaleString('en-US', {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
  });
}

function formatDuration(ms: number | undefined): string {
  if (ms === undefined) return '—';
  const days = Math.floor(ms / 86_400_000);
  const hours = Math.floor((ms % 86_400_000) / 3_600_000);
  if (days > 0) return `${days}d ${hours}h`;
  const mins = Math.floor((ms % 3_600_000) / 60_000);
  return `${hours}h ${mins}m`;
}

function timeRemainingMs(license: GetLicenseResponse): number | null {
  if (!license.validUntil) return null;
  return license.validUntil - Date.now();
}

function formatTimeRemaining(license: GetLicenseResponse): string {
  const rem = timeRemainingMs(license);
  if (rem === null) return '—';
  if (rem <= 0) return 'Expired';
  return formatDuration(rem);
}

function statusMeta(status: LicenseStatus): {
  label: string;
  badgeClass: string;
  Icon: React.ElementType;
  iconColor: string;
} {
  switch (status) {
    case 'valid':
      return { label: 'Valid', badgeClass: 'badge-success', Icon: ShieldCheck, iconColor: 'var(--color-success)' };
    case 'expired':
      return { label: 'Expired', badgeClass: 'badge-danger', Icon: ShieldX, iconColor: 'var(--color-danger)' };
    case 'notYetValid':
      return { label: 'Not Yet Valid', badgeClass: 'badge-warning', Icon: ShieldAlert, iconColor: 'var(--color-warning)' };
    default:
      return { label: 'Invalid', badgeClass: 'badge-danger', Icon: ShieldX, iconColor: 'var(--color-danger)' };
  }
}

/* ─── sub-components ──────────────────────────────────────────────────────── */

interface InfoRowProps {
  icon: React.ElementType;
  label: string;
  value: React.ReactNode;
  mono?: boolean;
}
const InfoRow: React.FC<InfoRowProps> = ({ icon: Icon, label, value, mono }) => (
  <div style={styles.infoRow}>
    <Icon size={15} color="var(--color-text-muted)" style={{ flexShrink: 0 }} />
    <span style={styles.infoLabel}>{label}</span>
    <span style={{ ...styles.infoValue, fontFamily: mono ? "'JetBrains Mono', monospace" : undefined }}>
      {value}
    </span>
  </div>
);

interface ConfirmDeleteModalProps {
  onConfirm: () => void;
  onCancel: () => void;
  loading: boolean;
}
const ConfirmDeleteModal: React.FC<ConfirmDeleteModalProps> = ({ onConfirm, onCancel, loading }) => (
  <div className="modal-backdrop">
    <div className="modal-dialog">
      <div className="modal-header">
        <span style={{ fontWeight: 700, fontSize: '1rem', display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          <AlertTriangle size={18} color="var(--color-danger)" />
          Delete License
        </span>
        <button onClick={onCancel} disabled={loading} style={styles.closeBtn} aria-label="Close">✕</button>
      </div>
      <div className="modal-body">
        <div className="alert-banner alert-danger" style={{ marginBottom: '1rem' }}>
          <AlertTriangle size={18} style={{ flexShrink: 0, marginTop: 1 }} />
          <div>
            <strong>This will stop the engine and take the channel off air.</strong>
            <p style={{ marginTop: '0.25rem', fontWeight: 400 }}>
              The channel will remain offline until a new license is uploaded. This action cannot be undone.
            </p>
          </div>
        </div>
        <p style={{ fontSize: '0.875rem', color: 'var(--color-text-secondary)' }}>
          Are you sure you want to delete the installed license?
        </p>
      </div>
      <div className="modal-footer">
        <button className="btn btn-secondary" onClick={onCancel} disabled={loading}>
          Cancel
        </button>
        <button className="btn btn-danger" onClick={onConfirm} disabled={loading} id="confirm-delete-license">
          {loading ? (
            <><RefreshCw size={14} style={styles.spinIcon} /> Deleting…</>
          ) : (
            <><Trash2 size={14} /> Yes, Delete License</>
          )}
        </button>
      </div>
    </div>
  </div>
);

/* ─── main page ───────────────────────────────────────────────────────────── */

type PageState = 'loading' | 'loaded' | 'no-license' | 'error';
type UploadPhase = 'idle' | 'validating' | 'uploading' | 'success' | 'error';

export const LicensePage: React.FC = () => {
  const [pageState, setPageState] = useState<PageState>('loading');
  const [license, setLicense] = useState<GetLicenseResponse | null>(null);
  const [fetchError, setFetchError] = useState<string | null>(null);

  // Upload state
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [uploadPhase, setUploadPhase] = useState<UploadPhase>('idle');
  const [uploadMessage, setUploadMessage] = useState<string | null>(null);
  const [isDragOver, setIsDragOver] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  // Delete state
  const [showDeleteModal, setShowDeleteModal] = useState(false);
  const [deleteLoading, setDeleteLoading] = useState(false);
  const [deleteMessage, setDeleteMessage] = useState<{ type: 'success' | 'error'; text: string } | null>(null);

  /* load */
  const loadLicense = useCallback(async () => {
    setPageState('loading');
    setFetchError(null);
    try {
      const data = await fetchLicense();
      if (data === null) {
        setLicense(null);
        setPageState('no-license');
      } else {
        setLicense(data);
        setPageState('loaded');
      }
    } catch (err) {
      setFetchError(err instanceof ApiError ? err.message : 'Failed to load license');
      setPageState('error');
    }
  }, []);

  useEffect(() => { loadLicense(); }, [loadLicense]);

  /* file selection */
  const handleFileChange = (file: File | null) => {
    setSelectedFile(file);
    setUploadPhase('idle');
    setUploadMessage(null);
    setDeleteMessage(null);
  };

  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    setIsDragOver(false);
    const file = e.dataTransfer.files[0];
    if (file) handleFileChange(file);
  }, []);

  /* validate (dry-run) */
  const handleValidate = async () => {
    if (!selectedFile) return;
    setUploadPhase('validating');
    setUploadMessage(null);
    try {
      const res = await uploadLicense(selectedFile, true);
      const s = res.license?.status;
      setUploadMessage(
        `✓ Validated: ${res.license?.name ?? 'License'} — ${s === 'valid' ? 'Ready to install.' : `Status: ${s}`}`
      );
      setUploadPhase('success');
    } catch (err) {
      setUploadMessage(err instanceof ApiError ? err.message : 'Validation failed');
      setUploadPhase('error');
    }
  };

  /* upload (install) */
  const handleInstall = async () => {
    if (!selectedFile) return;
    setUploadPhase('uploading');
    setUploadMessage(null);
    try {
      const res = await uploadLicense(selectedFile, false);
      setUploadMessage(`✓ License installed. Engine state: ${res.engine ?? 'restarting…'}`);
      setUploadPhase('success');
      setSelectedFile(null);
      if (fileInputRef.current) fileInputRef.current.value = '';
      await loadLicense();
    } catch (err) {
      setUploadMessage(err instanceof ApiError ? err.message : 'Installation failed');
      setUploadPhase('error');
    }
  };

  /* delete */
  const handleDelete = async () => {
    setDeleteLoading(true);
    try {
      const res = await deleteLicense();
      setDeleteMessage({ type: 'success', text: `License removed. Engine: ${res.engine}` });
      setLicense(null);
      setPageState('no-license');
    } catch (err) {
      setDeleteMessage({ type: 'error', text: err instanceof ApiError ? err.message : 'Delete failed' });
    } finally {
      setDeleteLoading(false);
      setShowDeleteModal(false);
    }
  };

  /* ── derived display ── */
  const isExpiringSoon =
    license?.status === 'valid' &&
    license.validUntil !== undefined &&
    license.validUntil - Date.now() < 30 * 86_400_000; // < 30 days

  const meta = license ? statusMeta(license.status) : null;

  /* ── render ── */
  return (
    <div>
      {/* Page Title */}
      <div style={styles.pageHeader}>
        <div style={styles.pageTitleRow}>
          <Shield size={22} color="var(--color-primary)" />
          <h1 style={styles.pageTitle}>Licence</h1>
          {meta && (
            <span className={`badge ${meta.badgeClass}`}>
              <meta.Icon size={12} />
              {meta.label}
            </span>
          )}
        </div>
        <p style={styles.pageSubtitle}>
          Manage the engine license installed on this appliance.
          <span style={styles.engineNote}>&nbsp;Upload replaces the license and restarts the engine. Delete stops the engine.</span>
        </p>
      </div>

      {/* ── Current License Card ── */}
      <div className="card">
        <div className="card-header">
          <span className="card-title">
            <ShieldCheck size={16} />
            Installed License
          </span>
          <button
            className="btn btn-secondary"
            onClick={loadLicense}
            disabled={pageState === 'loading'}
            id="refresh-license"
            style={{ fontSize: '0.8125rem', padding: '0.375rem 0.75rem' }}
          >
            <RefreshCw size={13} style={pageState === 'loading' ? styles.spinIcon : undefined} />
            Refresh
          </button>
        </div>

        <div className="card-body">
          {/* Loading */}
          {pageState === 'loading' && (
            <div style={styles.centeredState}>
              <RefreshCw size={28} style={{ ...styles.spinIcon, color: 'var(--color-primary)' }} />
              <p style={{ color: 'var(--color-text-muted)', marginTop: '0.75rem' }}>Loading license…</p>
            </div>
          )}

          {/* Error */}
          {pageState === 'error' && (
            <div className="alert-banner alert-danger">
              <XCircle size={18} style={{ flexShrink: 0 }} />
              <div>
                <strong>Could not load license</strong>
                <p style={{ marginTop: '0.25rem', fontWeight: 400 }}>{fetchError}</p>
              </div>
            </div>
          )}

          {/* No license installed */}
          {pageState === 'no-license' && (
            <div style={styles.centeredState}>
              <Shield size={40} color="var(--color-text-muted)" />
              <p style={{ color: 'var(--color-text-secondary)', marginTop: '0.75rem', fontWeight: 600 }}>
                No license installed
              </p>
              <p style={{ color: 'var(--color-text-muted)', fontSize: '0.875rem', marginTop: '0.25rem' }}>
                Upload a <code>.lic</code> file below to activate the engine.
              </p>
            </div>
          )}

          {/* License loaded */}
          {pageState === 'loaded' && license && meta && (
            <>
              {/* Expiry warning */}
              {isExpiringSoon && (
                <div className="alert-banner alert-warning" style={{ marginBottom: '1.25rem' }}>
                  <AlertTriangle size={16} style={{ flexShrink: 0 }} />
                  <span>
                    <strong>License expiring soon.</strong> Time remaining: {formatTimeRemaining(license)}.
                    Upload a new license before expiry to avoid downtime.
                  </span>
                </div>
              )}

              {/* Invalid / error */}
              {(license.status === 'expired' || license.status === 'invalid' || license.status === 'notYetValid') && (
                <div className={`alert-banner ${license.status === 'notYetValid' ? 'alert-warning' : 'alert-danger'}`} style={{ marginBottom: '1.25rem' }}>
                  <meta.Icon size={16} style={{ flexShrink: 0 }} />
                  <span>
                    <strong>License {meta.label}.</strong>{' '}
                    {license.status === 'expired' && 'Upload a renewed license to restore the channel.'}
                    {license.status === 'notYetValid' && 'This license is not yet active.'}
                    {license.status === 'invalid' && (license.error ?? 'The engine cannot use this license.')}
                  </span>
                </div>
              )}

              {/* Status hero strip */}
              <div style={{ ...styles.statusHero, borderColor: license.status === 'valid' ? 'var(--color-success)' : license.status === 'notYetValid' ? 'var(--color-warning)' : 'var(--color-danger)' }}>
                <meta.Icon size={36} color={meta.iconColor} />
                <div>
                  <div style={{ fontWeight: 700, fontSize: '1.125rem', color: 'var(--color-text-main)' }}>
                    {license.name ?? '—'}
                  </div>
                  <div style={{ fontSize: '0.8125rem', color: 'var(--color-text-muted)', marginTop: '0.125rem' }}>
                    {license.id}
                  </div>
                </div>
                <div style={{ marginLeft: 'auto', textAlign: 'right' }}>
                  <span className={`badge ${meta.badgeClass}`} style={{ fontSize: '0.8125rem' }}>
                    <meta.Icon size={12} />{meta.label}
                  </span>
                  {license.status === 'valid' && (
                    <div style={{ fontSize: '0.75rem', color: 'var(--color-text-muted)', marginTop: '0.375rem' }}>
                      {formatTimeRemaining(license)} remaining
                    </div>
                  )}
                </div>
              </div>

              {/* Detail grid */}
              <div className="grid-2" style={{ marginTop: '1.25rem', gap: '0' }}>
                <div style={styles.detailBlock}>
                  <InfoRow icon={Hash}         label="License ID"     value={license.id ?? '—'}          mono />
                  <InfoRow icon={Key}           label="Created"        value={formatTs(license.createdAt)} />
                  <InfoRow icon={CalendarClock} label="Valid From"     value={formatTs(license.validSince)} />
                  <InfoRow icon={CalendarClock} label="Valid Until"    value={formatTs(license.validUntil)} />
                </div>
                <div style={styles.detailBlock}>
                  <InfoRow icon={Clock}   label="Duration"    value={formatDuration(license.ttl)} />
                  <InfoRow
                    icon={license.offline ? WifiOff : Wifi}
                    label="Mode"
                    value={license.offline ? 'Offline' : 'Online'}
                  />
                  {!license.offline && (
                    <InfoRow icon={Users} label="Max Sessions" value={license.maxSessions ?? '—'} />
                  )}
                  <InfoRow icon={ChevronRight} label="Path" value={license.path ?? '—'} mono />
                </div>
              </div>

              {/* Delete button */}
              <div style={{ marginTop: '1.5rem', paddingTop: '1rem', borderTop: '1px solid var(--color-border)' }}>
                {deleteMessage && (
                  <div className={`alert-banner ${deleteMessage.type === 'success' ? 'alert-success' : 'alert-danger'}`} style={{ marginBottom: '1rem' }}>
                    {deleteMessage.type === 'success'
                      ? <CheckCircle2 size={16} style={{ flexShrink: 0 }} />
                      : <XCircle size={16} style={{ flexShrink: 0 }} />}
                    <span>{deleteMessage.text}</span>
                  </div>
                )}
                <button
                  id="delete-license"
                  className="btn btn-danger"
                  onClick={() => { setDeleteMessage(null); setShowDeleteModal(true); }}
                >
                  <Trash2 size={14} />
                  Delete License
                </button>
                <span style={{ fontSize: '0.8125rem', color: 'var(--color-text-muted)', marginLeft: '0.75rem' }}>
                  ⚠ Stops the engine and takes the channel off air.
                </span>
              </div>
            </>
          )}

          {/* delete message after removal (no-license state) */}
          {pageState === 'no-license' && deleteMessage && (
            <div className={`alert-banner ${deleteMessage.type === 'success' ? 'alert-success' : 'alert-danger'}`} style={{ marginTop: '1rem', marginBottom: 0 }}>
              {deleteMessage.type === 'success'
                ? <CheckCircle2 size={16} style={{ flexShrink: 0 }} />
                : <XCircle size={16} style={{ flexShrink: 0 }} />}
              <span>{deleteMessage.text}</span>
            </div>
          )}
        </div>
      </div>

      {/* ── Upload Card ── */}
      <div className="card">
        <div className="card-header">
          <span className="card-title">
            <UploadCloud size={16} />
            Upload License
          </span>
        </div>
        <div className="card-body">
          <div className="alert-banner alert-info" style={{ marginBottom: '1.25rem' }}>
            <Info size={16} style={{ flexShrink: 0 }} />
            <span>
              Use <strong>Validate</strong> to check a license file before installing it.
              The engine is not restarted during validation. <strong>Install</strong> replaces the
              current license and restarts the engine — the channel will be briefly off air.
            </span>
          </div>

          {/* Drop zone */}
          <div
            style={{
              ...styles.dropZone,
              ...(isDragOver ? styles.dropZoneActive : {}),
              ...(selectedFile ? styles.dropZoneHasFile : {}),
            }}
            onDragOver={(e) => { e.preventDefault(); setIsDragOver(true); }}
            onDragLeave={() => setIsDragOver(false)}
            onDrop={handleDrop}
            onClick={() => fileInputRef.current?.click()}
            role="button"
            tabIndex={0}
            id="license-drop-zone"
            onKeyDown={(e) => e.key === 'Enter' && fileInputRef.current?.click()}
            aria-label="Upload license file"
          >
            <input
              ref={fileInputRef}
              type="file"
              accept=".lic"
              style={{ display: 'none' }}
              id="license-file-input"
              onChange={(e) => handleFileChange(e.target.files?.[0] ?? null)}
            />
            {selectedFile ? (
              <>
                <FileCheck2 size={32} color="var(--color-primary)" />
                <p style={{ fontWeight: 600, color: 'var(--color-text-main)', marginTop: '0.5rem' }}>
                  {selectedFile.name}
                </p>
                <p style={{ fontSize: '0.8125rem', color: 'var(--color-text-muted)' }}>
                  {(selectedFile.size / 1024).toFixed(1)} KB · click to change
                </p>
              </>
            ) : (
              <>
                <UploadCloud size={32} color="var(--color-text-muted)" />
                <p style={{ fontWeight: 600, color: 'var(--color-text-secondary)', marginTop: '0.5rem' }}>
                  Drop a <code>.lic</code> file here, or click to browse
                </p>
                <p style={{ fontSize: '0.8125rem', color: 'var(--color-text-muted)' }}>
                  Maximum 64 KB
                </p>
              </>
            )}
          </div>

          {/* Upload feedback */}
          {uploadMessage && (
            <div
              className={`alert-banner ${uploadPhase === 'error' ? 'alert-danger' : 'alert-success'}`}
              style={{ marginTop: '1rem' }}
            >
              {uploadPhase === 'error'
                ? <XCircle size={16} style={{ flexShrink: 0 }} />
                : <CheckCircle2 size={16} style={{ flexShrink: 0 }} />}
              <span>{uploadMessage}</span>
            </div>
          )}

          {/* Action buttons */}
          <div style={{ display: 'flex', gap: '0.75rem', marginTop: '1.25rem', flexWrap: 'wrap' }}>
            <button
              id="validate-license"
              className="btn btn-outline"
              onClick={handleValidate}
              disabled={!selectedFile || uploadPhase === 'validating' || uploadPhase === 'uploading'}
            >
              {uploadPhase === 'validating'
                ? <><RefreshCw size={14} style={styles.spinIcon} /> Validating…</>
                : <><ShieldCheck size={14} /> Validate (Dry Run)</>}
            </button>

            <button
              id="install-license"
              className="btn btn-primary"
              onClick={handleInstall}
              disabled={!selectedFile || uploadPhase === 'validating' || uploadPhase === 'uploading'}
            >
              {uploadPhase === 'uploading'
                ? <><RefreshCw size={14} style={styles.spinIcon} /> Installing…</>
                : <><UploadCloud size={14} /> Install License</>}
            </button>

            {selectedFile && (
              <button
                className="btn btn-secondary"
                onClick={() => { setSelectedFile(null); setUploadPhase('idle'); setUploadMessage(null); if (fileInputRef.current) fileInputRef.current.value = ''; }}
              >
                Clear
              </button>
            )}
          </div>
        </div>
      </div>

      {/* ── Delete Confirm Modal ── */}
      {showDeleteModal && (
        <ConfirmDeleteModal
          onConfirm={handleDelete}
          onCancel={() => setShowDeleteModal(false)}
          loading={deleteLoading}
        />
      )}
    </div>
  );
};

/* ─── inline styles (layout-only; colors from CSS vars) ──────────────────── */
const styles: Record<string, React.CSSProperties> = {
  pageHeader: {
    marginBottom: '1.5rem',
  },
  pageTitleRow: {
    display: 'flex',
    alignItems: 'center',
    gap: '0.625rem',
    marginBottom: '0.375rem',
  },
  pageTitle: {
    fontSize: '1.375rem',
    fontWeight: 700,
    color: 'var(--color-text-main)',
    margin: 0,
  },
  pageSubtitle: {
    fontSize: '0.875rem',
    color: 'var(--color-text-secondary)',
    margin: 0,
  },
  engineNote: {
    color: 'var(--color-text-muted)',
  },
  centeredState: {
    display: 'flex',
    flexDirection: 'column' as const,
    alignItems: 'center',
    padding: '3rem 1rem',
    textAlign: 'center' as const,
  },
  spinIcon: {
    animation: 'spin 1s linear infinite',
  },
  closeBtn: {
    background: 'none',
    border: 'none',
    cursor: 'pointer',
    fontSize: '1rem',
    color: 'var(--color-text-muted)',
    lineHeight: 1,
    padding: '0.25rem',
  },
  statusHero: {
    display: 'flex',
    alignItems: 'center',
    gap: '1rem',
    padding: '1rem 1.25rem',
    background: 'var(--color-bg-subtle)',
    borderRadius: 'var(--radius-md)',
    border: '1.5px solid',
  },
  detailBlock: {
    padding: '0.75rem 0',
  },
  infoRow: {
    display: 'flex',
    alignItems: 'center',
    gap: '0.5rem',
    padding: '0.4375rem 0',
    borderBottom: '1px solid var(--color-border-subtle)',
    fontSize: '0.875rem',
  },
  infoLabel: {
    color: 'var(--color-text-muted)',
    minWidth: '110px',
    flexShrink: 0,
    fontSize: '0.8125rem',
  },
  infoValue: {
    color: 'var(--color-text-main)',
    fontWeight: 500,
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap' as const,
  },
  dropZone: {
    border: '2px dashed var(--color-border)',
    borderRadius: 'var(--radius-md)',
    padding: '2.5rem 1.5rem',
    textAlign: 'center' as const,
    cursor: 'pointer',
    transition: 'all var(--transition-fast)',
    display: 'flex',
    flexDirection: 'column' as const,
    alignItems: 'center',
    gap: '0.25rem',
    background: 'var(--color-bg-subtle)',
  },
  dropZoneActive: {
    borderColor: 'var(--color-primary)',
    background: 'var(--color-primary-light)',
  },
  dropZoneHasFile: {
    borderColor: 'var(--color-primary)',
    borderStyle: 'solid',
    background: 'var(--color-primary-light)',
  },
};

/* ─── CSS animation for spinner (injected once) ──────────────────────────── */
if (!document.getElementById('license-spin-style')) {
  const s = document.createElement('style');
  s.id = 'license-spin-style';
  s.textContent = `@keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }`;
  document.head.appendChild(s);
}
