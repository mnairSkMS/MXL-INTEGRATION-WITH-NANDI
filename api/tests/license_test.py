"""The installed license: inspect, replace-and-restart, stop-and-remove.

The license can only be decrypted by the transcoder binary, so the service shells out to
`transcoder -l <file> --license-info`. These tests stand in a small script for that binary
which reads the "license" it is given and echoes it back as the engine's report - so each test
chooses the verdict by choosing the file content, and the real subprocess path is exercised.

What matters most here is what a refused upload must not do: replace the installed license or
touch the engine. Several tests assert on the file and on the supervisor calls for that reason.
"""

from __future__ import annotations

import json
import stat
import sys

import pytest

from transcoder_api.routers import config as config_router

# Reuses the stateful supervisor fake and its fault stand-in from the config tests.
from .config_test import _FakeSupervisor, _SupervisorFault


def _report(status="valid", **fields):
    report = {
        "status": status,
        "id": "6e54eb21",
        "name": "e2e offline 7d",
        "validSince": 1790227249000,
        "validUntil": 1790832049000,
        "ttl": 604800000,
        "offline": True,
        "maxSessions": 1,
    }
    if status == "invalid":
        report = {"status": "invalid", "error": "GenericLicenseException: License is not valid"}
    report.update(fields)
    return report


def _license_bytes(report) -> bytes:
    return json.dumps(report).encode()


@pytest.fixture()
def env(tmp_path, monkeypatch):
    """A license directory, an engine config, and a fake engine binary."""
    license_dir = tmp_path / "license"
    license_dir.mkdir()
    installed = license_dir / "transcoder.lic"
    installed.write_bytes(_license_bytes(_report(name="installed")))

    config = tmp_path / "config.json"
    config.write_text(json.dumps({"version": "1.4.0", "application": {}, "transcoder": {}}))

    # Mimics the real binary's output: a log line first, then the one-line JSON report.
    binary = tmp_path / "transcoder"
    binary.write_text(
        f"#!{sys.executable}\n"
        "import sys, pathlib\n"
        "path = pathlib.Path(sys.argv[sys.argv.index('-l') + 1])\n"
        "assert '--license-info' in sys.argv\n"
        "print('[ 09/24/26 05:20:35.636 ] [root ] [ off ] Initialized UTPL library')\n"
        "print(path.read_text())\n"
    )
    binary.chmod(binary.stat().st_mode | stat.S_IEXEC)

    monkeypatch.setenv("TRANSCODER_LICENSE_PATH", str(installed))
    monkeypatch.setenv("TRANSCODER_BINARY", str(binary))
    monkeypatch.setenv("TRANSCODER_CONFIG_PATH", str(config))
    monkeypatch.setenv("TRANSCODER_RESTART_GRACE_SECONDS", "0")
    return installed


@pytest.fixture()
def supervisor(monkeypatch):
    fake = _FakeSupervisor("RUNNING")
    proxy = type("Proxy", (), {"supervisor": fake})()
    monkeypatch.setattr(config_router, "_supervisor_proxy", lambda: proxy)
    monkeypatch.setattr(config_router.xmlrpc.client, "Fault", _SupervisorFault)
    return fake


def _upload(client, report, dry_run=False):
    return client.put(
        "/license" + ("?dryRun=true" if dry_run else ""),
        files={"file": ("new.lic", _license_bytes(report), "application/octet-stream")},
    )


def _actions(fake):
    return [call[0] for call in fake.calls if call[0] != "info"]


# ---------------------------------------------------------------------------
# GET
# ---------------------------------------------------------------------------


def test_get_describes_the_installed_license(client, env):
    response = client.get("/license")

    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "valid"
    assert body["name"] == "installed"
    assert body["path"] == str(env)


def test_get_describes_an_installed_license_the_engine_would_refuse(client, env):
    """A bad installed file is reported, not turned into an error, so the UI can show why."""
    env.write_bytes(_license_bytes(_report("expired")))

    response = client.get("/license")

    assert response.status_code == 200
    assert response.json()["status"] == "expired"


def test_get_with_no_license_installed_is_404(client, env):
    env.unlink()

    assert client.get("/license").status_code == 404


def test_get_reports_503_when_the_engine_binary_is_missing(client, env, monkeypatch, tmp_path):
    monkeypatch.setenv("TRANSCODER_BINARY", str(tmp_path / "absent"))

    response = client.get("/license")

    assert response.status_code == 503
    assert "binary" in response.json()["message"]


def test_get_reports_503_for_a_binary_without_license_info(client, env, tmp_path, monkeypatch):
    """An engine older than --license-info prints usage and no report."""
    old = tmp_path / "old-transcoder"
    old.write_text("#!/bin/sh\necho 'Allowed options:'\nexit 4\n")
    old.chmod(0o755)
    monkeypatch.setenv("TRANSCODER_BINARY", str(old))

    assert client.get("/license").status_code == 503


