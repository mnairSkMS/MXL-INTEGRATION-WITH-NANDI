import json
import struct
import zlib

import pytest


def _png(width: int, height: int) -> bytes:
    """A minimal valid RGBA PNG, so ffprobe reports real dimensions."""

    def chunk(tag: bytes, payload: bytes) -> bytes:
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    raw = b"".join(b"\x00" + b"\x00\x00\x00\x00" * width for _ in range(height))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


@pytest.fixture()
def graphics_env(client, monkeypatch, tmp_path):
    catalog = tmp_path / "graphics.json"
    catalog.write_text(json.dumps({"assets": []}))
    monkeypatch.setenv("GRAPHICS_CATALOG_PATH", str(catalog))
    monkeypatch.setenv("GRAPHICS_MEDIA_DIR", str(tmp_path / "media"))
    monkeypatch.setenv("GRAPHICS_UPLOAD_DIR", str(tmp_path / "uploads"))
    return catalog


def _stage(client, name="lband.png", data=None):
    return client.post(
        "/graphics/uploads",
        files={"file": (name, data if data is not None else _png(64, 36), "image/png")},
    )


def test_stage_returns_dimensions_and_preview(client, graphics_env):
    response = _stage(client)
    assert response.status_code == 201
    body = response.json()
    # The popup needs the artwork's pixel size to lay out its drawing surface and to
    # convert the rectangle the operator drags into frame fractions.
    assert body["width"] == 64
    assert body["height"] == 36
    assert body["mediaType"] == "image"
    assert body["previewUrl"] == f"/graphics/uploads/{body['uploadId']}/preview"

    preview = client.get(body["previewUrl"])
    assert preview.status_code == 200
    assert preview.content[:8] == b"\x89PNG\r\n\x1a\n"


def test_stage_rejects_an_unsupported_extension(client, graphics_env):
    response = _stage(client, name="sponsor.pdf")
    assert response.status_code == 422
    assert "unsupported graphics file extension" in response.json()["message"]


def test_commit_lband_derives_everything_but_the_squeeze_window(client, graphics_env):
    upload_id = _stage(client).json()["uploadId"]

    response = client.post(
        "/graphics/assets",
        json={
            "uploadId": upload_id,
            "id": "lband-acme",
            "kind": "lband",
            "squeezeWindow": {"x": 0, "y": 0, "width": 0.78125, "height": 0.763889},
        },
    )
    assert response.status_code == 201
    entry = response.json()
    assert entry["geometry"] == {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0}
    assert entry["squeezeWindow"] == {"x": 0, "y": 0, "width": 0.78125, "height": 0.763889}
    assert entry["supportedModes"] == ["snap", "shrink_overlay", "reveal"]
    assert entry["defaultMode"] == "reveal"
    assert entry["defaultDurationMs"] == 15000
    assert entry["mediaType"] == "image"

    catalog = client.get("/graphics/catalog").json()
    assert [asset["id"] for asset in catalog["assets"]] == ["lband-acme"]
    assert catalog["assets"][0]["enabled"] is True
    # The computed flag is derived on read and must not be persisted into the file.
    assert "enabled" not in json.loads(graphics_env.read_text())["assets"][0]

    # The staged upload is consumed by the commit.
    assert client.get(f"/graphics/uploads/{upload_id}/preview").status_code == 404


def test_commit_logo_keeps_its_placement_box(client, graphics_env):
    upload_id = _stage(client, name="logo.png").json()["uploadId"]

    entry = client.post(
        "/graphics/assets",
        json={
            "uploadId": upload_id,
            "id": "logo-acme",
            "kind": "logo",
            "geometry": {"x": 0.03125, "y": 0.055556, "width": 0.171875, "height": 0.125},
        },
    ).json()
    assert entry["geometry"] == {
        "x": 0.03125, "y": 0.055556, "width": 0.171875, "height": 0.125,
    }
    assert "squeezeWindow" not in entry
    assert entry["supportedModes"] == ["snap", "fade"]
    assert entry["defaultMode"] == "fade"


def test_commit_enforces_the_rectangle_that_matches_the_kind(client, graphics_env):
    lband = _stage(client).json()["uploadId"]
    response = client.post(
        "/graphics/assets",
        json={"uploadId": lband, "id": "lband-nogeo", "kind": "lband"},
    )
    assert response.status_code == 422
    assert "squeezeWindow" in response.json()["message"]

    logo = _stage(client).json()["uploadId"]
    response = client.post(
        "/graphics/assets",
        json={
            "uploadId": logo,
            "id": "logo-squeezes",
            "kind": "logo",
            "squeezeWindow": {"x": 0, "y": 0, "width": 0.5, "height": 0.5},
        },
    )
    assert response.status_code == 422
    assert "do not squeeze" in response.json()["message"]


