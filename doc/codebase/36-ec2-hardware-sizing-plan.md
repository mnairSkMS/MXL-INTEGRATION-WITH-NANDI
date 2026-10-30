# EC2 Hardware Sizing Plan

Written 2026-10-16 against source version `1.1.0-beta.2-skandha.3`. **This is a test plan, not a
result.** No EC2 instance has been benchmarked. Every number below is either an existing desktop
observation, with its source, or a planning prior that the plan is designed to replace.

## Goal and scope

Produce a **minimum** and a **recommended** Ubuntu EC2 hardware specification for each of three
workload profiles, expressed as channels per instance and cost per channel-hour:

| Profile | What it is | Status of the media path today |
| --- | --- | --- |
| **P1 Playout** | The Compose channel: managed playlist + SCTE ad-insertion secondary input + static graphics/squeeze → 1080p25 H.264 (x264 `veryfast`/`zerolatency`, 6 Mbps) + AAC stereo, MPEG-TS over UDP. The preview relay is removed. | Runtime verified ([02](02-feature-catalog.md)) |
| **P2 HLS ABR ladder** | P1's input graph with a four-rung x264 ladder (1080p/720p/540p/360p) delivered as multivariant HLS to an HTTP origin, plus one S3 variant. | Group/multivariant HLS is statically implemented; S3 is integration gated |
| **P3 Heavier codecs/resolutions** | Variants of P1: 1080p50, x265 HEVC, SVT-AV1, 2160p input, 2160p output, Dolby E-AC-3 5.1 audio. | H.265 unverified; 4K/AV1 integration gated; Dolby build verified only |

Sizing target: **many channels per instance**. Encoders: **CPU only** (x264, x265, SVT-AV1). GPU
(NVENC/VA-API) and Graviton (arm64) are out of scope for this round.

## What is already known

| Observation | Value | Source |
| --- | --- | --- |
| P1 with the preview relay (two x264 encodes) | 1.74–2.79 host cores, 668–834 MiB | [29](29-old-version-feature-migration.md), `docker stats` snapshots |
| Preview relay alone | ≈1 core, ≈219 MiB | [10](10-video-processing.md) |
| P1 idle on a low-resolution proxy | 2–4% CPU, ≈300 MiB | [03](03-build-development-and-runtime.md) |
| Host that produced those numbers | Intel i7-14700F, up to 5.4 GHz, 28 threads | `lscpu` on the development workstation |
| Runtime image | 1.78 GB, Ubuntu 24.04.4, includes `ffmpeg`, `ffprobe`, `tsp`; **no** `gst-launch-1.0`/`gst-inspect-1.0` | `skandha-transcoder:mbb` |
| Encoder plugins in the image | `x264`, `x265`, `svtav1`, `aom`, `openh264` (plus `nvcodec`, `va`, unused here) | image plugin directory |

These are single snapshots on a desktop CPU that clocks well above any EC2 part. **Expect higher
per-channel vCPU use on EC2.**

### Product constraints that shape the test

- **One channel per container.** The parser consumes one channel definition per process, and the
  gRPC server binds a hard-coded `unix:///tmp/transcoder.grpc.socket`
  (`transcoder/app/src/application/transcoder/api/grpc.cpp:164`). Two engines sharing `/tmp` would
  collide. Density therefore means N containers, each with its own API, Supervisor, SQLite files,
  runtime directory, API port, and UDP output port. Per-channel memory includes the Python API.
- **x264 must run `tune=zerolatency`.** This uses sliced threads, so one encoder scales across cores
  less well than frame-threaded x264 ([10](10-video-processing.md)). Many small encoders packed on
  one host is the expected shape, which suits many-channel density.
- **No product telemetry.** All measurements come from the host, cgroups, GStreamer tracers, and
  captured output ([32](32-performance-and-tooling-findings.md)).
- **Known slow-growth risks** that a short test will not show: SQLite WAL growth (32 §5), unbounded
  Docker JSON logs when the logger is at `log` level, and open long-duration repeated-cue memory
  testing ([29](29-old-version-feature-migration.md)). The soak phase exists for these.

### Planning priors (to be replaced by measurement)

Use these only to choose instance sizes for the test so the step-load has room to find a limit.

