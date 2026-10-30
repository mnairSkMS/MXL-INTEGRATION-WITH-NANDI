"""Managed media and playout queue persistence.

The API owns desired queue state. The transcoder applies revisions asynchronously,
so this module deliberately does not manipulate GStreamer or gRPC directly.
"""

from __future__ import annotations

import hashlib
import json
import logging
import os
import pathlib
import shutil
import subprocess
import tempfile
import threading
import uuid
from urllib.parse import quote
from typing import Iterable

from sqlalchemy import func
from sqlalchemy.orm import Session

from ..exceptions import ConflictException, NotFoundException, UnprocessableException
from ..model.db import MediaAsset, PlaylistItem, PlaylistState
from . import transcoder

log = logging.getLogger(__name__)


VIDEO_EXTENSIONS = {
    ".avi",
    ".m2ts",
    ".m4v",
    ".mkv",
    ".mov",
    ".mp4",
    ".mpeg",
    ".mpg",
    ".mxf",
    ".ts",
    ".webm",
}
MAX_UPLOAD_BYTES = int(os.environ.get("MEDIA_MAX_UPLOAD_BYTES", str(10 * 1024 * 1024 * 1024)))
# Files accepted by one POST /media call. Each file is still capped by MAX_UPLOAD_BYTES.
MAX_UPLOAD_FILES = int(os.environ.get("MEDIA_MAX_UPLOAD_FILES", "20"))

# Thumbnails are cut once per (asset, position, width) and kept, so a library grid or a
# scrub bar costs one ffmpeg run per distinct frame. At most two run at a time, so a
# grid of fifty tiles cannot take every core away from the engine.
THUMBNAIL_WIDTHS = (160, 320, 480, 640, 960, 1280)
THUMBNAIL_DEFAULT_WIDTH = 480
_THUMBNAIL_SLOTS = threading.BoundedSemaphore(int(os.environ.get("MEDIA_THUMBNAIL_CONCURRENCY", "2")))


def media_root() -> pathlib.Path:
    return pathlib.Path(os.environ.get("MEDIA_DIR", "/media/queue")).resolve()


def _safe_relative_path(value: str) -> pathlib.Path:
    candidate = pathlib.PurePosixPath(value)
    if not value or candidate.is_absolute() or ".." in candidate.parts:
        raise UnprocessableException("relativePath must name a file below MEDIA_DIR")
    return pathlib.Path(*candidate.parts)


def _absolute_path(relative_path: str) -> pathlib.Path:
    root = media_root()
    path = (root / _safe_relative_path(relative_path)).resolve()
    if root not in path.parents:
        raise UnprocessableException("media path escapes MEDIA_DIR")
    return path


