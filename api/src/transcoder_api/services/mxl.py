"""The flows in the MXL domain: what is there, what each carries, and whether a writer holds it.

There is no registry process. MXL v1.1.0 keeps one directory per flow in the domain,
`<flow id>.mxl-flow/`, with the flow's description (`flow_def.json`, an NMOS-style flow resource)
beside its data file, so everything here is read from those files. The API needs no MXL library.

A flow is "written" while a process holds a shared advisory lock (flock) on the flow's data file;
the writer takes it when it creates the flow, and the lock goes when the writer exits or dies. MXL
tests it by taking an exclusive lock for an instant, which could make a writer that is just being
created fail to take its own. This module does not do that: it reads /proc/locks, which lists the
locks and the inode they are on, and compares the data file's inode. It cannot disturb a writer, at
the price of needing a readable /proc/locks; without one `writerActive` is null ("unknown").
A reader holds no lock, so an unread flow and a read flow look the same.

A flow whose writer is gone stays in the domain until something removes it (the engine does it
when it starts), so the list shows it with `writerActive: false`.
"""

from __future__ import annotations

import json
import logging
import os
import pathlib
import re
from typing import Any, Optional

log = logging.getLogger(__name__)

# Overridden by the tests.
PROC_ROOT = pathlib.Path("/proc")

FLOW_SUFFIX = ".mxl-flow"
DESCRIPTOR_FILE = "flow_def.json"
DATA_FILE = "data"

_UUID = re.compile(r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")
GROUP_HINT = "urn:x-nmos:tag:grouphint/v1.0"


def domain_path() -> pathlib.Path:
    """The MXL domain directory: MXL_DOMAIN, or /run/mxl (the Compose volume)."""
    return pathlib.Path(os.environ.get("MXL_DOMAIN", "/run/mxl"))


def _locked_inodes() -> Optional[set[tuple[int, int]]]:
    """(device, inode) of every file with a flock held, or None when /proc/locks cannot be read.

    A line looks like `29: FLOCK  ADVISORY  READ 8 00:93:59542138 0 EOF`; the fifth field is
    major:minor:inode, in hexadecimal for the device and decimal for the inode.
    """
    try:
        text = (PROC_ROOT / "locks").read_text()
    except OSError:
        return None

    found: set[tuple[int, int]] = set()
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 6 or fields[1] != "FLOCK":
            continue
        parts = fields[5].split(":")
        if len(parts) != 3:
            continue
        try:
            found.add(((int(parts[0], 16) << 8) | int(parts[1], 16), int(parts[2])))
        except ValueError:
            continue
    return found


def _device_id(st_dev: int) -> int:
    """The kernel's (major << 8 | minor) form of a stat() st_dev, which /proc/locks uses for small numbers."""
    return (os.major(st_dev) << 8) | os.minor(st_dev)


def _describe(definition: dict[str, Any]) -> dict[str, Any]:
    """The fields of an NMOS flow resource that a person choosing a flow needs."""
    fmt = str(definition.get("format", "")).rsplit(":", 1)[-1] or None  # urn:x-nmos:format:video -> video
    out: dict[str, Any] = {
        "label": definition.get("label") or definition.get("description"),
        "format": fmt,
        "mediaType": definition.get("media_type"),
    }

    if fmt == "video":
        rate = definition.get("grain_rate") or {}
        out["width"] = definition.get("frame_width")
        out["height"] = definition.get("frame_height")
        out["interlaceMode"] = definition.get("interlace_mode")
        out["rate"] = _fraction(rate)
    elif fmt == "audio":
        out["sampleRate"] = _fraction(definition.get("sample_rate") or {})
        out["channels"] = definition.get("channel_count")
        out["bitDepth"] = definition.get("bit_depth")
    elif fmt == "data":
        out["rate"] = _fraction(definition.get("grain_rate") or {})

    hints = (definition.get("tags") or {}).get(GROUP_HINT)
    out["groupHints"] = [str(h) for h in hints] if isinstance(hints, list) else []
    return out


def _fraction(value: dict[str, Any]) -> Optional[str]:
    """{"numerator": 30000, "denominator": 1001} -> "30000/1001"; a whole rate has no denominator part."""
    num = value.get("numerator")
    if not isinstance(num, int):
        return None
    den = value.get("denominator", 1)
    return str(num) if den in (1, None) else f"{num}/{den}"


def list_flows(domain: Optional[pathlib.Path] = None) -> dict[str, Any]:
    """Every flow in the domain, in order of flow id. Never raises for a missing or unreadable domain."""
    domain = domain or domain_path()
    result: dict[str, Any] = {"domain": str(domain), "available": False, "flows": []}

    try:
        entries = sorted(e for e in domain.iterdir() if e.is_dir() and e.name.endswith(FLOW_SUFFIX))
    except OSError as exc:
        result["error"] = f"the MXL domain cannot be read: {exc.strerror or exc}"
        return result

    result["available"] = True
    locked = _locked_inodes()
    flows = []

    for entry in entries:
        flow_id = entry.name[: -len(FLOW_SUFFIX)]
        if not _UUID.match(flow_id):
            continue

        flow: dict[str, Any] = {"id": flow_id}

        # A flow that is being created or removed has a directory and no description yet: list it, with what is known.
        try:
            definition = json.loads((entry / DESCRIPTOR_FILE).read_text())
            if not isinstance(definition, dict):
                raise ValueError("not an object")
            flow.update(_describe(definition))
        except (OSError, ValueError) as exc:
            flow["error"] = f"no readable flow description: {exc}"

        try:
            st = (entry / DATA_FILE).stat()
            flow["writerActive"] = None if locked is None else (_device_id(st.st_dev), st.st_ino) in locked
        except OSError:
            flow["writerActive"] = None

        flows.append(flow)

    result["flows"] = flows
    return result
