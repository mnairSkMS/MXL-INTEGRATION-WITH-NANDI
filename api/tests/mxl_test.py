"""GET /mxl/flows and the reading of the MXL domain behind it.

Each test builds a domain of flow directories in a temporary directory, as MXL v1.1.0 lays them out
(`<id>.mxl-flow/flow_def.json` and `data`), and a fake /proc/locks, so what a flow reports can be
asserted exactly. The real layout and the real /proc/locks line were taken from a running writer
(tests/tests/mxl/tap/15-registry runs the engine against real writers).
"""

from __future__ import annotations

import json
import os
import pathlib

import pytest

from transcoder_api.services import mxl

VIDEO = "5fbec3b1-1b0f-417d-9059-8b94a47197ed"
AUDIO = "b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10"
GONE = "0c6f2d4a-3b8e-4f57-9a21-7d1e5c9b8a44"

VIDEO_DEF = {
    "id": VIDEO,
    "label": "MXL Video Flow, 240p29.97",
    "description": "MXL Video Flow, 240p29.97",
    "tags": {"urn:x-nmos:tag:grouphint/v1.0": ["Media Function 8 pipeline0:Video rsmxlsink0"]},
    "format": "urn:x-nmos:format:video",
    "media_type": "video/v210",
    "grain_rate": {"numerator": 30000, "denominator": 1001},
    "frame_width": 320,
    "frame_height": 240,
    "interlace_mode": "progressive",
}
AUDIO_DEF = {
    "id": AUDIO,
    "label": "MXL Audio Flow, 2 ch, 48 kHz",
    "tags": {},
    "format": "urn:x-nmos:format:audio",
    "media_type": "audio/float32",
    "sample_rate": {"numerator": 48000, "denominator": 1},
    "channel_count": 2,
    "bit_depth": 32,
}


def make_flow(domain: pathlib.Path, flow_id: str, definition: dict | None) -> pathlib.Path:
    flow = domain / f"{flow_id}.mxl-flow"
    flow.mkdir(parents=True)
    (flow / "data").write_bytes(b"\0" * 16)
    if definition is not None:
        (flow / "flow_def.json").write_text(json.dumps(definition))
    return flow


def set_locks(proc: pathlib.Path, *data_files: pathlib.Path):
    """A /proc/locks that lists a shared flock on each file, plus noise: a POSIX lock and another file's flock."""
    lines = ["1: POSIX  ADVISORY  WRITE 99 08:01:12345 0 EOF", "2: FLOCK  ADVISORY  WRITE 98 08:01:777 0 EOF"]
    for number, path in enumerate(data_files, start=3):
        st = path.stat()
        lines.append(f"{number}: FLOCK  ADVISORY  READ 8 {os.major(st.st_dev):02x}:{os.minor(st.st_dev):02x}:{st.st_ino} 0 EOF")
    (proc / "locks").write_text("\n".join(lines) + "\n")


@pytest.fixture()
def host(tmp_path, monkeypatch):
    domain = tmp_path / "domain"
    domain.mkdir()
    proc = tmp_path / "proc"
    proc.mkdir()
    (proc / "locks").write_text("")
    monkeypatch.setattr(mxl, "PROC_ROOT", proc)
    monkeypatch.setenv("MXL_DOMAIN", str(domain))
    return domain, proc


def test_lists_flows_with_what_a_person_choosing_one_needs(host):
    domain, proc = host
    video = make_flow(domain, VIDEO, VIDEO_DEF)
    make_flow(domain, AUDIO, AUDIO_DEF)
    set_locks(proc, video / "data")

    report = mxl.list_flows()

    assert report["available"] is True
    assert report["domain"] == str(domain)
    by_id = {f["id"]: f for f in report["flows"]}

    v = by_id[VIDEO]
    assert (v["format"], v["mediaType"], v["width"], v["height"], v["rate"]) == ("video", "video/v210", 320, 240, "30000/1001")
    assert v["interlaceMode"] == "progressive"
    assert v["label"] == "MXL Video Flow, 240p29.97"
    assert v["groupHints"] == ["Media Function 8 pipeline0:Video rsmxlsink0"]
    assert v["writerActive"] is True

    a = by_id[AUDIO]
    assert (a["format"], a["mediaType"], a["channels"], a["sampleRate"], a["bitDepth"]) == ("audio", "audio/float32", 2, "48000", 32)
    assert a["groupHints"] == []
    assert a["writerActive"] is False


def test_a_flow_whose_writer_is_gone_is_listed_as_not_written(host):
    domain, proc = host
    make_flow(domain, GONE, VIDEO_DEF | {"id": GONE})

    flows = mxl.list_flows()["flows"]

    assert [f["id"] for f in flows] == [GONE]
    assert flows[0]["writerActive"] is False


def test_writer_activity_is_unknown_when_the_lock_list_cannot_be_read(host):
    domain, proc = host
    make_flow(domain, VIDEO, VIDEO_DEF)
    (proc / "locks").unlink()

    assert mxl.list_flows()["flows"][0]["writerActive"] is None


def test_a_flow_being_created_is_listed_with_the_reason_it_is_not_described(host):
    domain, _ = host
    make_flow(domain, VIDEO, None)

    flow = mxl.list_flows()["flows"][0]

    assert flow["id"] == VIDEO
    assert "no readable flow description" in flow["error"]


def test_a_description_that_is_not_json_is_reported_not_raised(host):
    domain, _ = host
    flow = make_flow(domain, VIDEO, None)
    (flow / "flow_def.json").write_text("{not json")

    assert "no readable flow description" in mxl.list_flows()["flows"][0]["error"]


def test_things_that_are_not_flows_are_ignored(host):
    domain, _ = host
    make_flow(domain, VIDEO, VIDEO_DEF)
    (domain / "not-a-uuid.mxl-flow").mkdir()
    (domain / "options.json").write_text("{}")
    (domain / "readme.txt").write_text("x")

    assert [f["id"] for f in mxl.list_flows()["flows"]] == [VIDEO]


def test_a_missing_domain_is_reported_as_not_available(tmp_path, monkeypatch):
    monkeypatch.setenv("MXL_DOMAIN", str(tmp_path / "nowhere"))

    report = mxl.list_flows()

    assert report["available"] is False
    assert report["flows"] == []
    assert "cannot be read" in report["error"]


def test_the_route_returns_the_report(client, host):
    domain, proc = host
    video = make_flow(domain, VIDEO, VIDEO_DEF)
    set_locks(proc, video / "data")

    response = client.get("/mxl/flows")

    assert response.status_code == 200
    body = response.json()
    assert body["available"] is True
    assert body["flows"][0]["id"] == VIDEO
    assert body["flows"][0]["writerActive"] is True


def test_the_route_answers_200_for_a_deployment_without_mxl(client, tmp_path, monkeypatch):
    monkeypatch.setenv("MXL_DOMAIN", str(tmp_path / "nowhere"))

    response = client.get("/mxl/flows")

    assert response.status_code == 200
    assert response.json()["available"] is False
