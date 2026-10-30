"""Two-phase graphics upload: stage the media, then commit a catalog entry.

The operator draws an asset's geometry on top of the artwork, so the geometry is not
known when the file arrives. Staging returns the media's pixel dimensions and a preview
URL for the drawing surface; the commit carries the rectangle the operator drew.
"""

from __future__ import annotations

import fcntl
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import threading
import time
import uuid
from dataclasses import dataclass
from typing import Any, Iterable

from ..exceptions import (
    ConflictException,
    NotFoundException,
    TranscoderApiException,
    UnprocessableException,
)
from ..model.rest_api import ErrorCode
from . import graphics, media


# The engine decodes overlay media with libav and converts whatever it finds to BGRA
# (stream/input/image.cpp), so the real limit is what ffmpeg can decode, not a codec
# this service picks. Video containers are shared with managed media, which asks the
# same question of the same decoder.
IMAGE_EXTENSIONS = {
    ".apng", ".avif", ".bmp", ".gif", ".jpeg", ".jpg",
    ".png", ".tga", ".tif", ".tiff", ".webp",
}
GRAPHICS_EXTENSIONS = IMAGE_EXTENSIONS | media.VIDEO_EXTENSIONS

ASSET_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*$")
UPLOAD_ID_PATTERN = re.compile(r"^[0-9a-f-]{36}$")

MAX_UPLOAD_BYTES = int(os.environ.get("GRAPHICS_MAX_UPLOAD_BYTES", str(256 * 1024 * 1024)))
UPLOAD_TTL_SECONDS = int(os.environ.get("GRAPHICS_UPLOAD_TTL_SECONDS", str(3600)))

FULL_FRAME = {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0}
DEFAULT_ANIMATION_MS = 600
DEFAULT_SQUEEZE_DURATION_MS = 15000

# Every shipped asset follows these, so the operator is never asked for them.
KIND_DEFAULTS: dict[str, dict[str, Any]] = {
    "logo": {"supportedModes": ["snap", "fade"], "defaultMode": "fade"},
    "bug": {"supportedModes": ["snap", "fade"], "defaultMode": "fade"},
    "lband": {
        "supportedModes": ["snap", "shrink_overlay", "reveal"],
        "defaultMode": "reveal",
        "defaultDurationMs": DEFAULT_SQUEEZE_DURATION_MS,
    },
    "aston": {
        "supportedModes": ["snap", "shrink_overlay", "reveal"],
        "defaultMode": "reveal",
        "defaultDurationMs": DEFAULT_SQUEEZE_DURATION_MS,
    },
}
SQUEEZE_KINDS = {"lband", "aston"}

_catalog_lock = threading.Lock()


@dataclass(frozen=True)
class StagedUpload:
    upload_id: str
    media_type: str
    width: int
    height: int
    expires_at: float
    content_box: dict | None


def media_root() -> pathlib.Path:
    return pathlib.Path(os.environ.get("GRAPHICS_MEDIA_DIR", "/media/graphics")).resolve()


def upload_root() -> pathlib.Path:
    return pathlib.Path(
        os.environ.get("GRAPHICS_UPLOAD_DIR", str(media_root() / ".uploads"))
    ).resolve()


def _probe(path: pathlib.Path) -> tuple[int, int, str, float | None]:
    executable = shutil.which("ffprobe")
    if not executable:
        raise UnprocessableException("ffprobe is required to validate graphics uploads")
    result = subprocess.run(
        [
            executable, "-v", "error", "-select_streams", "v:0", "-count_packets",
            "-show_entries", "stream=width,height,nb_frames,nb_read_packets:format=duration",
            "-of", "json", str(path),
        ],
        capture_output=True,
        text=True,
        timeout=30,
    )
    if result.returncode != 0:
        raise UnprocessableException("uploaded file is not decodable media")
    try:
        probed = json.loads(result.stdout)
        stream = probed["streams"][0]
        width, height = int(stream["width"]), int(stream["height"])
    except (json.JSONDecodeError, KeyError, IndexError, ValueError) as exc:
        raise UnprocessableException("uploaded file has no readable video stream") from exc
    if width <= 0 or height <= 0:
        raise UnprocessableException("uploaded file has no readable video stream")
    try:
        duration = float(probed["format"]["duration"])
    except (KeyError, TypeError, ValueError):
        duration = None
    # Count frames, not extensions: .gif and .webp are still-image container names that
    # may hold an animation, and .mov may hold one frame. Duration is no help either --
    # a lone still reports a nominal one-frame duration. Stills report exactly one
    # packet, and whichever counter the demuxer fills in is the one that answers it.
    frames = 1
    for field in ("nb_frames", "nb_read_packets"):
        try:
            frames = max(frames, int(stream[field]))
        except (KeyError, TypeError, ValueError):
            continue
    media_type = "video" if frames > 1 else "image"
    return width, height, media_type, duration


