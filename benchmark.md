
# EC2 Sizing Benchmark Report: Skandha / UTPL Transcoder

Date: 2026-10-16. Kit: `ec2-sizing-kit` 1.0.0 (built from `1.1.0-beta.2-skandha.3`).
Image: `skandha-transcoder:bench-20261016` (`sha256:fe67deb7fad23f492b49369dcca3431f5b1617ecb51c81afceaa4ce7322369c7`).

## 1. Scope and status

This report covers the **functional gate** (one channel, N=1) for all seven profiles, run on two
instance sizes. It is **not** a sizing result. The step-load (N_max, N_75, N_50), the 24 h soak,
and the screening of other CPU families were **not** run.

| Phase in sizing plan | Status |
| --- | --- |
| Phase 0 functional gate, N=1 | Done on c6a.xlarge (partial) and c6a.2xlarge (all 7 profiles) |
| Phase 1 screening (c7a, c8a, c7i, c8i, c6i) | Not done. No AWS CLI or credentials in this session, so other instances could not be launched |
| Phase 2 step-load density | Not done |
| Phase 3/4 preset quality, 24 h soak | Not done |

## 2. Test environment

| Item | c6a.xlarge (first box) | c6a.2xlarge (main results) |
| --- | --- | --- |
| vCPU / RAM | 4 / 7.55 GiB | 8 / 15.25 GiB |
| CPU | AMD EPYC 7R13, 2 threads per core | AMD EPYC 7R13 |
| Region / AZ | ap-south-1 / ap-south-1b | ap-south-1 |
| OS / kernel | Ubuntu 24.04.4 LTS, 6.17.0-1017-aws | same |
| Docker | 29.8.0 (Docker CE) | same |

The box was resized in place between the two sessions. The image and media survived the resize.

Setup deviations from `setup/ec2-setup.sh`:

- `docker.io` was skipped because Docker CE and `containerd.io` were already installed and conflict with it.
- The script's other changes were applied: Docker log limits (`/etc/docker/daemon.json`), UDP buffer sysctls (`/etc/sysctl.d/90-utplbench.conf`), and disabled `unattended-upgrades` and `apt-daily` timers.

Gate method (from `bench.py`): one container per channel, 90 s warm-up, 60 s UDP capture analysed with
`ffprobe` and TSDuck. A channel passes if delivered fps is within 0.5% of target, media time is
within 1% of wall time, there are no missing video frames and **zero** audio gaps, there are no
continuity errors, the resolution is right, the container is still running, and CPU steal is at most 2%.

Content:

- P1, P2, P3a, P3b, P3f: Xiph derf 1080p high-motion footage (crowd_run, park_joy, ducks_take_off).
- P3d, P3e: synthetic 4K clip (`testsrc2` plus temporal noise, x264 veryfast crf 23). It is **not** real footage.
- Every clip was looped to 10 minutes by stream copy (see section 6).

## 3. Results on c6a.2xlarge (8 vCPU, 15 GiB)

CPU steal was at most 0.025% in every run.

| Profile | What it is | Result | Channel vCPU | Host CPU avg / p95 / max | Memory (channel peak) | Host RAM used |
| --- | --- | --- | --- | --- | --- | --- |
| P1 playout | Playlist plus SCTE ad insertion plus L-band, 1080p25 x264 6 Mbps, AAC stereo, MPEG-TS/UDP | **Pass** | 2.73 | 34.2 / 38.1 / 38.3 % | 646 MiB | 13.8 % |
| P3f E-AC-3 5.1 | P1 with E-AC-3 5.1 audio | **Pass** | 2.82 | 35.3 / 39.4 / 39.6 % | 557 MiB | 13.6 % |
| P3d 2160p input | 4K source decoded and scaled to 1080p25 output | **Pass** | 2.61 | 32.8 / 33.2 / 33.4 % | 754 MiB | 15.0 % |
| P3a 1080p50 | P1 at 50 fps, 8 Mbps | **Pass** | 4.57 | 57.5 / 63.6 / 63.7 % | 496 MiB | 13.1 % |
| P3e 2160p output | 4K x264 at 20 Mbps | **Pass** | 4.81 | 60.8 / 61.6 / 61.7 % | 1,180 MiB | 17.7 % |
| P3b x265 | HEVC 4 Mbps instead of H.264 | **Fail** | 6.93 | 87.3 / 89.4 / 90.0 % | 621 MiB | 14.0 % |
| P2 HLS ladder | 4 x264 levels (1080/720/540/360p), multivariant HLS, 6 s segments to disk | **Fail (functional)** | – | – | – | – |

Stream measurements for the passing runs (all with 0 missing frames, 0 duplicate frames, 0 continuity errors, 0 audio gaps):

| Profile | Delivered fps | Video frames in 60 s | Bitrate (video plus audio) | Resolution |
| --- | --- | --- | --- | --- |
| P1 | 25.000 | 1500 | 6,481 kbps | 1920x1080 |
| P3f | 25.000 | 1500 | 6,600 kbps | 1920x1080 |
| P3d | 25.000 | 1500 | 6,536 kbps | 1920x1080 |
| P3a | 49.999 | 3000 | 8,555 kbps | 1920x1080 |
| P3e | 25.000 | 1500 | 20,844 kbps | 3840x2160 |

### Failure details

**P3b (x265).** Two problems were reported.

