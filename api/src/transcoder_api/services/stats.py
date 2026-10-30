"""Resource utilization of the appliance: CPU, memory, storage, storage writes, network.

Everything is read straight from kernel counters - /proc and the container's cgroup v2 files -
with no subprocess and no third-party dependency. One sample is a dozen small file reads and
costs well under a millisecond.

Rates need two readings, so a background sampler takes one every STATS_SAMPLE_INTERVAL_SECONDS
(2 s by default) and keeps the report computed over the last window. GET /stats returns that
report as it stands: a request never waits, and any number of clients cost nothing extra.

Three scopes are reported where the kernel makes them available:

* container - the cgroup the API and the engine share, which is what the appliance consumes;
* engine    - the transcoder process alone, found by its binary path;
* host      - machine-wide CPU and memory, because /proc/stat and /proc/meminfo are not
              namespaced and a busy neighbour on the host still starves the channel.

Two counters need care, and both are handled below rather than left to the caller:

* Block I/O is counted once per physical disk. io.stat lists every layer of a device-mapper
  stack (LVM, dm-crypt) with the same bytes, so summing every line multiplies the write rate
  by the stack depth. Only devices with no underlying devices are summed.
* Container memory excludes inactive page cache, as `docker stats` does. memory.current counts
  cache the kernel reclaims on demand, and an HLS writer fills it continuously.

Storage writes are reported twice because they are two different facts. The engine's figure
(/proc/<pid>/io write_bytes) is the data it writes, accounted as it writes it. The container's
figure (io.stat) is what reaches the disk, which the kernel flushes from the page cache in bursts
up to vm.dirty_expire_centisecs (30 s by default) later - so over a 2 s window it swings between
zero and a spike, and it is also given as an average over the last minute. Over a long window
the two agree.

A counter that goes backwards - an engine restart resets its per-process counters - yields a
null rate for that window instead of a negative one.
"""

from __future__ import annotations

import asyncio
import collections
import logging
import os
import pathlib
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Optional

log = logging.getLogger(__name__)

DEFAULT_INTERVAL_SECONDS = 2.0

# The window for the averaged disk-write rate. Longer than the kernel's default dirty expiry,
# so a steady writer's average is not dominated by where the last flush happened to land.
AVERAGE_WINDOW_SECONDS = 60.0

# Overridden by the tests, which build a fake /proc and /sys under a temporary directory.
PROC_ROOT = pathlib.Path("/proc")
SYS_ROOT = pathlib.Path("/sys")


MIN_INTERVAL_SECONDS = 0.5


def interval_seconds() -> float:
    """The sampling interval, floored so a misconfiguration cannot spin the loop."""
    try:
        value = float(os.environ.get("STATS_SAMPLE_INTERVAL_SECONDS", DEFAULT_INTERVAL_SECONDS))
    except ValueError:
        value = DEFAULT_INTERVAL_SECONDS
    return max(value, MIN_INTERVAL_SECONDS)


def _engine_binary() -> str:
    return os.environ.get("TRANSCODER_BINARY", "/opt/utpl/bin/transcoder")


def _read(path: pathlib.Path) -> Optional[str]:
    try:
        return path.read_text()
    except OSError:
        return None


def _keyed(text: Optional[str]) -> dict[str, int]:
    """Parse "key value" lines, as in cpu.stat, memory.stat and /proc/<pid>/io."""
    values: dict[str, int] = {}
    for line in (text or "").splitlines():
        parts = line.replace(":", " ").split()
        if len(parts) >= 2 and parts[1].isdigit():
            values[parts[0]] = int(parts[1])
    return values


def _cpu_list_count(text: str) -> int:
    """Count the CPUs in a cpuset list such as "0-3,8,10-11"."""
    count = 0
    for part in text.strip().split(","):
        if not part:
            continue
        if "-" in part:
            low, high = part.split("-", 1)
            count += int(high) - int(low) + 1
        else:
            count += 1
    return count


# ---------------------------------------------------------------------------
# Raw counters
# ---------------------------------------------------------------------------


@dataclass
class Counters:
    """Cumulative counters at one instant. Every field is None when unavailable."""

    monotonic: float
    wall: datetime

    container_cpu_usec: Optional[int] = None
    host_cpu_busy: Optional[int] = None
    host_cpu_total: Optional[int] = None
    container_write_bytes: Optional[int] = None
    engine_pid: Optional[int] = None
    engine_cpu_ticks: Optional[int] = None
    engine_write_bytes: Optional[int] = None
    network: dict[str, tuple[int, int]] = field(default_factory=dict)


def _cgroup() -> pathlib.Path:
    return SYS_ROOT / "fs" / "cgroup"


def container_cpu_usec() -> Optional[int]:
    return _keyed(_read(_cgroup() / "cpu.stat")).get("usage_usec")


