/**
 * The Stop/Start Engine control.
 *
 * What matters here is that stopping is treated as different from restarting. A restart ends
 * with the channel back on air; a stop leaves it down until someone acts. The confirmation
 * text and the absence of any automatic recovery are the whole point.
 */
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { CreateChannelPage } from '../../src/features/channels/pages/CreateChannelPage';
import {
  fetchEngineConfig,
  checkTranscoderState,
  stopTranscoderEngine,
  startTranscoderEngine,
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

async function renderAt(state: string) {
  vi.mocked(fetchEngineConfig).mockResolvedValue(CONFIG as any);
  vi.mocked(checkTranscoderState).mockResolvedValue({ message: state as any });
  render(<CreateChannelPage />);
  await screen.findByText('Create New Channel');
}

describe('engine power control', () => {
  beforeEach(() => vi.clearAllMocks());

  it('offers Stop while the engine is up', async () => {
    await renderAt('RUNNING');
    expect(screen.getByRole('button', { name: /stop engine/i })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /start engine/i })).not.toBeInTheDocument();
  });

  it('offers Start once the engine is down', async () => {
    await renderAt('STOPPED');
    expect(screen.getByRole('button', { name: /start engine/i })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /stop engine/i })).not.toBeInTheDocument();
  });

  it('does not stop the engine without confirmation', async () => {
    await renderAt('RUNNING');

    fireEvent.click(screen.getByRole('button', { name: /stop engine/i }));

    // The dialog is open, but nothing has been sent.
    await screen.findByText(/takes the channel off air and leaves it there/i);
    expect(stopTranscoderEngine).not.toHaveBeenCalled();
  });

  it('warns that a stop is not self-healing, unlike a restart', async () => {
    await renderAt('RUNNING');
    fireEvent.click(screen.getByRole('button', { name: /stop engine/i }));

    await screen.findByText(/nothing brings the broadcast back automatically/i);
    // The restart modal's "about 10 seconds" promise must not appear here.
    expect(screen.queryByText(/approximately 10 seconds/i)).not.toBeInTheDocument();
  });

  it('cancelling the dialog sends nothing', async () => {
    await renderAt('RUNNING');
    fireEvent.click(screen.getByRole('button', { name: /stop engine/i }));
    await screen.findByText(/takes the channel off air/i);

    fireEvent.click(screen.getByRole('button', { name: /^cancel$/i }));

    await waitFor(() => expect(stopTranscoderEngine).not.toHaveBeenCalled());
  });

  it('stops on confirmation and reports the source is now probeable', async () => {
    await renderAt('RUNNING');
    vi.mocked(stopTranscoderEngine).mockResolvedValueOnce({ message: 'STOPPED' as any });

    fireEvent.click(screen.getByRole('button', { name: /stop engine/i }));
    await screen.findByText(/takes the channel off air/i);
    fireEvent.click(screen.getByRole('button', { name: /go off air/i }));

    await waitFor(() => expect(stopTranscoderEngine).toHaveBeenCalledTimes(1));
    await screen.findByText(/inputs are released and can now be probed/i);
  });

  it('starting reports the failure instead of pretending the channel is back', async () => {
    await renderAt('STOPPED');
    vi.mocked(startTranscoderEngine).mockRejectedValueOnce(
      Object.assign(new Error('Supervisor refused to start'), { name: 'ApiError' })
    );

    fireEvent.click(screen.getByRole('button', { name: /start engine/i }));

    await screen.findByText(/engine start failed: supervisor refused to start/i);
  });
});
