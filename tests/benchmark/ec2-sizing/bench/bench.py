#!/usr/bin/env python3
"""EC2 sizing harness for the UTPL / Skandha transcoder.

Runs N production transcoder containers (one channel each) on one host, measures host and
per-channel cost, and checks that every channel still delivers real-time, correct output.
Python 3.10+ standard library only; media analysis runs inside the transcoder image.

Commands (see README.md):
  check      environment and image sanity checks
  gate       functional gate: one channel, one capture, pass/fail
  screen     raw encoder throughput (ffmpeg inside the image) for instance ranking
  stepload   add channels until real time breaks; derive N_max / N_75 / N_50
  soak       run N channels for hours with periodic output checks and memory-growth tracking
  stop       remove every harness container
"""
import argparse
import csv
import datetime as dt
import json
import os
import pathlib
import random
import selectors
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

KIT = pathlib.Path(__file__).resolve().parent.parent
PROFILES = KIT / "profiles"
MEDIA = KIT / "assets" / "media"
LICENSE = KIT / "assets" / "license" / "transcoder.lic"
RESULTS = KIT / "results"
LABEL = "utplbench"
API_BASE_PORT = 18100
UDP_BASE_PORT = 20000
KIT_VERSION = "1.0.0"


# --------------------------------------------------------------------------------------------
# helpers

def log(msg):
    print(f"[{dt.datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


def run(cmd, check=True, capture=True, timeout=None):
    proc = subprocess.run(cmd, capture_output=capture, text=True, timeout=timeout)
    if check and proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
    return proc


def resolve_image(arg):
    if arg:
        return arg
    if os.environ.get("BENCH_IMAGE"):
        return os.environ["BENCH_IMAGE"]
    marker = KIT / "image.txt"
    if marker.exists():
        return marker.read_text().split()[0]
    sys.exit("no image: pass --image, set BENCH_IMAGE, or run setup/load-image.sh")


def load_profile(name):
    pdir = PROFILES / name
    if not pdir.is_dir():
        sys.exit(f"unknown profile {name}; available: {', '.join(sorted(p.name for p in PROFILES.iterdir()))}")
    meta = json.loads((pdir / "profile.json").read_text())
    config = (pdir / "config.json").read_text()
    return meta, config


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, default=str) + "\n")
    tmp.replace(path)


def new_run_dir(kind, profile, tag):
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    itype = imds("meta-data/instance-type") or socket.gethostname()
    name = f"{stamp}_{itype}_{kind}_{profile}" + (f"_{tag}" if tag else "")
    path = RESULTS / name
    path.mkdir(parents=True)
    return path


# --------------------------------------------------------------------------------------------
# host information

def imds(path):
    try:
        req = urllib.request.Request("http://169.254.169.254/latest/api/token", method="PUT",
                                     headers={"X-aws-ec2-metadata-token-ttl-seconds": "300"})
        token = urllib.request.urlopen(req, timeout=1).read().decode()
        req = urllib.request.Request(f"http://169.254.169.254/latest/{path}",
                                     headers={"X-aws-ec2-metadata-token": token})
        return urllib.request.urlopen(req, timeout=1).read().decode()
    except Exception:
        return None