def container_cpu_capacity() -> Optional[float]:
    """CPUs the container may use: its CFS quota, else its cpuset, else every online CPU."""
    quota = (_read(_cgroup() / "cpu.max") or "").split()
    if len(quota) == 2 and quota[0] != "max":
        return int(quota[0]) / int(quota[1])

    cpuset = _read(_cgroup() / "cpuset.cpus.effective")
    if cpuset and cpuset.strip():
        return float(_cpu_list_count(cpuset))

    return float(os.cpu_count()) if os.cpu_count() else None


def host_cpu_times() -> tuple[Optional[int], Optional[int]]:
    """(busy, total) jiffies over all CPUs from the aggregate line of /proc/stat."""
    text = _read(PROC_ROOT / "stat") or ""
    for line in text.splitlines():
        if line.startswith("cpu "):
            values = [int(v) for v in line.split()[1:]]
            # user nice system idle iowait irq softirq steal guest guest_nice. guest and
            # guest_nice are already included in user and nice, so they are not added again.
            total = sum(values[:8])
            idle = values[3] + (values[4] if len(values) > 4 else 0)
            return total - idle, total
    return None, None


def _is_physical(device: str) -> bool:
    """A block device with nothing underneath it - a disk, not a device-mapper layer.

    When sysfs cannot say, the device is counted, so a missing /sys under-reports nothing.
    """
    slaves = SYS_ROOT / "dev" / "block" / device / "slaves"
    try:
        return not any(slaves.iterdir())
    except OSError:
        return True


def container_write_bytes() -> Optional[int]:
    text = _read(_cgroup() / "io.stat")
    if text is None:
        return None

    total = 0
    for line in text.splitlines():
        parts = line.split()
        if not parts or not _is_physical(parts[0]):
            continue
        for item in parts[1:]:
            if item.startswith("wbytes="):
                total += int(item.split("=", 1)[1])
    return total


def network_bytes() -> dict[str, tuple[int, int]]:
    """{interface: (received, transmitted)} bytes, loopback excluded."""
    interfaces: dict[str, tuple[int, int]] = {}
    for line in (_read(PROC_ROOT / "net" / "dev") or "").splitlines()[2:]:
        name, _, rest = line.partition(":")
        name = name.strip()
        values = rest.split()
        if name and name != "lo" and len(values) >= 9:
            interfaces[name] = (int(values[0]), int(values[8]))
    return interfaces


_engine_pid_cache: Optional[int] = None


def _cmdline(pid: int) -> Optional[str]:
    raw = _read(PROC_ROOT / str(pid) / "cmdline")
    return raw.split("\0", 1)[0] if raw else None


def engine_pid() -> Optional[int]:
    """The transcoder's pid. Cached, and re-found only when that pid stops being the engine."""
    global _engine_pid_cache

    binary = _engine_binary()
    if _engine_pid_cache is not None and _cmdline(_engine_pid_cache) == binary:
        return _engine_pid_cache

    _engine_pid_cache = None
    try:
        entries = [entry.name for entry in PROC_ROOT.iterdir() if entry.name.isdigit()]
    except OSError:
        return None

    for name in entries:
        if _cmdline(int(name)) == binary:
            _engine_pid_cache = int(name)
            break
    return _engine_pid_cache


def engine_cpu_ticks(pid: int) -> Optional[int]:
    text = _read(PROC_ROOT / str(pid) / "stat")
    if not text:
        return None
    # The command name is parenthesised and may contain spaces; fields resume after ")".
    fields = text.rsplit(")", 1)[1].split()
    # utime and stime are fields 14 and 15 of the full line, 11 and 12 after the name.
    return int(fields[11]) + int(fields[12])


def engine_rss_bytes(pid: int) -> Optional[int]:
    kilobytes = _keyed(_read(PROC_ROOT / str(pid) / "status")).get("VmRSS")
    return kilobytes * 1024 if kilobytes is not None else None


def engine_write_bytes(pid: int) -> Optional[int]:
    return _keyed(_read(PROC_ROOT / str(pid) / "io")).get("write_bytes")


def container_memory() -> dict[str, Optional[int]]:
    current = _read(_cgroup() / "memory.current")
    if current is None:
        return {"usedBytes": None, "limitBytes": None}

    inactive = _keyed(_read(_cgroup() / "memory.stat")).get("inactive_file", 0)
    used = max(int(current) - inactive, 0)

    limit_text = (_read(_cgroup() / "memory.max") or "max").strip()
    limit = None if limit_text == "max" else int(limit_text)
    if limit is None:
        limit = host_memory()["totalBytes"]

    return {"usedBytes": used, "limitBytes": limit}


