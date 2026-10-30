"""Resource utilization of the appliance - the channel UI's Stats tab.

One route, GET /stats, returning CPU, memory, storage, storage-write and network figures for
the container, the engine process and the host. See services/stats.py for where each number
comes from and why the rates are computed in the background.
"""

from __future__ import annotations

import asyncio
from typing import Any

from fastapi import APIRouter

from ..services import stats as stats_service

router = APIRouter(prefix="/stats", tags=["Stats"])


@router.get("")
async def get_stats() -> dict[str, Any]:
    """The report from the latest sampling window.

    Returns immediately with the figures from the last completed window, so polling it costs
    nothing beyond serialisation. Straight after start-up, before the sampler has run once,
    it takes one reading itself; rates are then null and windowSeconds says so.
    """
    report = stats_service.sampler.report
    if report is None:
        report = await asyncio.to_thread(stats_service.sampler.sample)
    return report
