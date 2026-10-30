/**
 * Canonical Backend Route Endpoints
 * Reference: api/api.yaml and API-CREATE-CHANNEL.md
 */
export const ENDPOINTS = {
  // Engine Configuration
  CONFIG: '/config',
  CONFIG_DRY_RUN: '/config?dryRun=true',
  
  // Supervisor and Engine Lifecycle
  CHECK_TRANSCODER: '/config/check-transcoder',
  RESTART_TRANSCODER: '/config/restart-transcoder',
  STOP_TRANSCODER: '/config/stop-transcoder',
  START_TRANSCODER: '/config/start-transcoder',
  
  // Input stream probing. Served by routers/input.py via services/probe.py (ffprobe).
  INPUTS_PROBE: '/inputs/probe',
  
  // Engine license — GET/PUT/DELETE /license (routers/license.py)
  LICENSE: '/license',
  LICENSE_DRY_RUN: '/license?dryRun=true',

  // Playout / Operator Links
  HLS_MASTER: '/hls/master.m3u8',
  OPERATOR: '/operator',
  PLAYER: '/player',
} as const;