def host_memory() -> dict[str, Optional[int]]:
    info = _keyed(_read(PROC_ROOT / "meminfo"))
    total = info.get("MemTotal")
    available = info.get("MemAvailable")
    return {
        "totalBytes": total * 1024 if total is not None else None,
        "availableBytes": available * 1024 if available is not None else None,
    }


def storage_paths() -> list[str]:
    """Where the appliance keeps data. STATS_STORAGE_PATHS (comma separated) overrides.

    The container's own root is left out: nothing the channel keeps lives there, and on an
    overlay filesystem it reports the host disk under a different device and fsid, so it
    would appear as a second copy of the same disk.
    """
    configured = os.environ.get("STATS_STORAGE_PATHS")
    if configured:
        return [p.strip() for p in configured.split(",") if p.strip()]

    paths = [
        os.environ.get("HLS_DIR", "/runtime/hls"),
        os.environ.get("MEDIA_DIR", "/media/queue"),
    ]
    for variable in ("DB_PATH", "TRANSCODER_CONFIG_PATH", "TRANSCODER_LICENSE_PATH"):
        value = os.environ.get(variable)
        if value:
            paths.append(str(pathlib.Path(value).parent))
    return paths


def storage() -> list[dict[str, Any]]:
    """One entry per filesystem, listing which of the storage paths live on it."""
    filesystems: dict[int, dict[str, Any]] = {}
    for path in storage_paths():
        try:
            device = os.stat(path).st_dev
            if device not in filesystems:
                fs = os.statvfs(path)
                used = (fs.f_blocks - fs.f_bfree) * fs.f_frsize
                available = fs.f_bavail * fs.f_frsize
                filesystems[device] = {
                    "paths": [],
                    "totalBytes": fs.f_blocks * fs.f_frsize,
                    "usedBytes": used,
                    "availableBytes": available,
                    # As df computes it: the root-reserved blocks count as neither.
                    "percent": _percent(used, used + available),
                }
            filesystems[device]["paths"].append(path)
        except OSError:
            continue
    return list(filesystems.values())


def read_counters() -> Counters:
    counters = Counters(monotonic=time.monotonic(), wall=datetime.now(timezone.utc))

    counters.container_cpu_usec = container_cpu_usec()
    counters.host_cpu_busy, counters.host_cpu_total = host_cpu_times()
    counters.container_write_bytes = container_write_bytes()
    counters.network = network_bytes()

    pid = engine_pid()
    if pid is not None:
        counters.engine_pid = pid
        counters.engine_cpu_ticks = engine_cpu_ticks(pid)
        counters.engine_write_bytes = engine_write_bytes(pid)

    return counters


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------


def _percent(part: Optional[float], whole: Optional[float]) -> Optional[float]:
    if part is None or not whole:
        return None
    return round(100.0 * part / whole, 1)


def _rate(new: Optional[int], old: Optional[int], seconds: float) -> Optional[float]:
    """Per-second rate, or None when either reading is missing or the counter went back."""
    if new is None or old is None or seconds <= 0 or new < old:
        return None
    return (new - old) / seconds


_CLOCK_TICKS = os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") else 100


