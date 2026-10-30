"""The slate catalog and the media it serves.

The engine downloads slate media over HTTP with libcurl rather than reading it off disk
(transcoder/.../input/overlay.cpp), so the catalog hands out absolute URLs pointing back at
this API instead of the file:// URIs the graphics catalog uses. These tests pin that, and pin
that a request can never name a path.
"""

from __future__ import annotations

import json

import pytest

from transcoder_api.services import slates


@pytest.fixture()
def catalog(tmp_path, monkeypatch):
    media = tmp_path / "slates"
    media.mkdir()
    (media / "with-audio.mp4").write_bytes(b"video bytes")
    (media / "card.jpg").write_bytes(b"image bytes")

    path = tmp_path / "slates_catalog.json"
    path.write_text(json.dumps({"assets": [
        {"id": "with-audio", "label": "Has audio", "file": "with-audio.mp4",
         "mediaType": "video", "hasAudio": True},
        {"id": "card", "label": "Card", "file": "card.jpg",
         "mediaType": "image", "hasAudio": False},
        # Listed but not on disk: must be dropped rather than offered.
        {"id": "ghost", "label": "Missing", "file": "not-there.mp4",
         "mediaType": "video", "hasAudio": False},
    ]}))

    monkeypatch.setenv("SLATES_CATALOG_PATH", str(path))
    monkeypatch.setenv("SLATES_DIR", str(media))
    monkeypatch.setenv("SLATES_BASE_URL", "http://127.0.0.1:8080")
    return media


def test_catalog_lists_the_media_that_exists(client, catalog):
    body = client.get("/slates/catalog").json()

    assert [entry["id"] for entry in body] == ["with-audio", "card"]


def test_a_catalogued_slate_whose_file_is_missing_is_not_offered(client, catalog):
    body = client.get("/slates/catalog").json()

    # Offering it would mean a slate that fails to fetch part way through a programme.
    assert "ghost" not in [entry["id"] for entry in body]


def test_each_entry_carries_an_absolute_url_for_the_engine(client, catalog):
    body = client.get("/slates/catalog").json()

    for entry in body:
        assert entry["mediaUri"] == f"http://127.0.0.1:8080/slates/media/{entry['id']}"


def test_has_audio_is_reported_so_the_ui_can_warn(client, catalog):
    body = {entry["id"]: entry for entry in client.get("/slates/catalog").json()}

    assert body["with-audio"]["hasAudio"] is True
    assert body["card"]["hasAudio"] is False


def test_media_serves_the_bytes_for_a_known_id(client, catalog):
    response = client.get("/slates/media/card")

    assert response.status_code == 200
    assert response.content == b"image bytes"


def test_media_404s_for_an_unknown_id(client, catalog):
    assert client.get("/slates/media/nope").status_code == 404


def test_media_404s_when_the_file_is_gone(client, catalog):
    assert client.get("/slates/media/ghost").status_code == 404


@pytest.mark.parametrize("attempt", [
    "../../../../etc/passwd",
    "..%2f..%2fetc%2fpasswd",
    "card/../../../etc/passwd",
])
def test_a_path_cannot_be_smuggled_through_the_id(client, catalog, attempt):
    """The id is looked up in the catalog; only the catalog's own file name is ever joined."""
    response = client.get(f"/slates/media/{attempt}")

    assert response.status_code in (404, 400), response.text
    assert b"root:" not in response.content


def test_a_catalog_entry_naming_a_traversal_is_refused(tmp_path, monkeypatch):
    """Belt and braces: even a bad catalog cannot reach outside the media directory."""
    media = tmp_path / "slates"
    media.mkdir()
    outside = tmp_path / "secret.txt"
    outside.write_text("secret")

    path = tmp_path / "slates_catalog.json"
    path.write_text(json.dumps({"assets": [
        {"id": "escape", "label": "Escape", "file": "../secret.txt",
         "mediaType": "image", "hasAudio": False},
    ]}))

    monkeypatch.setenv("SLATES_CATALOG_PATH", str(path))
    monkeypatch.setenv("SLATES_DIR", str(media))

    from transcoder_api.exceptions import NotFoundException

    with pytest.raises(NotFoundException):
        slates.resolve("escape")
