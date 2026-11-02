"""The engine config document: what may be written, and what a failed write must not do.

Two things here are load-bearing and neither is obvious from the code.

Validation is deliberately shallow. transcoder/config/doc/openapi.yaml describes this document
but diverges from the C++ parser that actually reads it, so a schema check would refuse configs
the engine accepts. test_unknown_keys_survive_a_round_trip pins that: anything the parser might
understand has to reach it untouched.

The four set-* routes are gone rather than fixed. They returned HTTP 200 while writing nothing,
so a client could not tell a successful write from a failed one - which is why several tests
below assert on the file rather than only on the response.
"""

from __future__ import annotations

import json

import pytest

from transcoder_api.exceptions import TranscoderApiException
from transcoder_api.routers import config as config_router
from transcoder_api.services import engine_config


def _document(**overrides):
    """The shape the shipped configs have: version, application, transcoder."""
    document = {
        "version": "1.1.0",
        "application": {"logger": {"level": "info"}},
        "transcoder": {
            "inputs": [{"transport": {"uri": "srt://127.0.0.1:1234"}}],
            "outputs": [{"type": "SINGLE", "name": "out"}],
        },
    }
    document.update(overrides)
    return document


@pytest.fixture()
def env(tmp_path, monkeypatch):
    path = tmp_path / "config.json"
    path.write_text(json.dumps(_document(), indent=4) + "\n")
    monkeypatch.setenv("TRANSCODER_CONFIG_PATH", str(path))
    monkeypatch.setenv("TRANSCODER_RESTART_GRACE_SECONDS", "0")
    return path


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------


def test_the_shipped_shape_validates():
    engine_config.validate(_document())


@pytest.mark.parametrize("document", [[], "a string", 7, None])
def test_a_document_that_is_not_an_object_is_refused(document):
    with pytest.raises(TranscoderApiException) as excinfo:
        engine_config.validate(document)

    assert excinfo.value.http_code == 422


def test_a_document_without_a_version_is_refused():
    document = _document()
    del document["version"]

    with pytest.raises(TranscoderApiException) as excinfo:
        engine_config.validate(document)

    assert "version" in excinfo.value.message


@pytest.mark.parametrize("transcoder", [None, [], "x"])
def test_a_document_without_a_transcoder_object_is_refused(transcoder):
    with pytest.raises(TranscoderApiException) as excinfo:
        engine_config.validate(_document(transcoder=transcoder))

    assert "transcoder" in excinfo.value.message


@pytest.mark.parametrize("key", ["inputs", "outputs"])
@pytest.mark.parametrize("value", [{}, [], "x", None])
def test_empty_or_non_array_inputs_and_outputs_are_refused(key, value):
    """An object where an array belongs is exactly what the retired setters used to write."""
    document = _document()
    document["transcoder"][key] = value

    with pytest.raises(TranscoderApiException) as excinfo:
        engine_config.validate(document)

    assert f"transcoder.{key}" in excinfo.value.message


def test_an_entry_that_is_not_an_object_is_refused():
    document = _document()
    document["transcoder"]["outputs"] = ["not an object"]

    with pytest.raises(TranscoderApiException) as excinfo:
        engine_config.validate(document)

    assert "transcoder.outputs[0]" in excinfo.value.message


def test_a_non_object_application_is_refused():
    with pytest.raises(TranscoderApiException):
        engine_config.validate(_document(application=[]))


def test_unknown_keys_survive_a_round_trip(env):
    """The schema diverges from the parser, so nothing unrecognised may be dropped.

    'bandwith' is one of the real spelling divergences recorded in doc/codebase/27: the schema
    says bandwidth, the parser reads bandwith. Refusing or stripping either one would break a
    config that works.
    """
    document = _document()
    document["transcoder"]["outputs"][0]["bandwith"] = 5000000
    document["nditest"] = {"kept": True}

    engine_config.write(document)

    written = engine_config.read()
    assert written["transcoder"]["outputs"][0]["bandwith"] == 5000000
    assert written["nditest"] == {"kept": True}