def build_report(
    previous: Optional[Counters], current: Counters, average_base: Optional[Counters] = None
) -> dict[str, Any]:
    """The GET /stats document. Rates are over [previous, current] and null without previous.

    average_base is the oldest reading inside the averaging window; the averaged disk-write
    rate is over [average_base, current].
    """
    window = current.monotonic - previous.monotonic if previous else 0.0
    prev = previous or Counters(monotonic=0.0, wall=current.wall)

    capacity = container_cpu_capacity()

    # CPU
    container_cores = _rate(current.container_cpu_usec, prev.container_cpu_usec, window)
    container_cores = container_cores / 1_000_000 if container_cores is not None else None

    same_engine = previous is not None and current.engine_pid == previous.engine_pid
    engine_cores = None
    if same_engine:
        ticks = _rate(current.engine_cpu_ticks, prev.engine_cpu_ticks, window)
        engine_cores = ticks / _CLOCK_TICKS if ticks is not None else None

    host_percent = None
    if previous is not None and None not in (current.host_cpu_busy, prev.host_cpu_busy):
        total = (current.host_cpu_total or 0) - (prev.host_cpu_total or 0)
        busy = (current.host_cpu_busy or 0) - (prev.host_cpu_busy or 0)
        host_percent = _percent(busy, total) if total > 0 and busy >= 0 else None

    # Memory
    container_mem = container_memory()
    host_mem = host_memory()
    host_used = (
        host_mem["totalBytes"] - host_mem["availableBytes"]
        if None not in (host_mem["totalBytes"], host_mem["availableBytes"])
        else None
    )
    engine_rss = engine_rss_bytes(current.engine_pid) if current.engine_pid else None

    # Storage writes
    engine_write_rate = (
        _rate(current.engine_write_bytes, prev.engine_write_bytes, window) if same_engine else None
    )

    average_window = current.monotonic - average_base.monotonic if average_base else 0.0
    average_write_rate = (
        _rate(current.container_write_bytes, average_base.container_write_bytes, average_window)
        if average_base
        else None
    )
    # The engine writes a whole HLS segment at once, so its short-window rate is bursty too.
    engine_average_write_rate = (
        _rate(current.engine_write_bytes, average_base.engine_write_bytes, average_window)
        if average_base and average_base.engine_pid == current.engine_pid
        else None
    )

    # Network
    interfaces = []
    total_rx = total_tx = 0.0
    rates_known = previous is not None
    for name, (rx, tx) in sorted(current.network.items()):
        old = prev.network.get(name)
        rx_rate = _rate(rx, old[0], window) if old else None
        tx_rate = _rate(tx, old[1], window) if old else None
        if rx_rate is None or tx_rate is None:
            rates_known = False
        else:
            total_rx += rx_rate
            total_tx += tx_rate
        interfaces.append(
            {
                "name": name,
                "receiveBitsPerSecond": _bits(rx_rate),
                "transmitBitsPerSecond": _bits(tx_rate),
                "receivedBytes": rx,
                "transmittedBytes": tx,
            }
        )

    return {
        "sampledAt": current.wall.isoformat().replace("+00:00", "Z"),
        "windowSeconds": round(window, 3) if previous else None,
        "cpu": {
            "container": {
                "coresUsed": _round(container_cores, 3),
                "capacityCores": _round(capacity, 3),
                "percent": _percent(container_cores, capacity),
            },
            "engine": {
                "pid": current.engine_pid,
                "coresUsed": _round(engine_cores, 3),
                "percent": _percent(engine_cores, capacity),
            },
            "host": {"percent": host_percent, "cores": os.cpu_count()},
        },
        "memory": {
            "container": {**container_mem, "percent": _percent(container_mem["usedBytes"], container_mem["limitBytes"])},
            "engine": {"rssBytes": engine_rss},
            "host": {
                "totalBytes": host_mem["totalBytes"],
                "usedBytes": host_used,
                "availableBytes": host_mem["availableBytes"],
                "percent": _percent(host_used, host_mem["totalBytes"]),
            },
        },
        "storage": storage(),
        "storageWrite": {
            "container": {
                "bytesPerSecond": _round(_rate(current.container_write_bytes, prev.container_write_bytes, window), 0),
                "averageBytesPerSecond": _round(average_write_rate, 0),
                "averageWindowSeconds": round(average_window, 3) if average_base else None,
                "totalBytes": current.container_write_bytes,
            },
            "engine": {
                "bytesPerSecond": _round(engine_write_rate, 0),
                "averageBytesPerSecond": _round(engine_average_write_rate, 0),
                "averageWindowSeconds": round(average_window, 3) if engine_average_write_rate is not None else None,
                "totalBytes": current.engine_write_bytes,
            },
        },
        "network": {
            "receiveBitsPerSecond": _bits(total_rx) if rates_known and interfaces else None,
            "transmitBitsPerSecond": _bits(total_tx) if rates_known and interfaces else None,
            "interfaces": interfaces,
        },
    }


def _bits(bytes_per_second: Optional[float]) -> Optional[int]:
    return round(bytes_per_second * 8) if bytes_per_second is not None else None


def _round(value: Optional[float], digits: int) -> Optional[float]:
    if value is None:
        return None
    return round(value) if digits == 0 else round(value, digits)


# ---------------------------------------------------------------------------
# Sampler
# ---------------------------------------------------------------------------


class Sampler:
    """Keeps recent readings and the report computed from the last window."""

    def __init__(self) -> None:
        # Readings within AVERAGE_WINDOW_SECONDS; the last one is the previous sample.
        self._history: collections.deque[Counters] = collections.deque()
        self.report: Optional[dict[str, Any]] = None

    def sample(self) -> dict[str, Any]:
        current = read_counters()

        while self._history and current.monotonic - self._history[0].monotonic > AVERAGE_WINDOW_SECONDS:
            self._history.popleft()

        previous = self._history[-1] if self._history else None
        average_base = self._history[0] if self._history else None

        self.report = build_report(previous, current, average_base)
        self._history.append(current)
        return self.report


sampler = Sampler()


async def run_sampler() -> None:
    """Sample forever. A failed sample is logged and the loop continues."""
    while True:
        try:
            await asyncio.to_thread(sampler.sample)
        except asyncio.CancelledError:
            raise
        except Exception:
            log.exception("resource sample failed; continuing")
        await asyncio.sleep(interval_seconds())
