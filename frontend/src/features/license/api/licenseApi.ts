/**
 * License Feature — API Client Functions
 *
 * Wraps GET /license, PUT /license (upload + dry-run), and DELETE /license.
 * Reference: api/src/transcoder_api/routers/license.py
 */

import { ApiError } from '../../../services/apiError';
import {
  GetLicenseResponse,
  PutLicenseResponse,
  DeleteLicenseResponse,
} from '../types/api';

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || '';

export const LICENSE_ENDPOINT = '/license';

/** GET /license — returns null when no license is installed (404). */
export async function fetchLicense(
  signal?: AbortSignal
): Promise<GetLicenseResponse | null> {
  try {
    const response = await fetch(`${API_BASE_URL}${LICENSE_ENDPOINT}`, {
      method: 'GET',
      headers: { Accept: 'application/json' },
      signal,
    });

    if (response.status === 404) return null;

    if (!response.ok) {
      throw await ApiError.fromResponse(response);
    }

    return await response.json();
  } catch (err) {
    if (err instanceof ApiError) throw err;
    if (err instanceof DOMException && err.name === 'AbortError') throw err;
    throw new ApiError(500, (err as Error).message || 'Network connection failure');
  }
}

/**
 * PUT /license — upload a .lic file.
 *
 * @param file   The license file selected by the user.
 * @param dryRun When true, validates without installing (engine not touched).
 */
export async function uploadLicense(
  file: File,
  dryRun: boolean = false,
  signal?: AbortSignal
): Promise<PutLicenseResponse> {
  const url = dryRun
    ? `${API_BASE_URL}${LICENSE_ENDPOINT}?dryRun=true`
    : `${API_BASE_URL}${LICENSE_ENDPOINT}`;

  const formData = new FormData();
  // The API requires the field to be named exactly "file".
  formData.append('file', file);

  try {
    const response = await fetch(url, {
      method: 'PUT',
      headers: { Accept: 'application/json' },
      body: formData,
      signal,
    });

    if (!response.ok) {
      throw await ApiError.fromResponse(response);
    }

    return await response.json();
  } catch (err) {
    if (err instanceof ApiError) throw err;
    if (err instanceof DOMException && err.name === 'AbortError') throw err;
    throw new ApiError(500, (err as Error).message || 'Network connection failure');
  }
}

/**
 * DELETE /license — stops the engine and removes the installed license.
 * ⚠️ Takes the channel off air until a new license is uploaded.
 */
export async function deleteLicense(
  signal?: AbortSignal
): Promise<DeleteLicenseResponse> {
  try {
    const response = await fetch(`${API_BASE_URL}${LICENSE_ENDPOINT}`, {
      method: 'DELETE',
      headers: { Accept: 'application/json' },
      signal,
    });

    if (!response.ok) {
      throw await ApiError.fromResponse(response);
    }

    return await response.json();
  } catch (err) {
    if (err instanceof ApiError) throw err;
    if (err instanceof DOMException && err.name === 'AbortError') throw err;
    throw new ApiError(500, (err as Error).message || 'Network connection failure');
  }
}