def test_commit_rejects_a_box_past_the_frame_edge(client, graphics_env):
    upload_id = _stage(client).json()["uploadId"]
    response = client.post(
        "/graphics/assets",
        json={
            "uploadId": upload_id,
            "id": "lband-overflow",
            "kind": "lband",
            "squeezeWindow": {"x": 0.5, "y": 0, "width": 0.75, "height": 0.5},
        },
    )
    assert response.status_code == 422
    assert "past the edge" in response.json()["message"]


def test_commit_rejects_a_duplicate_id(client, graphics_env):
    first = _stage(client).json()["uploadId"]
    window = {"x": 0, "y": 0, "width": 0.5, "height": 0.5}
    assert client.post(
        "/graphics/assets",
        json={"uploadId": first, "id": "lband-dup", "kind": "lband", "squeezeWindow": window},
    ).status_code == 201

    second = _stage(client).json()["uploadId"]
    response = client.post(
        "/graphics/assets",
        json={"uploadId": second, "id": "lband-dup", "kind": "lband", "squeezeWindow": window},
    )
    assert response.status_code == 409


def test_discarding_an_upload_removes_it(client, graphics_env):
    upload_id = _stage(client).json()["uploadId"]
    assert client.delete(f"/graphics/uploads/{upload_id}").status_code == 204
    assert client.get(f"/graphics/uploads/{upload_id}/preview").status_code == 404


def test_unknown_upload_is_not_found(client, graphics_env):
    response = client.post(
        "/graphics/assets",
        json={
            "uploadId": "00000000-0000-0000-0000-000000000000",
            "id": "lband-ghost",
            "kind": "lband",
            "squeezeWindow": {"x": 0, "y": 0, "width": 0.5, "height": 0.5},
        },
    )
    assert response.status_code == 404


def test_uploaded_asset_is_cueable_end_to_end(client, graphics_env):
    upload_id = _stage(client).json()["uploadId"]
    client.post(
        "/graphics/assets",
        json={
            "uploadId": upload_id,
            "id": "lband-live",
            "kind": "lband",
            "squeezeWindow": {"x": 0, "y": 0, "width": 0.78125, "height": 0.763889},
        },
    )

    cue = client.post(
        "/graphics/cues",
        json={"assetId": "lband-live", "mode": "reveal", "durationMs": 15000, "fadeMs": 600},
    )
    assert cue.status_code == 201

    by_type = {e["type"]: e["details"] for e in client.get("/scheduled_events").json()}
    assert by_type["squeeze"]["squeeze"]["output_window_norm"] == {
        "x": 0, "y": 0, "width": 0.78125, "height": 0.763889,
    }
    assert client.get("/graphics/assets/lband-live/thumbnail").status_code == 200


def _ffmpeg(*args) -> bytes:
    """Renders a fixture with ffmpeg, or skips when it is unavailable."""
    import pathlib, shutil, subprocess, tempfile

    if not shutil.which("ffmpeg"):
        pytest.skip("ffmpeg is required for the format fixtures")
    with tempfile.TemporaryDirectory() as directory:
        out = pathlib.Path(directory) / args[-1]
        result = subprocess.run(
            ["ffmpeg", "-v", "error", "-y", *args[:-1], str(out)],
            capture_output=True, timeout=60,
        )
        if result.returncode != 0 or not out.exists():
            pytest.skip(f"this ffmpeg cannot write {args[-1]}")
        return out.read_bytes()


@pytest.mark.parametrize("name, args", [
    ("still.webp", ("-f", "lavfi", "-i", "color=c=blue:s=64x36:d=1", "-frames:v", "1", "still.webp")),
    ("still.tga",  ("-f", "lavfi", "-i", "color=c=blue:s=64x36:d=1", "-frames:v", "1", "still.tga")),
    ("still.tiff", ("-f", "lavfi", "-i", "color=c=blue:s=64x36:d=1", "-frames:v", "1", "still.tiff")),
])
def test_other_still_formats_are_images(client, graphics_env, name, args):
    response = client.post("/graphics/uploads", files={"file": (name, _ffmpeg(*args), "image/*")})
    assert response.status_code == 201, response.text
    assert response.json()["mediaType"] == "image"
    assert response.json()["width"] == 64


@pytest.mark.parametrize("name, args", [
    ("clip.mov",  ("-f", "lavfi", "-i", "testsrc=s=64x36:d=1:r=10", "-c:v", "qtrle", "clip.mov")),
    ("clip.webm", ("-f", "lavfi", "-i", "testsrc=s=64x36:d=1:r=10", "-c:v", "libvpx-vp9", "clip.webm")),
])
def test_video_formats_are_typed_as_video(client, graphics_env, name, args):
    response = client.post("/graphics/uploads", files={"file": (name, _ffmpeg(*args), "video/*")})
    assert response.status_code == 201, response.text
    assert response.json()["mediaType"] == "video"


