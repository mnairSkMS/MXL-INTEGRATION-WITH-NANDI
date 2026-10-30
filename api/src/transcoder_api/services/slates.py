"""The curated slate list the operator picks from.

Shaped like services/graphics.py, with one deliberate difference: the engine fetches slate
media over **HTTP**, not from disk. OverlayInput hands the URI to cpr/libcurl and downloads it
to a temp file (transcoder/lib/utpl/src/utpl/transcoder/input/overlay.cpp), so a
`file:///media/...` URI of the kind graphics_catalog.json uses is not safe to assume here -
whether this libcurl build enables the FILE protocol is a build detail, not a contract. The
engine's own integration test serves slate media over http, which is the path this follows: we
serve the bytes and hand the engine an absolute URL back to ourselves.
"""

from __future__ import annotations

import json
import os
import pathlib

from ..exceptions import NotFoundException, UnprocessableException


DEFAULT_CATALOG = (
    pathlib.Path(__file__).resolve().parent.parent / "assets" / "slates_catalog.json"
)


def catalog_path() -> pathlib.Path:
    return pathlib.Path(os.environ.get("SLATES_CATALOG_PATH", str(DEFAULT_CATALOG)))


def media_dir() -> pathlib.Path:
    return pathlib.Path(os.environ.get("SLATES_DIR", "/media/slates")).resolve()


def _self_base_url() -> str:
    """Where the engine should call back to reach us.

    The transcoder and this API share a container, so loopback is both correct and the only
    address guaranteed to work regardless of how the container is networked.
    """
    return os.environ.get(
        "SLATES_BASE_URL", f"http://127.0.0.1:{os.environ.get('API_PORT', '8080')}"
    )


def catalog() -> dict:
    try:
        data = json.loads(catalog_path().read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise UnprocessableException("slate catalog is unavailable or invalid") from exc

    if not isinstance(data.get("assets"), list):
        raise UnprocessableException("slate catalog must contain an assets array")

    return data


def assets() -> list[dict]:
    """The catalog as the UI sees it, each entry carrying the URL to fire.

    Entries whose file is missing from the media directory are dropped rather than listed:
    offering a slate that would fail to fetch mid-programme is worse than not offering it.
    """
    directory = media_dir()
    listed = []

    for asset in catalog()["assets"]:
        name = asset.get("file")
        if not name:
            continue

        if not (directory / name).is_file():
            continue

        listed.append(
            {
                "id": asset["id"],
                "label": asset.get("label", asset["id"]),
                "mediaType": asset.get("mediaType", "video"),
                # Whether firing this slate will mute the programme audio. The UI surfaces
                # this so the operator knows before they commit, not after.
                "hasAudio": bool(asset.get("hasAudio", True)),
                "mediaUri": f"{_self_base_url()}/slates/media/{asset['id']}",
            }
        )

    return listed


def resolve(asset_id: str) -> pathlib.Path:
    """The file for an id, or 404.

    The id is looked up in the catalog and only the catalog's own file name is ever joined to
    the media directory, so nothing from the request reaches the filesystem. The containment
    check below is belt and braces against a catalog entry that itself names a traversal.
    """
    for asset in catalog()["assets"]:
        if asset.get("id") != asset_id:
            continue

        directory = media_dir()
        path = (directory / asset.get("file", "")).resolve()

        if not path.is_relative_to(directory) or not path.is_file():
            raise NotFoundException(f"slate media for '{asset_id}' is missing")

        return path

    raise NotFoundException(f"no slate with id '{asset_id}'")
