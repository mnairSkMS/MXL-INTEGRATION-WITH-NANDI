import { describe, it, expect } from 'vitest';
import { channelFormToConfig } from '../src/features/channels/mappers/channelFormToConfig';
import { configToChannelForm } from '../src/features/channels/mappers/configToChannelForm';
import { ChannelFormValues } from '../src/features/channels/types/form';
import { EngineConfigDto } from '../src/features/channels/types/api';
import { CHANNEL_PRESETS } from '../src/features/channels/constants/presets';
import { channelFormSchema } from '../src/features/channels/schemas/channelSchema';

describe('Configuration Mappers', () => {
  const sampleFormValues: ChannelFormValues = {
    channelName: 'Test-Channel-1',
    templatePreset: 'custom',
    inputs: [
      {
        id: 'in-1',
        name: 'Primary SRT Ingest',
        transportType: 'srt',
        uri: 'srt://10.0.0.1:8000?mode=caller',
        srtMode: 'caller',
        srtLatency: 140,
      },
    ],
    outputs: [
      {
        id: 'out-1',
        name: 'test-hls',
        container: 'mpegts',
        outputType: 'hls',
        destinationUri: 'hls+file:/runtime/hls',
        video: {
          codec: 'h264',
          width: 1920,
          height: 1080,
          frameRate: 25,
          bitrateKbps: 5500,
          profile: 'high',
          speedPreset: 'veryfast',
          tune: 'zerolatency',
          keyIntMax: 50,
          frameFormat: 'progressive',
          rateControlMode: 'cbr',
          bFrames: 0,
        },
        audio: {
          codec: 'aac',
          sampleRateHz: 48000,
          bitrateKbps: 128,
          channels: 2,
        },
        scte: {
          enabled: true,
          mode: 'passthrough',
          pid: 4269,
          nullInterval: 0,
        },
      },
    ],
  };

  it('generates an engine config at the newest version this parser supports', () => {
    const config = channelFormToConfig(sampleFormValues);

    expect(config.version).toBe('1.4.2');
    expect(config.transcoder.inputs).toHaveLength(1);
    expect(config.transcoder.inputs[0].transport.uri).toBe('srt://10.0.0.1:8000?mode=caller');
    expect(config.transcoder.inputs[0].transport.srt?.latency).toBe(140);

    expect(config.transcoder.outputs).toHaveLength(1);
    const out = config.transcoder.outputs[0];
    expect(out.name).toBe('test-hls');
    expect(out.container).toBe('mpegts'); // required below schema 1.4.0, always written

    // Video streams check
    const videoStream = out.streams.find((s) => s.type === 'video') as any;
    expect(videoStream).toBeDefined();
    expect(videoStream.codec).toBe('h264');
    expect(videoStream.h264?.properties?.bitrate).toBe('5500'); // Values must be strings
    expect(videoStream.h264?.properties?.['speed-preset']).toBe('veryfast');

    // SCTE check
    expect(out.scte?.mode).toBe('passthrough');
    expect(out.scte?.pid).toBe(4269);
  });

  it('PRESERVES unknown and unedited fields from existing base configuration', () => {
    const existingBaseConfig: EngineConfigDto = {
      version: '1.4.2',
      // Custom / unknown fields
      application: {
        logger: {
          level: 'DEBUG',
          console: true,
          file: true,
          path: '/var/log/nandi.log',
        },
        _customLicenseToken: 'LIC-SECURE-9988',
      },
      transcoder: {
        inputs: [
          {
            transport: {
              uri: 'srt://old-host:8000',
            },
            _unknownCustomInputProperty: { hardwareAccelerated: true },
          },
        ],
        outputs: [
          {
            name: 'test-hls',
            type: 'single',
            container: 'mpegts',
            transports: [{ uri: 'old-uri' }],
            streams: [],
            _internalMuxerBacklogMs: 250,
          },
        ],
        _customEngineTag: 'PROD-CLUSTER-EAST',
      },
      _rootMetadataTag: 'KEEP_THIS_FIELD_VERBATIM',
    };

    const updatedConfig = channelFormToConfig(sampleFormValues, existingBaseConfig);

    // Root unknown field preserved
    expect(updatedConfig._rootMetadataTag).toBe('KEEP_THIS_FIELD_VERBATIM');

    // Application block and custom logger settings preserved
    expect(updatedConfig.application?.logger?.level).toBe('DEBUG');
    expect(updatedConfig.application?.logger?.path).toBe('/var/log/nandi.log');
    expect((updatedConfig.application as any)._customLicenseToken).toBe('LIC-SECURE-9988');

    // Transcoder custom block preserved
    expect((updatedConfig.transcoder as any)._customEngineTag).toBe('PROD-CLUSTER-EAST');

    // Sub-block unknown properties preserved
    expect((updatedConfig.transcoder.inputs[0] as any)._unknownCustomInputProperty).toEqual({
      hardwareAccelerated: true,
    });
    expect((updatedConfig.transcoder.outputs[0] as any)._internalMuxerBacklogMs).toBe(250);
  });

  it('PRESERVES extra streams, transports, and nested credentials during form-to-config mapping', () => {
    const existingBaseConfig: EngineConfigDto = {
      version: '1.4.2',
      transcoder: {
        inputs: [
          {
            transport: {
              uri: 'hls+file:/input/hls',
              hls: {
                bandwith: 5000000,
                scte: { source: 'segments' },
                credentials: { username: 'input-user', password: 'input-pass' }
              }
            }
          }
        ],
        outputs: [
          {
            name: 'test-hls',
            type: 'single',
            container: 'mpegts',
            transports: [
              {
                uri: 'hls+file:/runtime/hls',
                hls: {
                  credentials: { username: 'admin', password: 'secure' },
                }
              },
              { uri: 'udp://extra-destination:5000' }
            ],
            streams: [
              { type: 'video', codec: 'h264', h264: { encoder: 'x264enc', properties: { customHardwareProp: 'true' } } },
              { type: 'audio', codec: 'aac' },
              { type: 'audio', codec: 'mp2', audio: { rate: 48000, channels: 6 } }, // Secondary audio track
              { type: 'telemetry', custom: true } // Unknown stream type
            ],
          },
          {
            name: 'untouched-output',
            type: 'single',
            container: 'fmp4',
            transports: [],
            streams: []
          }
        ],
      },
    };

    const formWithHlsInput: ChannelFormValues = {
      ...sampleFormValues,
      inputs: [
        { ...sampleFormValues.inputs[0], transportType: 'hls', uri: 'hls+file:/input/hls' }
      ]
    };

    const updatedConfig = channelFormToConfig(formWithHlsInput, existingBaseConfig);
    const out = updatedConfig.transcoder.outputs[0];
    
    // Primary transport preserved nested credentials
    expect((out.transports[0] as any).hls.credentials).toEqual({ username: 'admin', password: 'secure' });
    
    // Extra transport preserved
    expect(out.transports.length).toBe(2);
    expect(out.transports[1].uri).toBe('udp://extra-destination:5000');

    // Video stream preserved custom property
    const video = out.streams.find(s => s.type === 'video');
    expect((video as any).h264.properties.customHardwareProp).toBe('true');
    expect((video as any).h264.properties['speed-preset']).toBe('veryfast'); // New mapped prop

    // Extra streams preserved
    expect(out.streams.length).toBe(4);
    const secondaryAudio = out.streams[2];
    expect(secondaryAudio.codec).toBe('mp2');
    const telemetry = out.streams[3];
    expect(telemetry.type).toBe('telemetry');

    // Input HLS credentials preserved
    expect(updatedConfig.transcoder.inputs[0].transport.hls?.credentials).toEqual({ username: 'input-user', password: 'input-pass' });
    expect(updatedConfig.transcoder.inputs[0].transport.hls?.bandwith).toBe(5000000); // Existing property preserved
    
    // Explicit deletion logic: if form only contains 1 output, the 2nd is deleted
    expect(updatedConfig.transcoder.outputs.length).toBe(1);
  });

  it('respects explicit output deletion (A+B -> A, A+B -> B)', () => {
    const existingBaseConfig: EngineConfigDto = {
      version: '1.4.2',
      transcoder: {
        inputs: [],
        outputs: [
          { name: 'OutputA', type: 'single', container: 'mpegts', transports: [], streams: [] },
          { name: 'OutputB', type: 'single', container: 'mpegts', transports: [], streams: [] }
        ]
      }
    };

    // Case 1: A+B -> remove B (Form only has A)
    const formRemoveB: ChannelFormValues = {
      ...sampleFormValues,
      outputs: [
        { ...sampleFormValues.outputs[0], name: 'OutputA' }
      ]
    };
    const configRemoveB = channelFormToConfig(formRemoveB, existingBaseConfig);
    expect(configRemoveB.transcoder.outputs).toHaveLength(1);
    expect(configRemoveB.transcoder.outputs[0].name).toBe('OutputA');

    // Case 2: A+B -> remove A (Form only has B)
    const formRemoveA: ChannelFormValues = {
      ...sampleFormValues,
      outputs: [
        { ...sampleFormValues.outputs[0], name: 'OutputB' }
      ]
    };
    const configRemoveA = channelFormToConfig(formRemoveA, existingBaseConfig);
    expect(configRemoveA.transcoder.outputs).toHaveLength(1);
    expect(configRemoveA.transcoder.outputs[0].name).toBe('OutputB');
  });

  it('correctly parses an existing engine config into form values', () => {
    const config = channelFormToConfig(sampleFormValues);
    const parsedForm = configToChannelForm(config);

    expect(parsedForm.inputs).toHaveLength(1);
    expect(parsedForm.inputs[0].uri).toBe('srt://10.0.0.1:8000?mode=caller');
    expect(parsedForm.inputs[0].transportType).toBe('srt');
    expect(parsedForm.inputs[0].srtLatency).toBe(140);

    expect(parsedForm.outputs).toHaveLength(1);
    expect(parsedForm.outputs[0].name).toBe('test-hls');
    expect(parsedForm.outputs[0].video.bitrateKbps).toBe(5500);
    expect(parsedForm.outputs[0].video.profile).toBe('high');
  });

  it('writes the chosen SRT output mode over a carried-over caller block', () => {
    const base = {
      transcoder: {
        outputs: [
          {
            name: 'out',
            transports: [{ uri: 'srt://127.0.0.1:9072', srt: { mode: 'caller', latency: 120 } }],
          },
        ],
      },
    } as unknown as EngineConfigDto;
    const form: ChannelFormValues = {
      ...sampleFormValues,
      outputs: [
        {
          ...sampleFormValues.outputs[0],
          outputType: 'srt',
          destinationUri: 'srt://0.0.0.0:9092',
          srtMode: 'listener',
          srtLatency: 200,
        },
      ],
    };

    const config = channelFormToConfig(form, base);
    const srt = config.transcoder!.outputs![0].transports![0].srt;

    expect(srt).toMatchObject({ mode: 'listener', latency: 200 });
    const back = configToChannelForm(config).outputs[0];
    expect(back.srtMode).toBe('listener');
    expect(back.srtLatency).toBe(200);
  });

  it('round-trips an rtmp input as a bare URI with no transport block', () => {
    const uri = 'rtmp://127.0.0.1:1935/live/AbCdEfGhIjKlMnOpQrStUvWx';
    const form: ChannelFormValues = {
      ...sampleFormValues,
      inputs: [{ ...sampleFormValues.inputs[0], transportType: 'rtmp', uri }],
    };

    const config = channelFormToConfig(form, {} as EngineConfigDto);
    const transport = config.transcoder!.inputs![0].transport;

    expect(transport.uri).toBe(uri);
    expect(transport.srt).toBeUndefined();
    expect(configToChannelForm(config).inputs[0].transportType).toBe('rtmp');
    expect(channelFormSchema.shape.inputs.element.shape.transportType.safeParse('rtmp').success).toBe(true);
  });
});

