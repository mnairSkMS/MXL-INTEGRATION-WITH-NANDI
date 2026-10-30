"""GET /stats and the counters behind it.

Each test builds a fake /proc and /sys under a temporary directory and points the service at it,
so every number can be asserted exactly. Two counters are easy to get wrong and are pinned here:
block I/O must be counted once per physical disk, not once per device-mapper layer, and
container memory must leave out the page cache the kernel can reclaim.
"""

from __future__ import annotations

import pathlib

import pytest

from transcoder_api.services import stats

ENGINE = "/opt/utpl/bin/transcoder"


class FakeHost:
    """A writable fake /proc and /sys. Counters are set per test and advanced between samples."""

    def __init__(self, root: pathlib.Path):
        self.proc = root / "proc"
        self.sys = root / "sys"
        self.cgroup = self.sys / "fs" / "cgroup"
        self.cgroup.mkdir(parents=True)
        (self.proc / "net").mkdir(parents=True)

        self.set_cgroup(cpu_usec=0, memory=500 * 2**20, inactive_file=100 * 2**20)
        self.set_cpu_max("max 100000")
        (self.cgroup / "cpuset.cpus.effective").write_text("0-3\n")
        self.set_host_cpu(busy=0, idle=0)
        self.set_meminfo(total_kb=8 * 2**20, available_kb=6 * 2**20)
        self.set_io({"8:0": 0, "252:0": 0, "252:1": 0})
        self.stack_devices()
        self.set_net({"lo": (0, 0), "eth0": (0, 0)})

    def set_cgroup(self, cpu_usec: int, memory: int, inactive_file: int, memory_max: str = "max"):
        (self.cgroup / "cpu.stat").write_text(f"usage_usec {cpu_usec}\nuser_usec 0\nsystem_usec 0\n")
        (self.cgroup / "memory.current").write_text(f"{memory}\n")
        (self.cgroup / "memory.stat").write_text(f"anon 1\nfile 2\ninactive_file {inactive_file}\n")
        (self.cgroup / "memory.max").write_text(f"{memory_max}\n")

    def set_cpu_max(self, value: str):
        (self.cgroup / "cpu.max").write_text(value + "\n")

    def set_host_cpu(self, busy: int, idle: int):
        # user nice system idle iowait irq softirq steal guest guest_nice
        (self.proc / "stat").write_text(f"cpu  {busy} 0 0 {idle} 0 0 0 0 0 0\ncpu0 0 0 0 0\n")

    def set_meminfo(self, total_kb: int, available_kb: int):
        (self.proc / "meminfo").write_text(
            f"MemTotal:       {total_kb} kB\nMemFree:        1 kB\nMemAvailable:   {available_kb} kB\n"
        )

    def set_io(self, wbytes: dict[str, int]):
        lines = [f"{dev} rbytes=0 wbytes={value} rios=0 wios=0 dbytes=0 dios=0" for dev, value in wbytes.items()]
        (self.cgroup / "io.stat").write_text("\n".join(lines) + "\n")

    def stack_devices(self):
        """sda (8:0) under dm-0 (252:0) under dm-1 (252:1), as on an LVM host."""
        for device, slaves in (("8:0", []), ("252:0", ["sda3"]), ("252:1", ["dm-0"])):
            directory = self.sys / "dev" / "block" / device / "slaves"
            directory.mkdir(parents=True, exist_ok=True)
            for slave in slaves:
                (directory / slave).touch()

    def set_net(self, interfaces: dict[str, tuple[int, int]]):
        lines = [
            "Inter-|   Receive                                                |  Transmit",
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets",
        ]
        for name, (rx, tx) in interfaces.items():
            lines.append(f"{name:>6}: {rx} 0 0 0 0 0 0 0 {tx} 0 0 0 0 0 0 0")
        (self.proc / "net" / "dev").write_text("\n".join(lines) + "\n")

    def set_process(self, pid: int, cmdline: str, cpu_ticks: int = 0, rss_kb: int = 0, write_bytes: int = 0):
        directory = self.proc / str(pid)
        directory.mkdir(exist_ok=True)
        (directory / "cmdline").write_text(cmdline + "\0-c\0/opt/utpl/channel/config.json\0")
        # Fields 14 and 15 are utime and stime; the name contains a space on purpose.
        (directory / "stat").write_text(
            f"{pid} (trans coder) S 1 1 1 0 -1 0 0 0 0 0 {cpu_ticks} 0 0 0 20 0 1 0 1 1 1\n"
        )
        (directory / "status").write_text(f"Name:\ttranscoder\nVmRSS:\t   {rss_kb} kB\n")
        (directory / "io").write_text(f"rchar: 0\nwchar: 0\nread_bytes: 0\nwrite_bytes: {write_bytes}\n")


