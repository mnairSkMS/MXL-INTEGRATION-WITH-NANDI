/**
 * License Feature — API Response DTOs
 *
 * These types mirror what `GET /license`, `PUT /license`, and `DELETE /license`
 * return from api/src/transcoder_api/services/license.py and routers/license.py.
 */

/** Status values that the engine binary can report for a license file. */
export type LicenseStatus = 'valid' | 'expired' | 'notYetValid' | 'invalid';

/**
 * The engine's full report on a license file — produced by
 * `transcoder -l <file> --license-info` and forwarded verbatim by the API.
 */
export interface LicenseReport {
  status: LicenseStatus;
  /** Human-readable name embedded in the license. */
  name?: string;
  /** Unique license identifier. */
  id?: string;
  /** Validity start — milliseconds since epoch. */
  validSince?: number;
  /** Validity end — milliseconds since epoch (validSince + ttl). */
  validUntil?: number;
  /** Time-to-live in milliseconds. */
  ttl?: number;
  /** True when no license-server contact is required. */
  offline?: boolean;
  /** Maximum concurrent online sessions allowed (online licenses only). */
  maxSessions?: number;
  /** Timestamp when the license was signed — milliseconds since epoch. */
  createdAt?: number;
  /** Timestamp when the engine most recently checked the license. */
  checkedAt?: number;
  /** Path to the installed license file on disk. */
  path?: string;
  /** Human-readable refusal reason when status is 'invalid'. */
  error?: string;
}

/** Response shape for GET /license */
export interface GetLicenseResponse extends LicenseReport {
  path: string;
}

/** Response shape for PUT /license (install or dry-run) */
export interface PutLicenseResponse {
  state: 'installed' | 'validated';
  path: string;
  license: LicenseReport;
  /** Only present on dry-run responses. */
  restartRequired?: boolean;
  /** Supervisor state string after engine restart (install only). */
  engine?: string;
}

/** Response shape for DELETE /license */
export interface DeleteLicenseResponse {
  state: 'removed';
  path: string;
  engine: string;
}
