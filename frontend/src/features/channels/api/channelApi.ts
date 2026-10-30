/**
 * Channel Feature API Functions
 * Reference: API-CREATE-CHANNEL.md
 */

import { apiClient } from '../../../services/apiClient';
import { ENDPOINTS } from '../../../services/endpoints';
import { ApiError } from '../../../services/apiError';
import { EngineConfigDto } from '../types/api';
import { InputTransportType, ProbeResponse, SupervisorState } from '../types/view';

export async function fetchEngineConfig(signal?: AbortSignal): Promise<EngineConfigDto | null> {
  try {
    return await apiClient.get<EngineConfigDto>(ENDPOINTS.CONFIG, signal);
  } catch (err) {
    // 404 is the "unconfigured" appliance case, not a fatal failure
    if (err instanceof ApiError && err.status === 404) {
      return null;
    }
    throw err;
  }
}

export interface ProbeRequest {
  uri: string;
  transport: InputTransportType;
  /**
   * Seconds the transport may spend connecting. The API clamps this to 1..30 and rejects
   * anything outside it, so callers pick a budget rather than letting it default: an SRT
   * listener has to wait for a human to start an encoder elsewhere, a caller does not.
   */
  timeoutSeconds?: number;
  /**
   * Local interface address to join a UDP multicast group on. Only meaningful for udp, and
   * only ever operator-supplied - an appliance with several NICs has no correct default.
   */
  interfaceAddress?: string;
}

export async function probeInputSource(
  { uri, transport, timeoutSeconds, interfaceAddress }: ProbeRequest,
  signal?: AbortSignal
): Promise<ProbeResponse> {
  const query = new URLSearchParams({ uri, transport });
  if (timeoutSeconds !== undefined) {
    query.set('timeoutSeconds', String(timeoutSeconds));
  }
  if (transport === 'udp' && interfaceAddress) {
    query.set('interfaceAddress', interfaceAddress);
  }
  return await apiClient.get<ProbeResponse>(`${ENDPOINTS.INPUTS_PROBE}?${query.toString()}`, signal);
}

export async function validateEngineConfig(
  config: EngineConfigDto,
  signal?: AbortSignal
): Promise<{ state: 'validated'; path: string; restartRequired: false }> {
  return await apiClient.put<{ state: 'validated'; path: string; restartRequired: false }, EngineConfigDto>(
    ENDPOINTS.CONFIG_DRY_RUN,
    config,
    signal
  );
}

export async function saveEngineConfig(
  config: EngineConfigDto,
  signal?: AbortSignal
): Promise<{ state: 'written'; path: string; restartRequired: true }> {
  return await apiClient.put<{ state: 'written'; path: string; restartRequired: true }, EngineConfigDto>(
    ENDPOINTS.CONFIG,
    config,
    signal
  );
}

export async function restartTranscoderEngine(signal?: AbortSignal): Promise<{ message: string }> {
  return await apiClient.post<{ message: string }>(ENDPOINTS.RESTART_TRANSCODER, undefined, signal);
}

export async function checkTranscoderState(signal?: AbortSignal): Promise<{ message: SupervisorState }> {
  return await apiClient.get<{ message: SupervisorState }>(ENDPOINTS.CHECK_TRANSCODER, signal);
}

/**
 * Stop the engine and leave it stopped, without disturbing this API.
 *
 * Not self-healing: the channel stays off air until something starts it again. The returned
 * state is what supervisor reports once the stop has been applied.
 */
export async function stopTranscoderEngine(signal?: AbortSignal): Promise<{ message: SupervisorState }> {
  return await apiClient.post<{ message: SupervisorState }>(ENDPOINTS.STOP_TRANSCODER, undefined, signal);
}

/**
 * Start a stopped engine. It reads the configuration on disk as it comes up.
 *
 * Normally answers STARTING rather than RUNNING - poll checkTranscoderState for the outcome,
 * because a configuration the engine rejects surfaces as BACKOFF or FATAL rather than as a
 * failure of this call.
 */
export async function startTranscoderEngine(signal?: AbortSignal): Promise<{ message: SupervisorState }> {
  return await apiClient.post<{ message: SupervisorState }>(ENDPOINTS.START_TRANSCODER, undefined, signal);
}