@pytest.fixture()
def host(tmp_path, monkeypatch):
    fake = FakeHost(tmp_path)
    monkeypatch.setattr(stats, "PROC_ROOT", fake.proc)
    monkeypatch.setattr(stats, "SYS_ROOT", fake.sys)
    monkeypatch.setattr(stats, "_engine_pid_cache", None)
    monkeypatch.setattr(stats, "_CLOCK_TICKS", 100)
    monkeypatch.setenv("TRANSCODER_BINARY", ENGINE)
    monkeypatch.setenv("STATS_STORAGE_PATHS", str(tmp_path))
    return fake


def _pair(host, before, after, seconds=2.0):
    """Build a report over a window of `seconds`, with `after` applied to the host in between."""
    before()
    first = stats.read_counters()
    after()
    second = stats.read_counters()
    second.monotonic = first.monotonic + seconds
    return stats.build_report(first, second)


# ---------------------------------------------------------------------------
# CPU
# ---------------------------------------------------------------------------


def test_container_cpu_is_cores_used_over_the_window_and_percent_of_capacity(host):
    report = _pair(
        host,
        lambda: host.set_cgroup(cpu_usec=0, memory=1, inactive_file=0),
        lambda: host.set_cgroup(cpu_usec=3_000_000, memory=1, inactive_file=0),
    )

    # 3 CPU-seconds over 2 s is 1.5 cores, of the 4 in the cpuset.
    assert report["cpu"]["container"] == {"coresUsed": 1.5, "capacityCores": 4.0, "percent": 37.5}


def test_a_cpu_quota_is_the_capacity_when_one_is_set(host):
    host.set_cpu_max("150000 100000")

    report = _pair(
        host,
        lambda: host.set_cgroup(cpu_usec=0, memory=1, inactive_file=0),
        lambda: host.set_cgroup(cpu_usec=1_500_000, memory=1, inactive_file=0),
    )

    assert report["cpu"]["container"]["capacityCores"] == 1.5
    assert report["cpu"]["container"]["percent"] == 50.0


def test_host_cpu_percent_comes_from_proc_stat(host):
    report = _pair(host, lambda: host.set_host_cpu(busy=100, idle=100), lambda: host.set_host_cpu(busy=400, idle=200))

    # 300 busy of 400 elapsed jiffies.
    assert report["cpu"]["host"]["percent"] == 75.0


def test_engine_cpu_is_read_from_its_own_process(host):
    report = _pair(
        host,
        lambda: host.set_process(12, ENGINE, cpu_ticks=1000),
        lambda: host.set_process(12, ENGINE, cpu_ticks=1200),
    )

    # 200 ticks at 100 Hz over 2 s is one core.
    assert report["cpu"]["engine"] == {"pid": 12, "coresUsed": 1.0, "percent": 25.0}


# ---------------------------------------------------------------------------
# Memory
# ---------------------------------------------------------------------------


def test_container_memory_leaves_out_reclaimable_page_cache(host):
    host.set_cgroup(cpu_usec=0, memory=500 * 2**20, inactive_file=100 * 2**20, memory_max=str(1000 * 2**20))

    report = stats.build_report(None, stats.read_counters())

    assert report["memory"]["container"] == {
        "usedBytes": 400 * 2**20,
        "limitBytes": 1000 * 2**20,
        "percent": 40.0,
    }


