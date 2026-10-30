/**
 * The probe safety guard's comparison.
 *
 * Getting this wrong fails in one of two expensive ways: too loose and the UI reopens the
 * socket the engine is holding, taking a live channel off air; too strict and the screen
 * refuses to probe any new source while a channel runs, which is the main thing it is for.
 */
import { describe, it, expect } from 'vitest';
import { sourceIdentity, isHeldBy } from '../src/features/channels/utils/sourceIdentity';

describe('sourceIdentity', () => {
  it('treats the same endpoint with different query parameters as one source', () => {
    // Otherwise editing `latency` would appear to make a held socket safe to probe.
    expect(sourceIdentity('srt://10.0.0.1:9000?mode=caller&latency=120')).toBe(
      sourceIdentity('srt://10.0.0.1:9000?mode=caller')
    );
    expect(sourceIdentity('srt://10.0.0.1:9000')).toBe(sourceIdentity('srt://10.0.0.1:9000?x=1'));
  });

  it('distinguishes different ports and hosts', () => {
    expect(sourceIdentity('srt://10.0.0.1:9000')).not.toBe(sourceIdentity('srt://10.0.0.1:9001'));
    expect(sourceIdentity('srt://10.0.0.1:9000')).not.toBe(sourceIdentity('srt://10.0.0.2:9000'));
  });

  it('is case-insensitive on scheme and host but not on path', () => {
    expect(sourceIdentity('SRT://Host:9000')).toBe(sourceIdentity('srt://host:9000'));
    expect(sourceIdentity('file:///media/A.mp4')).not.toBe(sourceIdentity('file:///media/a.mp4'));
  });

  it('handles the engine compound schemes rather than throwing', () => {
    expect(sourceIdentity('pls+file:///media/queue/playlist.m3u8')).toBeTruthy();
    expect(sourceIdentity('hls+file:///media/x.m3u8')).toBeTruthy();
    expect(sourceIdentity('ndi://STUDIO-A')).toBe('ndi://studio-a');
  });

  it('never identifies an empty or whitespace URI', () => {
    // A null identity must never match, or a blank field would compare equal to another blank.
    expect(sourceIdentity('')).toBeNull();
    expect(sourceIdentity('   ')).toBeNull();
    expect(sourceIdentity(undefined)).toBeNull();
  });
});

describe('isHeldBy', () => {
  const held = ['srt://127.0.0.1:9091?mode=caller'];

  it('blocks the exact source the engine is running', () => {
    expect(isHeldBy('srt://127.0.0.1:9091?mode=caller', held)).toBe(true);
  });

  it('blocks it even when the operator retypes the query differently', () => {
    expect(isHeldBy('srt://127.0.0.1:9091', held)).toBe(true);
  });

  it('allows a different port on the same host', () => {
    // The case that matters: configuring a new feed while the current one is on air.
    expect(isHeldBy('srt://127.0.0.1:9095?mode=caller', held)).toBe(false);
  });

  it('allows an entirely different source', () => {
    expect(isHeldBy('udp://239.255.0.1:5000', held)).toBe(false);
    expect(isHeldBy('file:///media/queue/clip.mp4', held)).toBe(false);
  });

  it('does not match a blank field against a blank held entry', () => {
    expect(isHeldBy('', [''])).toBe(false);
    expect(isHeldBy('srt://h:1', [undefined])).toBe(false);
  });
});