def test_an_inputs_mxl_tap_block_survives_a_write(env):
    """inputs[n].mxlTap is read by the engine's parser but has no form field or API model.

    Setting it by hand in the config must not be undone by the next save.
    """
    tap = {
        "domain": "/run/mxl",
        "video": {"flowId": "5fbec3b1-1b0b-4fd4-8f76-1d6a3c5a0c01"},
        "audio": {"flowId": "5fbec3b1-1b0b-4fd4-8f76-1d6a3c5a0c02", "channels": 2},
    }
    document = _document()
    document["transcoder"]["inputs"][0]["mxlTap"] = tap

    engine_config.validate(document)
    engine_config.write(document)

    assert engine_config.read()["transcoder"]["inputs"][0]["mxlTap"] == tap


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------


def test_reading_a_missing_config_reports_404(tmp_path, monkeypatch):
    """The previous reader fell off the end and raised UnboundLocalError instead."""
    monkeypatch.setenv("TRANSCODER_CONFIG_PATH", str(tmp_path / "absent.json"))

    with pytest.raises(TranscoderApiException) as excinfo:
        engine_config.read()

    assert excinfo.value.http_code == 404


def test_reading_a_malformed_config_reports_422(tmp_path, monkeypatch):
    path = tmp_path / "config.json"
    path.write_text("{not json")
    monkeypatch.setenv("TRANSCODER_CONFIG_PATH", str(path))

    with pytest.raises(TranscoderApiException) as excinfo:
        engine_config.read()

    assert excinfo.value.http_code == 422


# ---------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------


def test_a_write_replaces_the_file_and_leaves_no_residue(env):
    document = _document(version="1.4.2")

    outcome = engine_config.write(document)

    assert outcome["state"] == "written"
    assert outcome["restartRequired"] is True
    assert json.loads(env.read_text())["version"] == "1.4.2"
    assert list(env.parent.glob("*.tmp")) == []
    assert list(env.parent.glob(".*.writable")) == []


def test_a_rejected_write_leaves_the_previous_file_untouched(env):
    before = env.read_bytes()
    document = _document()
    document["transcoder"]["outputs"] = {}

    with pytest.raises(TranscoderApiException):
        engine_config.write(document)

    assert env.read_bytes() == before


def test_the_file_mode_is_preserved_across_a_write(env):
    """os.replace installs a new inode, so the mode has to be reapplied deliberately."""
    env.chmod(0o640)

    engine_config.write(_document())

    assert env.stat().st_mode & 0o777 == 0o640


def test_a_write_into_a_missing_directory_is_refused(tmp_path, monkeypatch):
    """Provoked with a missing directory rather than a mode change, because the image's
    pytest gate runs as root and root ignores directory modes."""
    monkeypatch.setenv("TRANSCODER_CONFIG_PATH", str(tmp_path / "gone" / "config.json"))

    with pytest.raises(TranscoderApiException) as excinfo:
        engine_config.write(_document())

    assert excinfo.value.http_code == 409


def test_a_dry_run_validates_without_writing(env):
    before = env.read_bytes()

    outcome = engine_config.check(_document(version="9.9.9"))

    assert outcome["state"] == "validated"
    assert outcome["restartRequired"] is False
    assert env.read_bytes() == before


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------


def test_get_config_returns_the_whole_document(client, env):
    body = client.get("/config").json()

    assert body["version"] == "1.1.0"
    assert "transcoder" in body and "application" in body


def test_put_config_writes_and_reports_that_a_restart_is_needed(client, env):
    response = client.put("/config", json=_document(version="1.3.0"))

    assert response.status_code == 200
    assert response.json()["state"] == "written"
    assert response.json()["restartRequired"] is True
    assert json.loads(env.read_text())["version"] == "1.3.0"


def test_put_config_with_dry_run_does_not_write(client, env):
    before = env.read_bytes()

    response = client.put("/config?dryRun=true", json=_document(version="1.3.0"))

    assert response.status_code == 200
    assert response.json()["state"] == "validated"
    assert env.read_bytes() == before