1. *Audio gaps 25.* The host ran at 87% average CPU and the channel used 6.93 of 8 vCPU. Video still held 25.07 fps with no missing frames, but audio fell behind. This is consistent with a capacity limit.
2. *Resolution 0x0.* `ffprobe` could not read the HEVC stream parameters from the 60 s capture. This could be a real HEVC output problem (the docs list H.265 as not runtime-verified) or a mid-stream capture artefact. **It was not diagnosed.**

Also unexplained: P3b's measured bitrate was 6,031 kbps against a 4,128 kbps video target.

**P2 (HLS ladder).** The container started and built its video pipeline, but the channel's `hls/` directory stayed empty for the full 180 s wait (`no output within 180s`). The engine log contains no HLS message, error or segment write. This is not a CPU symptom. Per the sizing plan, a profile that fails the functional gate is reported as "not supported yet" and not sized. **The cause was not found.** An earlier P2 attempt on the 4 vCPU box was killed by the tool (exit 137) and gave no usable result.

## 4. Results on c6a.xlarge (4 vCPU, 7.55 GiB), for comparison

Run with the corrected 10-minute content.

| Profile | Result | Channel vCPU (of 4) | Host CPU avg / p95 / max | Memory |
| --- | --- | --- | --- | --- |
| P1 playout | Pass | 2.84 | 72.4 / 82.6 / 82.6 % | 472 MiB |
| P3f E-AC-3 5.1 | Pass | 2.95 | 75.1 / 85.2 / 85.3 % | 505 MiB |
| P3a 1080p50 | **Fail** (279 audio gaps) | 3.67 | 93.4 / 96.2 / 97.2 % | 517 MiB |
| P2, P3b, P3d, P3e | Not run | – | – | – |

P1 and P3f sat above the plan's 75% CPU line for the minimum spec on this box, and P3a ran the host at 93%, so one channel
of 1080p50 does not fit in 4 vCPU. The same P1 channel costs about 2.7-2.8 vCPU on both boxes, so the 8 vCPU box
passed P3a because of headroom, not because the work changed.

## 5. Findings

1. On an 8 vCPU c6a.2xlarge, every profile except P2 and P3b passes the one-channel gate.
2. Per-channel CPU: P1, P3f and P3d about 2.6-2.8 vCPU; P3a and P3e about 4.6-4.8 vCPU; P3b about 6.9 vCPU and
   failing. P3e is cheaper than the plan's prior (about 4x P1, 6-12 vCPU); the measurement is about 1.8x P1.
3. Memory is not a constraint. The largest run was a 4K-output channel at 1.18 GiB, with host RAM at most 18% used.
4. Channel density is **not** established. P1, P3f and P3d use roughly a third of the host, which suggests 2 channels
   per 8 vCPU, but only the step-load can show that.
5. The 4K results use synthetic content and may not match real 4K footage.

## 6. Mistakes made during testing and their corrections

These affected earlier numbers, so they are recorded here.

1. **Contended first run.** The first P1 gate on the 4 vCPU box ran while the media-encoding job was using all four cores. That result (fps 24.8, 125 audio gaps) was invalid and is discarded.
2. **Audio gaps were a test-content artefact, not a product defect.** The Xiph clips are about 10 s each, so the 30 s test programme looped twice per capture. Each loop restart left one audio hole, at exactly 30 s spacing, matching the "Committed playlist item ... index 0" log lines. The reported "34" and "280" gap counts were a few holes multiplied by the missing frames. With 10-minute looped clips, P1 and P3f show zero gaps. The `fdkaacenc` log warnings cited earlier were unrelated.
3. **PCR jitter counts (about 900-1,400 per run) are not meaningful.** TSDuck was run on a variable-bitrate file capture. The counts appear on passing runs and the gate ignores them.
4. **Killed media job.** A 4K encode with a noise filter at crf 14 was killed mid-run, leaving a corrupt file that was deleted and rebuilt with lighter settings.
5. **Stray containers.** An earlier P2 attempt left a container running after the tool was killed. A later "container exited" on the 4 vCPU box was caused by the cleanup command, not by P2.

## 7. Not done and next steps

- Step-load (`bench.py stepload`) on c6a.2xlarge, at least P1, to get N_max, N_75 and N_50 and per-channel slope. The README estimates 15 min per step, or about 5 min per step with `--warmup 120 --measure 180`.
- Diagnose P2 (empty HLS output) and P3b (HEVC parameter sets, bitrate overshoot) in the bundled source.
- Real 4K footage (`make-media.sh real4k`, a 6.2 GB download) for P3d and P3e.
- Phase 1 screening on c7a, c8a, c7i, c8i and c6i `.2xlarge`, which needs AWS access. Run each result on two separately launched instances.
- 24 h soak at the recommended channel count.
- On-demand pricing is needed to turn any of this into cost per channel-hour.

## 8. Artefacts on the instance

| Path | Content |
| --- | --- |
| `/home/ubuntu/ec2-sizing-kit/ec2-sizing-kit/results/*c6a.2xlarge_gate_*` | Main run data: `gate.json`, `samples.csv`, `host.json`, channel logs |
| `/home/ubuntu/ec2-sizing-kit/ec2-sizing-kit/results/*c6a.xlarge_gate_*` | 4 vCPU runs. The 07:19, 07:43-07:54 and 08:00 runs used the short 30 s clip and are superseded; use the 08:13-08:26 runs |
| `assets/media/queue/xiph-1080p*.mp4` | 10-minute looped clips (about 24 GB); 30 s originals kept as `*-30s.mp4.orig` |
| `assets/media/queue/stress-2160p25.mp4` | Synthetic 4K clip |

The kit contains the offline transcoder license, so treat the zip and results as confidential.