describe('engine config version handling', () => {
  it('stamps a greenfield config at the version this engine parses, not an older dialect', () => {
    // parser.cpp accepts 1.0.0..1.4.2 and changes behaviour below 1.1.0 and below 1.4.0, so a
    // fresh document must not be written at an arbitrary lower version.
    const config = channelFormToConfig(CHANNEL_PRESETS['srt-to-hls'].values, null);

    expect(config.version).toBe('1.4.2');
  });

  it('never re-stamps the version of a config it did not create', () => {
    // Re-dialecting a live channel is not this screen's decision. An older document is
    // rewritten in place at its own version.
    const existing = {
      version: '1.2.0',
      transcoder: { inputs: [{ transport: { uri: 'srt://old:8000' } }], outputs: [] },
    };

    const config = channelFormToConfig(CHANNEL_PRESETS['srt-to-hls'].values, existing as any);

    expect(config.version).toBe('1.2.0');
  });
});

describe('frame format', () => {
  const withOutput = (patch: (o: ChannelFormValues['outputs'][0]) => void): ChannelFormValues => {
    const form: ChannelFormValues = JSON.parse(JSON.stringify(Object.values(CHANNEL_PRESETS)[0].values));
    patch(form.outputs[0]);
    return form;
  };
  const videoOf = (config: EngineConfigDto) =>
    config.transcoder.outputs[0].streams!.find((s) => s.type === 'video') as { video?: Record<string, unknown> };

  it('writes interleaved with the chosen field order', () => {
    expect(videoOf(channelFormToConfig(withOutput((o) => { o.video.frameFormat = 'tff'; }))).video)
      .toMatchObject({ interlace: 'interleaved', fieldOrder: 'tff' });
    expect(videoOf(channelFormToConfig(withOutput((o) => { o.video.frameFormat = 'bff'; }))).video)
      .toMatchObject({ interlace: 'interleaved', fieldOrder: 'bff' });
  });

  it('writes progressive by removing both keys, including ones carried from the base config', () => {
    const base = channelFormToConfig(withOutput((o) => { o.video.frameFormat = 'bff'; }));
    const video = videoOf(channelFormToConfig(withOutput(() => {}), base)).video!;
    expect(video).not.toHaveProperty('interlace');
    expect(video).not.toHaveProperty('fieldOrder');
  });

  it('reads the frame format back from a config', () => {
    for (const frameFormat of ['progressive', 'tff', 'bff'] as const) {
      const config = channelFormToConfig(withOutput((o) => { o.video.frameFormat = frameFormat; }));
      expect(configToChannelForm(config).outputs[0].video.frameFormat).toBe(frameFormat);
    }
  });

  it('refuses interlaced H.265, which the engine would refuse at startup', () => {
    const result = channelFormSchema.safeParse(withOutput((o) => { o.video.codec = 'h265'; o.video.frameFormat = 'tff'; }));
    expect(result.success).toBe(false);
    expect(result.success ? [] : result.error.issues.map((i) => i.path.join('.'))).toContain('outputs.0.video.frameFormat');
  });
});