def test_an_animated_gif_is_video_despite_its_image_extension(client, graphics_env):
    """The case an extension allowlist gets wrong: .gif is a still container name but
    a multi-frame file, and it must be gated by ENABLE_VIDEO_GRAPHICS like any other
    moving graphic."""
    data = _ffmpeg("-f", "lavfi", "-i", "testsrc=s=64x36:d=1:r=10", "anim.gif")
    upload = client.post("/graphics/uploads", files={"file": ("anim.gif", data, "image/gif")})
    assert upload.status_code == 201
    assert upload.json()["mediaType"] == "video"

    entry = client.post("/graphics/assets", json={
        "uploadId": upload.json()["uploadId"], "id": "lband-anim", "kind": "lband",
        "squeezeWindow": {"x": 0, "y": 0, "width": 0.5, "height": 0.5},
    }).json()
    assert entry["mediaType"] == "video"
    assert entry["clipDurationMs"] > 0


def test_a_single_frame_gif_is_an_image(client, graphics_env):
    data = _ffmpeg("-f", "lavfi", "-i", "color=c=red:s=64x36:d=1", "-frames:v", "1", "one.gif")
    response = client.post("/graphics/uploads", files={"file": ("one.gif", data, "image/gif")})
    assert response.status_code == 201
    assert response.json()["mediaType"] == "image"


def test_a_non_media_file_is_rejected_even_with_a_good_extension(client, graphics_env):
    response = client.post(
        "/graphics/uploads", files={"file": ("evil.png", b"not an image at all", "image/png")}
    )
    assert response.status_code == 422


def test_unwritable_storage_reports_which_setting_to_change(client, monkeypatch, tmp_path):
    """The shipped compose file mounts the graphics media read-only, so this is the most
    likely first-run failure. It must not surface as an opaque 500."""
    import os

    if os.geteuid() == 0:
        pytest.skip("root ignores the directory mode, so the path stays writable")
    catalog = tmp_path / "graphics.json"
    catalog.write_text(json.dumps({"assets": []}))
    readonly = tmp_path / "readonly"
    readonly.mkdir()
    readonly.chmod(0o555)
    monkeypatch.setenv("GRAPHICS_CATALOG_PATH", str(catalog))
    monkeypatch.setenv("GRAPHICS_MEDIA_DIR", str(readonly))
    monkeypatch.setenv("GRAPHICS_UPLOAD_DIR", str(readonly / "uploads"))

    response = _stage(client)
    assert response.status_code == 503
    assert response.headers["content-type"].startswith("application/json")
    assert "not writable" in response.json()["message"]
    assert "GRAPHICS_UPLOAD_DIR" in response.json()["message"]


def _lband_png(w=320, h=180, hole_w=250, hole_h=138) -> bytes:
    """Full-frame artwork with a transparent hole, like a real L-band."""
    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            row += bytes((0, 0, 0, 0)) if (x < hole_w and y < hole_h) else bytes((9, 60, 40, 255))
        rows.append(bytes(row))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"".join(rows)))
        + chunk(b"IEND", b"")
    )


def test_stage_derives_the_programme_window_from_the_alpha_hole(client, graphics_env):
    """Hand-dragging cannot land on the hole exactly, so the server derives it."""
    response = _stage(client, data=_lband_png())
    assert response.status_code == 201
    box = response.json()["contentBox"]
    assert box is not None, "no transparent region detected in artwork that has one"
    assert box["x"] == 0 and box["y"] == 0
    assert abs(box["width"] - 250 / 320) < 1e-4, box
    assert abs(box["height"] - 138 / 180) < 1e-4, box


def test_opaque_artwork_has_no_content_box(client, graphics_env):
    opaque = _lband_png(hole_w=0, hole_h=0)
    assert _stage(client, data=opaque).json()["contentBox"] is None


def test_stored_media_is_readable_outside_the_container(client, graphics_env, tmp_path):
    """The API runs as root in the container and NamedTemporaryFile creates 0600, which
    would leave uploaded artwork unreadable to anything on the host."""
    import stat

    upload_id = _stage(client).json()["uploadId"]
    client.post("/graphics/assets", json={
        "uploadId": upload_id, "id": "lband-perms", "kind": "lband",
        "squeezeWindow": {"x": 0, "y": 0, "width": 0.5, "height": 0.5},
    })
    stored = tmp_path / "media" / "lband-perms.png"
    assert stored.exists()
    assert stat.S_IMODE(stored.stat().st_mode) == 0o644
