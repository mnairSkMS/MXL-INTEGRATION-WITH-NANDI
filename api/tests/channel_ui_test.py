"""The /channel route and its asset mount.

The SPA in frontend/ is built into transcoder_api/static/channel. These tests pin the two
things that silently break that arrangement: the index must not be cached (it names
content-hashed assets, so a cached copy keeps pointing at the previous build after an
upgrade), and the assets must be, or every load re-fetches the whole bundle.
"""

import pathlib

import pytest

from transcoder_api import main

CHANNEL_UI = pathlib.Path(main.__file__).resolve().parent / "static" / "channel"
BUILT = (CHANNEL_UI / "index.html").is_file()
needs_build = pytest.mark.skipif(not BUILT, reason="frontend/ has not been built")


@needs_build
def test_channel_index_is_served_uncached(client):
    response = client.get("/channel")

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("text/html")
    assert response.headers["cache-control"] == "no-store"
    assert '<div id="root">' in response.text


@needs_build
def test_channel_index_references_prefixed_assets(client):
    """A base path of "/" would 404 every asset once the page is served from /channel."""
    body = client.get("/channel").text

    assert "/channel/assets/" in body
    assert 'src="/assets/' not in body


@needs_build
def test_channel_assets_are_cached_immutably(client):
    asset = next((CHANNEL_UI / "assets").glob("*.js"))

    response = client.get(f"/channel/assets/{asset.name}")

    assert response.status_code == 200
    assert "immutable" in response.headers["cache-control"]


@needs_build
def test_unknown_channel_asset_is_not_the_index(client):
    """No SPA fallback on the asset mount: a stale asset URL must fail, not return HTML."""
    response = client.get("/channel/assets/index-doesnotexist.js")

    assert response.status_code == 404


def test_operator_ui_is_unaffected(client):
    """The channel UI is an addition. The operator UI keeps its own route and headers."""
    response = client.get("/operator")

    assert response.status_code == 200
    assert response.headers["cache-control"] == "no-store"
