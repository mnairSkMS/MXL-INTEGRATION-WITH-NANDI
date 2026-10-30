/**
 * A failed probe has to be visible.
 *
 * The page's error banner renders above the form; the button that starts a probe sits inside
 * the input section, far below the fold. A failure reported only to the banner is invisible to
 * the operator who pressed the button, and reads as the button doing nothing at all. So the
 * dialog opens either way.
 */
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { CreateChannelPage } from '../../src/features/channels/pages/CreateChannelPage';
import {
  fetchEngineConfig,
  checkTranscoderState,
  probeInputSource,
} from '../../src/features/channels/api/channelApi';

vi.mock('../../src/features/channels/api/channelApi', () => ({
  fetchEngineConfig: vi.fn(),
  checkTranscoderState: vi.fn(),
  validateEngineConfig: vi.fn(),
  saveEngineConfig: vi.fn(),
  restartTranscoderEngine: vi.fn(),
  probeInputSource: vi.fn(),
  stopTranscoderEngine: vi.fn(),
  startTranscoderEngine: vi.fn(),
}));

const CONFIG = {
  version: '1.4.2',
  transcoder: {
    inputs: [{ transport: { uri: 'srt://127.0.0.1:9091?mode=caller' } }],
    outputs: [],
  },
};

async function renderStopped() {
  vi.mocked(fetchEngineConfig).mockResolvedValue(CONFIG as any);
  // STOPPED, so the probe guard allows the configured source.
  vi.mocked(checkTranscoderState).mockResolvedValue({ message: 'STOPPED' as any });
  render(<CreateChannelPage />);
  await screen.findByText('Create New Channel');
}

function clickDetect() {
  fireEvent.click(screen.getAllByRole('button', { name: /detect source/i })[0]);
}

describe('probe failure reporting', () => {
  beforeEach(() => vi.clearAllMocks());

  it('opens a dialog when the source cannot be reached', async () => {
    await renderStopped();
    vi.mocked(probeInputSource).mockRejectedValueOnce(
      Object.assign(new Error('Connection to srt://127.0.0.1:9091 failed: Input/output error'), {
        name: 'ApiError',
        status: 422,
      })
    );

    clickDetect();

    await screen.findByText(/source could not be probed/i);
    await screen.findByText(/no media was read from this source/i);
  });

  it('shows the appliance message verbatim, not a paraphrase', async () => {
    await renderStopped();
    const raw = 'Connection to srt://127.0.0.1:9091 failed: Input/output error';
    vi.mocked(probeInputSource).mockRejectedValueOnce(
      Object.assign(new Error(raw), { name: 'ApiError', status: 422 })
    );

    clickDetect();

    await screen.findByText(raw);
    await screen.findByText(/HTTP 422/i);
  });

  it('translates a connection refusal into something actionable', async () => {
    await renderStopped();
    vi.mocked(probeInputSource).mockRejectedValueOnce(
      Object.assign(new Error('Connection to srt://h:1 failed: Input/output error'), {
        name: 'ApiError',
        status: 422,
      })
    );

    clickDetect();

    await screen.findByText(/nothing accepted the connection at that address/i);
  });

  it('tells the operator to stop the engine when the source is in use', async () => {
    await renderStopped();
    vi.mocked(probeInputSource).mockRejectedValueOnce(
      Object.assign(new Error('input is in use'), { name: 'ApiError', status: 409 })
    );

    clickDetect();

    await screen.findByText(/stop the engine to release it/i);
  });

  it('states plainly that a failed probe changed nothing', async () => {
    await renderStopped();
    vi.mocked(probeInputSource).mockRejectedValueOnce(
      Object.assign(new Error('boom'), { name: 'ApiError', status: 404 })
    );

    clickDetect();

    await screen.findByText(/the output ladder is untouched/i);
    // No "Apply to Output Ladder" on a failure - there is nothing to apply.
    expect(screen.queryByRole('button', { name: /apply to output ladder/i })).not.toBeInTheDocument();
  });

  it('clears a previous failure when the next probe succeeds', async () => {
    await renderStopped();
    vi.mocked(probeInputSource).mockRejectedValueOnce(
      Object.assign(new Error('boom'), { name: 'ApiError', status: 404 })
    );
    clickDetect();
    await screen.findByText(/source could not be probed/i);
    fireEvent.click(screen.getByRole('button', { name: /dismiss/i }));

    vi.mocked(probeInputSource).mockResolvedValueOnce({
      uri: 'srt://127.0.0.1:9091',
      transport: 'srt',
      probedUri: 'srt://127.0.0.1:9091',
      container: 'mpegts',
      isLive: true,
      durationSeconds: null,
      bitrateBps: 5_000_000,
      waitedForCaller: false,
      video: { index: 0, codec: 'h264', width: 1920, height: 1080, frameRate: 25, scanType: 'progressive', pid: 256 },
      audio: [],
      data: [],
      programs: [],
      warnings: [],
    } as any);

    clickDetect();

    await screen.findByText(/source stream inspection details/i);
    await waitFor(() =>
      expect(screen.queryByText(/source could not be probed/i)).not.toBeInTheDocument()
    );
  });
});
