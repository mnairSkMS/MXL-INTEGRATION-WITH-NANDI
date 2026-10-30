"""The engine configuration document and the supervisor process that reads it.

The config is parsed once at engine start, so this is a provisioning surface: PUT the document,
then restart. Writes go through services/engine_config.py, which validates structurally and
replaces the file with an atomic rename.

The four set-* routes this file used to carry are retired. They never worked - each called
json.loads() on a body FastAPI had already parsed, so every one of them returned HTTP 200 while
writing nothing - and they reached only transcoder.inputs and transcoder.outputs, leaving
version, squeeze, scte, blanking, blackout and the whole application object unaddressable. One
whole-document PUT replaces all four and covers the rest of the file.

Process control lives here too. The engine and this API are separate supervisor programs, so
stop/start/restart move the channel without touching the control plane serving the request.
"""

from __future__ import annotations

import asyncio
import os
import xmlrpc.client
from typing import Any

from fastapi import APIRouter, Body, HTTPException

from ..exceptions import GoneException
from ..services import engine_config

router = APIRouter(prefix="/config", tags=["Config"])

SUPERVISOR_SOCKET_PATH = "/var/run/supervisor.sock"
PROGRAM_NAME = "transcoder"

_RETIRED = (
    "this route never wrote anything and has been retired; "
    "use PUT /config to replace the whole configuration document"
)


def _supervisor_proxy() -> xmlrpc.client.ServerProxy:
    """Built per call rather than at import.

    supervisor ships in the runtime image rather than being a declared API dependency, so an
    import-time reference would make this whole module unimportable wherever it is absent.
    Building it here also gives the tests a single seam to substitute.
    """
    import supervisor.xmlrpc

    return xmlrpc.client.ServerProxy(
        "http://127.0.0.1",
        transport=supervisor.xmlrpc.SupervisorTransport(
            serverurl="unix://" + SUPERVISOR_SOCKET_PATH
        ),
    )


def _restart_grace_seconds() -> float:
    """How long to wait between stopping and starting the engine."""
    return float(os.environ.get("TRANSCODER_RESTART_GRACE_SECONDS", "10"))


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------


@router.get("")
async def get_config() -> dict[str, Any]:
    return engine_config.read()


@router.get("/get-config", deprecated=True)
async def get_config_legacy() -> dict[str, Any]:
    """The original spelling of GET /config, kept so existing clients keep working."""
    return engine_config.read()


@router.get("/get-outputs")
async def get_outputs():
    return engine_config.read()["transcoder"]["outputs"]


@router.get("/get-output/{output_id}")
async def get_output(output_id: int):
    data = engine_config.read()
    if output_id < len(data["transcoder"]["outputs"]):
        return data["transcoder"]["outputs"][output_id]
    else:
        return {"message": "output_id doesn't exist"}


@router.get("/get-inputs")
async def get_inputs():
    return engine_config.read()["transcoder"]["inputs"]


@router.get("/get-input/{input_id}")
async def get_input(input_id: int):
    data = engine_config.read()
    if input_id < len(data["transcoder"]["inputs"]):
        return data["transcoder"]["inputs"][input_id]
    else:
        return {"message": "input_id doesn't exist"}


# ---------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------


@router.put("")
async def put_config(
    document: dict[str, Any] = Body(...),
    dryRun: bool = False,
) -> dict[str, Any]:
    """Replace the engine configuration wholesale.

    dryRun is a query parameter rather than a body field because the body is the document
    itself; the name matches the dryRun on the rundown service's apply request.

    Declaring the body as a dict means a JSON array, string or malformed body is refused by
    FastAPI as a request validation error - a 400 - before any of this runs. A structurally
    unusable object comes back 422, an unwritable destination 409.

    The engine reads its config once at start, so the returned restartRequired is always true:
    nothing here takes effect until POST /config/restart-transcoder.
    """
    if dryRun:
        return engine_config.check(document)
    return engine_config.write(document)


@router.post("/set-outputs", deprecated=True)
async def set_outputs_retired():
    raise GoneException(_RETIRED)


@router.post("/set-output/{output_id}", deprecated=True)
async def set_output_retired(output_id: str):
    raise GoneException(_RETIRED)


@router.post("/set-inputs", deprecated=True)
async def set_inputs_retired():
    raise GoneException(_RETIRED)


@router.post("/set-input/{input_id}", deprecated=True)
async def set_input_retired(input_id: str):
    raise GoneException(_RETIRED)