describe('audio bitrate', () => {
  const withAudio = (audio: Partial<ChannelFormValues['outputs'][0]['audio']>): ChannelFormValues => {
    const form: ChannelFormValues = JSON.parse(JSON.stringify(Object.values(CHANNEL_PRESETS)[0].values));
    Object.assign(form.outputs[0].audio, audio);
    return form;
  };
  const audioOf = (config: EngineConfigDto) =>
    config.transcoder.outputs[0].streams!.find((s) => s.type === 'audio') as Record<string, any>;

  it('reaches the engine as a property of a named encoder, in bits/s for AAC and MP2', () => {
    expect(audioOf(channelFormToConfig(withAudio({ codec: 'aac', bitrateKbps: 96 }))).aac)
      .toEqual({ encoder: 'fdkaacenc', properties: { bitrate: '96000' } });
    expect(audioOf(channelFormToConfig(withAudio({ codec: 'mp2', bitrateKbps: 192 }))).mp2)
      .toEqual({ encoder: 'avenc_mp2', properties: { bitrate: '192000' } });
  });

  it('uses the Dolby enum for AC-3 at 48 kHz and avenc_ac3 otherwise', () => {
    expect(audioOf(channelFormToConfig(withAudio({ codec: 'ac3', sampleRateHz: 48000, bitrateKbps: 192 }))).ac3)
      .toEqual({ encoder: 'dlbpatenc', properties: { bitrate: '192k' } });
    expect(audioOf(channelFormToConfig(withAudio({ codec: 'ac3', sampleRateHz: 44100, bitrateKbps: 192 }))).ac3)
      .toEqual({ encoder: 'avenc_ac3', properties: { bitrate: '192000' } });
  });

  it('keeps a configured encoder and its other properties, and drops blocks for other codecs', () => {
    const base = channelFormToConfig(withAudio({ codec: 'ac3', bitrateKbps: 384 }));
    const ac3 = audioOf(base).ac3;
    ac3.properties.dialnorm = '-24';

    const updated = audioOf(channelFormToConfig(withAudio({ codec: 'ac3', bitrateKbps: 448 }), base));
    expect(updated.ac3).toEqual({ encoder: 'dlbpatenc', properties: { dialnorm: '-24', bitrate: '448k' } });

    const custom = channelFormToConfig(withAudio({ codec: 'aac' }));
    audioOf(custom).aac.encoder = 'voaacenc';
    expect(audioOf(channelFormToConfig(withAudio({ codec: 'aac', bitrateKbps: 64 }), custom)).aac.encoder).toBe('voaacenc');

    const switched = audioOf(channelFormToConfig(withAudio({ codec: 'aac', bitrateKbps: 128 }), base));
    expect(switched).not.toHaveProperty('ac3');
  });

  it('reads the bitrate back instead of always showing 128', () => {
    for (const audio of [
      { codec: 'aac' as const, sampleRateHz: 48000, bitrateKbps: 96 },
      { codec: 'ac3' as const, sampleRateHz: 48000, bitrateKbps: 384 },
      { codec: 'ac3' as const, sampleRateHz: 44100, bitrateKbps: 160 },
    ]) {
      expect(configToChannelForm(channelFormToConfig(withAudio(audio))).outputs[0].audio.bitrateKbps).toBe(audio.bitrateKbps);
    }
  });

  it('refuses an AC-3 rate the Dolby encoder does not offer', () => {
    const result = channelFormSchema.safeParse(withAudio({ codec: 'ac3', sampleRateHz: 48000, bitrateKbps: 100 }));
    expect(result.success).toBe(false);
    expect(result.success ? [] : result.error.issues.map((i) => i.path.join('.'))).toContain('outputs.0.audio.bitrateKbps');
  });

});

