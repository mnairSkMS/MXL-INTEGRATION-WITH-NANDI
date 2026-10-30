import React, { useState, useEffect, useRef } from 'react';
import { useForm } from 'react-hook-form';
import { zodResolver } from '@hookform/resolvers/zod';
import { channelFormSchema } from '../schemas/channelSchema';
import { ChannelFormValues } from '../types/form';
import { EngineConfigDto } from '../types/api';
import { ProbeResponse, SupervisorState } from '../types/view';
import { CHANNEL_PRESETS } from '../constants/presets';
import { channelFormToConfig } from '../mappers/channelFormToConfig';
import { configToChannelForm } from '../mappers/configToChannelForm';
import {
  fetchEngineConfig,
  validateEngineConfig,
  saveEngineConfig,
  restartTranscoderEngine,
  checkTranscoderState,
  probeInputSource,
  stopTranscoderEngine,
  startTranscoderEngine,
} from '../api/channelApi';
import { deepEqual } from '../../../utils/deepEqual';
import { isHeldBy } from '../utils/sourceIdentity';
import { InputSection } from '../components/InputSection';
import { OutputSection } from '../components/OutputSection';
import { ProbeResultModal } from '../components/ProbeResultModal';
import { RestartConfirmModal } from '../components/RestartConfirmModal';
import { StopConfirmModal } from '../components/StopConfirmModal';
import { Button } from '../../../components/Button/Button';
import { Input } from '../../../components/Input/Input';
import { Select } from '../../../components/Select/Select';
import { AlertBanner } from '../../../components/AlertBanner/AlertBanner';
import { StatusBadge } from '../../../components/StatusBadge/StatusBadge';
import { Save, RefreshCw, Play, Square, ExternalLink } from 'lucide-react';