def _storage_error(path: pathlib.Path, setting: str, exc: OSError) -> TranscoderApiException:
    """The shipped compose file mounts the graphics media read-only, so an unwritable
    path is a likely first-run misconfiguration rather than a bug. Name the path and the
    setting that moves it, instead of surfacing an opaque 500."""
    return TranscoderApiException(
        f"graphics storage at {path} is not writable ({exc.strerror}); "
        f"mount it read-write or point {setting} at a writable directory",
        ErrorCode.INVALID_REQUEST,
        http_code=503,
    )


def _alpha_box(path: pathlib.Path, width: int, height: int) -> dict | None:
    """The bounding box of the artwork's transparent region, as frame fractions.

    A squeeze window has to match the hole in the PNG exactly, and dragging one by hand
    cannot hit it -- one pixel of mouse travel is several pixels of a 1920 frame. Deriving
    it from the alpha channel removes the guesswork; the operator can still adjust it.

    Returns None when the media has no transparency (an opaque logo, most video), in
    which case cropdetect reports nothing and there is no hole to snap to.
    """
    executable = shutil.which("ffmpeg")
    if not executable:
        return None
    try:
        result = subprocess.run(
            [
                executable, "-v", "info", "-loop", "1", "-i", str(path),
                "-vf", "alphaextract,negate,cropdetect=limit=16:round=1:skip=0",
                "-frames:v", "2", "-f", "null", "-",
            ],
            capture_output=True, text=True, timeout=30,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    edges = re.findall(r"x1:(\d+) x2:(\d+) y1:(\d+) y2:(\d+)", result.stderr)
    if not edges:
        return None
    x1, x2, y1, y2 = (int(v) for v in edges[-1])
    box_w, box_h = x2 - x1 + 1, y2 - y1 + 1
    if box_w <= 0 or box_h <= 0 or (box_w >= width and box_h >= height):
        return None
    return {
        "x": round(x1 / width, 6),
        "y": round(y1 / height, 6),
        "width": round(box_w / width, 6),
        "height": round(box_h / height, 6),
    }


def _prune_expired() -> None:
    root = upload_root()
    if not root.is_dir():
        return
    deadline = time.time() - UPLOAD_TTL_SECONDS
    for staged in root.iterdir():
        try:
            if staged.is_file() and staged.stat().st_mtime < deadline:
                staged.unlink(missing_ok=True)
        except OSError:
            continue


def staged_file(upload_id: str) -> pathlib.Path:
    if not UPLOAD_ID_PATTERN.match(upload_id):
        raise NotFoundException("graphics upload does not exist")
    root = upload_root()
    match = next((path for path in root.glob(f"{upload_id}.*") if path.is_file()), None)
    if match is None:
        raise NotFoundException("graphics upload does not exist")
    if match.stat().st_mtime < time.time() - UPLOAD_TTL_SECONDS:
        match.unlink(missing_ok=True)
        raise NotFoundException("graphics upload does not exist")
    return match


def stage_upload(filename: str, chunks: Iterable[bytes]) -> StagedUpload:
    suffix = pathlib.Path(filename).suffix.lower()
    if suffix not in GRAPHICS_EXTENSIONS:
        raise UnprocessableException(
            "unsupported graphics file extension; expected one of "
            + ", ".join(sorted(GRAPHICS_EXTENSIONS))
        )

    _prune_expired()
    root = upload_root()
    try:
        root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise _storage_error(root, "GRAPHICS_UPLOAD_DIR", exc) from exc

    upload_id = str(uuid.uuid4())
    final_path = root / f"{upload_id}{suffix}"
    written = 0
    # The suffix has to be on the temp file too: formats without magic bytes (TGA among
    # them) are identified by extension, and ffprobe cannot read an extensionless copy.
    try:
        staging = tempfile.NamedTemporaryFile(
            dir=root, prefix=".staging-", suffix=suffix, delete=False
        )
    except OSError as exc:
        raise _storage_error(root, "GRAPHICS_UPLOAD_DIR", exc) from exc
    with staging as temporary:
        temporary_path = pathlib.Path(temporary.name)
        try:
            for chunk in chunks:
                written += len(chunk)
                if written > MAX_UPLOAD_BYTES:
                    raise UnprocessableException("upload exceeds GRAPHICS_MAX_UPLOAD_BYTES")
                temporary.write(chunk)
            temporary.flush()
            os.fsync(temporary.fileno())
        except Exception:
            temporary_path.unlink(missing_ok=True)
            raise
    try:
        width, height, media_type, _ = _probe(temporary_path)
        os.replace(temporary_path, final_path)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise
    return StagedUpload(
        upload_id=upload_id,
        media_type=media_type,
        width=width,
        height=height,
        expires_at=final_path.stat().st_mtime + UPLOAD_TTL_SECONDS,
        content_box=_alpha_box(final_path, width, height),
    )


def discard_upload(upload_id: str) -> None:
    staged_file(upload_id).unlink(missing_ok=True)


def _write_catalog(data: dict) -> None:
    path = graphics.catalog_path()
    lock_path = path.with_suffix(path.suffix + ".lock")
    try:
        handle = open(lock_path, "w")
    except OSError as exc:
        raise _storage_error(path.parent, "GRAPHICS_CATALOG_PATH", exc) from exc
    with handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        try:
            with tempfile.NamedTemporaryFile(
                dir=path.parent, prefix=".catalog-", mode="w", delete=False
            ) as temporary:
                temporary_path = pathlib.Path(temporary.name)
                json.dump(data, temporary, indent=2)
                temporary.write("\n")
                temporary.flush()
                os.fsync(temporary.fileno())
            os.replace(temporary_path, path)
            # Same reason as the stored media: written as root via a 0600 temp file.
            os.chmod(path, 0o644)
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def commit_asset(
    upload_id: str,
    asset_id: str,
    kind: str,
    geometry: dict | None = None,
    squeeze_window: dict | None = None,
    label: str | None = None,
) -> dict:
    if kind not in KIND_DEFAULTS:
        raise UnprocessableException("kind must be one of logo, bug, lband, aston")
    if not ASSET_ID_PATTERN.match(asset_id):
        raise UnprocessableException(
            "id must be lowercase alphanumeric with hyphens, starting with a letter or digit"
        )

    # Report a rectangle that does not belong to this kind before one that is merely
    # missing: sending it is the actual mistake, and it says what the caller got wrong.
    if kind in SQUEEZE_KINDS:
        if geometry is not None:
            raise UnprocessableException(
                f"{kind} geometry is always the full frame and cannot be set"
            )
        if squeeze_window is None:
            raise UnprocessableException(f"{kind} graphics need a squeezeWindow")
        _validate_rect_payload(squeeze_window, "squeezeWindow", asset_id)
    else:
        if squeeze_window is not None:
            raise UnprocessableException(f"{kind} graphics do not squeeze the programme")
        if geometry is None:
            raise UnprocessableException(f"{kind} graphics need a geometry")
        _validate_rect_payload(geometry, "geometry", asset_id)

    staged = staged_file(upload_id)
    suffix = staged.suffix.lower()
    _, _, media_type, duration = _probe(staged)

    entry: dict[str, Any] = {
        "id": asset_id,
        "label": label or asset_id,
        "kind": kind,
        "mediaType": media_type,
        "mediaUri": f"file://{media_root() / f'{asset_id}{suffix}'}",
        "thumbnail": f"/graphics/assets/{asset_id}/thumbnail",
        "geometry": dict(FULL_FRAME) if kind in SQUEEZE_KINDS else geometry,
        "defaultAnimationMs": DEFAULT_ANIMATION_MS,
        **KIND_DEFAULTS[kind],
    }
    if kind in SQUEEZE_KINDS:
        entry["squeezeWindow"] = squeeze_window
    if media_type == "video" and duration is not None:
        entry["clipDurationMs"] = int(duration * 1000)

    root = media_root()
    try:
        root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise _storage_error(root, "GRAPHICS_MEDIA_DIR", exc) from exc
    final_media = root / f"{asset_id}{suffix}"

    with _catalog_lock:
        data = graphics.catalog()
        if any(asset.get("id") == asset_id for asset in data["assets"]):
            raise ConflictException("a graphics asset with that id already exists")
        try:
            os.replace(staged, final_media)
            # NamedTemporaryFile creates 0600, and the API runs as root in the container,
            # which would leave the artwork unreadable to anything on the host.
            os.chmod(final_media, 0o644)
        except OSError as exc:
            raise _storage_error(root, "GRAPHICS_MEDIA_DIR", exc) from exc
        try:
            # catalog() annotates each asset with a computed `enabled`; it is derived on
            # read, so it must not be persisted back into the file.
            for asset in data["assets"]:
                asset.pop("enabled", None)
            data["assets"].append(entry)
            _write_catalog(data)
        except Exception:
            final_media.unlink(missing_ok=True)
            raise
    return entry


def asset_media(asset_id: str) -> pathlib.Path:
    _, asset = graphics._asset(asset_id)
    uri = asset.get("mediaUri", "")
    if not uri.startswith("file://"):
        raise NotFoundException("graphics asset has no local media")
    path = pathlib.Path(uri[len("file://"):])
    if not path.is_file():
        raise NotFoundException("graphics asset media is missing")
    return path


def _validate_rect_payload(rect: dict, field: str, asset_id: str) -> None:
    missing = {"x", "y", "width", "height"} - set(rect)
    if missing:
        raise UnprocessableException(f"{field} needs x, y, width and height")
    graphics._validate_rect(rect, field, asset_id)
    if rect["width"] <= 0 or rect["height"] <= 0:
        raise UnprocessableException(f"{field} needs a positive width and height")
    if rect["x"] + rect["width"] > 1.0 or rect["y"] + rect["height"] > 1.0:
        raise UnprocessableException(f"{field} extends past the edge of the frame")