def test_an_unlimited_container_is_measured_against_host_memory(host):
    report = stats.build_report(None, stats.read_counters())

    assert report["memory"]["container"]["limitBytes"] == 8 * 2**30
    assert report["memory"]["host"]["usedBytes"] == 2 * 2**30
    assert report["memory"]["host"]["percent"] == 25.0


def test_engine_rss_is_reported(host):
    host.set_process(12, ENGINE, rss_kb=2048)

    report = stats.build_report(None, stats.read_counters())

    assert report["memory"]["engine"]["rssBytes"] == 2 * 2**20


# ---------------------------------------------------------------------------
# Storage and storage writes
# ---------------------------------------------------------------------------


def test_writes_are_counted_once_per_physical_disk_not_per_device_mapper_layer(host):
    """io.stat lists sda, dm-0 and dm-1 with the same bytes; summing all three triples them."""
    report = _pair(
        host,
        lambda: host.set_io({"8:0": 0, "252:0": 0, "252:1": 0}),
        lambda: host.set_io({"8:0": 4_000_000, "252:0": 4_000_000, "252:1": 4_000_000}),
    )

    assert report["storageWrite"]["container"]["bytesPerSecond"] == 2_000_000
    assert report["storageWrite"]["container"]["totalBytes"] == 4_000_000


def test_disk_writes_are_also_averaged_over_the_longer_window(host):
    """The kernel flushes in bursts, so the short-window rate alone swings between 0 and a spike."""
    host.set_io({"8:0": 0})
    base = stats.read_counters()
    host.set_io({"8:0": 5_000_000})
    previous = stats.read_counters()
    host.set_io({"8:0": 6_000_000})
    current = stats.read_counters()
    previous.monotonic = base.monotonic + 58.0
    current.monotonic = base.monotonic + 60.0

    write = stats.build_report(previous, current, base)["storageWrite"]["container"]

    assert write["bytesPerSecond"] == 500_000
    assert write["averageBytesPerSecond"] == 100_000
    assert write["averageWindowSeconds"] == 60.0


def test_the_sampler_keeps_only_the_averaging_window(host, monkeypatch):
    clock = iter([0.0, 30.0, 61.0, 62.0])
    monkeypatch.setattr(stats.time, "monotonic", lambda: next(clock))
    sampler = stats.Sampler()

    for _ in range(4):
        report = sampler.sample()

    # The reading at 0 s fell out of the window; 30 s is now the oldest.
    assert report["storageWrite"]["container"]["averageWindowSeconds"] == 32.0
    assert report["windowSeconds"] == 1.0


def test_engine_write_rate_comes_from_its_io_accounting(host):
    report = _pair(
        host,
        lambda: host.set_process(12, ENGINE, write_bytes=1_000_000),
        lambda: host.set_process(12, ENGINE, write_bytes=3_000_000),
    )

    assert report["storageWrite"]["engine"]["bytesPerSecond"] == 1_000_000
    assert report["storageWrite"]["engine"]["totalBytes"] == 3_000_000


def test_engine_writes_are_averaged_only_across_one_engine_process(host):
    host.set_process(12, ENGINE, write_bytes=0)
    base = stats.read_counters()
    host.set_process(12, ENGINE, write_bytes=6_000_000)
    current = stats.read_counters()
    current.monotonic = base.monotonic + 60.0

    engine = stats.build_report(base, current, base)["storageWrite"]["engine"]
    assert engine["averageBytesPerSecond"] == 100_000

    current.engine_pid = 40
    engine = stats.build_report(base, current, base)["storageWrite"]["engine"]
    assert engine["averageBytesPerSecond"] is None
    assert engine["averageWindowSeconds"] is None


