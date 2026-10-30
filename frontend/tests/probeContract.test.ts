/**
 * Contract tests for GET /inputs/probe as this appliance actually implements it.
 *
 * The shapes asserted here come from `InputProbe` in
 * api/src/transcoder_api/model/rest_api.py and the behaviour of
 * api/src/transcoder_api/services/probe.py - not from a mock. The point of the file is that
 * the client sends a request that router accepts, and survives a response in which every
 * optional field is genuinely absent, which is the normal case for a live MPEG-TS.
 */

import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { probeInputSource } from '../src/features/channels/api/channelApi';
import { ApiError } from '../src/services/apiError';
import { ProbeResponse } from '../src/features/channels/types/view';

function jsonResponse(status: number, body: unknown): Response {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: async () => body,
  } as Response;
}

/** What probe.py returns for a live SRT source with no per-stream numbers available. */
const SPARSE_LIVE_PROBE: ProbeResponse = {
  uri: 'srt://103.248.72.80:8000?mode=caller',
  transport: 'srt',
  probedUri: 'srt://103.248.72.80:8000?mode=caller&timeout=10000000',
  container: 'mpegts',
  isLive: true,
  durationSeconds: null,
  bitrateBps: null,
  waitedForCaller: false,
  video: {
    index: 0,
    codec: 'h264',
    width: 1920,
    height: 1080,
    frameRate: null,
    scanType: 'unknown',
    pid: null,
    bitrateBps: null,
  },
  audio: [{ index: 1, codec: 'aac', channels: 2, sampleRateHz: null, pid: null }],
  data: [],
  programs: [],
  warnings: ['frame rate could not be derived from avg_frame_rate or r_frame_rate'],
};

describe('probeInputSource request construction', () => {
  let fetchMock: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    fetchMock = vi.fn().mockResolvedValue(jsonResponse(200, SPARSE_LIVE_PROBE));
    vi.stubGlobal('fetch', fetchMock);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  function requestedUrl(): URL {
    return new URL(fetchMock.mock.calls[0][0], 'http://appliance.local');
  }

  it('sends uri and transport as the router names them', async () => {
    await probeInputSource({ uri: 'srt://host:8000', transport: 'srt' });

    const url = requestedUrl();
    expect(url.pathname).toBe('/inputs/probe');
    expect(url.searchParams.get('uri')).toBe('srt://host:8000');
    expect(url.searchParams.get('transport')).toBe('srt');
    expect(fetchMock.mock.calls[0][1].method).toBe('GET');
  });

  it('omits timeoutSeconds entirely when not given, so the server default applies', async () => {
    await probeInputSource({ uri: 'srt://host:8000', transport: 'srt' });
    expect(requestedUrl().searchParams.has('timeoutSeconds')).toBe(false);
  });

  it('sends a listener budget the API will accept (1..30)', async () => {
    await probeInputSource({ uri: 'srt://:8000?mode=listener', transport: 'srt', timeoutSeconds: 20 });

    const timeout = Number(requestedUrl().searchParams.get('timeoutSeconds'));
    expect(timeout).toBe(20);
    expect(timeout).toBeGreaterThanOrEqual(1);
    expect(timeout).toBeLessThanOrEqual(30);
  });

  it('sends interfaceAddress only for udp', async () => {
    await probeInputSource({
      uri: 'udp://239.255.0.1:5000',
      transport: 'udp',
      interfaceAddress: '192.168.1.10',
    });
    expect(requestedUrl().searchParams.get('interfaceAddress')).toBe('192.168.1.10');

    fetchMock.mockClear();
    await probeInputSource({
      uri: 'srt://host:8000',
      transport: 'srt',
      interfaceAddress: '192.168.1.10',
    });
    expect(requestedUrl().searchParams.has('interfaceAddress')).toBe(false);
  });
});

describe('probeInputSource response handling', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('accepts a response whose optional fields are all null', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(jsonResponse(200, SPARSE_LIVE_PROBE)));

    const probe = await probeInputSource({ uri: 'srt://host:8000', transport: 'srt' });

    expect(probe.video?.width).toBe(1920);
    expect(probe.video?.frameRate).toBeNull();
    expect(probe.bitrateBps).toBeNull();
    expect(probe.audio[0].sampleRateHz).toBeNull();
    expect(probe.programs).toEqual([]);
    expect(probe.warnings).toHaveLength(1);
  });

  it.each([
    [400, 'uri is required'],
    [404, 'source did not deliver any data before the timeout'],
    [409, 'input is in use by the running engine'],
    [422, 'ffprobe reported no usable streams'],
  ])('surfaces a %i from the probe route as an ApiError, never as data', async (status, message) => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(jsonResponse(status, { message, code: 'INVALID_REQUEST' }))
    );

    await expect(probeInputSource({ uri: 'srt://host:8000', transport: 'srt' })).rejects.toMatchObject({
      name: 'ApiError',
      status,
      message,
    });
  });

  it('does not substitute simulated data when the route is absent', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(jsonResponse(404, { detail: 'Not Found' })));

    const err = await probeInputSource({ uri: 'srt://host:8000', transport: 'srt' }).catch((e) => e);
    expect(err).toBeInstanceOf(ApiError);
    expect(err.status).toBe(404);
  });

  it('propagates an abort rather than wrapping it', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockRejectedValue(new DOMException('The operation was aborted', 'AbortError'))
    );

    await expect(
      probeInputSource({ uri: 'srt://host:8000', transport: 'srt' })
    ).rejects.toThrow(/aborted/);
  });
});
