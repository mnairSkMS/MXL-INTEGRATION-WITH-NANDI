import os
import time

from transcoder_api.exceptions import TranscoderApiException
from transcoder_api.model.rest_api import ErrorCode
from transcoder_api.services import preview, transcoder

INPUT = "11111111-2222-3333-4444-555555555555"
JPEG = b"\xff\xd8\xff\xe0fake-jpeg\xff\xd9"


def _engine(monkeypatch, frame, taps=1, calls=None):
    def set_input_preview(input_id, mode, ttl_ms):
        if calls is not None:
            calls.append((str(input_id), mode, ttl_ms))
        return {"framePath": str(frame), "taps": taps}

    monkeypatch.setattr(transcoder, "setInputPreview", set_input_preview)


def test_still_returns_the_fresh_frame_and_renews_still_demand(client, monkeypatch, tmp_path):
    frame = tmp_path / f"{INPUT}.jpg"
    frame.write_bytes(JPEG)
    calls = []
    _engine(monkeypatch, frame, calls=calls)

    response = client.get(f"/inputs/{INPUT}/preview.jpg")

    assert response.status_code == 200
    assert response.headers["content-type"] == "image/jpeg"
    assert response.headers["cache-control"] == "no-store"
    assert response.content == JPEG
    assert calls == [(INPUT, "still", preview.STILL_TTL_MS)]


def test_still_without_a_video_tap_is_unavailable(client, monkeypatch, tmp_path):
    _engine(monkeypatch, tmp_path / "none.jpg", taps=0)

    response = client.get(f"/inputs/{INPUT}/preview.jpg")

    assert response.status_code == 503
    assert response.json()["message"] == "the input has no video stream to preview yet"


def test_still_with_only_a_stale_frame_gives_up_after_the_wait(client, monkeypatch, tmp_path):
    frame = tmp_path / f"{INPUT}.jpg"
    frame.write_bytes(JPEG)
    old = time.time() - 60
    os.utime(frame, (old, old))
    _engine(monkeypatch, frame)
    monkeypatch.setattr(preview, "FIRST_FRAME_WAIT_SECONDS", 0.2)

    response = client.get(f"/inputs/{INPUT}/preview.jpg")

    assert response.status_code == 503
    assert "retry" in response.json()["message"]


def test_unknown_input_is_not_found(client, monkeypatch):
    def unknown(*_):
        raise TranscoderApiException("grpc.INVALID_ARGUMENT: no such input", ErrorCode.INVALID_REQUEST, http_code=404)

    monkeypatch.setattr(transcoder, "setInputPreview", unknown)

    assert client.get(f"/inputs/{INPUT}/preview.jpg").status_code == 404
    assert client.get(f"/inputs/{INPUT}/preview.mjpeg").status_code == 404


def test_live_streams_frames_as_multipart_mjpeg(client, monkeypatch, tmp_path):
    frame = tmp_path / f"{INPUT}.jpg"
    frame.write_bytes(JPEG)
    calls = []
    _engine(monkeypatch, frame, calls=calls)
    monkeypatch.setattr(preview, "LIVE_MAX_SECONDS", 0.3)

    response = client.get(f"/inputs/{INPUT}/preview.mjpeg")

    assert response.status_code == 200
    assert response.headers["content-type"] == f"multipart/x-mixed-replace; boundary={preview.BOUNDARY}"
    # One unchanged file is sent once, not once per poll.
    assert response.content.count(b"Content-Type: image/jpeg") == 1
    assert JPEG in response.content
    assert calls[0] == (INPUT, "live", preview.LIVE_TTL_MS)