def host_info(image):
    info = {"kit_version": KIT_VERSION, "captured_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "hostname": socket.gethostname(), "nproc": os.cpu_count()}
    for key in ("instance-type", "placement/availability-zone", "ami-id", "instance-id"):
        info[f"ec2_{key.split('/')[-1]}"] = imds(f"meta-data/{key}")
    try:
        info["lscpu"] = json.loads(run(["lscpu", "-J"]).stdout)["lscpu"]
        info["cpu_model"] = next((e["data"] for e in info["lscpu"] if e["field"].startswith("Model name")), None)
    except Exception:
        pass
    mem = read_meminfo()
    info["mem_total_gib"] = round(mem.get("MemTotal", 0) / 1048576, 2)
    info["kernel"] = os.uname().release
    try:
        info["os"] = dict(l.split("=", 1) for l in pathlib.Path("/etc/os-release").read_text().split("\n") if "=" in l).get("PRETTY_NAME", "").strip('"')
    except Exception:
        pass
    info["threads_per_core"] = threads_per_core()
    info["docker"] = run(["docker", "version", "--format", "{{.Server.Version}}"], check=False).stdout.strip()
    insp = run(["docker", "image", "inspect", image, "--format", "{{.Id}} {{json .RepoDigests}}"], check=False).stdout.strip()
    info["image"] = image
    info["image_id"] = insp.split(" ")[0] if insp else None
    return info


def threads_per_core():
    try:
        sib = pathlib.Path("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list").read_text().strip()
        return len(parse_cpulist(sib))
    except Exception:
        return None


def parse_cpulist(text):
    cpus = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            cpus.extend(range(int(a), int(b) + 1))
        elif part:
            cpus.append(int(part))
    return cpus


def cpu_order_by_core():
    """CPUs ordered so hyperthread siblings are adjacent, for whole-core pinning."""
    seen, order = set(), []
    for cpu in sorted(int(p.name[3:]) for p in pathlib.Path("/sys/devices/system/cpu").glob("cpu[0-9]*")):
        if cpu in seen:
            continue
        try:
            sib = parse_cpulist(pathlib.Path(f"/sys/devices/system/cpu/cpu{cpu}/topology/thread_siblings_list").read_text().strip())
        except Exception:
            sib = [cpu]
        for s in sib:
            if s not in seen:
                seen.add(s)
                order.append(s)
    return order


def read_meminfo():
    out = {}
    for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
        key, val = line.split(":", 1)
        out[key] = int(val.split()[0])
    return out


# --------------------------------------------------------------------------------------------
# sampling

class HostSampler(threading.Thread):
    """Samples /proc/stat, /proc/meminfo and each channel's cgroup every `interval` seconds."""

    def __init__(self, channels, interval=5.0):
        super().__init__(daemon=True)
        self.channels = channels
        self.interval = interval
        self.samples = []
        self._halt = threading.Event()

    @staticmethod
    def _stat():
        cpus = {}
        for line in pathlib.Path("/proc/stat").read_text().splitlines():
            if line.startswith("cpu"):
                parts = line.split()
                vals = list(map(int, parts[1:9]))
                cpus[parts[0]] = vals
        return cpus

    def _cg(self):
        out = {}
        for ch in self.channels:
            usage = mem = None
            if ch.cgroup:
                try:
                    for line in (ch.cgroup / "cpu.stat").read_text().splitlines():
                        if line.startswith("usage_usec"):
                            usage = int(line.split()[1])
                    mem = int((ch.cgroup / "memory.current").read_text())
                    stat = dict(l.split() for l in (ch.cgroup / "memory.stat").read_text().splitlines())
                    mem -= int(stat.get("inactive_file", 0))
                except Exception:
                    pass
            out[ch.name] = (usage, mem)
        return out

    def run(self):
        prev_t, prev, prev_cg = time.monotonic(), self._stat(), self._cg()
        prev_self = time.process_time()
        while not self._halt.wait(self.interval):
            now_t, now, now_cg = time.monotonic(), self._stat(), self._cg()
            now_self = time.process_time()
            dtime = now_t - prev_t
            sample = {"t": time.time()}
            core_busy = []
            for key, vals in now.items():
                old = prev.get(key)
                if not old:
                    continue
                delta = [a - b for a, b in zip(vals, old)]
                total = sum(delta) or 1
                idle = delta[3] + delta[4]
                busy = 100.0 * (total - idle) / total
                if key == "cpu":
                    sample["cpu_busy_pct"] = round(busy, 2)
                    sample["cpu_steal_pct"] = round(100.0 * delta[7] / total, 3)
                    sample["cpu_iowait_pct"] = round(100.0 * delta[4] / total, 3)
                else:
                    core_busy.append(busy)
            if core_busy:
                sample["max_core_busy_pct"] = round(max(core_busy), 1)
            mem = read_meminfo()
            sample["mem_used_gib"] = round((mem["MemTotal"] - mem["MemAvailable"]) / 1048576, 3)
            sample["mem_used_pct"] = round(100.0 * (mem["MemTotal"] - mem["MemAvailable"]) / mem["MemTotal"], 2)
            sample["harness_cores"] = round((now_self - prev_self) / dtime, 4)
            chans = {}
            for name, (usage, memb) in now_cg.items():
                pu = prev_cg.get(name, (None, None))[0]
                cores = round((usage - pu) / 1e6 / dtime, 4) if usage is not None and pu is not None else None
                chans[name] = {"cores": cores, "mem_mib": round(memb / 1048576, 1) if memb else None}
            sample["channels"] = chans
            self.samples.append(sample)
            prev_t, prev, prev_cg, prev_self = now_t, now, now_cg, now_self

    def stop(self):
        self._halt.set()
        self.join(timeout=self.interval * 2)


def summarise_samples(samples):
    def stats(values):
        values = sorted(v for v in values if v is not None)
        if not values:
            return None
        return {"avg": round(sum(values) / len(values), 3), "p95": round(values[int(0.95 * (len(values) - 1))], 3),
                "max": round(values[-1], 3)}

    out = {key: stats([s.get(key) for s in samples]) for key in
           ("cpu_busy_pct", "cpu_steal_pct", "cpu_iowait_pct", "max_core_busy_pct", "mem_used_gib", "mem_used_pct", "harness_cores")}
    per_ch_cores, per_ch_mem = [], []
    for s in samples:
        for c in s.get("channels", {}).values():
            per_ch_cores.append(c["cores"])
            per_ch_mem.append(c["mem_mib"])
    out["channel_cores"] = stats(per_ch_cores)
    out["channel_mem_mib"] = stats(per_ch_mem)
    out["samples"] = len(samples)
    return out


def write_samples_csv(path, samples):
    with open(path, "a", newline="") as handle:
        writer = csv.writer(handle)
        if handle.tell() == 0:
            writer.writerow(["time", "cpu_busy_pct", "cpu_steal_pct", "max_core_busy_pct", "mem_used_gib",
                             "harness_cores", "channels", "sum_channel_cores", "max_channel_mem_mib"])
        for s in samples:
            chans = s.get("channels", {}).values()
            cores = [c["cores"] for c in chans if c["cores"] is not None]
            mems = [c["mem_mib"] for c in chans if c["mem_mib"] is not None]
            writer.writerow([dt.datetime.fromtimestamp(s["t"]).isoformat(timespec="seconds"), s.get("cpu_busy_pct"),
                             s.get("cpu_steal_pct"), s.get("max_core_busy_pct"), s.get("mem_used_gib"),
                             s.get("harness_cores"), len(cores), round(sum(cores), 3), max(mems) if mems else ""])


# --------------------------------------------------------------------------------------------
# channels

class Channel:
    def __init__(self, index, profile, meta, config_text, run_dir, opts):
        self.index = index
        self.name = f"{LABEL}-ch{index:03d}"
        self.api_port = API_BASE_PORT + index
        self.udp_port = UDP_BASE_PORT + index
        self.meta = meta
        self.dir = run_dir / "channels" / f"ch{index:03d}"
        self.opts = opts
        self.cgroup = None
        self.sock = None
        self._render(config_text)

    # ---- configuration
    def _render(self, config_text):
        for sub in ("channel", "runtime", "hls"):
            (self.dir / sub).mkdir(parents=True, exist_ok=True)
        cfg = json.loads(config_text.replace("@OUT_PORT@", str(self.udp_port)))
        for output in cfg["transcoder"]["outputs"]:
            for stream in output.get("streams", []):
                if stream.get("type") != "video":
                    continue
                enc = stream.get(stream.get("codec", ""), {})
                props = enc.setdefault("properties", {})
                if self.opts.encoder_threads is not None:
                    props["threads"] = str(self.opts.encoder_threads)
                if self.opts.preset and enc.get("encoder") in ("x264enc", "x265enc"):
                    props["speed-preset"] = self.opts.preset
        (self.dir / "config.json").write_text(json.dumps(cfg, indent=2))
        content = self.meta["content"][self.opts.content]
        lines = ["channel: main", "name: Benchmark programme", "timezone: UTC", "playlist:"]
        for item in content["program"]:
            lines.append(f"- media: {item['media']}")
            if item.get("lband"):
                lines += ["  graphics:", "  - use: lband-playboxtv", "    mode: reveal", "    at: 0",
                          "    duration: 6s", "    fade: 0.6s"]
        (self.dir / "channel" / "channel.yml").write_text("\n".join(lines) + "\n")
        ads = ["channel: ads", "name: Benchmark break reel", "timezone: UTC", "playlist:"]
        ads += [f"- media: {m}" for m in self.meta["ads"]]
        (self.dir / "channel" / "ads.yml").write_text("\n".join(ads) + "\n")

    # ---- lifecycle
    def bind(self):
        if self.meta["output"]["kind"] != "udp":
            return
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
        self.sock.bind(("127.0.0.1", self.udp_port))
        self.sock.setblocking(False)

    def start(self, image, cpu_args):
        self.bind()
        env = {
            "API_PORT": str(self.api_port), "MEDIA_DIR": "/media/queue",
            "DB_PATH": "/runtime/managed-playlist.sqlite", "MANAGED_PLAYLIST_ID": "main",
            "PLAYLIST_SEED_FILE": "/media/queue/playlist.m3u8", "MANAGED_PLAYLIST_FILE": "/runtime/main.m3u8",
            "PLAYLIST_BOOTSTRAP_FILES": "/opt/utpl/channel/channel.yml,/opt/utpl/channel/ads.yml",
            "PLAYLIST_BOOTSTRAP_WRITEBACK": "false", "PLAYLIST_BOOTSTRAP_STRICT": "false",
            "ENABLE_VIDEO_GRAPHICS": "false", "ENABLE_RUNDOWN_SERVICE": "true",
        }
        cmd = ["docker", "run", "-d", "--name", self.name, "--label", f"{LABEL}=1", "--network", "host",
               "--log-opt", "max-size=20m", "--log-opt", "max-file=2"]
        for key, val in env.items():
            cmd += ["-e", f"{key}={val}"]
        cmd += ["-v", f"{MEDIA / 'queue'}:/media/queue:ro", "-v", f"{MEDIA / 'graphics'}:/media/graphics:ro",
                "-v", f"{self.dir / 'runtime'}:/runtime", "-v", f"{self.dir / 'channel'}:/opt/utpl/channel",
                "-v", f"{self.dir / 'hls'}:/hls",
                "-v", f"{self.dir / 'config.json'}:/opt/utpl/etc/config.json:ro",
                "-v", f"{LICENSE}:/opt/utpl/etc/transcoder.lic:ro"]
        cmd += cpu_args + [image]
        cid = run(cmd).stdout.strip()
        for candidate in (pathlib.Path(f"/sys/fs/cgroup/system.slice/docker-{cid}.scope"),
                          pathlib.Path(f"/sys/fs/cgroup/docker/{cid}")):
            if (candidate / "cpu.stat").exists():
                self.cgroup = candidate
                break
        if self.cgroup is None:
            log(f"warning: cgroup for {self.name} not found; per-channel CPU/memory will be missing")

    def drain(self):
        if not self.sock:
            return 0
        got = 0
        try:
            while True:
                got += len(self.sock.recv(65536))
        except BlockingIOError:
            pass
        return got

    def wait_ready(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.running():
                return False, "container exited"
            if self.meta["output"]["kind"] == "udp":
                if self.drain() > 0:
                    return True, "udp output flowing"
            else:
                if any(self.dir.joinpath("hls").rglob("*.ts")):
                    return True, "hls segments present"
            time.sleep(1)
        return False, f"no output within {timeout}s"

    def running(self):
        out = run(["docker", "inspect", "-f", "{{.State.Running}} {{.RestartCount}}", self.name], check=False).stdout
        return out.startswith("true")

    def stop(self):
        run(["docker", "rm", "-f", self.name], check=False)
        if self.sock:
            self.sock.close()
            self.sock = None

    def save_logs(self, dest):
        proc = run(["docker", "logs", "--tail", "400", self.name], check=False)
        (dest / f"{self.name}.log").write_text((proc.stdout or "") + (proc.stderr or ""))


def cpu_args_for(index, opts, order):
    if opts.cpu_mode == "quota":
        return [f"--cpus={opts.cpus_per_channel}"]
    if opts.cpu_mode == "cpuset":
        k = int(opts.cpus_per_channel)
        start = ((index - 1) * k) % len(order)
        cpus = [order[(start + j) % len(order)] for j in range(k)]
        return [f"--cpuset-cpus={','.join(map(str, cpus))}"]
    return []


def stop_all():
    ids = run(["docker", "ps", "-aq", "--filter", f"label={LABEL}=1"], check=False).stdout.split()
    if ids:
        run(["docker", "rm", "-f", *ids], check=False)
    return len(ids)


# --------------------------------------------------------------------------------------------
# capture and analysis

def capture_udp(channels, seconds, capdir):
    """Receive every channel's UDP output for `seconds` of wall time into capdir/<name>.ts."""
    capdir.mkdir(parents=True, exist_ok=True)
    sel = selectors.DefaultSelector()
    files = {}
    for ch in channels:
        if ch.sock is None:
            continue
        ch.drain()
        files[ch.name] = open(capdir / f"{ch.name}.ts", "wb", buffering=1 << 20)
        sel.register(ch.sock, selectors.EVENT_READ, ch.name)
    cpu0 = time.process_time()
    start = time.monotonic()
    end = start + seconds
    while True:
        remaining = end - time.monotonic()
        if remaining <= 0:
            break
        for key, _ in sel.select(timeout=min(remaining, 0.5)):
            sock, name = key.fileobj, key.data
            handle = files[name]
            try:
                while True:
                    handle.write(sock.recv(65536))
            except BlockingIOError:
                pass
    wall = time.monotonic() - start
    for sock_key in list(sel.get_map().values()):
        sel.unregister(sock_key.fileobj)
    for handle in files.values():
        handle.close()
    walls = {f"{name}.ts": round(wall, 3) for name in files}
    write_json(capdir / "wall.json", walls)
    return {"wall_s": round(wall, 3), "harness_cpu_s": round(time.process_time() - cpu0, 3)}


def analyse_dir(image, capdir, fps, jobs=4):
    cmd = ["docker", "run", "--rm", "--network", "none", "--entrypoint", "python3",
           "-v", f"{KIT / 'bench'}:/kit:ro", "-v", f"{capdir}:/cap", image,
           "-I", "/kit/analyze_capture.py", "/cap", "--fps", str(fps), "--jobs", str(jobs)]
    proc = run(cmd, check=False, timeout=1800)
    if proc.returncode != 0:
        raise RuntimeError(f"analysis failed: {proc.stderr[-2000:]}")
    return json.loads(proc.stdout)


def parse_playlist(path):
    seq, durs, segs = 0, [], []
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return None
    for i, line in enumerate(lines):
        if line.startswith("#EXT-X-MEDIA-SEQUENCE:"):
            seq = int(line.split(":", 1)[1])
        elif line.startswith("#EXTINF:"):
            durs.append(float(line.split(":", 1)[1].split(",")[0]))
            if i + 1 < len(lines):
                segs.append(lines[i + 1].strip())
    return {"seq": seq, "durations": durs, "segments": segs}


def hls_snapshot(channel):
    snap = {}
    for pl in channel.dir.joinpath("hls").rglob("*.m3u8"):
        text = pl.read_text(errors="replace")
        if "#EXT-X-STREAM-INF" in text:
            snap["_master_variants"] = text.count("#EXT-X-STREAM-INF")
            continue
        parsed = parse_playlist(pl)
        if parsed and parsed["durations"]:
            snap[str(pl.relative_to(channel.dir / "hls"))] = parsed
    return snap


def hls_evaluate(channels, before, after, window, capdir, meta):
    """Compare playlist snapshots taken `window` seconds apart; copy the newest segment per variant."""
    capdir.mkdir(parents=True, exist_ok=True)
    seg_len = meta["output"]["segment_seconds"]
    report = {}
    for ch in channels:
        b, a = before.get(ch.name, {}), after.get(ch.name, {})
        variants = {}
        for key, cur in a.items():
            if key.startswith("_"):
                continue
            prev = b.get(key)
            last_new = cur["seq"] + len(cur["durations"]) - 1
            if prev:
                last_old = prev["seq"] + len(prev["durations"]) - 1
                produced = last_new - last_old
                new_durations = cur["durations"][-produced:] if 0 < produced <= len(cur["durations"]) else []
                media_s = sum(new_durations) if new_durations else produced * seg_len
            else:
                produced, media_s = None, None
            seg = cur["segments"][-1] if cur["segments"] else None
            if seg:
                src = (ch.dir / "hls" / key).parent / seg
                if src.exists():
                    shutil.copy(src, capdir / f"seg-{ch.name}-{key.replace('/', '_')}.ts")
            variants[key] = {"produced": produced, "media_s": round(media_s, 2) if media_s is not None else None,
                             "avg_extinf": round(sum(cur["durations"]) / len(cur["durations"]), 3)}
        report[ch.name] = {"variants": variants, "master_variants": a.get("_master_variants"), "window_s": window}
    return report


def judge_udp(result, meta, crit):
    fps = meta["output"]["fps"]
    reasons = []
    if "error" in result:
        return False, [result["error"]]
    fps_wall = result.get("video_fps_wall", 0)
    if abs(fps_wall - fps) > fps * crit["fps_tol"]:
        reasons.append(f"fps {fps_wall} vs {fps}")
    rate = result.get("video_media_rate", 0)
    if abs(rate - 1) > crit["rate_tol"]:
        reasons.append(f"video media rate {rate}")
    if result.get("missing_frames", 0) > crit["max_missing_frames"]:
        reasons.append(f"missing frames {result['missing_frames']}")
    arate = result.get("audio_media_rate")
    if arate is None:
        reasons.append("no audio")
    elif abs(arate - 1) > crit["audio_rate_tol"]:
        reasons.append(f"audio media rate {arate}")
    if result.get("audio_gap_ms", 0) > crit["audio_gap_budget_ms"]:
        reasons.append(f"audio gaps {result.get('audio_gap_ms')} ms (budget {crit['audio_gap_budget_ms']})")
    if result.get("cc_errors", 0) > 0:
        reasons.append(f"continuity errors {result['cc_errors']}")
    if (result.get("width"), result.get("height")) != (meta["output"]["width"], meta["output"]["height"]):
        reasons.append(f"resolution {result.get('width')}x{result.get('height')}")
    return not reasons, reasons


def judge_hls(ch_report, seg_results, meta, crit):
    reasons = []
    out = meta["output"]
    variants = ch_report["variants"]
    if len(variants) < out["variants"]:
        reasons.append(f"{len(variants)} media playlists, expected {out['variants']}")
    if ch_report.get("master_variants") not in (None, out["variants"]):
        reasons.append(f"master lists {ch_report.get('master_variants')} variants")
    window = ch_report["window_s"]
    for key, v in variants.items():
        if v["media_s"] is None:
            reasons.append(f"{key}: no baseline")
            continue
        if abs(v["media_s"] - window) > 1.5 * out["segment_seconds"]:
            reasons.append(f"{key}: {v['media_s']} s of media in {window} s")
    for seg in seg_results:
        expected = out["segment_seconds"] * out["fps"]
        frames = seg.get("video_frames", 0)
        if "error" in seg or abs(frames - expected) > max(2, expected * 0.05):
            reasons.append(f"{seg['file']}: {frames} frames, expected ~{expected}")
    return not reasons, reasons


# --------------------------------------------------------------------------------------------
# event storm

class EventStorm(threading.Thread):
    """Per channel: alternate an L-band graphic cue and a 20 s SCTE splice-insert ad break."""

    def __init__(self, channels, interval):
        super().__init__(daemon=True)
        self.channels = channels
        self.interval = interval
        self.counts = {"ok": 0, "conflict": 0, "error": 0}
        self._halt = threading.Event()

    def _post(self, port, path, body):
        req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
        try:
            urllib.request.urlopen(req, timeout=10).read()
            self.counts["ok"] += 1
        except urllib.error.HTTPError as exc:
            self.counts["conflict" if exc.code == 409 else "error"] += 1
        except Exception:
            self.counts["error"] += 1

    def _fire(self, ch, n):
        if n % 2 == 0:
            self._post(ch.api_port, "/graphics/cues",
                       {"assetId": random.choice(["lband-voltedge", "lband-aquapure", "lband-greenfuel"]),
                        "mode": "reveal", "durationMs": 15000, "fadeMs": 600})
        else:
            event_id = int(time.time()) % 2_000_000_000 + ch.index
            ends = (dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=24)).isoformat()
            self._post(ch.api_port, "/scheduled_events", [
                {"type": "scte35_spliceinsert", "time": None,
                 "details": {"scte35_spliceinsert": {"duration": 20000, "splice_event_id": event_id}}},
                {"type": "scte35_returntonetwork", "time": ends,
                 "details": {"scte35_returntonetwork": {"splice_event_id": event_id}}}])

    def run(self):
        offsets = {ch.name: random.uniform(0, self.interval) for ch in self.channels}
        start = time.monotonic()
        fired = {ch.name: 0 for ch in self.channels}
        while not self._halt.wait(1.0):
            elapsed = time.monotonic() - start
            for ch in list(self.channels):
                due = offsets.setdefault(ch.name, random.uniform(0, self.interval)) + fired.setdefault(ch.name, 0) * self.interval
                if elapsed >= due:
                    self._fire(ch, fired[ch.name])
                    fired[ch.name] += 1

    def stop(self):
        self._halt.set()


# --------------------------------------------------------------------------------------------
# measurement of one step

def measure(channels, image, meta, opts, step_dir, measure_s):
    """Sample for measure_s; capture output during the final capture window; judge every channel."""
    sampler = HostSampler(channels, opts.sample_interval)
    sampler.start()
    kind = meta["output"]["kind"]
    cap_s = min(opts.capture_seconds, measure_s)
    hls_window = measure_s
    before = {}
    if kind == "hls":
        before = {ch.name: hls_snapshot(ch) for ch in channels}
        time.sleep(hls_window)
        cap_info = {"wall_s": hls_window}
    else:
        time.sleep(max(0, measure_s - cap_s))
        cap_info = capture_udp(channels, cap_s, step_dir / "capture")
    sampler.stop()
    alive = {ch.name: ch.running() for ch in channels}
    crit = {"fps_tol": opts.fps_tol, "rate_tol": opts.rate_tol, "audio_rate_tol": opts.audio_rate_tol,
            "max_missing_frames": opts.max_missing_frames, "audio_gap_budget_ms": opts.audio_gap_budget_ms}
    per_channel = {}
    if kind == "hls":
        after = {ch.name: hls_snapshot(ch) for ch in channels}
        report = hls_evaluate(channels, before, after, hls_window, step_dir / "capture", meta)
        segs = analyse_dir(image, step_dir / "capture", meta["output"]["fps"], opts.analysis_jobs)
        for ch in channels:
            mine = [s for s in segs if s["file"].startswith(f"seg-{ch.name}-")]
            ok, reasons = judge_hls(report[ch.name], mine, meta, crit)
            per_channel[ch.name] = {"pass": ok and alive[ch.name], "reasons": reasons + ([] if alive[ch.name] else ["container not running"]),
                                    "hls": report[ch.name], "segments": mine}
    else:
        results = {r["file"][:-3]: r for r in analyse_dir(image, step_dir / "capture", meta["output"]["fps"], opts.analysis_jobs)}
        for ch in channels:
            res = results.get(ch.name, {"error": "no capture"})
            ok, reasons = judge_udp(res, meta, crit)
            per_channel[ch.name] = {"pass": ok and alive[ch.name], "reasons": reasons + ([] if alive[ch.name] else ["container not running"]),
                                    "output": res}
        if not opts.keep_captures:
            shutil.rmtree(step_dir / "capture", ignore_errors=True)
    host = summarise_samples(sampler.samples)
    write_samples_csv(step_dir.parent / "samples.csv", sampler.samples)
    host_ok = []
    if host.get("cpu_steal_pct") and host["cpu_steal_pct"]["avg"] > opts.max_steal:
        host_ok.append(f"steal {host['cpu_steal_pct']['avg']}%")
    passed = all(c["pass"] for c in per_channel.values()) and not host_ok
    return {"pass": passed, "host_reasons": host_ok, "host": host, "capture": cap_info,
            "channels_passed": sum(c["pass"] for c in per_channel.values()), "per_channel": per_channel}


# --------------------------------------------------------------------------------------------
# commands

def add_common(p):
    p.add_argument("--profile", default="p1-playout")
    p.add_argument("--image")
    p.add_argument("--content", help="content set from profile.json: real | synthetic | repo")
    p.add_argument("--tag", default="", help="free-form label added to the results directory name")
    p.add_argument("--cpu-mode", choices=["shared", "quota", "cpuset"], default="shared")
    p.add_argument("--cpus-per-channel", type=float, default=2.0, help="for quota (fractional ok) and cpuset (integer)")
    p.add_argument("--encoder-threads", type=int, help="set the video encoder 'threads' property")
    p.add_argument("--preset", help="override x264enc/x265enc speed-preset")
    p.add_argument("--ready-timeout", type=int, default=180)
    p.add_argument("--start-gap", type=float, default=3.0, help="seconds between container starts")
    p.add_argument("--capture-seconds", type=int, default=60)
    p.add_argument("--sample-interval", type=float, default=5.0)
    p.add_argument("--analysis-jobs", type=int, default=4)
    p.add_argument("--fps-tol", type=float, default=0.005, help="allowed relative deviation of delivered fps")
    p.add_argument("--rate-tol", type=float, default=0.01, help="allowed deviation of media time vs wall time")
    p.add_argument("--audio-rate-tol", type=float, default=0.02)
    p.add_argument("--max-missing-frames", type=int, default=0)
    p.add_argument("--audio-gap-budget-ms", type=float, default=3000.0,
                   help="audio dropout allowed per capture. The engine drops about 1.1 s of audio per minute even on an idle "
                        "28-vCPU host (observed 2026-10-16), so zero cannot be the criterion; watch audio_gap_ms grow with N")
    p.add_argument("--max-steal", type=float, default=2.0)
    p.add_argument("--events", action="store_true", help="run the graphics/ad-break event storm")
    p.add_argument("--event-interval", type=float, default=120.0)
    p.add_argument("--keep-captures", action="store_true")
    p.add_argument("--keep-running", action="store_true", help="leave channels running at the end")


def prepare(opts, kind):
    image = resolve_image(opts.image)
    meta, config = load_profile(opts.profile)
    opts.content = opts.content or meta.get("default_content", "real")
    if opts.content not in meta["content"]:
        sys.exit(f"profile {opts.profile} has no content set '{opts.content}' (has {', '.join(meta['content'])})")
    missing = [i["media"] for i in meta["content"][opts.content]["program"] if not (MEDIA / "queue" / i["media"]).exists()]
    missing += [m for m in meta["ads"] if not (MEDIA / "queue" / m).exists()]
    if missing:
        sys.exit(f"missing media in {MEDIA / 'queue'}: {', '.join(missing)}\nrun media-gen/make-media.sh first")
    if not LICENSE.exists():
        sys.exit(f"missing license {LICENSE}")
    if stop_all():
        log("removed leftover harness containers")
    run_dir = new_run_dir(kind, opts.profile, opts.tag)
    info = host_info(image)
    write_json(run_dir / "host.json", info)
    write_json(run_dir / "options.json", {**vars(opts), "func": None})
    log(f"results -> {run_dir}")
    log(f"host {info.get('ec2_instance-type') or info['hostname']}: {info['nproc']} vCPU, {info['mem_total_gib']} GiB, "
        f"{info.get('cpu_model')}, threads/core {info.get('threads_per_core')}")
    return image, meta, config, run_dir


class Fleet:
    def __init__(self, image, meta, config, run_dir, opts):
        self.image, self.meta, self.config, self.run_dir, self.opts = image, meta, config, run_dir, opts
        self.channels = []
        self.order = cpu_order_by_core()

    def grow(self, n):
        while len(self.channels) < n:
            idx = len(self.channels) + 1
            ch = Channel(idx, self.opts.profile, self.meta, self.config, self.run_dir, self.opts)
            ch.start(self.image, cpu_args_for(idx, self.opts, self.order))
            self.channels.append(ch)
            time.sleep(self.opts.start_gap)
        failures = []
        for ch in self.channels:
            ok, why = ch.wait_ready(self.opts.ready_timeout)
            if not ok:
                failures.append(f"{ch.name}: {why}")
        return failures

    def save_logs(self, dest):
        dest.mkdir(parents=True, exist_ok=True)
        for ch in self.channels:
            ch.save_logs(dest)

    def stop(self):
        for ch in self.channels:
            ch.stop()


def cmd_gate(opts):
    image, meta, config, run_dir = prepare(opts, "gate")
    fleet = Fleet(image, meta, config, run_dir, opts)
    storm = None
    try:
        failures = fleet.grow(1)
        if failures:
            fleet.save_logs(run_dir / "logs")
            write_json(run_dir / "gate.json", {"pass": False, "reasons": failures})
            log(f"GATE FAIL: {failures}  (logs in {run_dir / 'logs'})")
            return 1
        log(f"channel up; warming {opts.warmup}s")
        if opts.events:
            storm = EventStorm(fleet.channels, opts.event_interval)
            storm.start()
        time.sleep(opts.warmup)
        opts.keep_captures = True
        result = measure(fleet.channels, image, meta, opts, run_dir / "gate", opts.measure)
        if storm:
            result["events"] = storm.counts
        fleet.save_logs(run_dir / "logs")
        write_json(run_dir / "gate.json", result)
        ch = result["per_channel"][fleet.channels[0].name]
        detail = ch.get("output") or ch.get("hls")
        log(f"GATE {'PASS' if result['pass'] else 'FAIL'} {ch['reasons'] or ''}")
        log(f"output: {json.dumps(detail)[:800]}")
        h = result["host"]
        log(f"channel cores avg {h['channel_cores'] and h['channel_cores']['avg']}, mem {h['channel_mem_mib'] and h['channel_mem_mib']['max']} MiB")
        return 0 if result["pass"] else 1
    finally:
        if storm:
            storm.stop()
        if not opts.keep_running:
            fleet.stop()


def linear_fit(xs, ys):
    pts = [(x, y) for x, y in zip(xs, ys) if y is not None]
    if len(pts) < 2:
        return None
    n = len(pts)
    mx = sum(p[0] for p in pts) / n
    my = sum(p[1] for p in pts) / n
    sxx = sum((p[0] - mx) ** 2 for p in pts)
    if sxx == 0:
        return None
    slope = sum((p[0] - mx) * (p[1] - my) for p in pts) / sxx
    return {"slope": round(slope, 4), "intercept": round(my - slope * mx, 4)}


def summarise_steps(steps, info, opts, meta):
    passing = [s for s in steps if s["pass"]]
    ncpu = info["nproc"]
    mem_total = info["mem_total_gib"]

    def best(cpu_limit, mem_limit):
        ok = [s["n"] for s in passing
              if s["host"]["cpu_busy_pct"] and s["host"]["cpu_busy_pct"]["avg"] <= cpu_limit
              and s["host"]["mem_used_pct"] and s["host"]["mem_used_pct"]["max"] <= mem_limit]
        return max(ok) if ok else 0

    xs = [s["n"] for s in passing]
    host_cores = [s["host"]["cpu_busy_pct"]["avg"] * ncpu / 100 if s["host"]["cpu_busy_pct"] else None for s in passing]
    mem = [s["host"]["mem_used_gib"]["max"] if s["host"]["mem_used_gib"] else None for s in passing]
    first_fail = next((s for s in steps if not s["pass"]), None)
    return {
        "profile": opts.profile, "content": opts.content, "cpu_mode": opts.cpu_mode,
        "instance_type": info.get("ec2_instance-type"), "vcpu": ncpu, "mem_gib": mem_total,
        "cpu_model": info.get("cpu_model"), "threads_per_core": info.get("threads_per_core"),
        "events": opts.events, "n_max": max(xs) if xs else 0,
        "n_75": best(75, 80), "n_50": best(50, 60),
        "host_vcpu_fit": linear_fit(xs, host_cores), "host_mem_gib_fit": linear_fit(xs, mem),
        "per_channel_vcpu": (linear_fit(xs, host_cores) or {}).get("slope"),
        "per_channel_mem_gib": (linear_fit(xs, mem) or {}).get("slope"),
        "first_failure": {"n": first_fail["n"], "reasons": first_fail.get("reasons_summary")} if first_fail else None,
        "steps": [{"n": s["n"], "pass": s["pass"], "cpu_avg": s["host"]["cpu_busy_pct"] and s["host"]["cpu_busy_pct"]["avg"],
                   "cpu_p95": s["host"]["cpu_busy_pct"] and s["host"]["cpu_busy_pct"]["p95"],
                   "max_core_p95": s["host"]["max_core_busy_pct"] and s["host"]["max_core_busy_pct"]["p95"],
                   "mem_gib": s["host"]["mem_used_gib"] and s["host"]["mem_used_gib"]["max"],
                   "ch_cores_avg": s["host"]["channel_cores"] and s["host"]["channel_cores"]["avg"],
                   "ch_mem_max_mib": s["host"]["channel_mem_mib"] and s["host"]["channel_mem_mib"]["max"],
                   "audio_gap_ms_max": max([c.get("output", {}).get("audio_gap_ms", 0) for c in s.get("per_channel", {}).values()] or [0]),
                   "steal": s["host"]["cpu_steal_pct"] and s["host"]["cpu_steal_pct"]["avg"],
                   "passed": f"{s['channels_passed']}/{s['n']}"} for s in steps],
        "output_kbps_per_channel": meta["output"].get("video_kbps"),
    }


def print_summary(summary):
    log("step results:")
    print(f"  {'N':>3} {'pass':>5} {'cpu%':>6} {'p95':>6} {'core95':>6} {'memGiB':>7} {'ch_cores':>8} {'ch_MiB':>7} {'steal':>5}  ok")
    for s in summary["steps"]:
        print(f"  {s['n']:>3} {str(s['pass']):>5} {s['cpu_avg'] or 0:>6.1f} {s['cpu_p95'] or 0:>6.1f} {s['max_core_p95'] or 0:>6.1f} "
              f"{s['mem_gib'] or 0:>7.2f} {s['ch_cores_avg'] or 0:>8.2f} {s['ch_mem_max_mib'] or 0:>7.0f} {s['steal'] or 0:>5.2f}  {s['passed']}")
    print(f"  N_max={summary['n_max']}  N_75 (minimum spec)={summary['n_75']}  N_50 (recommended)={summary['n_50']}  "
          f"per-channel vCPU={summary['per_channel_vcpu']}  per-channel GiB={summary['per_channel_mem_gib']}")
    if summary["first_failure"]:
        print(f"  first failure at N={summary['first_failure']['n']}: {summary['first_failure']['reasons']}")


def cmd_stepload(opts):
    image, meta, config, run_dir = prepare(opts, "stepload")
    info = json.loads((run_dir / "host.json").read_text())
    fleet = Fleet(image, meta, config, run_dir, opts)
    storm = EventStorm(fleet.channels, opts.event_interval) if opts.events else None
    if storm:
        storm.start()
    steps = []
    n = opts.start
    retries = opts.retries
    try:
        while n <= opts.max:
            log(f"step N={n}: starting channels")
            step_dir = run_dir / f"step-{n:03d}"
            failures = fleet.grow(n)
            if failures:
                fleet.save_logs(step_dir / "logs")
                step = {"n": n, "pass": False, "reasons_summary": failures, "host": summarise_samples([]), "channels_passed": 0}
                steps.append(step)
                write_json(step_dir / "step.json", step)
                log(f"step N={n}: startup failure {failures}")
                break
            log(f"step N={n}: warmup {opts.warmup}s, measure {opts.measure}s")
            time.sleep(opts.warmup)
            mem_pct = 100 - 100 * read_meminfo()["MemAvailable"] / read_meminfo()["MemTotal"]
            result = measure(fleet.channels, image, meta, opts, step_dir, opts.measure)
            result["n"] = n
            result["mem_pct_before"] = round(mem_pct, 1)
            if storm:
                result["events"] = dict(storm.counts)
            bad = {k: v["reasons"] for k, v in result["per_channel"].items() if not v["pass"]}
            result["reasons_summary"] = (result["host_reasons"] + [f"{k}: {'; '.join(v)}" for k, v in list(bad.items())[:5]])
            write_json(step_dir / "step.json", result)
            h = result["host"]
            log(f"step N={n}: {'PASS' if result['pass'] else 'FAIL'} cpu {h['cpu_busy_pct'] and h['cpu_busy_pct']['avg']}% "
                f"mem {h['mem_used_gib'] and h['mem_used_gib']['max']} GiB, {result['channels_passed']}/{n} channels ok "
                f"{result['reasons_summary'][:3] if not result['pass'] else ''}")
            if not result["pass"]:
                fleet.save_logs(step_dir / "logs")
                if retries > 0:
                    retries -= 1
                    log(f"step N={n}: retrying once to rule out a transient")
                    continue
                steps.append(result)
                break
            steps.append(result)
            retries = opts.retries
            n += opts.step
            if read_meminfo()["MemAvailable"] < 0.08 * read_meminfo()["MemTotal"]:
                log("stopping: less than 8% memory available")
                break
    except KeyboardInterrupt:
        log("interrupted; summarising completed steps")
    finally:
        if storm:
            storm.stop()
        if not opts.keep_running:
            fleet.stop()
        summary = summarise_steps(steps, info, opts, meta)
        write_json(run_dir / "summary.json", summary)
        print_summary(summary)
        log(f"summary -> {run_dir / 'summary.json'}")
    return 0


def cmd_soak(opts):
    image, meta, config, run_dir = prepare(opts, "soak")
    fleet = Fleet(image, meta, config, run_dir, opts)
    storm = None
    checks = []
    try:
        failures = fleet.grow(opts.channels)
        if failures:
            fleet.save_logs(run_dir / "logs")
            log(f"startup failure: {failures}")
            return 1
        if opts.events:
            storm = EventStorm(fleet.channels, opts.event_interval)
            storm.start()
        end = time.monotonic() + opts.hours * 3600
        k = 0
        while time.monotonic() < end:
            k += 1
            window = min(opts.check_every, max(opts.capture_seconds + 10, end - time.monotonic()))
            result = measure(fleet.channels, image, meta, opts, run_dir / f"check-{k:03d}", window)
            result["elapsed_h"] = round((opts.hours * 3600 - (end - time.monotonic())) / 3600, 2)
            result["wal_bytes"] = {ch.name: sum(p.stat().st_size for p in (ch.dir / "runtime").glob("*-wal")) for ch in fleet.channels}
            if storm:
                result["events"] = dict(storm.counts)
            write_json(run_dir / f"check-{k:03d}" / "check.json", result)
            h = result["host"]
            checks.append({"k": k, "elapsed_h": result["elapsed_h"], "pass": result["pass"],
                           "cpu_avg": h["cpu_busy_pct"] and h["cpu_busy_pct"]["avg"],
                           "ch_mem_max_mib": h["channel_mem_mib"] and h["channel_mem_mib"]["max"],
                           "ch_mem_avg_mib": h["channel_mem_mib"] and h["channel_mem_mib"]["avg"],
                           "max_wal_mib": round(max(result["wal_bytes"].values() or [0]) / 1048576, 2),
                           "passed": f"{result['channels_passed']}/{len(fleet.channels)}"})
            log(f"check {k} at {result['elapsed_h']} h: {'PASS' if result['pass'] else 'FAIL'} "
                f"cpu {checks[-1]['cpu_avg']}% channel mem avg {checks[-1]['ch_mem_avg_mib']} MiB max {checks[-1]['ch_mem_max_mib']} MiB "
                f"wal {checks[-1]['max_wal_mib']} MiB")
            write_json(run_dir / "soak.json", {"checks": checks})
    except KeyboardInterrupt:
        log("interrupted")
    finally:
        if storm:
            storm.stop()
        fleet.save_logs(run_dir / "logs")
        if not opts.keep_running:
            fleet.stop()
        later = [c for c in checks if c["elapsed_h"] >= 1.0]
        growth = linear_fit([c["elapsed_h"] for c in later], [c["ch_mem_avg_mib"] for c in later])
        verdict = {"checks": checks, "all_pass": bool(checks) and all(c["pass"] for c in checks),
                   "channel_mem_growth_mib_per_h": growth and growth["slope"], "channels": opts.channels,
                   "profile": opts.profile, "instance_type": imds("meta-data/instance-type")}
        write_json(run_dir / "soak.json", verdict)
        log(f"soak: all checks pass={verdict['all_pass']}, channel memory growth after 1 h = "
            f"{verdict['channel_mem_growth_mib_per_h']} MiB/h")
    return 0


SCREEN_CODECS = {
    "x264-1080p25": {"fps": 25, "args": "-vf scale=1920:1080,format=yuv420p -c:v libx264 -preset veryfast -tune zerolatency -b:v 6M -g 50 -f null -"},
    "x264-1080p50": {"fps": 50, "args": "-vf scale=1920:1080,format=yuv420p -c:v libx264 -preset veryfast -tune zerolatency -b:v 8M -g 100 -f null -"},
    "x264-ladder4": {"fps": 25, "args": ("-filter_complex \"[0:v]format=yuv420p,split=4[a][b][c][d];[a]scale=1920:1080[o1];[b]scale=1280:720[o2];"
                                          "[c]scale=960:540[o3];[d]scale=640:360[o4]\" "
                                          + " ".join(f"-map [{o}] -c:v libx264 -preset veryfast -tune zerolatency -b:v {b} -g 50 -f null -"
                                                     for o, b in (("o1", "6M"), ("o2", "3500k"), ("o3", "2M"), ("o4", "800k"))))},
    "x265-1080p25": {"fps": 25, "args": "-vf scale=1920:1080,format=yuv420p -c:v libx265 -preset superfast -tune zerolatency -b:v 4M -g 50 -x265-params log-level=error -f null -"},
    "svtav1-1080p25": {"fps": 25, "args": "-vf scale=1920:1080,format=yuv420p -c:v libsvtav1 -preset 10 -b:v 3M -g 50 -f null -"},
    "x264-2160p25": {"fps": 25, "args": "-vf scale=3840:2160,format=yuv420p -c:v libx264 -preset veryfast -tune zerolatency -b:v 20M -g 50 -f null -"},
}


def cmd_screen(opts):
    image = resolve_image(opts.image)
    clip = opts.clip or next((c for c in ("xiph-1080p25.mp4", "stress-1080p25.mp4", "coke-commercial-1080p-h264.mp4")
                              if (MEDIA / "queue" / c).exists()), None)
    if not clip:
        sys.exit("no clip found; run media-gen/make-media.sh")
    run_dir = new_run_dir("screen", "ffmpeg", opts.tag)
    info = host_info(image)
    write_json(run_dir / "host.json", info)
    ncpu = info["nproc"]
    parallel = [int(p) for p in opts.parallel.split(",")] if opts.parallel else \
        sorted({1, 2, 4, 8, 16, 32, 64, 96, 128, ncpu // 2, ncpu} & set(range(1, ncpu + 1)))
    rows = []
    log(f"screening on {info.get('ec2_instance-type') or info['hostname']} ({ncpu} vCPU) with {clip}")
    for codec in opts.codecs.split(","):
        spec = SCREEN_CODECS[codec]
        best = 0
        for p in parallel:
            inner = (f"ffmpeg -nostdin -hide_banner -loglevel error -stream_loop -1 -i /media/{clip} "
                     f"-frames:v {opts.frames} -an {spec['args']}")
            script = (f"s=$(date +%s.%N); for i in $(seq {p}); do {inner} & done; wait; e=$(date +%s.%N); "
                      f"echo $s $e")
            proc = run(["docker", "run", "--rm", "--network", "none", "--entrypoint", "sh",
                        "-v", f"{MEDIA / 'queue'}:/media:ro", image, "-c", script], check=False, timeout=7200)
            try:
                s, e = map(float, proc.stdout.split()[-2:])
            except ValueError:
                log(f"{codec} x{p}: failed {proc.stderr[-300:]}")
                break
            wall = e - s
            agg = p * opts.frames / wall
            row = {"codec": codec, "parallel": p, "frames_each": opts.frames, "wall_s": round(wall, 2),
                   "aggregate_fps": round(agg, 1), "fps_per_copy": round(agg / p, 1),
                   "realtime_copies": round(agg / spec["fps"], 2), "each_realtime": agg / p >= spec["fps"]}
            rows.append(row)
            log(f"{codec} x{p}: {row['aggregate_fps']} fps total, {row['fps_per_copy']} each, = {row['realtime_copies']} real-time streams")
            if agg < best * 1.03:
                break
            best = max(best, agg)
    with open(run_dir / "screen.csv", "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()) if rows else ["codec"])
        writer.writeheader()
        writer.writerows(rows)
    ceiling = {}
    for r in rows:
        ceiling[r["codec"]] = max(ceiling.get(r["codec"], 0), r["realtime_copies"])
    write_json(run_dir / "screen.json", {"host": info, "clip": clip, "rows": rows, "realtime_ceiling": ceiling})
    log(f"theoretical ceilings (real-time streams of the encode alone): {ceiling}")
    log(f"results -> {run_dir}")
    return 0


def cmd_check(opts):
    image = resolve_image(opts.image)
    ok = True
    info = host_info(image)
    print(json.dumps({k: v for k, v in info.items() if k != "lscpu"}, indent=2))
    if not info["image_id"]:
        print(f"FAIL image {image} not present (setup/load-image.sh)")
        ok = False
    for path in (LICENSE, MEDIA / "queue" / "playlist.m3u8", MEDIA / "graphics" / "lband_playboxtv.png"):
        if not path.exists():
            print(f"FAIL missing {path}")
            ok = False
    try:
        rmem = int(pathlib.Path("/proc/sys/net/core/rmem_max").read_text())
        if rmem < 8 * 1024 * 1024:
            print(f"WARN net.core.rmem_max={rmem}; run setup/ec2-setup.sh (captures may drop packets)")
    except OSError:
        pass
    if info.get("ec2_instance-type", "") and info["ec2_instance-type"].split(".")[0].startswith("t"):
        print("WARN burstable T-family instance: results will not represent sustained capacity")
    if info["image_id"]:
        plugins = run(["docker", "run", "--rm", "--entrypoint", "sh", image, "-c",
                       "ls /usr/lib/x86_64-linux-gnu/gstreamer-1.0/ | grep -E 'x264|x265|libav|svtav1'"], check=False).stdout.split()
        print("encoder plugins:", " ".join(plugins))
    for name in sorted(p.name for p in PROFILES.iterdir()):
        meta = json.loads((PROFILES / name / "profile.json").read_text())
        sets = []
        for cset, spec in meta["content"].items():
            have = all((MEDIA / "queue" / i["media"]).exists() for i in spec["program"])
            sets.append(f"{cset}:{'ok' if have else 'missing'}")
        print(f"profile {name}: {' '.join(sets)}")
    print("OK" if ok else "NOT READY")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("check")
    p.add_argument("--image")
    p.set_defaults(func=cmd_check)

    p = sub.add_parser("gate")
    add_common(p)
    p.add_argument("--warmup", type=int, default=90)
    p.add_argument("--measure", type=int, default=90)
    p.set_defaults(func=cmd_gate)

    p = sub.add_parser("stepload")
    add_common(p)
    p.add_argument("--start", type=int, default=1)
    p.add_argument("--step", type=int, default=1)
    p.add_argument("--max", type=int, default=256)
    p.add_argument("--warmup", type=int, default=300)
    p.add_argument("--measure", type=int, default=600)
    p.add_argument("--retries", type=int, default=1, help="re-measure a failing step this many times")
    p.set_defaults(func=cmd_stepload)

    p = sub.add_parser("soak")
    add_common(p)
    p.add_argument("--channels", type=int, required=True)
    p.add_argument("--hours", type=float, default=24)
    p.add_argument("--check-every", type=int, default=3600)
    p.set_defaults(func=cmd_soak)

    p = sub.add_parser("screen")
    p.add_argument("--image")
    p.add_argument("--clip")
    p.add_argument("--codecs", default="x264-1080p25,x264-ladder4,x264-1080p50,x265-1080p25,svtav1-1080p25,x264-2160p25")
    p.add_argument("--parallel", help="comma list of parallel copies (default 1,2,4,... up to nproc)")
    p.add_argument("--frames", type=int, default=1500)
    p.add_argument("--tag", default="")
    p.set_defaults(func=cmd_screen)

    p = sub.add_parser("stop")
    p.set_defaults(func=lambda o: print(f"removed {stop_all()} containers") or 0)

    opts = parser.parse_args()
    sys.exit(opts.func(opts))


if __name__ == "__main__":
    main()