| Profile | Prior per channel | Reasoning |
| --- | --- | --- |
| P1 | 1.5–3 vCPU, ≤1 GiB | Desktop ≈0.7–1.8 cores with the relay removed; EC2 cores are slower, Intel vCPUs are hyperthreads |
| P2 | 2.5–5 vCPU, 1–1.5 GiB | Ladder pixel count ≈1.8× a single 1080p rung, plus one decode shared by all rungs |
| P3 x265 / SVT-AV1 | 3–8× P1 at comparable speed presets | Typical codec cost ratios; highly preset-dependent |
| P3 2160p output | ≈4× P1 | Four times the pixels; the compositor runs on one aggregator thread and may bind first |

With ≈2 GiB per vCPU on compute-optimised families, **memory is unlikely to be the binding limit**.
Phase 2 must confirm that before the C family is chosen over M.

## Candidate instances

| Family | vCPU meaning | Why include it |
| --- | --- | --- |
| `c7a`, `c8a` (AMD EPYC) | **1 vCPU = 1 physical core**, no SMT | Likely best x264 cost per channel; c8a is the newer generation with higher clocks |
| `c7i`, `c8i` (Intel Xeon) | 1 vCPU = 1 hyperthread | AVX-512 and high all-core turbo; compare on cost per channel, never per vCPU |
| `m7a` or `m8a` | as `c7a` | Control group. Proves whether extra memory per vCPU buys anything |
| `c6i` | hyperthread | Older and cheaper baseline. Drop it if it loses the screening round clearly |

**Excluded:** T-family instances. Their CPU credits run out under 24/7 encoding and output stops
keeping up with real time. Also excluded are `-flex` variants, which are built for workloads that do
not run at sustained full CPU; test one explicitly before deploying on it. Graviton and GPU families
are out of scope for this round.

Do not quote vendor speed-up percentages in the result. The only valid comparison is this
workload's measured channels per instance multiplied by the current on-demand price in the
deployment region.

## Configuration matrix

| Dimension | Values | Applies to |
| --- | --- | --- |
| Profile | P1, P2, P3a–P3f | all |
| Instance | Screening: 2xlarge of each candidate. Density: two shortlisted families at 4xlarge and 8xlarge | phases 1–2 |
| Channels per instance | 1, 2, 3 … up to failure (step-load) | phase 2 |
| CPU placement | (a) shared default scheduling; (b) `--cpus=<n>` quota per container; (c) `--cpuset-cpus` pinning | phase 2, shortlisted size |
| x264 `threads` | 0 (auto, which sees **all** host cores and oversubscribes at high N) vs. explicit per-channel value | P1/P2. Changes slice count, so re-check decoder compatibility |
| x264 preset | `superfast`, `veryfast` (current), `faster` | P1 quality check (phase 3) |
| Content | Low motion (current Air Asia / One Cent Call 1080p25) and **high motion** (sports or confetti-style 1080p25/50 clips) | all. The spec uses high motion |
| Event load | Steady (no cues) vs. event storm (graphic in/out, squeeze, ad break every few minutes) | P1/P2 |
| HLS delivery | Local nginx origin (PUT/WebDAV) vs. S3 | P2 |
| Storage | gp3 defaults (3000 IOPS, 125 MB/s) | P2 segment writes, SQLite, logs |

P3 variants:

| ID | Change from P1 |
| --- | --- |
| P3a | 1080p50 source and output (`rate: 50`, `key-int-max: 100`) |
| P3b | `x265enc` HEVC 1080p25, `speed-preset=superfast`/`veryfast`, `tune=zerolatency` |
| P3c | `svtav1enc` 1080p25, presets 10 and 12 |
| P3d | 2160p H.264/HEVC source, 1080p25 output (decode and scale cost) |
| P3e | 2160p25 x264 output |
| P3f | Dolby E-AC-3 5.1 audio path in place of AAC stereo |

## Tooling