# ---------------------------------------------------------------------------
# PUT
# ---------------------------------------------------------------------------


def test_upload_installs_the_license_and_restarts_the_engine(client, env, supervisor):
    response = _upload(client, _report(name="uploaded"))

    assert response.status_code == 200
    body = response.json()
    assert body["state"] == "installed"
    assert body["license"]["name"] == "uploaded"
    assert body["engine"] == "RUNNING"
    assert json.loads(env.read_text())["name"] == "uploaded"
    assert _actions(supervisor) == ["stop", "start"]


def test_upload_leaves_no_staging_residue(client, env, supervisor):
    _upload(client, _report())

    assert sorted(p.name for p in env.parent.iterdir()) == ["transcoder.lic"]


@pytest.mark.parametrize("status", ["invalid", "expired", "notYetValid"])
def test_a_license_the_engine_would_refuse_is_rejected_untouched(client, env, supervisor, status):
    before = env.read_bytes()

    response = _upload(client, _report(status))

    assert response.status_code == 422
    assert "license refused" in response.json()["message"]
    assert env.read_bytes() == before
    assert _actions(supervisor) == []
    assert sorted(p.name for p in env.parent.iterdir()) == ["transcoder.lic"]


def test_an_online_license_needs_a_license_server_in_the_config(client, env, supervisor):
    """Application::configure refuses an online license without an HTTP(S) server URI."""
    response = _upload(client, _report(offline=False))

    assert response.status_code == 422
    assert "licenseServer.uri" in response.json()["message"]
    assert _actions(supervisor) == []


def test_an_online_license_is_accepted_once_a_license_server_is_configured(
    client, env, supervisor, tmp_path
):
    config = tmp_path / "config.json"
    config.write_text(
        json.dumps(
            {
                "version": "1.4.0",
                "application": {"licenseServer": {"uri": "https://licenses.example:8000"}},
                "transcoder": {},
            }
        )
    )

    response = _upload(client, _report(offline=False))

    assert response.status_code == 200
    assert _actions(supervisor) == ["stop", "start"]


def test_a_dry_run_checks_without_installing_or_restarting(client, env, supervisor):
    before = env.read_bytes()

    response = _upload(client, _report(name="checked"), dry_run=True)

    assert response.status_code == 200
    assert response.json()["state"] == "validated"
    assert response.json()["license"]["name"] == "checked"
    assert env.read_bytes() == before
    assert _actions(supervisor) == []


def test_an_empty_upload_is_refused(client, env, supervisor):
    response = client.put("/license", files={"file": ("x.lic", b"", "text/plain")})

    assert response.status_code == 422


def test_an_oversized_upload_is_refused(client, env, supervisor):
    response = client.put("/license", files={"file": ("x.lic", b"x" * (65 * 1024), "text/plain")})

    assert response.status_code == 422
    assert _actions(supervisor) == []


def test_upload_installs_where_no_license_existed(client, env, supervisor):
    """After DELETE the directory is empty; an upload must be able to start from there."""
    env.unlink()

    response = _upload(client, _report())

    assert response.status_code == 200
    assert env.is_file()


def test_upload_into_a_missing_directory_is_409(client, env, supervisor, monkeypatch, tmp_path):
    monkeypatch.setenv("TRANSCODER_LICENSE_PATH", str(tmp_path / "absent" / "transcoder.lic"))

    assert _upload(client, _report()).status_code == 409


def test_a_failed_restart_says_the_license_was_installed(client, env, supervisor, monkeypatch):
    def broken():
        raise ConnectionRefusedError("no supervisor socket")

    monkeypatch.setattr(config_router, "_supervisor_proxy", broken)

    response = _upload(client, _report(name="uploaded"))

    assert response.status_code == 500
    assert "installed" in response.json()["detail"]
    assert json.loads(env.read_text())["name"] == "uploaded"


# ---------------------------------------------------------------------------
# DELETE
# ---------------------------------------------------------------------------


def test_delete_stops_the_engine_and_removes_the_license(client, env, supervisor):
    response = client.delete("/license")

    assert response.status_code == 200
    assert response.json()["state"] == "removed"
    assert response.json()["engine"] == "STOPPED"
    assert not env.exists()
    assert _actions(supervisor) == ["stop"]


def test_delete_with_nothing_installed_does_not_stop_the_engine(client, env, supervisor):
    env.unlink()

    response = client.delete("/license")

    assert response.status_code == 404
    assert _actions(supervisor) == []


def test_a_failed_stop_leaves_the_license_in_place(client, env, monkeypatch):
    def broken():
        raise ConnectionRefusedError("no supervisor socket")

    monkeypatch.setattr(config_router, "_supervisor_proxy", broken)

    response = client.delete("/license")

    assert response.status_code == 500
    assert env.is_file()
