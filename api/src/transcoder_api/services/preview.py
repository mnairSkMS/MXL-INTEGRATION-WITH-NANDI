"""Input preview: JPEG frames the engine writes while someone is watching.

The engine taps each input's video and writes `<id>.jpg` (640 px wide) to a RAM-backed
directory, but only while demand is renewed over gRPC; it stops by itself when renewals
stop. Two read paths share that file:

* still - a fresh keyframe every GOP (~1-2 s). The engine decodes keyframes only, so this
  costs about 2% of a core per previewed 1080i input. Each GET renews demand.
* live - up to 10 frames a second, sent as multipart/x-mixed-replace (MJPEG), which an
  <img> tag plays directly. Full decode: about a quarter of a core per previewed input,
  only while a viewer is connected.

When the on-air path already decodes an input (transcoded output), the tap reuses those
frames and only scales and JPEG-encodes them.
"""

from __future__ import annotations

import asyncio
import os
import pathlib
import time
from typing import AsyncIterator

from ..exceptions import TranscoderApiException
from ..model.rest_api import ErrorCode
from . import transcoder

STILL_TTL_MS = 30_000          # a still poller renews well within this
LIVE_TTL_MS = 15_000           # a live stream renews every LIVE_RENEW_SECONDS
LIVE_RENEW_SECONDS = 5.0
FIRST_FRAME_WAIT_SECONDS = float(os.environ.get("PREVIEW_FIRST_FRAME_WAIT_SECONDS", "4"))
FRESH_SECONDS = 10.0           # an older frame means the engine stopped writing
LIVE_POLL_SECONDS = 0.05
LIVE_MAX_SECONDS = float(os.environ.get("PREVIEW_LIVE_MAX_SECONDS", "1800"))
BOUNDARY = "utplframe"


class PreviewUnavailable(TranscoderApiException):
    def __init__(self, message: str):
        super().__init__(message, ErrorCode.UNAVAILABLE, http_code=503)


def _mtime(path: pathlib.Path) -> float | None:
    try:
        return path.stat().st_mtime
    except FileNotFoundError:
        return None


def _demand(input_id, mode: str, ttl_ms: int) -> pathlib.Path:
    state = transcoder.setInputPreview(input_id, mode, ttl_ms)
    if state["taps"] == 0:
        raise PreviewUnavailable("the input has no video stream to preview yet")
    return pathlib.Path(state["framePath"])


async def still(input_id) -> bytes:
    """The newest frame, waiting briefly for the first keyframe after a cold start."""
    path = await asyncio.to_thread(_demand, input_id, "still", STILL_TTL_MS)
    deadline = time.monotonic() + FIRST_FRAME_WAIT_SECONDS
    while True:
        mtime = _mtime(path)
        if mtime is not None and time.time() - mtime < FRESH_SECONDS:
            try:
                return path.read_bytes()
            except FileNotFoundError:
                pass
        if time.monotonic() >= deadline:
            raise PreviewUnavailable("no preview frame yet; retry in a few seconds")
        await asyncio.sleep(0.1)


async def live(input_id) -> AsyncIterator[bytes]:
    """MJPEG parts for as long as the client stays connected (capped by LIVE_MAX_SECONDS)."""
    path = await asyncio.to_thread(_demand, input_id, "live", LIVE_TTL_MS)

    async def frames() -> AsyncIterator[bytes]:
        started = renewed = time.monotonic()
        sent = None
        while time.monotonic() - started < LIVE_MAX_SECONDS:
            if time.monotonic() - renewed >= LIVE_RENEW_SECONDS:
                try:
                    await asyncio.to_thread(_demand, input_id, "live", LIVE_TTL_MS)
                except TranscoderApiException:
                    return  # engine gone or input removed: end the stream
                renewed = time.monotonic()
            mtime = _mtime(path)
            if mtime is not None and mtime != sent:
                try:
                    jpeg = path.read_bytes()
                except FileNotFoundError:
                    jpeg = None
                if jpeg:
                    sent = mtime
                    yield (
                        f"--{BOUNDARY}\r\nContent-Type: image/jpeg\r\nContent-Length: {len(jpeg)}\r\n\r\n".encode()
                        + jpeg
                        + b"\r\n"
                    )
            await asyncio.sleep(LIVE_POLL_SECONDS)

    return frames()