# ---------------------------------------------------------------------------
# Process control
# ---------------------------------------------------------------------------


@router.get("/check-transcoder")
async def check_transcoder():
    try:
        process_info = _supervisor_proxy().supervisor.getProcessInfo(PROGRAM_NAME)
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Error communicating with Supervisor: {exc}. " "Check socket permissions and path."
            ),
        )

    return {"message": process_info["statename"]}


# States in which the engine holds its input sockets and is, or is trying to be, on air.
_ACTIVE_STATES = {"RUNNING", "STARTING", "BACKOFF"}


def _process_state(proxy) -> str:
    return proxy.supervisor.getProcessInfo(PROGRAM_NAME)["statename"]


@router.post("/stop-transcoder")
async def stop_transcoder():
    """Stop the engine and leave it stopped. The API stays up.

    `transcoder` and `transcoder_api` are separate supervisor programs, so this takes the
    channel off air without disturbing this API, the operator UI or the channel UI.

    Unlike restart, this is not self-healing: nothing brings the channel back but an explicit
    start. It exists because some operations require the engine to release its inputs - most
    obviously probing the source it is currently holding, which would otherwise reopen a
    single-subscriber socket underneath a live channel.

    Idempotent. Stopping an already-stopped engine reports the state rather than failing:
    supervisor raises NOT_RUNNING for that case, and a caller asking for "stopped" got what it
    asked for.
    """
    try:
        proxy = _supervisor_proxy()
        if _process_state(proxy) in _ACTIVE_STATES:
            proxy.supervisor.stopProcess(PROGRAM_NAME)
        return {"message": _process_state(proxy)}
    except xmlrpc.client.Fault as fault:
        # 70 is NOT_RUNNING. Anything else is a real supervisor failure.
        if fault.faultCode == 70:
            return {"message": "STOPPED"}
        raise HTTPException(
            status_code=500,
            detail=f"Supervisor refused to stop '{PROGRAM_NAME}': {fault.faultString}",
        )
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Error communicating with Supervisor: {exc}. " "Check socket permissions and path."
            ),
        )


@router.post("/start-transcoder")
async def start_transcoder():
    """Start a stopped engine, which reads the configuration on disk as it comes up.

    Idempotent for the same reason as stop: supervisor raises ALREADY_STARTED when the process
    is already up, and a caller asking for "started" got what it asked for.

    The returned state is the one supervisor reports immediately, which is normally STARTING
    rather than RUNNING. Poll GET /config/check-transcoder for the outcome - a config the
    engine rejects surfaces as BACKOFF or FATAL, not as a failure of this call.
    """
    try:
        proxy = _supervisor_proxy()
        if _process_state(proxy) not in _ACTIVE_STATES:
            proxy.supervisor.startProcess(PROGRAM_NAME, False)
        return {"message": _process_state(proxy)}
    except xmlrpc.client.Fault as fault:
        # 60 is ALREADY_STARTED.
        if fault.faultCode == 60:
            return {"message": _process_state(_supervisor_proxy())}
        raise HTTPException(
            status_code=500,
            detail=f"Supervisor refused to start '{PROGRAM_NAME}': {fault.faultString}",
        )
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Error communicating with Supervisor: {exc}. " "Check socket permissions and path."
            ),
        )


@router.post("/restart-transcoder")
@router.get("/restart-transcoder", deprecated=True)
async def restart_transcoder():
    """Restart the engine so it re-reads its configuration.

    Also reachable over GET, which is how it shipped and how existing clients call it. That
    spelling is deprecated because it side-effects; POST is the one to use.

    A stopped engine is started rather than refused: the previous version tested a stop result
    that only existed on the branch where the process was already running, so restarting
    anything not RUNNING raised UnboundLocalError and was reported as a supervisor failure.
    """
    try:
        proxy = _supervisor_proxy()
        process_info = proxy.supervisor.getProcessInfo(PROGRAM_NAME)

        if process_info["statename"] in {"RUNNING", "STARTING", "BACKOFF"}:
            proxy.supervisor.stopProcess(PROGRAM_NAME)
            await asyncio.sleep(_restart_grace_seconds())

        proxy.supervisor.startProcess(PROGRAM_NAME)
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Error communicating with Supervisor: {exc}. " "Check socket permissions and path."
            ),
        )

    return {"message": f"Program '{PROGRAM_NAME}' restarted successfully."}
