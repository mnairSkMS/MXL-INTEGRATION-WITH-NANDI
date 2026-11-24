"""The flows in the MXL domain - GET /mxl/flows. See services/mxl.py for where the data comes from."""

from __future__ import annotations

import asyncio
from typing import Any

from fastapi import APIRouter

from ..services import mxl as mxl_service

router = APIRouter(prefix="/mxl", tags=["MXL"])


@router.get("/flows")
async def get_flows() -> dict[str, Any]:
    """The flows in the MXL domain, each with its format, size or channels, rate, label and whether a writer holds it.

    The domain is read from the files in it, with no MXL library. `writerActive` is null when the
    kernel's lock list cannot be read. A domain that does not exist or cannot be read gives
    `available: false` and an `error`, not an HTTP error: a deployment without MXL is a normal one.
    """
    return await asyncio.to_thread(mxl_service.list_flows)
