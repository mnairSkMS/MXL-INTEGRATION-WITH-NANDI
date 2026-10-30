# Hardware Requirements: Skandha / UTPL Transcoder on AWS EC2 (Ubuntu)

Based on [benchmark.md](benchmark.md) (2026-10-16), image `skandha-transcoder:bench-20261016`,
Ubuntu 24.04, Docker, AMD EPYC 7R13 (c6a). Values are **per channel** (one transcoder container).

## Rules used

| Spec | Host CPU (avg) | Host RAM | Other |
| --- | --- | --- | --- |
| **Minimum** | ≤ 75 % | ≤ 80 % | Real-time output, 0 missing frames, 0 audio gaps, 0 TS errors |
| **Recommended** | ≤ 50 % | ≤ 60 % | Headroom for graphics, ad breaks, harder content |

On c6a, 1 vCPU is one hyperthread (2 vCPU = 1 physical core).

## Profile definitions

All profiles share the P1 input graph: a managed file playlist (primary input), an ad-break reel
(secondary input) switched in by SCTE-35 ad insertion, and an L-band graphic from the rundown.
Video uses `x264enc` (`veryfast`, `zerolatency`, High profile, 2 s GOP) unless stated otherwise.

| Profile | Video output | Audio output | Delivery | Test content |
| --- | --- | --- | --- | --- |
| **P1** playout | 1920×1080, 25 fps, H.264, 6,000 kbps | AAC, 48 kHz, stereo | MPEG-TS over UDP; SCTE-35 passthrough (PID 4269) | Xiph 1080p high-motion footage |
| **P2** HLS ladder | 4 renditions, 25 fps, H.264: 1080p 6,000 / 720p 3,500 / 540p 2,000 / 360p 800 kbps | AAC stereo per rendition | Multivariant HLS (MPEG-TS), 6 s segments, 6-segment window, local disk | Xiph 1080p |
| **P3a** 1080p50 | 1920×1080, 50 fps, H.264, 8,000 kbps | AAC stereo | As P1 | Xiph 1080p50 |
| **P3b** HEVC | 1920×1080, 25 fps, H.265 (`x265enc` `superfast`, `zerolatency`), 4,000 kbps | AAC stereo | As P1 | Xiph 1080p |
| **P3d** 4K input | 1920×1080, 25 fps, H.264, 6,000 kbps (as P1) | AAC stereo | As P1 | 2160p25 H.264 source (synthetic) |
| **P3e** 4K output | 3840×2160, 25 fps, H.264, 20,000 kbps | AAC stereo | As P1 | 2160p25 source (synthetic) |
| **P3f** E-AC-3 5.1 | As P1 | E-AC-3, 48 kHz, 5.1 (6 ch), 384 kbps | As P1 | Xiph 1080p with a 5.1 audio track |

The configurations are in `tests/benchmark/ec2-sizing/profiles/<profile>/config.json`. The VLC
preview relay that Compose adds (a second x264 encode) is not part of any profile.

## Requirements per channel

| Profile | Measured vCPU | Minimum | Recommended | Basis |
| --- | --- | --- | --- | --- |
| **P1 playout** (1080p25 H.264 6 Mbps, AAC, SCTE, graphics) | 2.73–2.84 | 4 vCPU / 8 GiB (`c6a.xlarge`) | 8 vCPU / 16 GiB (`c6a.2xlarge`) | Measured: 72.4 % on 4 vCPU, 34.2 % on 8 vCPU |
| **P3f** P1 with E-AC-3 5.1 audio | 2.82–2.95 | 8 vCPU / 16 GiB | 8 vCPU / 16 GiB | Measured: 75.1 % on 4 vCPU (just over the line), 35.3 % on 8 vCPU |
| **P3d** 2160p input → 1080p25 output | 2.61 | 4 vCPU / 8 GiB* | 8 vCPU / 16 GiB | Measured 32.8 % on 8 vCPU; 4 vCPU not run (estimated ≈ 65 %) |
| **P3a** 1080p50, 8 Mbps | 4.57 | 8 vCPU / 16 GiB | 16 vCPU / 32 GiB* | Measured 57.5 % on 8 vCPU; **failed** on 4 vCPU (93 %, audio gaps) |
| **P3e** 2160p25 output, 20 Mbps | 4.81 | 8 vCPU / 16 GiB | 16 vCPU / 32 GiB* | Measured 60.8 % on 8 vCPU; synthetic 4K content only |
| **P3b** HEVC (x265) 4 Mbps | 6.93 | **Not supported yet** | – | Failed on 8 vCPU: 87 % CPU, audio gaps, unreadable HEVC parameters |
| **P2** HLS ladder (1080/720/540/360) | – | **Not supported yet** | – | Functional failure: no HLS segments written; not a CPU limit |

\* Estimate from measured per-channel vCPU; that instance size was not run.

## Memory, disk, network

| Resource | Requirement | Evidence |
| --- | --- | --- |
| RAM | 1 GiB per channel (1.5 GiB for 4K output) + 2 GiB for OS/Docker | Peak 472–754 MiB at 1080p, 1,180 MiB at 4K output; host RAM ≤ 18 % used |
| Memory constraint? | No. Compute-optimised (C family, 2 GiB/vCPU) is sufficient | Every run left more than 80 % RAM free |
| Disk (gp3) | 30 GiB root + media library; default 3,000 IOPS / 125 MB/s | Image 1.7 GB; logs are capped by Docker |
| Network out | ≈ 6.5 Mbps (1080p25), 8.6 Mbps (1080p50), 21 Mbps (2160p) per channel | Measured output bitrate; negligible next to instance bandwidth |
| CPU steal | ≤ 0.025 % measured | Use non-burstable instances only: no T-family, no `-flex`, no Spot |

## Multiple channels per instance (estimate, not measured)

Channels per instance ≈ (vCPU × target utilisation) ÷ per-channel vCPU. P1 at 2.73 vCPU:

| Instance | vCPU | Minimum (≤ 75 %) | Recommended (≤ 50 %) |
| --- | --- | --- | --- |
| c6a.xlarge | 4 | 1 | 0 |
| c6a.2xlarge | 8 | 2 | 1 |
| c6a.4xlarge | 16 | 4 | 2 |
| c6a.8xlarge | 32 | 8 | 5 |

These assume linear scaling. Validate with `bench.py stepload` before committing to a density.

## Summary

- **One 1080p25 playout channel (P1):** minimum c6a.xlarge (4 vCPU, 8 GiB); recommended c6a.2xlarge (8 vCPU, 16 GiB).
- **1080p50 or 4K output:** minimum 8 vCPU; recommended 16 vCPU.
- **HEVC and the HLS ladder** cannot be sized until their functional failures are fixed.
- **CPU, not memory, is the limit.** About 2.7 vCPU per 1080p25 channel, about 4.6–4.8 vCPU per 1080p50 or 4K-output channel.

## Not yet established

- Multi-channel density (step-load) and 24 h stability (soak).
- Other CPU families. c7a/c8a have 1 vCPU = 1 physical core and may need fewer vCPU per channel; this is unmeasured.
- Real 4K footage for P3d and P3e (synthetic content was used).
- Cost per channel-hour (needs on-demand prices for the region, ap-south-1).
