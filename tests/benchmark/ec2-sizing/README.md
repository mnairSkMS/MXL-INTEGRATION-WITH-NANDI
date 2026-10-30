# EC2 Sizing Kit — UTPL / Skandha Transcoder

Finds the **minimum** and **recommended** EC2 instance for each workload profile by running real
production transcoder containers (one channel per container) and adding channels until output stops
being real-time. Full method: [docs/sizing-plan.md](docs/sizing-plan.md).

> The kit contains the offline transcoder license. Treat the zip as confidential.

## 1. What EC2 configuration to test

**Common to every instance:** Ubuntu Server 24.04 LTS x86_64 AMI (pin one AMI ID for all runs),
on-demand (not Spot), one region, **gp3 root volume 60 GiB** (3000 IOPS / 125 MB/s default; enough
for image + Xiph media + results). Security group: SSH from your IP only. The harness runs channel
APIs on ports 18101+ with host networking, so never open those ports.

**Phase 1: screening (about 1 hour each, cheap).** Rank CPU families with `bench.py screen`:

| Instance | vCPU / GiB | Why |
| --- | --- | --- |
| `c7a.2xlarge` | 8 / 16 | AMD Genoa, 1 vCPU = 1 physical core |
| `c8a.2xlarge` | 8 / 16 | AMD, newest generation |
| `c7i.2xlarge` | 8 / 16 | Intel Sapphire Rapids, 1 vCPU = 1 hyperthread |
| `c8i.2xlarge` | 8 / 16 | Intel Xeon 6, newest generation |
| `c6i.2xlarge` | 8 / 16 | Older, cheaper baseline |

**Phase 2: density (step-load) on the two cheapest-per-channel families from phase 1:**

| Size | vCPU / GiB | Purpose |
| --- | --- | --- |
| `.4xlarge` | 16 / 32 | Main density measurement |
| `.8xlarge` | 32 / 64 | Confirms channels scale linearly with size |
| `m7a.4xlarge` (once) | 16 / 64 | Control group: proves extra memory buys nothing |

**Phase 3: 24 h soak** on the chosen instance at the recommended channel count.

**Do not use:** T-family burstable instances (`t3`, `t4g`), whose CPU credits run out under 24/7
encoding. Also avoid `-flex` variants and Spot, which give noisy results. Graviton (`c8g`) needs an
arm64 image and GPU instances need NVENC validation, so both are outside this kit.

## 2. Quick start on the instance

```bash
unzip ec2-sizing-kit.zip && cd ec2-sizing-kit
bash setup/ec2-setup.sh            # docker, sysstat, sysctl, log rotation; then log out/in
bash setup/load-image.sh           # loads transcoder-image.tar.gz (or pass a path)
bash media-gen/make-media.sh real synthetic    # + real4k for the 2160p profiles
python3 bench/bench.py check
```

If the zip has no `transcoder-image.tar.gz`, export it on the build machine first:
`bash setup/pack-image.sh <image:tag>` and copy it to the instance (`scp`, or S3).

**The image must be built from the same source as the profiles.** The profiles use configuration
`version: 1.5.0` (two inputs plus ad insertion). An older image fails at start with *"License in
version 1.5.0 is not supported by this transcoder version"*, which really means the config version
is unsupported. Run `gate` first to catch this.

## 3. Commands

| Step | Command | Time |
| --- | --- | --- |
| Functional gate (must pass before sizing) | `python3 bench/bench.py gate --profile p1-playout` | ~5 min |
| Encoder screening (phase 1) | `python3 bench/bench.py screen` | 30–60 min |
| Step-load (phase 2) | `python3 bench/bench.py stepload --profile p1-playout --start 2 --events` | 15 min per step |
| Faster exploratory step-load | `... stepload --profile p1-playout --warmup 120 --measure 180` | 5 min per step |
| CPU placement variant | `... stepload --cpu-mode cpuset --cpus-per-channel 2` | |
| Soak (phase 3) | `python3 bench/bench.py soak --profile p1-playout --channels N --hours 24 --events` | 24 h |
| Clean up | `python3 bench/bench.py stop` | |

Use `--content real` (default: Xiph high-motion footage), `synthetic` (noise, offline), or `repo`
(the low-motion adverts from the repo). Run long jobs under `tmux` or `nohup`.

Profiles (`profiles/`):

| Profile | Workload |
| --- | --- |
| `p1-playout` | Compose channel: playlist + SCTE ad insertion + L-band → 1080p25 x264 6 Mbps, TS/UDP |
| `p2-hls-ladder` | Same inputs → 1080/720/540/360 x264 ladder, multivariant HLS, 6 s segments to disk |
| `p3a-1080p50` | 1080p50 8 Mbps |
| `p3b-x265` | HEVC x265 superfast 4 Mbps |
| `p3d-2160p-in` | 2160p source → 1080p output |
| `p3e-2160p-out` | 2160p25 x264 20 Mbps output |
| `p3f-eac3-51` | E-AC-3 5.1 audio (integration gated) |

AV1 cannot be configured in the product (the parser accepts only `h264`/`h265`), so SVT-AV1 appears
only in `screen`.

## 4. What a "pass" means

Each channel's output is captured for 60 s and checked inside the image with `ffprobe` and TSDuck:
delivered fps within 0.5% of target, media time within 1% of wall time (real-time), no missing video
frames, audio dropout within a budget (default 3 s per capture, `--audio-gap-budget-ms`), zero TS continuity errors, correct resolution, container still running, host
CPU steal ≤ 2%. HLS checks segment production rate, variant count and frames per segment. A step
passes only if **every** channel passes.

## 5. Reading the results

Each run writes `results/<time>_<instance>_<kind>_<profile>/` containing `host.json`,
`samples.csv` (5 s host/channel time series), `step-NNN/step.json`, and `summary.json`.

| Value | Meaning |
| --- | --- |
| `n_max` | Most channels that still pass |
| `n_75` → **minimum spec** | Most passing channels at ≤ 75% average host CPU and ≤ 80% memory |
| `n_50` → **recommended spec** | ≤ 50% CPU and ≤ 60% memory, then must also pass the 24 h soak |
| `per_channel_vcpu` / `per_channel_mem_gib` | Slope of host usage against channel count |

Cost per channel-hour = on-demand price ÷ `n_75` (minimum) or ÷ `n_50` (recommended). Compare
instances on that number, never per vCPU.

## 6. Known observation: audio dropouts on an idle host

On a 28-vCPU desktop at about 0.9 cores of load, with and without the event storm, P1 output had
video exactly on time (1500 frames per 60 s, none missing) but about 1.1 s of audio per minute was
missing, in 4 or 5 gaps of 0.14-0.58 s. Because the host was idle, this is not a capacity effect, and
the cause is not identified. The harness therefore tolerates a budget instead of zero and reports
`audio_gap_ms_max` per step. If it **grows** as channels are added, that is load; if it stays near
1 s it is the engine's baseline. Treat it as a product question to raise separately.