def test_storage_lists_each_filesystem_once_with_its_paths(host, tmp_path, monkeypatch):
    (tmp_path / "hls").mkdir()
    monkeypatch.setenv("STATS_STORAGE_PATHS", f"{tmp_path},{tmp_path / 'hls'},{tmp_path / 'absent'}")

    [filesystem] = stats.build_report(None, stats.read_counters())["storage"]

    assert filesystem["paths"] == [str(tmp_path), str(tmp_path / "hls")]
    assert filesystem["totalBytes"] > 0
    assert 0 <= filesystem["percent"] <= 100


# ---------------------------------------------------------------------------
# Network
# ---------------------------------------------------------------------------


def test_network_is_in_bits_per_second_and_leaves_out_loopback(host):
    report = _pair(
        host,
        lambda: host.set_net({"lo": (0, 0), "eth0": (1000, 2000)}),
        lambda: host.set_net({"lo": (10**9, 10**9), "eth0": (251_000, 1_002_000)}),
    )

    network = report["network"]
    assert [i["name"] for i in network["interfaces"]] == ["eth0"]
    # 250 000 B and 1 000 000 B over 2 s.
    assert network["receiveBitsPerSecond"] == 1_000_000
    assert network["transmitBitsPerSecond"] == 4_000_000


# ---------------------------------------------------------------------------
# Edges
# ---------------------------------------------------------------------------


def test_the_first_report_has_no_rates(host):
    host.set_process(12, ENGINE)

    report = stats.build_report(None, stats.read_counters())

    assert report["windowSeconds"] is None
    assert report["cpu"]["container"]["coresUsed"] is None
    assert report["storageWrite"]["container"]["bytesPerSecond"] is None
    assert report["network"]["receiveBitsPerSecond"] is None


def test_an_engine_restart_gives_null_engine_rates_not_negative_ones(host):
    """A new engine process starts its counters again from zero."""

    def restarted():
        (host.proc / "12" / "cmdline").write_text("/bin/sh\0")
        host.set_process(40, ENGINE, cpu_ticks=5, write_bytes=10)

    report = _pair(host, lambda: host.set_process(12, ENGINE, cpu_ticks=900, write_bytes=10**6), restarted)

    assert report["cpu"]["engine"]["pid"] == 40
    assert report["cpu"]["engine"]["coresUsed"] is None
    assert report["storageWrite"]["engine"]["bytesPerSecond"] is None


def test_a_stopped_engine_reports_nulls(host):
    report = stats.build_report(None, stats.read_counters())

    assert report["cpu"]["engine"] == {"pid": None, "coresUsed": None, "percent": None}
    assert report["memory"]["engine"]["rssBytes"] is None


def test_missing_cgroup_files_give_nulls_not_errors(host):
    for name in ("cpu.stat", "memory.current", "io.stat"):
        (host.cgroup / name).unlink()

    report = stats.build_report(None, stats.read_counters())

    assert report["cpu"]["container"]["coresUsed"] is None
    assert report["memory"]["container"]["usedBytes"] is None
    assert report["storageWrite"]["container"]["totalBytes"] is None


def test_a_zero_interval_cannot_spin_the_sampler(monkeypatch):
    monkeypatch.setenv("STATS_SAMPLE_INTERVAL_SECONDS", "0")

    assert stats.interval_seconds() == stats.MIN_INTERVAL_SECONDS


# ---------------------------------------------------------------------------
# Route
# ---------------------------------------------------------------------------


def test_get_stats_returns_the_latest_report(client, host, monkeypatch):
    sampler = stats.Sampler()
    sampler.report = {"sampledAt": "2026-09-24T00:00:00Z", "windowSeconds": 2.0}
    monkeypatch.setattr(stats, "sampler", sampler)

    response = client.get("/stats")

    assert response.status_code == 200
    assert response.json() == sampler.report


def test_get_stats_samples_once_when_no_report_exists_yet(client, host, monkeypatch):
    monkeypatch.setattr(stats, "sampler", stats.Sampler())

    body = client.get("/stats").json()

    assert body["windowSeconds"] is None
    assert set(body) == {"sampledAt", "windowSeconds", "cpu", "memory", "storage", "storageWrite", "network"}