describe('DASH output', () => {
  const dashForm = (patch: (o: ChannelFormValues['outputs'][0]) => void = () => {}): ChannelFormValues => {
    const form: ChannelFormValues = JSON.parse(JSON.stringify(Object.values(CHANNEL_PRESETS)[0].values));
    Object.assign(form.outputs[0], { outputType: 'dash', destinationUri: 'dash+file:/runtime/dash', container: 'fmp4' });
    form.outputs[0].audio.codec = 'aac';
    patch(form.outputs[0]);
    return form;
  };

  it('writes a dash block with the engine defaults and reads the output back as DASH', () => {
    const config = channelFormToConfig(dashForm(), null);
    const transport = config.transcoder.outputs[0].transports[0] as any;
    expect(transport.uri).toBe('dash+file:/runtime/dash');
    expect(transport.dash).toEqual({ segmentDuration: 2000, numSegments: 30 });
    expect(transport.hls).toBeUndefined();
    expect(config.transcoder.outputs[0].container).toBe('fmp4');
    expect(configToChannelForm(config).outputs[0].outputType).toBe('dash');
  });

  it('keeps edited dash settings and drops the block when the output stops being DASH', () => {
    const base = channelFormToConfig(dashForm(), null);
    (base.transcoder.outputs[0].transports[0] as any).dash = { segmentDuration: 3200, numSegments: 20, baseFileName: 'live' };
    expect((channelFormToConfig(dashForm(), base).transcoder.outputs[0].transports[0] as any).dash)
      .toEqual({ segmentDuration: 3200, numSegments: 20, baseFileName: 'live' });

    const hls = dashForm((o) => { o.outputType = 'hls'; o.destinationUri = 'hls+file:/runtime/hls'; });
    expect((channelFormToConfig(hls, base).transcoder.outputs[0].transports[0] as any).dash).toBeUndefined();
  });

  it('refuses a DASH output that is not fmp4 with AAC audio, as the engine would at startup', () => {
    const result = channelFormSchema.safeParse(dashForm((o) => { o.container = 'mpegts'; o.audio.codec = 'ac3'; }));
    expect(result.success).toBe(false);
    const paths = result.success ? [] : result.error.issues.map((i) => i.path.join('.'));
    expect(paths).toContain('outputs.0.container');
    expect(paths).toContain('outputs.0.audio.codec');
    expect(channelFormSchema.safeParse(dashForm()).success).toBe(true);
  });
});
