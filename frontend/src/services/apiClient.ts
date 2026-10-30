/**
 * Shared API Client for the Skandha transcoder management API.
 *
 * Every call here reaches the FastAPI app in `api/src/transcoder_api`. There is no mock
 * dispatch: this appliance implements `/config`, `/config/check-transcoder`,
 * `/config/restart-transcoder` and `/inputs/probe` for real, and a client-side simulation of
 * any of them would report a behaviour the engine does not have.
 *
 * `API_BASE_URL` is empty in the shipped build because the SPA is served from the API's own
 * origin at `/channel`. It exists for `npm run dev`, where the Vite proxy in vite.config.ts
 * forwards to the appliance instead.
 */

import { ApiError } from './apiError';

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || '';

async function request<T>(method: string, url: string, body?: unknown, signal?: AbortSignal): Promise<T> {
  const headers: Record<string, string> = { Accept: 'application/json' };
  if (body !== undefined) {
    headers['Content-Type'] = 'application/json';
  }

  try {
    const response = await fetch(`${API_BASE_URL}${url}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal,
    });

    if (!response.ok) {
      throw await ApiError.fromResponse(response);
    }

    return await response.json();
  } catch (err: unknown) {
    if (err instanceof ApiError) throw err;
    if (err instanceof DOMException && err.name === 'AbortError') throw err;
    throw new ApiError(500, (err as Error).message || 'Network connection failure');
  }
}

export const apiClient = {
  get<T>(url: string, signal?: AbortSignal): Promise<T> {
    return request<T>('GET', url, undefined, signal);
  },

  put<TResponse, TRequest>(url: string, body: TRequest, signal?: AbortSignal): Promise<TResponse> {
    return request<TResponse>('PUT', url, body, signal);
  },

  post<TResponse, TRequest = unknown>(url: string, body?: TRequest, signal?: AbortSignal): Promise<TResponse> {
    return request<TResponse>('POST', url, body, signal);
  },
};