def test_put_config_refuses_a_structurally_invalid_document(client, env):
    before = env.read_bytes()
    document = _document()
    document["transcoder"]["outputs"] = {}

    response = client.put("/config", json=document)

    assert response.status_code == 422
    assert "transcoder.outputs" in response.json()["message"]
    assert env.read_bytes() == before


def test_put_config_refuses_a_json_array_body(client, env):
    before = env.read_bytes()

    response = client.put("/config", json=[{"version": "1.1.0"}])

    assert response.status_code == 400
    assert env.read_bytes() == before


@pytest.mark.parametrize(
    "path",
    ["/config/set-outputs", "/config/set-inputs", "/config/set-output/0", "/config/set-input/0"],
)
def test_the_retired_setters_report_410_and_name_the_replacement(client, env, path):
    response = client.post(path, json={"anything": True})

    assert response.status_code == 410
    assert "PUT /config" in response.json()["message"]


# ---------------------------------------------------------------------------
# Process control
# ---------------------------------------------------------------------------


class _SupervisorFault(Exception):
    """Stands in for xmlrpc.client.Fault, which the routes catch by faultCode."""

    def __init__(self, faultCode, faultString=""):
        super().__init__(faultString)
        self.faultCode = faultCode
        self.faultString = faultString


class _FakeSupervisor:
    """A supervisor that actually transitions.

    The state has to move, because stop and start report the state they produced rather than
    the one they were called on. A fixed statename would let a route that never transitioned
    pass its own test.
    """

    ALREADY_STARTED = 60
    NOT_RUNNING = 70

    def __init__(self, statename, strict_faults=False):
        self.statename = statename
        self.calls = []
        # When set, the fake raises the faults real supervisor raises instead of tolerating a
        # redundant call. This is how the idempotence branches get exercised.
        self.strict_faults = strict_faults

    def getProcessInfo(self, name):
        self.calls.append(("info", name))
        return {"statename": self.statename}

    def stopProcess(self, name):
        self.calls.append(("stop", name))
        if self.statename == "STOPPED":
            if self.strict_faults:
                raise _SupervisorFault(self.NOT_RUNNING, "NOT_RUNNING")
            return True
        self.statename = "STOPPED"
        return True

    def startProcess(self, name, wait=True):
        self.calls.append(("start", name))
        if self.statename in {"RUNNING", "STARTING"}:
            if self.strict_faults:
                raise _SupervisorFault(self.ALREADY_STARTED, "ALREADY_STARTED")
            return True
        self.statename = "RUNNING" if wait else "STARTING"
        return True


@pytest.fixture()
def supervisor(monkeypatch):
    def install(statename, strict_faults=False):
        fake = _FakeSupervisor(statename, strict_faults=strict_faults)
        proxy = type("Proxy", (), {"supervisor": fake})()
        monkeypatch.setattr(config_router, "_supervisor_proxy", lambda: proxy)
        # The routes catch xmlrpc.client.Fault by faultCode; point that name at the stand-in
        # so the fake's faults take the same branch.
        monkeypatch.setattr(config_router.xmlrpc.client, "Fault", _SupervisorFault)
        return fake

    return install


def test_restart_stops_then_starts_a_running_transcoder(client, env, supervisor):
    fake = supervisor("RUNNING")

    response = client.post("/config/restart-transcoder")

    assert response.status_code == 200
    assert [call[0] for call in fake.calls] == ["info", "stop", "start"]


def test_restart_starts_a_stopped_transcoder(client, env, supervisor):
    """The previous version tested a stop result that was only assigned on the RUNNING
    branch, so this path raised UnboundLocalError and was reported as a supervisor failure."""
    fake = supervisor("STOPPED")

    response = client.post("/config/restart-transcoder")

    assert response.status_code == 200
    assert [call[0] for call in fake.calls] == ["info", "start"]


