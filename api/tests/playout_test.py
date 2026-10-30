from transcoder_api.services import media


def test_import_enqueue_move_and_delete(client, monkeypatch, tmp_path):
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(media, "_probe", lambda path: {"streams": [{"codec_type": "video"}]})
    monkeypatch.setattr(media, "_live_input_and_current", lambda *_: ("input", None))
    (tmp_path / "clip.mp4").write_bytes(b"media")

    response = client.post("/media/import", json={"relativePath": "clip.mp4"})
    assert response.status_code == 201
    asset = response.json()

    response = client.post("/playlists/main/items", json={"mediaId": asset["id"], "expectedRevision": 0})
    assert response.status_code == 202
    first = response.json()
    assert first["runState"] == "running"
    assert len(first["items"]) == 1

    response = client.post("/playlists/main/items", json={"mediaId": asset["id"], "expectedRevision": first["desiredRevision"]})
    assert response.status_code == 202
    second = response.json()
    assert len(second["items"]) == 2

    item = second["items"][1]
    response = client.patch(
        f"/playlists/main/items/{item['id']}",
        json={"position": 0, "expectedRevision": second["desiredRevision"]},
    )
    assert response.status_code == 202
    moved = response.json()
    assert moved["items"][0]["id"] == item["id"]

    response = client.delete(f"/playlists/main/items/{item['id']}?expectedRevision={moved['desiredRevision']}")
    assert response.status_code == 202
    assert len(response.json()["items"]) == 1


def test_playlist_rejects_stale_revision_and_traversal(client, monkeypatch, tmp_path):
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(media, "_probe", lambda path: {"streams": [{"codec_type": "video"}]})
    (tmp_path / "clip.mp4").write_bytes(b"media")
    asset = client.post("/media/import", json={"relativePath": "clip.mp4"}).json()
    snapshot = client.post("/playlists/main/items", json={"mediaId": asset["id"], "expectedRevision": 0}).json()
    item = snapshot["items"][0]
    assert (
        client.patch(
            f"/playlists/main/items/{item['id']}", json={"position": 0, "expectedRevision": 0}
        ).status_code
        == 409
    )
    assert client.post("/media/import", json={"relativePath": "../escape.mp4"}).status_code == 422


def test_managed_skip_does_not_change_queue_revision(client, monkeypatch, tmp_path):
    from types import SimpleNamespace
    from uuid import UUID

    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(media, "_probe", lambda path: {"streams": [{"codec_type": "video"}]})
    (tmp_path / "clip.mp4").write_bytes(b"media")
    asset = client.post("/media/import", json={"relativePath": "clip.mp4"}).json()
    first = client.post(
        "/playlists/main/items", json={"mediaId": asset["id"], "expectedRevision": 0}
    ).json()
    second = client.post(
        "/playlists/main/items",
        json={"mediaId": asset["id"], "expectedRevision": first["desiredRevision"]},
    ).json()

    # Skip is only legal after reconciliation has acknowledged the desired order.
    from transcoder_api.model.db import PlaylistState
    from .conftest import TestingSessionLocal
    db = TestingSessionLocal()
    playlist_state = db.get(PlaylistState, "main")
    playlist_state.applied_revision = playlist_state.revision
    db.commit()
    db.close()

    def identity(value):
        return SimpleNamespace(root=UUID(value))

    live = SimpleNamespace(
        id=identity("00000000-0000-0000-0000-000000000001"),
        currentStreamId=identity(second["items"][0]["id"]),
    )
    switched = []
    monkeypatch.setattr(media, "_live_input", lambda *_: live)
    monkeypatch.setattr(
        media.transcoder, "switchInputStream", lambda input_id, stream_id: switched.append((input_id, stream_id))
    )

    response = client.post("/playlists/main/skip")
    assert response.status_code == 202
    assert response.json()["desiredRevision"] == second["desiredRevision"]
    assert response.json()["skipSequence"] == 1
    assert switched[0][1] == second["items"][1]["id"]


def test_switch_adapter_passes_plain_uuid_to_public_update(monkeypatch):
    captured = []
    monkeypatch.setattr(
        media.transcoder,
        "updateInput",
        lambda input_id, parameters: captured.append((input_id, parameters.currentStreamId.root)),
    )
    input_id = "00000000-0000-0000-0000-000000000001"
    stream_id = "00000000-0000-0000-0000-000000000002"
    media.transcoder.switchInputStream(input_id, stream_id)
    assert str(captured[0][0]) == input_id
    assert str(captured[0][1]) == stream_id


def _probe_rejecting_bad(path):
    if path.read_bytes() == b"bad":
        raise media.UnprocessableException("uploaded media could not be probed")
    return {"streams": [{"codec_type": "video"}]}