export const CreateChannelPage: React.FC = () => {
  // Base configuration loaded from disk
  const [baseConfig, setBaseConfig] = useState<EngineConfigDto | null>(null);
  const [supervisorState, setSupervisorState] = useState<SupervisorState>('UNKNOWN');
  const [isLoadingInitial, setIsLoadingInitial] = useState(true);

  // Probe state
  const [isProbing, setIsProbing] = useState(false);
  const [probeCountdown, setProbeCountdown] = useState(10);
  const [probeResult, setProbeResult] = useState<ProbeResponse | null>(null);
  const [probeError, setProbeError] = useState<{ message: string; status?: number } | null>(null);
  const [isProbeModalOpen, setIsProbeModalOpen] = useState(false);

  // Submission & Lifecycle state
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [submitPhase, setSubmitPhase] = useState<'idle' | 'validating' | 'saving' | 'restarting' | 'polling' | 'done'>('idle');
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [successMessage, setSuccessMessage] = useState<string | null>(null);
  const [isRestartModalOpen, setIsRestartModalOpen] = useState(false);

  const defaultPreset = CHANNEL_PRESETS['srt-to-hls'].values;

  const form = useForm<ChannelFormValues>({
    resolver: zodResolver(channelFormSchema),
    defaultValues: defaultPreset,
    mode: 'onBlur',
  });

  const { register, handleSubmit, reset, setValue, watch, formState: { errors } } = form;

  const [readOnlyReason, setReadOnlyReason] = useState<string | null>(null);
  const isReadOnly = !!readOnlyReason;

  // Reasons a probe must not be attempted, decided per input rather than for the screen as a
  // whole. The hazard is reopening a source the engine is *already holding*, not the engine
  // being up: probing a URI the operator has just typed is safe and is the main reason to be
  // here while a channel runs.
  const watchedInputs = watch('inputs');
  const engineIsUp = supervisorState === 'RUNNING' || supervisorState === 'STARTING';

  // What the engine actually holds is the config on disk it was started from, not whatever is
  // currently in the form - the operator may have already edited the field.
  const heldUris = React.useMemo(
    () => (baseConfig?.transcoder?.inputs || []).map((i) => i.transport?.uri),
    [baseConfig]
  );

  const probeBlockedReason = React.useCallback(
    (index: number): string | null => {
      const input = watchedInputs?.[index];
      if (!input) return null;

      if (input.transportType === 'ndi') {
        // Confirmed against this appliance: GET /inputs/probe answers 422 for ndi because
        // ffmpeg in the runtime image has no NDI input protocol. Integration gated.
        return 'NDI sources cannot be probed: this build of ffmpeg has no NDI input protocol.';
      }

      if (engineIsUp && isHeldBy(input.uri, heldUris)) {
        return 'This is the source the engine is currently running. Probing it would reopen the socket and can interrupt the live channel. Edit the URI, or stop the engine first.';
      }

      return null;
    },
    [watchedInputs, engineIsUp, heldUris]
  );

  const probeAbortController = useRef<AbortController | null>(null);
  const submitAbortController = useRef<AbortController | null>(null);
  const isSubmittingRef = useRef(false);

  useEffect(() => {
    return () => {
      probeAbortController.current?.abort();
      submitAbortController.current?.abort();
    };
  }, []);

  // 1. Initial State Loading (GET /config and GET /config/check-transcoder)
  useEffect(() => {
    const abortController = new AbortController();
    async function loadInitial() {
      try {
        const [config, status] = await Promise.all([
          fetchEngineConfig(abortController.signal),
          checkTranscoderState(abortController.signal).catch(() => ({ message: 'UNKNOWN' as SupervisorState })),
        ]);

        if (abortController.signal.aborted) return;

        setBaseConfig(config);
        setSupervisorState(status.message);

        if (config) {
          const formValues = configToChannelForm(config);
          setReadOnlyReason(formValues.readOnlyReason || null);
          reset(formValues);
        }
      } catch (err: any) {
        if (err.name === 'AbortError') return;
        setErrorMessage(err.message || 'Failed loading appliance state');
      } finally {
        if (!abortController.signal.aborted) {
          setIsLoadingInitial(false);
        }
      }
    }

    loadInitial();
    return () => abortController.abort();
  }, [reset]);

  // 2. Preset Handler
  const handlePresetChange = (presetKey: string) => {
    setValue('templatePreset', presetKey);
    if (CHANNEL_PRESETS[presetKey]) {
      reset(CHANNEL_PRESETS[presetKey].values);
      setSuccessMessage(`Applied preset: ${CHANNEL_PRESETS[presetKey].label}`);
      setTimeout(() => setSuccessMessage(null), 3000);
    }
  };

  // Engine power control.
  //
  // Stopping is how an operator frees a source the engine is holding so it can be probed. It
  // is deliberately not part of the save sequence: this takes the channel off air and nothing
  // puts it back but an explicit start.
  const [isPoweringEngine, setIsPoweringEngine] = useState(false);
  const [isStopConfirmOpen, setIsStopConfirmOpen] = useState(false);
  const powerAbortController = useRef<AbortController | null>(null);

  const applyEnginePower = async (action: 'stop' | 'start') => {
    if (powerAbortController.current) powerAbortController.current.abort();
    powerAbortController.current = new AbortController();
    const signal = powerAbortController.current.signal;

    setIsPoweringEngine(true);
    setErrorMessage(null);
    setSuccessMessage(null);

    try {
      const result =
        action === 'stop'
          ? await stopTranscoderEngine(signal)
          : await startTranscoderEngine(signal);
      setSupervisorState(result.message);

      // The engine reports STARTING before it reports RUNNING, and a config it rejects lands
      // in BACKOFF or FATAL rather than failing the call above. Settle the badge by polling
      // rather than leaving it on the first answer.
      if (result.message === 'STARTING') {
        for (let attempt = 0; attempt < 20 && !signal.aborted; attempt += 1) {
          await new Promise((resolve) => setTimeout(resolve, 750));
          if (signal.aborted) return;
          const status = await checkTranscoderState(signal).catch(() => null);
          if (!status) continue;
          setSupervisorState(status.message);
          if (status.message !== 'STARTING') break;
        }
      }

      setSuccessMessage(
        action === 'stop'
          ? 'Engine stopped. The channel is off air; inputs are released and can now be probed.'
          : 'Engine start requested. Watch the badge for RUNNING.'
      );
      setTimeout(() => setSuccessMessage(null), 6000);
    } catch (err: any) {
      if (err.name === 'AbortError') return;
      setErrorMessage(`Engine ${action} failed: ${err.message}`);
    } finally {
      setIsPoweringEngine(false);
    }
  };

  // 3. Probing Handler
  //
  // GET /inputs/probe opens the source itself, so it is only safe when the engine is not
  // holding that same endpoint. probeBlockedReason is what enforces that; this handler
  // re-checks rather than trusting the button, because the URI field can change under it.
  const handleProbeSource = async (index: number) => {
    const input = form.getValues(`inputs.${index}`);
    if (!input?.uri) return;

    const blocked = probeBlockedReason(index);
    if (blocked) {
      setErrorMessage(blocked);
      return;
    }

    if (probeAbortController.current) {
      probeAbortController.current.abort();
    }
    probeAbortController.current = new AbortController();
    const signal = probeAbortController.current.signal;

    // An SRT listener binds a port and waits for an encoder to call in - a person walking to
    // another machine, not a network round trip. The API caps any budget at 30s.
    const timeoutSeconds =
      input.transportType === 'srt' && input.srtMode === 'listener' ? 20 : 10;

    setIsProbing(true);
    setProbeCountdown(timeoutSeconds);
    setErrorMessage(null);
    setProbeError(null);
    setProbeResult(null);

    const timer = setInterval(() => {
      setProbeCountdown((c) => Math.max(0, c - 1));
    }, 1000);

    try {
      const result = await probeInputSource(
        {
          uri: input.uri,
          transport: input.transportType,
          timeoutSeconds,
          interfaceAddress: input.udpInterface,
        },
        signal
      );
      setProbeResult(result);
      setIsProbeModalOpen(true);
    } catch (err: any) {
      if (err.name === 'AbortError') return;
      // Shown in the dialog rather than the page banner: the banner sits above the form and
      // the button that started this is below the fold, so a banner-only failure looks like
      // the button did nothing.
      setProbeError({ message: err.message || 'The probe failed.', status: err.status });
      setIsProbeModalOpen(true);
    } finally {
      clearInterval(timer);
      setIsProbing(false);
    }
  };

  // Copy what the probe actually established onto the first output. Everything read here is
  // optional in the InputProbe model - ffprobe reports no frame rate it cannot derive, and
  // PIDs exist only on MPEG-TS - so each field is applied only when present. Writing an
  // undefined into the form would surface as NaN in a number input and then fail zod on
  // submit, a long way from the cause.
  const handleApplyProbeToOutputs = (probe: ProbeResponse) => {
    const applied: string[] = [];

    if (probe.video) {
      setValue('outputs.0.video.width', probe.video.width);
      setValue('outputs.0.video.height', probe.video.height);
      applied.push(`${probe.video.width}×${probe.video.height}`);

      if (typeof probe.video.frameRate === 'number') {
        setValue('outputs.0.video.frameRate', probe.video.frameRate);
        applied.push(`${probe.video.frameRate} fps`);
      }
    }

    const firstAudio = probe.audio[0];
    if (firstAudio) {
      setValue('outputs.0.audio.channels', firstAudio.channels);
      applied.push(`${firstAudio.channels}ch audio`);

      if (typeof firstAudio.sampleRateHz === 'number') {
        setValue('outputs.0.audio.sampleRateHz', firstAudio.sampleRateHz);
        applied.push(`${firstAudio.sampleRateHz} Hz`);
      }
    }

    // A splice PID can only be passed through if the probe found one and the container
    // reported its PID. Enabling passthrough against an unknown PID would produce a config
    // the engine accepts and then silently carries no markers.
    const splice = probe.data.find((d) => d.codec === 'scte_35' && typeof d.pid === 'number');
    if (splice) {
      setValue('outputs.0.scte.enabled', true);
      setValue('outputs.0.scte.mode', 'passthrough');
      setValue('outputs.0.scte.pid', splice.pid as number);
      applied.push(`SCTE-35 passthrough on PID ${splice.pid}`);
    }

    setSuccessMessage(
      applied.length > 0
        ? `Applied to Output 1: ${applied.join(', ')}`
        : 'Probe returned nothing that could be applied to the output ladder'
    );
    setTimeout(() => setSuccessMessage(null), 5000);
  };

  // Promise-based restart confirmation that pauses execution until user decides
  const pendingRestartResolve = useRef<((confirmed: boolean) => void) | null>(null);

  const requestRestartConfirmation = (): Promise<boolean> => {
    return new Promise((resolve) => {
      pendingRestartResolve.current = resolve;
      setIsRestartModalOpen(true);
    });
  };

  const handleRestartConfirm = () => {
    setIsRestartModalOpen(false);
    pendingRestartResolve.current?.(true);
    pendingRestartResolve.current = null;
  };

  const handleRestartCancel = () => {
    setIsRestartModalOpen(false);
    pendingRestartResolve.current?.(false);
    pendingRestartResolve.current = null;
  };

  // 4. Form Submit Orchestration
  const onSubmit = async (values: ChannelFormValues) => {
    await executeCommitAndRestart(values, false);
  };

  const executeCommitAndRestart = async (values: ChannelFormValues, skipRunningCheck = false) => {
    if (isSubmittingRef.current) return; // Duplicate submission lock
    isSubmittingRef.current = true;

    if (submitAbortController.current) {
      submitAbortController.current.abort();
    }
    submitAbortController.current = new AbortController();
    const signal = submitAbortController.current.signal;

    let pollTimeoutId: ReturnType<typeof setTimeout> | null = null;
    let cancelled = false;
    let isTimeout = false;
    let restartConfirmed = false;

    setIsSubmitting(true);
    setSubmitPhase('validating');
    setErrorMessage(null);
    setSuccessMessage(null);

    try {
      // Step A: Map Form to Config (Preserving unknown fields)
      const assembledConfig = channelFormToConfig(values, baseConfig);

      // Just-in-Time RUNNING state check (before validation)
      if (!skipRunningCheck) {
        const preSubmitStatus = await checkTranscoderState(signal).catch(() => ({ message: 'UNKNOWN' as SupervisorState }));
        if (preSubmitStatus.message === 'RUNNING') {
          // Pause and ask user
          const confirmed = await requestRestartConfirmation();
          if (!confirmed || signal.aborted) {
            return; // User cancelled — finally block handles cleanup
          }
          restartConfirmed = true;
          // User confirmed — continue with save+restart
        }
      }

      // Step B: Pre-flight Dry Run (PUT /config?dryRun=true)
      await validateEngineConfig(assembledConfig, signal);

      // Concurrency check after dry-run, immediately before save
      const currentConfig = await fetchEngineConfig(signal);
      if (!deepEqual(currentConfig, baseConfig)) {
        throw new Error('Configuration changed externally. Reload the page to reconcile. Note: full concurrency protection requires backend ETag support.');
      }

      // Step C: Commit Write (PUT /config)
      setSubmitPhase('saving');
      try {
        await saveEngineConfig(assembledConfig, signal);
      } catch (saveErr: any) {
        if (saveErr.name === 'AbortError' || saveErr.message?.includes('timeout')) {
          throw new Error('Save request timed out or was aborted. The configuration may have been saved server-side. Please reload the page to reconcile state before retrying.');
        }
        throw saveErr;
      }
      setBaseConfig(assembledConfig);

      // Step D: Apply & Restart (POST /config/restart-transcoder)
      setSubmitPhase('restarting');

      // Refresh state right before the disruptive action
      const preRestartStatus = await checkTranscoderState(signal).catch(() => ({ message: 'UNKNOWN' as SupervisorState }));
      if (preRestartStatus.message === 'UNKNOWN') {
        throw new Error('Supervisor state is UNKNOWN. Cannot safely issue restart command.');
      }
      // If RUNNING and this submission was not already confirmed, pause for confirmation
      if (preRestartStatus.message === 'RUNNING' && !skipRunningCheck && !restartConfirmed) {
        const confirmed = await requestRestartConfirmation();
        if (!confirmed || signal.aborted) {
          // Config is saved but user declined restart
          setSuccessMessage('Configuration saved to disk. Restart was cancelled by the operator.');
          return; // finally block handles cleanup
        }
        restartConfirmed = true;
      }

      try {
        await restartTranscoderEngine(signal);
      } catch (restartErr: any) {
        if (restartErr.name === 'AbortError') throw restartErr;
        setErrorMessage(
          `Configuration was saved to disk, but engine restart failed: ${restartErr.message}. The channel was not taken on air.`
        );
        return; // finally block handles cleanup
      }

      // Step E: Poll Status until RUNNING (GET /config/check-transcoder)
      setSubmitPhase('polling');

      pollTimeoutId = setTimeout(() => {
        isTimeout = true;
        submitAbortController.current?.abort();
      }, 30000); // 30 seconds strict deadline

      while (!signal.aborted) {
        try {
          const status = await checkTranscoderState(signal);
          // Guard against late responses after cancellation
          if (cancelled || signal.aborted) return;

          setSupervisorState(status.message);

          if (status.message === 'RUNNING') {
            setSubmitPhase('done');
            setSuccessMessage(
              'Channel created and supervisor reports engine RUNNING. (Note: Engine RUNNING confirms the pipeline process started; verified media output requires active input signal delivery).'
            );
            return;
          } else if (status.message === 'BACKOFF' || status.message === 'FATAL') {
            setErrorMessage(
              `Configuration saved, but transcoder entered ${status.message} state. The engine rejected the configuration or crashed during startup. Inspect container logs.`
            );
            return;
          }
        } catch (pollErr: any) {
          if (pollErr.name === 'AbortError') {
            // Could be timeout or navigation — fall through to finally
            throw pollErr;
          }
          // Ignore transient network blips during polling
        }

        // Wait before next poll — also abortable via signal
        await new Promise<void>((resolve, reject) => {
          const waitId = setTimeout(resolve, 1500);
          // If signal aborts during the wait, throw AbortError so outer catch handles it
          const onAbort = () => {
            clearTimeout(waitId);
            reject(new DOMException('The operation was aborted', 'AbortError'));
          };
          signal.addEventListener('abort', onAbort, { once: true });
        });
      }
    } catch (err: any) {
      if (cancelled) return;
      if (err.name === 'AbortError') {
        if (isTimeout) {
          setErrorMessage('Restart polling timed out after 30 seconds. Engine did not reach RUNNING state. Verify appliance supervisor state.');
        }
        return; // finally handles cleanup
      }
      setErrorMessage(err.message || 'Configuration submission failed');
    } finally {
      cancelled = true;
      if (pollTimeoutId !== null) clearTimeout(pollTimeoutId);
      isSubmittingRef.current = false;
      setIsSubmitting(false);
      if (submitPhase !== 'done') {
        // Only reset phase if we didn't reach success
        setSubmitPhase((prev) => prev === 'done' ? 'done' : 'idle');
      }
    }
  };

  if (isLoadingInitial) {
    return (
      <div style={{ padding: '4rem 2rem', textAlign: 'center' }}>
        <RefreshCw size={28} className="animate-spin" style={{ margin: '0 auto 1rem auto', color: 'var(--color-primary)' }} />
        <p>Connecting to the transcoder appliance...</p>
      </div>
    );
  }

  return (
    <div>
      {/* Top Banner & State */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1.5rem' }}>
        <div>
          <h1 style={{ fontSize: '1.5rem', fontWeight: 800, color: 'var(--color-text-main)' }}>
            Create New Channel
          </h1>
          <p style={{ fontSize: '0.875rem', color: 'var(--color-text-secondary)', marginTop: '0.25rem' }}>
            Define the source, build the output encoding ladder, validate, and take the channel on air.
          </p>
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '1rem' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontSize: '0.875rem' }}>
            <span style={{ color: 'var(--color-text-muted)' }}>Appliance Engine:</span>
            <StatusBadge state={supervisorState} />
          </div>
          {baseConfig ? (
            <span className="badge badge-info">Configured</span>
          ) : (
            <span className="badge badge-neutral">Unconfigured (Blank)</span>
          )}

          {engineIsUp ? (
            <Button
              type="button"
              variant="danger"
              style={{ padding: '0.375rem 0.75rem', fontSize: '0.8125rem' }}
              icon={<Square size={14} />}
              isLoading={isPoweringEngine}
              disabled={isPoweringEngine || isSubmitting}
              onClick={() => setIsStopConfirmOpen(true)}
            >
              Stop Engine
            </Button>
          ) : (
            <Button
              type="button"
              variant="secondary"
              style={{ padding: '0.375rem 0.75rem', fontSize: '0.8125rem' }}
              icon={<Play size={14} />}
              isLoading={isPoweringEngine}
              disabled={isPoweringEngine || isSubmitting || !baseConfig}
              title={!baseConfig ? 'Nothing to start: this appliance has no saved configuration.' : undefined}
              onClick={() => applyEnginePower('start')}
            >
              Start Engine
            </Button>
          )}
        </div>
      </div>

      {/* Warning if replacing running config */}
      {baseConfig && (
        <AlertBanner
          type="warning"
          title="Single-Channel Appliance Notice"
          message="An engine configuration is already present on this appliance. Saving and taking this channel on air will overwrite the active configuration and replace the running broadcast."
        />
      )}

      {/* Success Notification */}
      {successMessage && (
        <AlertBanner
          type="success"
          title="Operation Completed"
          message={
            <div>
              {successMessage}
              {submitPhase === 'done' && (
                <div style={{ marginTop: '0.5rem', display: 'flex', gap: '1rem' }}>
                  <a href="/hls/master.m3u8" target="_blank" rel="noreferrer" className="btn btn-outline" style={{ padding: '0.25rem 0.5rem', fontSize: '0.75rem' }}>
                    <ExternalLink size={12} /> HLS Manifest
                  </a>
                  <a href="/player" target="_blank" rel="noreferrer" className="btn btn-primary" style={{ padding: '0.25rem 0.5rem', fontSize: '0.75rem' }}>
                    <Play size={12} /> Open Monitor Player
                  </a>
                </div>
              )}
            </div>
          }
        />
      )}

      {/* Read Only Complexity Notification */}
      {isReadOnly && (
        <AlertBanner
          type="warning"
          title="Advanced Configuration Locked"
          message={readOnlyReason || 'Configuration is too complex to be edited via this UI.'}
        />
      )}

      {/* Error Notification */}
      {errorMessage && (
        <AlertBanner
          type="danger"
          title="Action Failed"
          message={errorMessage}
        />
      )}

      {/* Main Form */}
      <form onSubmit={handleSubmit(onSubmit)}>
        {/* General Channel Header Card */}
        <div className="card">
          <div className="card-header">
            <h2 className="card-title">Channel Identity & Template</h2>
          </div>
          <div className="card-body">
            <div className="grid-2">
              <Input
                label="Channel Name"
                required
                {...register('channelName')}
                error={errors.channelName?.message}
                placeholder="e.g. Skandha-Live-24x7"
                helperText="Display name for monitoring and operational identification"
              />

              <Select
                label="Channel Preset Template"
                options={[
                  { value: 'srt-to-hls', label: 'SRT Caller → HLS (MPEG-TS)' },
                  { value: 'srt-to-udp', label: 'SRT Listener → UDP/TS' },
                  { value: 'srt-to-srt', label: 'SRT Caller → SRT Pass-through Relay' },
                ]}
                value={watch('templatePreset') || 'srt-to-hls'}
                onChange={(e) => handlePresetChange(e.target.value)}
                helperText="Applies proven broadcast defaults from config/ presets"
              />
            </div>
          </div>
        </div>

        {/* Input Sources Section */}
        <InputSection
          form={form}
          onProbeSource={handleProbeSource}
          isProbing={isProbing}
          probeCountdown={probeCountdown}
          probeBlockedReason={probeBlockedReason}
        />

        {/* Output & Encoding Ladders Section */}
        <OutputSection form={form} />

        {/* Action Controls */}
        <div style={{ display: 'flex', justifyContent: 'flex-end', gap: '1rem', marginTop: '2rem' }}>
          <Button
            type="button"
            variant="secondary"
            disabled={isSubmitting || isReadOnly}
            onClick={() => {
              const preset = CHANNEL_PRESETS['srt-to-hls'].values;
              reset(preset);
            }}
          >
            Reset to Defaults
          </Button>

          <Button
            type="submit"
            variant="primary"
            style={{ padding: '0.625rem 1.5rem', fontSize: '1rem' }}
            isLoading={isSubmitting}
            disabled={isSubmitting || isReadOnly}
            icon={<Save size={18} />}
          >
            {isSubmitting ? (
              submitPhase === 'validating' ? 'Validating Config (Dry Run)...' :
              submitPhase === 'saving' ? 'Writing Configuration...' :
              submitPhase === 'restarting' ? 'Restarting Transcoder Engine...' :
              submitPhase === 'polling' ? 'Waiting for Engine RUNNING State...' :
              'Processing...'
            ) : (
              supervisorState === 'RUNNING' ? 'Save, Apply & Restart' : 'Save & Take On Air'
            )}
          </Button>
        </div>
      </form>

      {/* Probing Details Modal */}
      <ProbeResultModal
        isOpen={isProbeModalOpen}
        onClose={() => {
          setIsProbeModalOpen(false);
          setProbeError(null);
        }}
        probeData={probeResult}
        probeError={probeError}
        onApplyToOutputs={handleApplyProbeToOutputs}
      />

      <RestartConfirmModal
        isOpen={isRestartModalOpen}
        onClose={handleRestartCancel}
        onConfirm={handleRestartConfirm}
        isLoading={false}
      />

      <StopConfirmModal
        isOpen={isStopConfirmOpen}
        onClose={() => setIsStopConfirmOpen(false)}
        onConfirm={() => {
          setIsStopConfirmOpen(false);
          applyEnginePower('stop');
        }}
        isLoading={isPoweringEngine}
      />
    </div>
  );
};
