import { render, screen, fireEvent, waitFor, act } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { CreateChannelPage } from '../../src/features/channels/pages/CreateChannelPage';
import {
  fetchEngineConfig,
  checkTranscoderState,
  validateEngineConfig,
  saveEngineConfig,
  restartTranscoderEngine
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

const BASE_CONFIG = { version: '1.4.2', transcoder: { inputs: [], outputs: [] } };

describe('Polling termination and pre-restart confirmation', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers({ shouldAdvanceTime: true });
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  // ── Test 1: Status request hangs until the deadline ──
  it('aborts a hanging status request at the 30s deadline and shows timeout', async () => {
    // Initial load: STOPPED
    vi.mocked(fetchEngineConfig).mockResolvedValue(BASE_CONFIG);
    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' }); // initial load

    render(<CreateChannelPage />);
    await waitFor(() => expect(screen.getByText('Create New Channel')).toBeInTheDocument());

    // JIT pre-submit: STOPPED
    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' })  // JIT
      .mockResolvedValueOnce({ message: 'STOPPED' }); // pre-restart
    vi.mocked(validateEngineConfig).mockResolvedValueOnce({ state: 'validated', path: '/tmp/cfg', restartRequired: false });
    vi.mocked(saveEngineConfig).mockResolvedValueOnce({ state: 'written', path: '/tmp/cfg', restartRequired: true });
    vi.mocked(restartTranscoderEngine).mockResolvedValueOnce({ message: 'restarting' });

    // The first poll call hangs forever — simulates a request that never completes
    // Must listen to AbortSignal to simulate fetch aborting!
    vi.mocked(checkTranscoderState).mockImplementation((signal) => {
      return new Promise((_, reject) => {
        if (signal?.aborted) reject(new DOMException('Aborted', 'AbortError'));
        signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')));
      });
    });

    fireEvent.click(screen.getByRole('button', { name: /Save & Take On Air/i }));

    // Advance 31s
    await act(async () => {
      await vi.advanceTimersByTimeAsync(31000);
    });

    await waitFor(() => {
      expect(screen.getByText(/polling timed out after 30 seconds/i)).toBeInTheDocument();
    });

    // Submission locks must be released
    const btn = screen.getByRole('button', { name: /Save/i });
    expect(btn).not.toBeDisabled();
  });

  // ── Test 2: Timeout during the delay between polls ──
  it('aborts cleanly when signal fires during the inter-poll delay', async () => {
    vi.mocked(fetchEngineConfig).mockResolvedValue(BASE_CONFIG);
    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' }); // initial load

    render(<CreateChannelPage />);
    await waitFor(() => expect(screen.getByText('Create New Channel')).toBeInTheDocument());

    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' })  // JIT
      .mockResolvedValueOnce({ message: 'STOPPED' }); // pre-restart
    vi.mocked(validateEngineConfig).mockResolvedValueOnce({ state: 'validated', path: '/tmp/cfg', restartRequired: false });
    vi.mocked(saveEngineConfig).mockResolvedValueOnce({ state: 'written', path: '/tmp/cfg', restartRequired: true });
    vi.mocked(restartTranscoderEngine).mockResolvedValueOnce({ message: 'restarting' });

    // Always return STARTING instantly, so the only place it waits is the 1.5s inter-poll delay
    vi.mocked(checkTranscoderState).mockResolvedValue({ message: 'STARTING' as any });

    fireEvent.click(screen.getByRole('button', { name: /Save & Take On Air/i }));

    // Advance just enough time for the first poll to complete and the 1.5s wait to start
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10);
    });

    // Now advance 30s so the deadline expires exactly during the 1.5s inter-poll wait
    await act(async () => {
      await vi.advanceTimersByTimeAsync(31000);
    });

    await waitFor(() => {
      expect(screen.getByText(/polling timed out after 30 seconds/i)).toBeInTheDocument();
    });
  });

  // ── Test 3: STOPPED before validation, RUNNING immediately before restart ──
  it('pauses for confirmation when pre-restart check finds RUNNING after config is saved', async () => {
    vi.mocked(fetchEngineConfig).mockResolvedValue(BASE_CONFIG);
    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' }); // initial load

    render(<CreateChannelPage />);
    await waitFor(() => expect(screen.getByText('Create New Channel')).toBeInTheDocument());

    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' })   // JIT pre-submit → proceed
      .mockResolvedValueOnce({ message: 'RUNNING' });  // pre-restart → should pause
    vi.mocked(validateEngineConfig).mockResolvedValueOnce({ state: 'validated', path: '/tmp/cfg', restartRequired: false });
    vi.mocked(saveEngineConfig).mockResolvedValueOnce({ state: 'written', path: '/tmp/cfg', restartRequired: true });

    fireEvent.click(screen.getByRole('button', { name: /Save & Take On Air/i }));

    await waitFor(() => {
      expect(screen.getByText(/Warning: Channel is Currently Live/i)).toBeInTheDocument();
    });

    // Config WAS saved
    expect(saveEngineConfig).toHaveBeenCalledTimes(1);
    // But restart was NOT called yet
    expect(restartTranscoderEngine).not.toHaveBeenCalled();
  });

  // ── Test 4a: Confirm after config has already been saved → restart proceeds without re-saving ──
  it('resumes restart without re-saving when user confirms after post-save RUNNING modal', async () => {
    vi.mocked(fetchEngineConfig).mockResolvedValue(BASE_CONFIG);
    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' }); // initial load

    render(<CreateChannelPage />);
    await waitFor(() => expect(screen.getByText('Create New Channel')).toBeInTheDocument());

    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' })   // JIT pre-submit
      .mockResolvedValueOnce({ message: 'RUNNING' });  // pre-restart → pause
    vi.mocked(validateEngineConfig).mockResolvedValueOnce({ state: 'validated', path: '/tmp/cfg', restartRequired: false });
    vi.mocked(saveEngineConfig).mockResolvedValueOnce({ state: 'written', path: '/tmp/cfg', restartRequired: true });
    vi.mocked(restartTranscoderEngine).mockResolvedValueOnce({ message: 'restarting' });

    fireEvent.click(screen.getByRole('button', { name: /Save & Take On Air/i }));

    // Wait for modal
    await waitFor(() => {
      expect(screen.getByText(/Warning: Channel is Currently Live/i)).toBeInTheDocument();
    });

    // After confirmation, polls return RUNNING
    vi.mocked(checkTranscoderState).mockResolvedValue({ message: 'RUNNING' });

    // Confirm
    fireEvent.click(screen.getByRole('button', { name: /Confirm & Take Off Air/i }));

    await act(async () => {
      await vi.advanceTimersByTimeAsync(100);
    });

    // Restart is called exactly once, save was called exactly once (no re-save)
    await waitFor(() => {
      expect(restartTranscoderEngine).toHaveBeenCalledTimes(1);
    });
    expect(saveEngineConfig).toHaveBeenCalledTimes(1);

    // Success message appears
    await waitFor(() => {
      expect(screen.getByText(/engine RUNNING/i)).toBeInTheDocument();
    });
  });

  // ── Test 4b: Cancel after config has already been saved → no restart, config-saved message ──
  it('cancels restart and reports saved-only when user declines after post-save RUNNING modal', async () => {
    vi.mocked(fetchEngineConfig).mockResolvedValue(BASE_CONFIG);
    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' }); // initial load

    render(<CreateChannelPage />);
    await waitFor(() => expect(screen.getByText('Create New Channel')).toBeInTheDocument());

    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' })   // JIT
      .mockResolvedValueOnce({ message: 'RUNNING' });  // pre-restart
    vi.mocked(validateEngineConfig).mockResolvedValueOnce({ state: 'validated', path: '/tmp/cfg', restartRequired: false });
    vi.mocked(saveEngineConfig).mockResolvedValueOnce({ state: 'written', path: '/tmp/cfg', restartRequired: true });

    fireEvent.click(screen.getByRole('button', { name: /Save & Take On Air/i }));

    await waitFor(() => {
      expect(screen.getByText(/Warning: Channel is Currently Live/i)).toBeInTheDocument();
    });

    // Cancel
    fireEvent.click(screen.getByRole('button', { name: /Cancel/i }));

    // Restart was never called
    expect(restartTranscoderEngine).not.toHaveBeenCalled();

    // Config was saved but restart cancelled
    await waitFor(() => {
      expect(screen.getByText(/Configuration saved to disk. Restart was cancelled/i)).toBeInTheDocument();
    });

    // Submission locks released
    await waitFor(() => {
      const btn = screen.getByRole('button', { name: /Save/i });
      expect(btn).not.toBeDisabled();
    });
  });

  // ── Test 5: Initially RUNNING: confirmation appears only once ──
  it('requests confirmation only once if initially RUNNING and pre-restart is also RUNNING', async () => {
    vi.mocked(fetchEngineConfig).mockResolvedValue(BASE_CONFIG);
    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'STOPPED' }); // initial load

    render(<CreateChannelPage />);
    await waitFor(() => expect(screen.getByText('Create New Channel')).toBeInTheDocument());

    vi.mocked(checkTranscoderState)
      .mockResolvedValueOnce({ message: 'RUNNING' })   // JIT pre-submit → should pause
      .mockResolvedValueOnce({ message: 'RUNNING' });  // pre-restart → should NOT pause because already confirmed
    vi.mocked(validateEngineConfig).mockResolvedValueOnce({ state: 'validated', path: '/tmp/cfg', restartRequired: false });
    vi.mocked(saveEngineConfig).mockResolvedValueOnce({ state: 'written', path: '/tmp/cfg', restartRequired: true });
    vi.mocked(restartTranscoderEngine).mockResolvedValueOnce({ message: 'restarting' });

    fireEvent.click(screen.getByRole('button', { name: /Save & Take On Air/i }));

    // Wait for the first confirmation modal
    await waitFor(() => {
      expect(screen.getByText(/Warning: Channel is Currently Live/i)).toBeInTheDocument();
    });

    // Confirm the first modal
    fireEvent.click(screen.getByRole('button', { name: /Confirm & Take Off Air/i }));

    // After confirmation, the flow proceeds.
    vi.mocked(checkTranscoderState).mockResolvedValue({ message: 'RUNNING' }); // polling

    await act(async () => {
      await vi.advanceTimersByTimeAsync(100);
    });

    await waitFor(() => {
      expect(restartTranscoderEngine).toHaveBeenCalledTimes(1);
    });

    // Ensure saveEngineConfig was called once
    expect(saveEngineConfig).toHaveBeenCalledTimes(1);

    // The modal should not be present anymore, meaning it didn't pause again
    expect(screen.queryByText(/Warning: Channel is Currently Live/i)).not.toBeInTheDocument();
  });
});
