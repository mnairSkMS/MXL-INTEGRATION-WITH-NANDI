import { describe, expect, it } from 'vitest';
import { inputItemSchema } from '../src/features/channels/schemas/channelSchema';

const VIDEO = '5fbec3b1-1b0f-417d-9059-8b94a47197ed';
const AUDIO = 'b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10';

const mxlInput = (over: Record<string, unknown> = {}) => ({
  id: 'in-1',
  name: 'Studio MXL',
  transportType: 'mxl',
  uri: 'mxl:///run/mxl',
  srtMode: 'caller',
  srtLatency: 120,
  mxlVideoFlowId: VIDEO,
  mxlAudioFlowId: AUDIO,
  ...over,
});

const problems = (input: unknown) => {
  const result = inputItemSchema.safeParse(input);
  return result.success ? [] : result.error.issues.map((i) => `${i.path.join('.')}: ${i.message}`);
};

describe('an MXL input in the channel form', () => {
  it('is accepted with both flows, or with one of them', () => {
    expect(problems(mxlInput())).toEqual([]);
    expect(problems(mxlInput({ mxlAudioFlowId: '' }))).toEqual([]);
    expect(problems(mxlInput({ mxlVideoFlowId: undefined }))).toEqual([]);
  });

  it('needs a flow, and a UUID for each flow it names', () => {
    expect(problems(mxlInput({ mxlVideoFlowId: '', mxlAudioFlowId: '' }))).toEqual(['mxlVideoFlowId: Name a video flow or an audio flow']);
    expect(problems(mxlInput({ mxlVideoFlowId: '12345' }))).toEqual(['mxlVideoFlowId: A flow id is a UUID']);
    expect(problems(mxlInput({ mxlAudioFlowId: 'not-a-uuid' }))).toEqual(['mxlAudioFlowId: A flow id is a UUID']);
  });

  it('needs two different flows, and a domain directory for its uri', () => {
    expect(problems(mxlInput({ mxlAudioFlowId: VIDEO }))).toEqual(['mxlAudioFlowId: Video and audio need different flows']);
    expect(problems(mxlInput({ uri: 'mxl://studio-a/run/mxl' }))).toEqual(['uri: An MXL input is a domain directory: mxl:///run/mxl']);
    expect(problems(mxlInput({ uri: 'mxl:///' }))).toEqual(['uri: An MXL input is a domain directory: mxl:///run/mxl']);
  });

  it('takes a delay of whole milliseconds from 0 to 2000, or none', () => {
    expect(problems(mxlInput({ mxlDelayMs: undefined }))).toEqual([]);
    expect(problems(mxlInput({ mxlDelayMs: 0 }))).toEqual([]);
    expect(problems(mxlInput({ mxlDelayMs: 2000 }))).toEqual([]);
    const refusal = ['mxlDelayMs: Delay is a whole number of milliseconds from 0 to 2000'];
    expect(problems(mxlInput({ mxlDelayMs: -1 }))).toEqual(refusal);
    expect(problems(mxlInput({ mxlDelayMs: 2001 }))).toEqual(refusal);
    expect(problems(mxlInput({ mxlDelayMs: 12.5 }))).toEqual(refusal);
  });

  it('does not ask the flows of an input of another transport', () => {
    expect(problems(mxlInput({ transportType: 'srt', uri: 'srt://h:1?mode=caller', mxlVideoFlowId: '', mxlAudioFlowId: '' }))).toEqual([]);
  });
});