def test_upload_stores_each_file_and_reports_failures(client, monkeypatch, tmp_path):
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(media, "_probe", _probe_rejecting_bad)
    files = [
        ("file", ("a.mp4", b"media-a", "video/mp4")),
        ("file", ("notes.txt", b"text", "text/plain")),
        ("file", ("broken.ts", b"bad", "video/mp2t")),
        ("file", ("b.mxf", b"media-b", "application/mxf")),
    ]

    response = client.post("/media", files=files)

    assert response.status_code == 207
    body = response.json()
    assert (body["created"], body["failed"]) == (2, 2)
    assert [r["filename"] for r in body["results"]] == ["a.mp4", "notes.txt", "broken.ts", "b.mxf"]
    assert [r["status"] for r in body["results"]] == [201, 422, 422, 201]
    assert body["results"][1]["error"] == "unsupported media file extension"
    assert body["results"][2]["media"] is None
    # The good files are real assets; the rejected ones left nothing behind.
    listed = {asset["name"] for asset in client.get("/media").json()}
    assert listed == {body["results"][0]["media"]["name"], body["results"][3]["media"]["name"]}
    assert sorted(p.suffix for p in tmp_path.iterdir()) == [".mp4", ".mxf"]


def test_upload_of_good_files_is_created(client, monkeypatch, tmp_path):
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(media, "_probe", _probe_rejecting_bad)
    files = [("file", (f"clip{i}.ts", b"media", "video/mp2t")) for i in range(3)]

    response = client.post("/media", files=files)

    assert response.status_code == 201
    assert response.json()["created"] == 3
    assert len(client.get("/media").json()) == 3


def test_upload_refuses_more_files_than_the_limit(client, monkeypatch, tmp_path):
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(media, "_probe", _probe_rejecting_bad)
    monkeypatch.setattr(media, "MAX_UPLOAD_FILES", 2)
    files = [("file", (f"clip{i}.ts", b"media", "video/mp2t")) for i in range(3)]

    response = client.post("/media", files=files)

    assert response.status_code == 422
    assert list(tmp_path.iterdir()) == []


def test_upload_of_one_file_keeps_the_same_request(client, monkeypatch, tmp_path):
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(media, "_probe", _probe_rejecting_bad)

    response = client.post("/media", files={"file": ("clip.ts", b"media", "video/mp2t")})

    assert response.status_code == 201
    result = response.json()["results"][0]
    assert (result["filename"], result["status"]) == ("clip.ts", 201)
    assert result["media"]["sizeBytes"] == 5


def _real_clip(tmp_path):
    import shutil
    import subprocess

    import pytest

    if not shutil.which("ffmpeg"):
        pytest.skip("ffmpeg is not installed")
    clip = tmp_path / "clip.mp4"
    subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc=size=640x360:rate=25:duration=4",
         "-pix_fmt", "yuv420p", "-y", str(clip)],
        check=True,
    )
    return clip


def test_thumbnail_is_cut_once_and_cached(client, monkeypatch, tmp_path):
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    _real_clip(tmp_path)
    monkeypatch.setattr(
        media, "_probe", lambda path: {"streams": [{"codec_type": "video"}], "format": {"duration": "4.0"}}
    )
    asset = client.post("/media/import", json={"relativePath": "clip.mp4"}).json()

    response = client.get(f"/media/{asset['id']}/thumbnail", params={"at": 2, "width": 200})

    assert response.status_code == 200
    assert response.headers["content-type"] == "image/jpeg"
    assert response.content[:2] == b"\xff\xd8"
    cached = list((tmp_path / ".thumbnails").glob("*.jpg"))
    assert [p.name for p in cached] == [f"{asset['id']}-20-320.jpg"]  # width snapped up to 320

    def no_ffmpeg(*args, **kwargs):
        raise AssertionError("a cached thumbnail must not run ffmpeg again")

    monkeypatch.setattr(media.subprocess, "run", no_ffmpeg)
    assert client.get(f"/media/{asset['id']}/thumbnail", params={"at": 2, "width": 320}).status_code == 200


def test_thumbnail_rejects_positions_past_the_end_and_is_removed_with_the_asset(client, monkeypatch, tmp_path):
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    _real_clip(tmp_path)
    monkeypatch.setattr(
        media, "_probe", lambda path: {"streams": [{"codec_type": "video"}], "format": {"duration": "4.0"}}
    )
    asset = client.post("/media/import", json={"relativePath": "clip.mp4"}).json()

    assert client.get(f"/media/{asset['id']}/thumbnail", params={"at": 9}).status_code == 422
    assert client.get("/media/no-such-id/thumbnail").status_code == 404
    assert client.get(f"/media/{asset['id']}/thumbnail").status_code == 200  # default: 10% in

    assert client.delete(f"/media/{asset['id']}").status_code == 204
    assert list((tmp_path / ".thumbnails").glob("*.jpg")) == []
