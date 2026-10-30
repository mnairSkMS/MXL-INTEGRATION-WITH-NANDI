import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { CreateChannelPage } from '../../src/features/channels/pages/CreateChannelPage';
import {
  fetchEngineConfig,
  checkTranscoderState,
  validateEngineConfig,
  saveEngineConfig,
} from '../../src/features/channels/api/channelApi';

// Mock the API layer entirely
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

describe('CreateChannelPage Component', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('handles concurrency conflict properly (fetch and compare)', async () => {
    const baseConfig = { version: '1.4.2', transcoder: { inputs: [], outputs: [] } };
    
    // Initial render setup
    vi.mocked(fetchEngineConfig).mockResolvedValueOnce(baseConfig);
    vi.mocked(checkTranscoderState).mockResolvedValue({ message: 'STOPPED' });

    render(<CreateChannelPage />);

    // Wait for initial load
    await waitFor(() => {
      expect(screen.getByText('Create New Channel')).toBeInTheDocument();
    });

    // Simulate external change when re-fetching
    const externallyModifiedConfig = { version: '1.4.2', transcoder: { inputs: [{ transport: { uri: 'udp://diff' } }], outputs: [] } };
    vi.mocked(fetchEngineConfig).mockResolvedValueOnce(externallyModifiedConfig);

    // Trigger save
    fireEvent.click(screen.getByRole('button', { name: /Save & Take On Air/i }));

    // Wait for the concurrency error to be displayed
    await waitFor(() => {
      expect(screen.getByText(/Configuration changed externally/i)).toBeInTheDocument();
    });

    // Ensure saveEngineConfig was NOT called
    expect(saveEngineConfig).not.toHaveBeenCalled();
  });

  it('prevents duplicate submissions', async () => {
    const baseConfig = { version: '1.4.2', transcoder: { inputs: [], outputs: [] } };
    
    vi.mocked(fetchEngineConfig).mockResolvedValue(baseConfig);
    vi.mocked(checkTranscoderState).mockResolvedValue({ message: 'STOPPED' });
    
    // Make validate hang so we can test double click
    vi.mocked(validateEngineConfig).mockImplementation(() => new Promise((resolve) => setTimeout(resolve, 100)));

    render(<CreateChannelPage />);

    await waitFor(() => {
      expect(screen.getByText('Create New Channel')).toBeInTheDocument();
    });

    const submitBtn = screen.getByRole('button', { name: /Save & Take On Air/i });
    
    // Click twice rapidly
    fireEvent.click(submitBtn);
    fireEvent.click(submitBtn);

    // Validate is only called once
    await waitFor(() => {
      expect(validateEngineConfig).toHaveBeenCalledTimes(1);
    });
  });

  it('shows modal and requires confirmation if channel is RUNNING just-in-time', async () => {
    const baseConfig = { version: '1.4.2', transcoder: { inputs: [], outputs: [] } };
    
    vi.mocked(fetchEngineConfig).mockResolvedValue(baseConfig);
    // Initially STOPPED
    vi.mocked(checkTranscoderState).mockResolvedValueOnce({ message: 'STOPPED' });

    render(<CreateChannelPage />);

    await waitFor(() => {
      expect(screen.getByText('Create New Channel')).toBeInTheDocument();
    });

    // But when we click submit, the JIT check returns RUNNING
    vi.mocked(checkTranscoderState).mockResolvedValueOnce({ message: 'RUNNING' });

    // Click submit
    fireEvent.click(screen.getByRole('button', { name: /Save & Take On Air/i }));

    // Modal should appear
    await waitFor(() => {
      expect(screen.getByText(/Warning: Channel is Currently Live/i)).toBeInTheDocument();
    });

    // Check we didn't start validating yet
    expect(validateEngineConfig).not.toHaveBeenCalled();
  });
});