| Purpose | Tool | Notes |
| --- | --- | --- |
| Quick per-instance CPU screening | **Phoronix Test Suite** `pts/x264`, `pts/x265`, `pts/svt-av1` | Rank families cheaply. A proxy only, not the spec |
| Maximum encode throughput of the real graph | `gst-launch-1.0` with `fakesink sync=false` in `utpl/devel` or `utpl-gstreamer-test` | Uses the same libx264 and patched GStreamer as production. Gives the theoretical ceiling as fps ÷ target fps |
| Host CPU, steal, per-core saturation | `sysstat`: `mpstat -P ALL`, `pidstat -u -r -t`, `sar` | `%steal` must stay ≈0. One core pinned at 100% with low total load means a single-thread bottleneck |
| Per-container CPU/memory | `docker stats`, **cAdvisor** | Per-channel RSS and its slope over time |
| In-pipeline latency and element cost | `GST_TRACERS="latency;rusage;stats"` with `gst-stats-1.0`; **GstShark** `proctime`, `interlatency`, `queuelevel`, `framerate` | Growing latency or full queues show falling behind before frames are dropped |
| Leak detection | `GST_TRACERS=leaks` (soak only, short targeted runs) | Pair with RSS slope |
| Output correctness | **TSDuck** `tsp -P continuity -P pcrverify -P bitrate_monitor`; `ffprobe` frame/PTS counts | A zero exit code is not a pass ([AGENTS.md](../../AGENTS.md)) |
| HLS correctness | Manifest/segment cadence check (script or `ffprobe` per segment) | Each segment must be published within its target duration |
| Quality per preset | ffmpeg `libvmaf` | Confirms the preset the spec is built on is acceptable |
| Hot-spot diagnosis when a step fails | `perf top`, `perf record -g` | Only to explain a limit, not to set it |
| Network and disk | `iperf3` (only if SRT/UDP crosses instances), `fio` | Delivery headroom; gp3 adequacy for P2 |
| Soak monitoring | node_exporter + cAdvisor + Prometheus/Grafana, or the CloudWatch agent | Needed for the 24 h runs |

### Representative commands

Maximum throughput of the P1 encode branch (run 1, 2, 4 … copies in parallel to see aggregate scaling):

```bash
/usr/bin/time -v gst-launch-1.0 -q filesrc location=clip1080p25.mp4 ! qtdemux ! h264parse ! avdec_h264 ! \
  videoconvertscale ! video/x-raw,width=1920,height=1080,format=I420 ! \
  x264enc speed-preset=veryfast tune=zerolatency bitrate=6000 key-int-max=50 ! \
  video/x-h264,profile=high ! fakesink sync=false
# fps = decoded frame count / elapsed wall time; channel ceiling ≈ fps / 25
```

Real-time verification of one channel's UDP output over 60 s:

```bash
tsp -I ip 127.0.0.1:5555 -P until --seconds 60 \
    -P continuity -P pcrverify -P bitrate_monitor --periodic-bitrate 10 -O file cap.ts
ffprobe -v error -select_streams v:0 -count_packets \
    -show_entries stream=nb_read_packets -of csv=p=0 cap.ts   # expect 1500 at 25 fps
```

Host sampling during a step:

```bash
mpstat -P ALL 5 > mpstat.log &  pidstat -u -r -h 5 > pidstat.log &
docker stats --no-stream --format '{{.Name}},{{.CPUPerc}},{{.MemUsage}}'   # every 30 s
```

## Pass/fail criteria for one channel at one step

A step passes only if **every** channel on the instance passes:

| Check | Pass |
| --- | --- |
| Video frame count over a 60 s capture | Exactly target fps × 60 (P1: 1500), no duplicates or gaps |
| TSDuck continuity / PCR | 0 continuity errors; PCR accuracy within TSDuck's default tolerance |
| Bitrate | Within ±10% of the configured rate, averaged over 10 s windows |
| Latency (`latency` tracer) | Flat over the step; no monotonic growth |
| Engine log | No QoS, late-buffer, or dropped-frame warnings |
| HLS (P2) | Every rung publishes each segment within its target duration; all rungs keep the same segment sequence |
| Host | `%steal` < 1%; no OOM kills; no container restarts |

## Phases

### Phase 0: preparation and functional gate (local, then one EC2 instance)

1. Build the image from the exact source being shipped and record its digest, `version.txt`, and
   plugin list. The sibling `skandha-transcoder-main` checkout carries newer RTMP/HLS work.
   Benchmark whichever build will deploy, not this one by default.
2. Write one channel config per profile, derived from
   `config/migrated-media-playlist.json`, with the preview relay removed. P2 needs
   a new group/multivariant HLS config. P3 needs one config per variant.
3. Prepare a ≥10 minute high-motion 1080p25 clip, a 1080p50 clip, a 2160p clip, and an E-AC-3 5.1
   source. Low-motion advertising clips understate encoder cost.
4. Build a multi-channel launcher: N containers with distinct names, API ports, UDP output ports,
   and runtime volumes, plus log rotation (`max-size`) in `/etc/docker/daemon.json`.
5. **Functional gate.** Each profile must pass the criteria above at N = 1 before it is benchmarked.
   A profile that fails (likely candidates: P2 S3, P3d–P3f) is reported as *not supported yet*,
   with the failure recorded in [27](27-known-gaps-contradictions-and-remediation.md), not sized.