def _probe(path: pathlib.Path) -> dict:
    executable = shutil.which("ffprobe")
    if not executable:
        raise UnprocessableException("ffprobe is required for managed media validation")
    result = subprocess.run(
        [executable, "-v", "error", "-show_streams", "-show_format", "-of", "json", str(path)],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    if result.returncode != 0:
        raise UnprocessableException("uploaded media could not be probed")
    try:
        probe = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise UnprocessableException("ffprobe returned invalid media metadata") from exc
    if not any(stream.get("codec_type") == "video" for stream in probe.get("streams", [])):
        raise UnprocessableException("managed media requires at least one video stream")
    return probe


def _digest(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _asset_model(path: pathlib.Path, relative_path: str, db: Session) -> MediaAsset:
    if path.suffix.lower() not in VIDEO_EXTENSIONS:
        raise UnprocessableException("unsupported media file extension")
    if not path.is_file():
        raise NotFoundException("media file does not exist")
    existing = db.query(MediaAsset).filter(MediaAsset.relative_path == relative_path).one_or_none()
    if existing:
        return existing
    asset = MediaAsset(
        id=str(uuid.uuid4()),
        relative_path=relative_path,
        display_name=path.name,
        size_bytes=path.stat().st_size,
        sha256=_digest(path),
        probe_json=json.dumps(_probe(path), separators=(",", ":")),
    )
    db.add(asset)
    db.flush()
    return asset


def import_asset(relative_path: str, db: Session) -> MediaAsset:
    path = _absolute_path(relative_path)
    asset = _asset_model(path, _safe_relative_path(relative_path).as_posix(), db)
    db.commit()
    db.refresh(asset)
    return asset


def store_upload(filename: str, chunks: Iterable[bytes], db: Session) -> MediaAsset:
    suffix = pathlib.Path(filename).suffix.lower()
    if suffix not in VIDEO_EXTENSIONS:
        raise UnprocessableException("unsupported media file extension")
    root = media_root()
    root.mkdir(parents=True, exist_ok=True)
    stored_name = f"{uuid.uuid4()}{suffix}"
    final_path = root / stored_name
    written = 0
    with tempfile.NamedTemporaryFile(dir=root, prefix=".upload-", delete=False) as temporary:
        temporary_path = pathlib.Path(temporary.name)
        try:
            for chunk in chunks:
                written += len(chunk)
                if written > MAX_UPLOAD_BYTES:
                    raise UnprocessableException("upload exceeds MEDIA_MAX_UPLOAD_BYTES")
                temporary.write(chunk)
            temporary.flush()
            os.fsync(temporary.fileno())
        except Exception:
            temporary_path.unlink(missing_ok=True)
            raise
    try:
        _probe(temporary_path)
        os.replace(temporary_path, final_path)
        asset = _asset_model(final_path, stored_name, db)
        db.commit()
        db.refresh(asset)
        return asset
    except Exception:
        temporary_path.unlink(missing_ok=True)
        final_path.unlink(missing_ok=True)
        raise


def list_assets(db: Session) -> list[MediaAsset]:
    return db.query(MediaAsset).order_by(MediaAsset.created_at.desc()).all()


def delete_asset(media_id: str, db: Session) -> None:
    asset = db.get(MediaAsset, media_id)
    if not asset:
        raise NotFoundException("media asset does not exist")
    if (
        db.query(PlaylistItem)
        .filter(PlaylistItem.media_id == media_id, PlaylistItem.state != "played")
        .count()
    ):
        raise ConflictException("media asset is still referenced by a playlist")
    path = _absolute_path(asset.relative_path)
    db.delete(asset)
    db.commit()
    path.unlink(missing_ok=True)
    for thumbnail in _thumbnail_root().glob(f"{media_id}-*.jpg"):
        thumbnail.unlink(missing_ok=True)


def _thumbnail_root() -> pathlib.Path:
    return media_root() / ".thumbnails"


def _duration_seconds(asset: MediaAsset) -> float | None:
    try:
        return float(json.loads(asset.probe_json).get("format", {}).get("duration"))
    except (TypeError, ValueError):
        return None


def thumbnail(media_id: str, at: float | None, width: int | None, db: Session) -> pathlib.Path:
    """Return a cached JPEG of one frame of a managed media asset.

    `at` is seconds from the start; omitted, it is 10% into the clip (at most 10 s), which
    skips the black or slate frames most clips open with. `width` snaps up to the next of
    THUMBNAIL_WIDTHS so arbitrary sizes cannot fill the cache.
    """
    asset = db.get(MediaAsset, media_id)
    if not asset:
        raise NotFoundException("media asset does not exist")

    duration = _duration_seconds(asset)
    if at is None:
        at = min(duration * 0.1, 10.0) if duration else 0.0
    if at < 0 or (duration is not None and at >= duration):
        raise UnprocessableException(
            f"at must be between 0 and the clip length ({duration:.1f} s)" if duration else "at must be 0 or more"
        )
    at = round(at, 1)

    requested = width or THUMBNAIL_DEFAULT_WIDTH
    width = next((w for w in THUMBNAIL_WIDTHS if w >= requested), THUMBNAIL_WIDTHS[-1])

    root = _thumbnail_root()
    target = root / f"{media_id}-{int(at * 10)}-{width}.jpg"
    if target.is_file():
        return target

    executable = shutil.which("ffmpeg")
    if not executable:
        raise UnprocessableException("ffmpeg is required for media thumbnails")
    source = _absolute_path(asset.relative_path)
    root.mkdir(parents=True, exist_ok=True)

    with _THUMBNAIL_SLOTS:
        if target.is_file():  # another request cut it while this one waited
            return target
        partial = root / f".{target.name}.{uuid.uuid4().hex}.jpg"
        try:
            # -ss before -i seeks by index, so only the GOP around `at` is decoded.
            result = subprocess.run(
                [
                    executable, "-v", "error", "-nostdin", "-ss", f"{at}", "-i", str(source),
                    "-map", "0:v:0", "-frames:v", "1", "-vf", f"scale={width}:-2", "-q:v", "4",
                    "-y", str(partial),
                ],
                capture_output=True,
                text=True,
                timeout=20,
                check=False,
            )
            if result.returncode != 0 or not partial.is_file() or partial.stat().st_size == 0:
                raise UnprocessableException("could not extract a frame at that position")
            os.replace(partial, target)
        except subprocess.TimeoutExpired as exc:
            raise UnprocessableException("thumbnail extraction timed out") from exc
        finally:
            partial.unlink(missing_ok=True)
    return target


def state_for(playlist_id: str, db: Session) -> PlaylistState:
    state = db.get(PlaylistState, playlist_id)
    if not state:
        state = PlaylistState(
            playlist_id=playlist_id, run_state="running", revision=0, applied_revision=0, skip_seq=0
        )
        db.add(state)
        # Flush but do not commit: callers performing a queue mutation hold a
        # BEGIN IMMEDIATE transaction, and creating the state must not release
        # that write lock between revision validation and the mutation.
        db.flush()
    elif state.run_state != "running":
        # Managed playlists are continuous loops.  Older databases may still
        # contain the retired paused/pausing values from the prototype API.
        state.run_state = "running"
    return state


def _bump(state: PlaylistState) -> None:
    state.revision += 1


def _renumber(items: list[PlaylistItem], db: Session) -> None:
    """Assign contiguous positions without violating SQLite's unique index mid-update."""
    for offset, item in enumerate(items, start=1):
        item.position = 1000000 + offset
    db.flush()
    for offset, item in enumerate(items, start=1):
        item.position = offset


def queue_items(playlist_id: str, db: Session) -> list[PlaylistItem]:
    return (
        db.query(PlaylistItem)
        .filter(PlaylistItem.playlist_id == playlist_id, PlaylistItem.state != "played")
        .order_by(PlaylistItem.position, PlaylistItem.created_at)
        .all()
    )


def enqueue(playlist_id: str, media_id: str, db: Session, insert_at: int | None = None) -> PlaylistItem:
    if not db.get(MediaAsset, media_id):
        raise NotFoundException("media asset does not exist")
    state = state_for(playlist_id, db)
    position = (
        db.query(func.max(PlaylistItem.position))
        .filter(PlaylistItem.playlist_id == playlist_id)
        .scalar()
        or 0
    ) + 1
    item = PlaylistItem(
        id=str(uuid.uuid4()),
        playlist_id=playlist_id,
        media_id=media_id,
        position=position,
        state="queued",
    )
    db.add(item)
    if insert_at is not None:
        ordered = [queued for queued in queue_items(playlist_id, db) if queued.id != item.id]
        ordered.insert(max(0, min(insert_at, len(ordered))), item)
        _renumber(ordered, db)
    _bump(state)
    db.commit()
    db.refresh(item)
    return item


def delete_item(playlist_id: str, item_id: str, db: Session) -> None:
    item = db.get(PlaylistItem, item_id)
    if not item or item.playlist_id != playlist_id:
        raise NotFoundException("playlist item does not exist")
    if item.state == "on_air":
        raise ConflictException("cannot delete the item on air; use skip")
    if len(queue_items(playlist_id, db)) <= 1:
        raise ConflictException("cannot delete the final playlist item")
    db.delete(item)
    db.flush()
    _renumber(queue_items(playlist_id, db), db)
    _bump(state_for(playlist_id, db))
    db.commit()


def move_item(
    playlist_id: str, item_id: str, position: int, expected_revision: int, db: Session
) -> None:
    state = state_for(playlist_id, db)
    if state.revision != expected_revision:
        raise ConflictException("playlist revision is stale")
    items = queue_items(playlist_id, db)
    target = next((item for item in items if item.id == item_id), None)
    if not target:
        raise NotFoundException("playlist item does not exist")
    if target.state == "on_air":
        raise ConflictException("the item on air cannot be reordered")
    items.remove(target)
    items.insert(max(0, min(position, len(items))), target)
    _renumber(items, db)
    _bump(state)
    db.commit()


def action(playlist_id: str, name: str, db: Session) -> PlaylistState:
    state = state_for(playlist_id, db)
    if name == "start":
        state.run_state = "running"
    elif name == "pause":
        state.run_state = (
            "pausing"
            if any(item.state == "playing" for item in queue_items(playlist_id, db))
            else "paused"
        )
    elif name == "skip":
        state.skip_seq += 1
    else:
        raise UnprocessableException("unsupported playlist action")
    _bump(state)
    db.commit()
    db.refresh(state)
    return state


def _asset_uri(asset: MediaAsset) -> str:
    # relative_path is already containment-checked at registration time. Quote only the
    # path payload so M3U8 parsing receives a valid file URI for spaces and unicode names.
    return "file:///media/queue/" + quote(asset.relative_path, safe="/-._~")


def live_entries(playlist_id: str | None = None) -> list[dict]:
    """Live sources pinned ahead of the managed queue.

    MANAGED_PLAYLIST_LIVE_URIS holds whitespace-separated live URIs (srt://, udp://,
    rtmp://). They are not media assets, so they never appear in the editable queue; every
    snapshot and the materialized M3U8 start with them, in order. Their IDs derive from the
    URI so that they stay stable across restarts and revisions.

    They belong to the programme playlist only. A channel with ad insertion also
    manages a break reel on its secondary input, and a live source pinned into the
    reel would play in every break.
    """
    if playlist_id is not None:
        from .bootstrap import default_channel_id

        if playlist_id != default_channel_id():
            return []
    return [
        {"id": str(uuid.uuid5(uuid.NAMESPACE_URL, uri)), "uri": uri}
        for uri in os.environ.get("MANAGED_PLAYLIST_LIVE_URIS", "").split()
    ]


def managed_playlist_order() -> list[str]:
    """Managed playlists in the order their channel files are declared.

    That order is the binding between a playlist and an engine input: the first
    declared channel is the channel's programme source, the next one is the
    first alternate source, and so on.  It matches the order the same
    deployment lists inputs in the transcoder configuration, because both are
    written together.
    """
    from .bootstrap import channel_files, default_channel_id

    ordered = list(channel_files())
    default = default_channel_id()
    if default not in ordered:
        ordered.insert(0, default)
    return ordered


def _live_input(playlist_id: str | None = None):
    """The engine input a managed playlist drives.

    With one managed playlist this is the only input the operator can touch,
    which is what every single-playlist deployment assumes.  With more than one
    - a programme rundown plus an ad break reel, say - the playlist is resolved
    positionally through `managed_playlist_order`.

    An unknown playlist, or one whose position has no matching input, falls back
    to the first input rather than failing: a misconfigured second playlist must
    not take the programme's own controls down with it.
    """
    inputs = list(transcoder.getInputs().root)
    if not inputs:
        raise RuntimeError("transcoder reported no inputs")
    if playlist_id is None or len(inputs) == 1:
        return inputs[0]

    order = managed_playlist_order()
    if playlist_id not in order:
        log.warning(
            "playlist %s is not declared in PLAYLIST_BOOTSTRAP_FILES; using the first input",
            playlist_id,
        )
        return inputs[0]

    index = order.index(playlist_id)
    if index >= len(inputs):
        log.warning(
            "playlist %s is declared at position %d but the engine reports only %d input(s); "
            "using the first input",
            playlist_id, index + 1, len(inputs),
        )
        return inputs[0]
    return inputs[index]


def _live_input_and_current(playlist_id: str | None = None) -> tuple[str, str | None]:
    """Return the managed input and its current stable playlist item ID.

    The engine is the authority for on-air state.  SQLite only mirrors it for
    the operator view, so a completed transition never leaves a stale row
    marked as on air.
    """
    input_ = _live_input(playlist_id)
    current = input_.currentStreamId
    return str(input_.id.root), (str(current.root) if current else None)


def refresh_live_state(playlist_id: str, db: Session) -> bool:
    """Mirror the engine's current item in the persistent operator snapshot."""
    input_ = _live_input(playlist_id)
    input_id = str(input_.id.root)
    current_item_id = str(input_.currentStreamId.root) if input_.currentStreamId else None
    live_states = {
        str(stream.id.root): getattr(stream.state, "value", str(stream.state))
        for stream in (input_.streams.root if input_.streams else [])
    }
    state = state_for(playlist_id, db)
    items = queue_items(playlist_id, db)
    changed = state.active_input_id != input_id
    state.active_input_id = input_id
    for item in items:
        if item.id == current_item_id:
            wanted = "on_air"
        elif live_states.get(item.id) in {"CREATED", "BUFFERING", "BUFFERED"}:
            wanted = "preloading"
        else:
            wanted = "error" if item.error else "queued"
        if item.state != wanted:
            item.state = wanted
            changed = True
    if changed:
        db.commit()
    return changed


def skip(playlist_id: str, db: Session) -> PlaylistState:
    """Switch the managed loop to its next stable item without changing its order."""
    state = state_for(playlist_id, db)
    if state.revision != state.applied_revision:
        raise ConflictException("playlist revision is still being applied")
    items = queue_items(playlist_id, db)
    if len(items) < 2:
        raise ConflictException("playlist needs at least two items to skip")
    input_ = _live_input(playlist_id)
    current = str(input_.currentStreamId.root) if input_.currentStreamId else None
    current_index = next((i for i, item in enumerate(items) if item.id == current), -1)
    target = items[(current_index + 1) % len(items)]
    transcoder.switchInputStream(str(input_.id.root), target.id)
    state.skip_seq += 1
    state.run_state = "running"
    db.commit()
    db.refresh(state)
    return state


def reconcile(playlist_id: str, db: Session) -> bool:
    """Apply the persisted queue to the running PlaylistInput exactly once per revision."""
    state = state_for(playlist_id, db)
    items = queue_items(playlist_id, db)
    # Position is both the persistent ordering contract and API output.  Repair
    # any legacy gaps before comparing revisions so the engine receives the
    # exact same ordering that operators see.
    if [item.position for item in items] != list(range(1, len(items) + 1)):
        _renumber(items, db)
        _bump(state)
        db.commit()
    if state.revision == state.applied_revision:
        try:
            refresh_live_state(playlist_id, db)
        except Exception:
            # A transient status read must not turn a successfully-applied queue
            # into an apparent reconciliation failure.
            pass
        return False

    if not items:
        state.apply_error = "managed playlist cannot be empty"
        db.commit()
        return False

    assets = {
        asset.id: asset
        for asset in db.query(MediaAsset).filter(MediaAsset.id.in_([item.media_id for item in items])).all()
    }
    try:
        input_id, _ = _live_input_and_current(playlist_id)
        response = transcoder.applyPlaylistSnapshot(
            input_id,
            playlist_id,
            state.revision,
            live_entries(playlist_id) + [
                {"id": item.id, "uri": _asset_uri(assets[item.media_id])}
                for item in items
            ],
        )
        state.active_input_id = input_id
        state.applied_revision = int(response.revision)
        state.apply_error = None

        current = response.input.currentStreamId
        for item in items:
            item.state = "on_air" if item.id == current else "queued"
            item.error = None
        db.commit()
        return True
    except Exception as error:
        state.apply_error = str(getattr(error, "message", error))[:1000]
        db.commit()
        return False