def test_restart_over_get_is_still_accepted(client, env, supervisor):
    fake = supervisor("RUNNING")

    response = client.get("/config/restart-transcoder")

    assert response.status_code == 200
    assert ("start", "transcoder") in fake.calls


def test_stop_takes_the_engine_off_air_and_reports_the_new_state(client, env, supervisor):
    """The point of the route: the channel stops, and this API is still here to say so."""
    fake = supervisor("RUNNING")

    response = client.post("/config/stop-transcoder")

    assert response.status_code == 200
    assert response.json() == {"message": "STOPPED"}
    assert ("stop", "transcoder") in fake.calls


def test_stop_is_idempotent(client, env, supervisor):
    """Asking a stopped engine to stop is not an error - the caller got what it asked for."""
    fake = supervisor("STOPPED")

    response = client.post("/config/stop-transcoder")

    assert response.status_code == 200
    assert response.json() == {"message": "STOPPED"}
    # Nothing was stopped, because there was nothing to stop.
    assert ("stop", "transcoder") not in fake.calls


def test_stop_tolerates_a_not_running_fault_from_supervisor(client, env, supervisor):
    """The state can change between the check and the call. NOT_RUNNING still means stopped."""
    fake = supervisor("RUNNING", strict_faults=True)
    # Move it underneath the route, after its getProcessInfo has already said RUNNING.
    original = fake.getProcessInfo

    def racing_info(name):
        result = original(name)
        fake.statename = "STOPPED"
        return result

    fake.getProcessInfo = racing_info

    response = client.post("/config/stop-transcoder")

    assert response.status_code == 200
    assert response.json() == {"message": "STOPPED"}


def test_stop_does_not_restart_the_engine(client, env, supervisor):
    """Distinguishes this route from restart. A stop that starts anything is the wrong route."""
    fake = supervisor("RUNNING")

    client.post("/config/stop-transcoder")

    assert ("start", "transcoder") not in fake.calls


def test_start_brings_a_stopped_engine_up(client, env, supervisor):
    fake = supervisor("STOPPED")

    response = client.post("/config/start-transcoder")

    assert response.status_code == 200
    # Reported immediately, so STARTING rather than RUNNING; the client polls check-transcoder.
    assert response.json() == {"message": "STARTING"}
    assert ("start", "transcoder") in fake.calls


def test_start_is_idempotent(client, env, supervisor):
    fake = supervisor("RUNNING")

    response = client.post("/config/start-transcoder")

    assert response.status_code == 200
    assert response.json() == {"message": "RUNNING"}
    assert ("start", "transcoder") not in fake.calls


def test_start_tolerates_an_already_started_fault(client, env, supervisor):
    fake = supervisor("STOPPED", strict_faults=True)
    original = fake.getProcessInfo

    def racing_info(name):
        result = original(name)
        fake.statename = "RUNNING"
        return result

    fake.getProcessInfo = racing_info

    response = client.post("/config/start-transcoder")

    assert response.status_code == 200
    assert response.json()["message"] in {"RUNNING", "STARTING"}


def test_a_real_supervisor_failure_on_stop_is_a_500(client, env, supervisor):
    """A fault that is not NOT_RUNNING must not be swallowed as success."""
    fake = supervisor("RUNNING")

    def broken(name):
        raise _SupervisorFault(fake.__class__.NOT_RUNNING + 1, "SHUTDOWN_STATE")

    fake.stopProcess = broken

    response = client.post("/config/stop-transcoder")

    assert response.status_code == 500
    assert "SHUTDOWN_STATE" in response.json()["detail"]


def test_stop_then_start_round_trips(client, env, supervisor):
    """The pair has to be usable as a pair: free the input, probe it, put the channel back."""
    supervisor("RUNNING")

    assert client.post("/config/stop-transcoder").json() == {"message": "STOPPED"}
    assert client.get("/config/check-transcoder").json() == {"message": "STOPPED"}

    assert client.post("/config/start-transcoder").json() == {"message": "STARTING"}
    assert client.get("/config/check-transcoder").json() == {"message": "STARTING"}
