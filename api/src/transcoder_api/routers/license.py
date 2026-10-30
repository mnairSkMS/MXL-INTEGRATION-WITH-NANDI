"""The installed engine license - the Licence tab of the channel UI.

GET describes the installed license, PUT replaces it and restarts the engine, DELETE removes it
and stops the engine. The engine reads its license only at start, so replacing the file without
a restart would change nothing, and removing it without a stop would leave a channel on air
under a license the operator believes is gone.

Issuing licenses (generate, sign, list what was issued) is not here and must not be: it needs
the signing private key, which belongs on the vendor side and never on a customer appliance.
See doc/license/license-tools.md.

Process control goes through the config router's supervisor routes, so there is one
implementation of stop and restart.
"""

from __future__ import annotations

import asyncio
from typing import Any

from fastapi import APIRouter, File, HTTPException, UploadFile

from ..exceptions import NotFoundException
from ..services import license as license_service
from . import config as config_router

router = APIRouter(prefix="/license", tags=["License"])


@router.get("")
async def get_license() -> dict[str, Any]:
    """The installed license as the engine reads it: name, id, validity window, mode, sessions.

    status is valid, expired, notYetValid or invalid. An installed file the engine would refuse
    is still described here rather than reported as an error, so the UI can say what is wrong.
    """
    return await asyncio.to_thread(license_service.read)


@router.put("")
async def put_license(file: UploadFile = File(...), dryRun: bool = False) -> dict[str, Any]:
    """Replace the installed license, then restart the engine on it.

    The upload is checked by the engine binary before it replaces anything. A license the
    engine would refuse - corrupted, signed for another build, expired, not yet valid, or online
    with no license server configured - comes back 422 and the installed one stays in place.

    The restart takes the channel off air for the restart grace period
    (TRANSCODER_RESTART_GRACE_SECONDS, 10 s by default) plus engine start-up. dryRun=true
    checks the upload without installing it or touching the engine.
    """
    content = await file.read(license_service.MAX_BYTES + 1)
    result = await asyncio.to_thread(license_service.install, content, dryRun)
    if dryRun:
        result["restartRequired"] = False
        return result

    try:
        await config_router.restart_transcoder()
    except HTTPException as exc:
        raise HTTPException(
            status_code=exc.status_code,
            detail=(
                "the license was installed but the engine could not be restarted on it; "
                f"restart it with POST /config/restart-transcoder. {exc.detail}"
            ),
        )

    result["engine"] = (await config_router.check_transcoder())["message"]
    return result


@router.delete("")
async def delete_license() -> dict[str, Any]:
    """Stop the engine, then delete the installed license.

    The engine stays stopped: it cannot start without a license, so the channel is off air
    until a new one is uploaded with PUT /license. The engine is stopped first so that a
    failure to stop leaves the license where it was, rather than a running engine with no
    license file behind it.
    """
    path = license_service.license_path()
    if not path.is_file():
        # Checked before the stop so a DELETE with nothing to delete does not take a channel
        # off air.
        raise NotFoundException(f"no license is installed at {path}")

    stopped = await config_router.stop_transcoder()
    result = await asyncio.to_thread(license_service.remove)
    result["engine"] = stopped["message"]
    return result