### Phase 1: screening (≈2 h per instance type)

On a 2xlarge of each candidate family, on a fresh Ubuntu 24.04 AMI with Docker CE:

1. Phoronix `pts/x264` and `pts/x265`.
2. The maximum-throughput pipeline for P1 and the P2 ladder, at 1, 2, 4, and 8 parallel copies.
3. Compute aggregate fps per dollar-hour. Shortlist the best two families.

### Phase 2: step-load density (≈4 h per family per size)

On each shortlisted family at 4xlarge and 8xlarge, per profile:

1. Start one channel. Allow 5 minutes of warm-up and 10 minutes of measurement. Apply the pass/fail
   criteria and record host CPU, per-container CPU and RSS, and steal.
2. Add one channel and repeat until a step fails. Run the event storm during the last step before
   failure.
3. Repeat the first passing configuration with the CPU-placement and `threads` variants. Keep the
   best one.
4. Run every result on two separately launched instances. Report the lower one, because silicon and
   host placement vary.

From each run derive:

- **N_max**: the highest passing channel count.
- **Per-channel cost**: the slope of a linear fit of host CPU-seconds per second against N, and of
  RSS against N (not a single-point division).
- **N_75** and **N_50**: the highest passing N with sustained host CPU ≤ 75% and ≤ 50%.

### Phase 3: preset quality check (P1, ≈2 h)

Encode the same high-motion clip at 6 Mbps with `superfast`, `veryfast`, and `faster`, and measure
VMAF against the source. If `superfast` stays within an agreed VMAF margin of `veryfast`, report the
density gain as an option. Do not change the shipped preset silently.

### Phase 4: soak (24 h per profile)

Run P1 and P2 at the candidate recommended N on the chosen instance, with an event storm every 10
minutes. Pass if:

- Every channel passes the 60 s output check sampled every hour.
- The per-container RSS slope is ≈0 after the first hour.
- The SQLite WAL and Docker logs stay bounded.
- No container restarts.

If the soak fails, the recommended N is not valid. Fix the defect or lower N.

## Deriving the specification

For each profile:

| Spec | Rule |
| --- | --- |
| **Minimum** | Smallest instance that passes at the stated N with sustained host CPU ≤ 75%, memory ≤ 80%, and the event storm passing. Not soak-validated. |
| **Recommended** | Passes at N with sustained host CPU ≤ 50% and memory ≤ 60%, the event storm passing, and the 24 h soak passing. |
| vCPU | ⌈N × per-channel CPU slope ÷ target utilisation⌉, rounded up to an instance size |
| Memory | N × peak per-channel RSS × 1.3 + 1 GiB for the OS and Docker |
| Root EBS (gp3) | 1.8 GB image + media library + rotated logs + SQLite/runtime + ≥20% free; at least 30 GiB |
| Network | ≥ 2 × N × the sum of output bitrates, checked against the instance's **baseline** bandwidth, not its burst figure |

Report each profile as a table with columns *profile, spec level, instance, vCPU, GiB, channels per
instance, cost per channel-hour, evidence run IDs*. Keep raw logs and the exact image digest with the
result.

## Deployment notes for Ubuntu EC2

- Use Ubuntu 24.04 LTS, which matches the image base. The host kernel matters little because the
  transcoder is containerised. Pin the AMI ID used for the benchmark.
- Raise `net.core.rmem_max` and `net.core.wmem_max` if inputs or outputs use UDP/SRT at high
  channel counts. Re-check the TSDuck criteria after any change.
- **UDP multicast does not work in a plain VPC.** It needs Transit Gateway multicast. Design for
  unicast UDP, SRT, or HLS unless multicast is a hard requirement.
- The management API is unauthenticated ([22](22-licensing-crypto-and-security.md)). Bind each
  container's API port to loopback or a private subnet, and do not open it in a security group.
- Disable `unattended-upgrades` for the duration of a benchmark run so package updates do not
  disturb measurements.

## Open inputs

The following were not specified when this plan was written. The values in brackets are the
assumed defaults.

- P2 ladder bitrates [6000/3500/2000/800 kbps] and segment length [the example config's 5 s].
- HLS origin for the main P2 run [local nginx; S3 as a variant].
- Deployment region [the one used for pricing].
- Acceptable VMAF margin for a cheaper preset [unset; owner to decide].
- Target channel count per instance, if fixed in advance [unset; the plan reports the curve].
