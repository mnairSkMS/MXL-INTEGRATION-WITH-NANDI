"""Declarative channel bootstrap: a flat YAML file as the source of truth.

The rundown service already owns playlist and graphics authoring, but its YAML
only ever arrives over HTTP, so a channel can only be reproduced by replaying
operator clicks.  This module adds the missing delivery mechanism: a flat,
hand-writable file that is reconciled into SQLite on every container start.

The flat document is deliberately *not* a second schema with a second
validator.  It is lowered into the canonical rundown v1 document and handed to
`rundown.parse_yaml`, which means it inherits catalog validation, animation
timing rules, clip-fit checks, and the stable UUIDv5 item identity for free.
`RundownVersion.source_yaml` is non-nullable and re-parsed on every read, so a
canonical rundown document has to exist regardless.
"""

from __future__ import annotations

import hashlib
import json
import logging
import os
import pathlib
import re
from dataclasses import dataclass, field
from datetime import timedelta
from typing import Any

import yaml  # type: ignore[import-untyped]
from sqlalchemy import text
from sqlalchemy.orm import Session

from ..exceptions import ConflictException, TranscoderApiException, UnprocessableException
from ..model.db import Rundown, RundownAuditLog, RundownVersion
from . import graphics, media, rundown

log = logging.getLogger(__name__)

# Bare integers are rejected so that `duration: 5` cannot silently mean a
# five-millisecond graphic, which no error message could ever catch.
DURATION_RE = re.compile(r"^(\d+(?:\.\d+)?)(ms|s)$")
SLUG_RE = re.compile(r"[^a-z0-9]+")
MAX_SLUG_CHARS = 100


def bootstrap_paths() -> list[pathlib.Path]:
    """Every declarative channel file, in declaration order.

    A channel is one managed playlist, and a runtime can hold more than one: an
    ad-insertion channel has a programme rundown and a break reel, and both are
    authored the same way.  PLAYLIST_BOOTSTRAP_FILES lists them; the older
    single-valued PLAYLIST_BOOTSTRAP_FILE still works and is treated as a list
    of one, so an existing deployment needs no change.
    """
    value = os.environ.get("PLAYLIST_BOOTSTRAP_FILES", "").strip()
    if not value:
        value = os.environ.get("PLAYLIST_BOOTSTRAP_FILE", "").strip()
    if not value:
        return []
    # Comma or the platform path separator, so a Compose list and a shell PATH
    # style value both work.
    parts = [part.strip() for part in value.replace(os.pathsep, ",").split(",")]
    return [pathlib.Path(part) for part in parts if part]


def channel_files() -> dict[str, pathlib.Path]:
    """Channel id to the file that declares it.

    The id is the file's own `channel:` key, which is what `lower_to_rundown_v1`
    uses, so this mapping cannot disagree with the one the bootstrap applies.  A
    file that cannot be read or names no channel is skipped here and reported
    properly when it is applied.
    """
    mapping: dict[str, pathlib.Path] = {}
    for path in bootstrap_paths():
        try:
            document = yaml.safe_load(path.read_text()) or {}
        except (OSError, yaml.YAMLError):
            continue
        if not isinstance(document, dict):
            continue
        channel_id = document.get("channel", default_channel_id())
        if isinstance(channel_id, str) and channel_id.strip():
            mapping.setdefault(rundown._key(channel_id, "channel"), path)
    return mapping


def bootstrap_path(channel_id: str | None = None) -> pathlib.Path | None:
    """The file backing one channel, or the first declared file.

    Publish writes back to the file that declares the channel being published,
    never to whichever file happens to be listed first.
    """
    paths = bootstrap_paths()
    if not paths:
        return None
    if channel_id is None:
        return paths[0]
    return channel_files().get(channel_id, paths[0] if len(paths) == 1 else None)


def strict() -> bool:
    return os.environ.get("PLAYLIST_BOOTSTRAP_STRICT", "false").lower() in {
        "1", "true", "yes", "on"
    }


def writeback_enabled() -> bool:
    """Whether Publish mirrors the schedule back into the channel file."""
    if not bootstrap_paths():
        return False
    return os.environ.get("PLAYLIST_BOOTSTRAP_WRITEBACK", "true").lower() not in {
        "0", "false", "no", "off"
    }


def default_channel_id() -> str:
    return os.environ.get("MANAGED_PLAYLIST_ID", "main")


@dataclass
class BootstrapResult:
    state: str  # applied | unchanged | failed | absent
    checksum: str | None = None
    version_id: str | None = None
    revision: int | None = None
    warnings: list[str] = field(default_factory=list)
    error: str | None = None

    @property
    def ok(self) -> bool:
        return self.state in {"applied", "unchanged"}


# --------------------------------------------------------------------------
# Flat document grammar
# --------------------------------------------------------------------------


def parse_duration(value: Any, field_name: str) -> int:
    """Parse a unit-suffixed duration into whole milliseconds."""
    if isinstance(value, bool):
        raise UnprocessableException(f"{field_name} must be a duration such as 5s or 600ms")
    if isinstance(value, int) and value == 0:
        return 0
    if not isinstance(value, str):
        raise UnprocessableException(
            f"{field_name} must include a unit, for example 5s or 600ms"
        )
    match = DURATION_RE.fullmatch(value.strip())
    if not match:
        raise UnprocessableException(
            f"{field_name} must include a unit, for example 5s or 600ms"
        )
    amount, unit = float(match.group(1)), match.group(2)
    return round(amount * (1000 if unit == "s" else 1))


def format_duration(value: int) -> str | int:
    """Render whole milliseconds in the grammar `parse_duration` accepts."""
    if value == 0:
        return 0
    if value % 1000 == 0:
        return f"{value // 1000}s"
    if value % 100 == 0:
        return f"{value / 1000:g}s"
    return f"{value}ms"


def slug_for(relative_path: str) -> str:
    """Derive a stable item key from a media file name.

    Item identity drives `playback_occurrences`, so it must survive a reorder.
    A positional key would rewrite every downstream item's UUID on a single
    insert and silently break as-run history for unrelated clips.
    """
    stem = pathlib.PurePosixPath(relative_path).stem.lower()
    candidate = SLUG_RE.sub("-", stem).strip("-")[:MAX_SLUG_CHARS]
    if not candidate or not candidate[0].isalnum():
        candidate = f"item-{candidate}".strip("-")
    return candidate


def _unique(candidate: str, taken: set[str]) -> str:
    if candidate not in taken:
        taken.add(candidate)
        return candidate
    suffix = 2
    while f"{candidate}-{suffix}" in taken:
        suffix += 1
    result = f"{candidate}-{suffix}"
    taken.add(result)
    return result


def _graphic_defaults(asset_id: str, raw: dict[str, Any], context: str) -> tuple[str, int | None, int]:
    """Resolve mode/duration/fade against the graphics catalog."""
    _, asset = graphics._asset(asset_id)
    mode = raw.get("mode") or asset.get("defaultMode") or (asset.get("supportedModes") or [None])[0]
    if not isinstance(mode, str):
        raise UnprocessableException(f"{context}.mode is required for {asset_id}")

    if "duration" in raw and raw["duration"] is not None:
        duration: int | None = parse_duration(raw["duration"], f"{context}.duration")
    else:
        duration = asset.get("defaultDurationMs")

    fade_raw = raw.get("fade")
    fade = parse_duration(fade_raw, f"{context}.fade") if fade_raw is not None else (
        asset.get("defaultAnimationMs", 0) if mode in {"fade", "shrink_overlay", "reveal"} else 0
    )
    return mode, duration, int(fade or 0)


def _lower_graphic(
    raw: Any, *, context: str, item_key: str | None, timezone_name: str,
    taken: set[str], warnings: list[str],
) -> dict[str, Any] | None:
    """Lower one flat graphics entry into its rundown v1 form.

    Returns None when a wall-clock entry has fallen outside the scheduler's
    window, which a static file does simply by sitting on disk long enough.
    """
    value = rundown._mapping(raw, context)
    asset_id = value.get("use")
    if not isinstance(asset_id, str) or not asset_id:
        raise UnprocessableException(f"{context}.use is required")
    mode, duration, fade = _graphic_defaults(asset_id, value, context)

    key = value.get("key")
    if key is None:
        base = f"{item_key}-{asset_id}" if item_key else f"sched-{asset_id}"
        key = _unique(base[:120], taken)
    else:
        key = _unique(rundown._key(key, f"{context}.key"), taken)

    lowered: dict[str, Any] = {
        "key": key, "assetId": asset_id, "mode": mode,
        "durationMs": duration, "fadeMs": fade,
    }

    if item_key is not None:
        at_raw = value.get("at", 0)
        lowered["trigger"] = {
            "type": "item-start",
            "offsetMs": parse_duration(at_raw, f"{context}.at") if at_raw is not None else 0,
        }
        return lowered

    at_raw = value.get("at")
    if at_raw is None:
        raise UnprocessableException(f"{context}.at is required for a scheduled graphic")
    starts_at = rundown.resolve_civil_datetime(timezone_name, at_raw, f"{context}.at")
    now = rundown.utc_now()
    # Nothing re-reads this file later, so an out-of-window entry is dropped,
    # not deferred.  Blocking startup on a stale date would dark the channel.
    if starts_at <= now + timedelta(milliseconds=rundown.DISPATCH_LEAD_MS):
        warnings.append(
            f"{context}: skipped {asset_id} scheduled for {starts_at.isoformat()} (already past)"
        )
        return None
    if starts_at > now + timedelta(days=14):
        warnings.append(
            f"{context}: skipped {asset_id} scheduled for {starts_at.isoformat()} "
            "(beyond the 14-day scheduling window)"
        )
        return None
    lowered["trigger"] = {"type": "wall-clock", "startsAt": starts_at.isoformat()}
    return lowered


def lower_to_rundown_v1(
    document: Any, db: Session
) -> tuple[str, dict[str, Any], list[str]]:
    """Transpile the flat channel file into a canonical rundown v1 document."""
    root = rundown._mapping(document, "document")
    channel_id = root.get("channel", default_channel_id())
    if not isinstance(channel_id, str):
        raise UnprocessableException("channel must be a string")
    channel_id = rundown._key(channel_id, "channel")
    name = root.get("name", channel_id)
    timezone_name = root.get("timezone", "UTC")
    if not all(isinstance(value, str) and value for value in (name, timezone_name)):
        raise UnprocessableException("name and timezone must be non-empty strings")

    warnings: list[str] = []
    item_keys: set[str] = set()
    graphic_keys: set[str] = set()
    items: list[dict[str, Any]] = []

    entries = rundown._list(root.get("playlist"), "playlist")
    if not entries:
        raise UnprocessableException("playlist must contain at least one media item")

    for index, raw_entry in enumerate(entries):
        context = f"playlist[{index}]"
        entry = rundown._mapping(raw_entry, context)
        relative_path = entry.get("media")
        if not isinstance(relative_path, str) or not relative_path:
            raise UnprocessableException(f"{context}.media is required")
        safe_path = media._safe_relative_path(relative_path).as_posix()

        declared_key = entry.get("key")
        key = _unique(
            rundown._key(declared_key, f"{context}.key") if declared_key is not None
            else slug_for(safe_path),
            item_keys,
        )

        lowered_graphics: list[dict[str, Any]] = []
        for graphic_index, raw_graphic in enumerate(
            rundown._list(entry.get("graphics", []) or [], f"{context}.graphics")
        ):
            lowered = _lower_graphic(
                raw_graphic, context=f"{context}.graphics[{graphic_index}]",
                item_key=key, timezone_name=timezone_name,
                taken=graphic_keys, warnings=warnings,
            )
            if lowered is not None:
                lowered_graphics.append(lowered)

        items.append({
            "key": key,
            "media": {"relativePath": safe_path},
            "graphics": lowered_graphics,
        })

    scheduled: list[dict[str, Any]] = []
    for index, raw_graphic in enumerate(
        rundown._list(root.get("scheduled", []) or [], "scheduled")
    ):
        lowered = _lower_graphic(
            raw_graphic, context=f"scheduled[{index}]", item_key=None,
            timezone_name=timezone_name, taken=graphic_keys, warnings=warnings,
        )
        if lowered is not None:
            scheduled.append(lowered)

    return channel_id, {
        "version": 1,
        "rundown": {
            "id": channel_id, "name": name, "timezone": timezone_name,
            "playlistId": channel_id, "loop": True,
        },
        "items": items,
        "scheduledGraphics": scheduled,
    }, warnings


# --------------------------------------------------------------------------
# Writeback
# --------------------------------------------------------------------------


GENERATED_HEADER = (
    "# Managed by the transcoder rundown service.\n"
    "# Rewritten in full on every publish: comments and terse shorthand are not\n"
    "# preserved, and every field is emitted explicitly so that the meaning of\n"
    "# this file cannot drift when the graphics catalog changes.\n"
    "# Hand edits are honoured on the next container start.\n"
)


def _lift_graphic(graphic: dict[str, Any], *, item_key: str | None, taken: set[str]) -> dict[str, Any]:
    """Raise one authoring graphic back into its flat channel-file form."""
    asset_id = graphic["assetId"]
    key = graphic["key"]
    entry: dict[str, Any] = {"use": asset_id, "mode": graphic["mode"]}

    # `_lower_graphic` derives the same key from the item and asset, so writing
    # it out again would only add noise to the common case.
    base = f"{item_key}-{asset_id}" if item_key else f"sched-{asset_id}"
    if _unique(base[:120], taken) != key:
        taken.add(key)
        entry["key"] = key

    if item_key is not None:
        entry["at"] = format_duration(int(graphic.get("trigger", {}).get("offsetMs") or 0))
    else:
        starts_at = (graphic.get("trigger") or {}).get("startsAt")
        if not starts_at:
            raise UnprocessableException(
                f"scheduled graphic {key} has no start time and cannot be written to the channel file"
            )
        entry["at"] = starts_at

    duration = graphic.get("durationMs")
    if duration is not None:
        entry["duration"] = format_duration(int(duration))
    entry["fade"] = format_duration(int(graphic.get("fadeMs") or 0))
    return entry


def lift_from_rundown_v1(document: dict[str, Any]) -> dict[str, Any]:
    """Raise an authoring document back into the flat channel-file grammar.

    The inverse of `lower_to_rundown_v1`, and deliberately lossy in one
    direction only: shorthand a human wrote is expanded, never contracted.
    Round-tripping a lifted document through `lower_to_rundown_v1` must yield
    the same canonical YAML, so that the start after a publish reports
    `unchanged` rather than reapplying what is already on air.
    """
    info = document.get("rundown") or {}
    graphic_keys: set[str] = set()
    item_keys: set[str] = set()

    playlist: list[dict[str, Any]] = []
    for item in document.get("items") or []:
        relative_path = item["relativePath"]
        entry: dict[str, Any] = {"media": relative_path}
        key = item["key"]
        if _unique(slug_for(relative_path), item_keys) != key:
            item_keys.add(key)
            entry["key"] = key
        graphics_entries = [
            _lift_graphic(graphic, item_key=key, taken=graphic_keys)
            for graphic in item.get("graphics") or []
        ]
        if graphics_entries:
            entry["graphics"] = graphics_entries
        playlist.append(entry)

    lifted: dict[str, Any] = {
        "channel": info.get("id", default_channel_id()),
        "name": info.get("name", info.get("id", default_channel_id())),
        "timezone": info.get("timezone", "UTC"),
        "playlist": playlist,
    }
    scheduled = [
        _lift_graphic(graphic, item_key=None, taken=graphic_keys)
        for graphic in document.get("scheduledGraphics") or []
    ]
    if scheduled:
        lifted["scheduled"] = scheduled
    return lifted


@dataclass
class StagedChannelFile:
    """A replacement channel file written and fsynced, but not yet swapped in."""

    path: pathlib.Path
    temporary: pathlib.Path
    uid: int
    gid: int
    mode: int


def require_writable_channel_file(channel_id: str | None = None) -> None:
    """Refuse a publish that could not be persisted, before anything goes live.

    Publishing on air without recording the change is not the publish that was
    asked for: the next container start would silently revert it.  Failing here
    costs nothing, because the queue has not moved yet.
    """
    if not writeback_enabled():
        return
    path = bootstrap_path(channel_id)
    if path is None:
        raise ConflictException(
            f"no declarative channel file declares channel {channel_id!r}, so a publish "
            "could not be persisted. Add it to PLAYLIST_BOOTSTRAP_FILES, or set "
            "PLAYLIST_BOOTSTRAP_WRITEBACK=false to publish without persisting."
        )
    directory = path.parent
    if not directory.is_dir():
        raise ConflictException(f"the channel file directory does not exist: {directory}")
    # Probed rather than inspected with os.access: the container runs as root,
    # for whom access() reports success on a directory it cannot in fact write
    # through.  Creating the file is the only answer that is never wrong, and
    # it also catches a read-only bind mount.
    probe = directory / f".{path.name}.writable"
    try:
        probe.touch()
        probe.unlink()
    except OSError as exc:
        raise ConflictException(
            f"the channel file directory is not writable: {directory} ({exc.strerror}). "
            "Mount it read-write, or set PLAYLIST_BOOTSTRAP_WRITEBACK=false to publish "
            "without persisting."
        ) from exc


def stage_channel_file(document: dict[str, Any]) -> StagedChannelFile | None:
    """Write the replacement alongside the channel file, without swapping it in.

    Returns None when writeback is disabled, which callers treat as a no-op.
    """
    if not writeback_enabled():
        return None
    # The document names its own channel, so a runtime with several of them
    # writes each publish back to the file that declares it.
    channel_id = (document.get("channel") or {}).get("id")
    path = bootstrap_path(channel_id)
    if path is None:
        return None
    body = GENERATED_HEADER + yaml.safe_dump(
        lift_from_rundown_v1(document), sort_keys=False, allow_unicode=True
    )

    # Captured before the write: `os.replace` installs a new inode, so the
    # container's root-owned default would otherwise leave the operator needing
    # sudo to hand-edit their own file.
    try:
        existing = path.stat()
        uid, gid, mode = existing.st_uid, existing.st_gid, existing.st_mode & 0o7777
    except FileNotFoundError:
        directory = path.parent.stat()
        uid, gid, mode = directory.st_uid, directory.st_gid, 0o644

    temporary = path.with_name(path.name + ".tmp")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.write(descriptor, body.encode("utf-8"))
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    return StagedChannelFile(path=path, temporary=temporary, uid=uid, gid=gid, mode=mode)


def commit_channel_file(staged: StagedChannelFile | None) -> dict[str, Any]:
    """Swap in a staged replacement.  Never raises: the publish is already live.

    A failure here leaves the previous file in place, so the next start reverts
    the schedule.  That is reported rather than thrown, because rolling the
    on-air queue back would be the more surprising of the two outcomes.
    """
    if staged is None:
        return {"state": "skipped", "path": None}
    try:
        os.replace(staged.temporary, staged.path)
        os.chmod(staged.path, staged.mode)
        if os.geteuid() == 0:
            os.chown(staged.path, staged.uid, staged.gid)
    except OSError as exc:
        log.error("channel file writeback failed: %s", exc)
        try:
            staged.temporary.unlink(missing_ok=True)
        except OSError:  # pragma: no cover - cleanup must not mask the cause
            pass
        return {"state": "failed", "path": str(staged.path), "error": str(exc)}
    return {"state": "written", "path": str(staged.path)}


# --------------------------------------------------------------------------
# Application
# --------------------------------------------------------------------------


def _register_media(document: dict[str, Any], db: Session) -> None:
    """Import every referenced file so rundown validation can resolve it.

    Runs in its own committed transaction: `_asset_model` shells out to ffprobe
    and hashes the whole file, which must not happen while the write lock for
    the destructive rewrite is held.
    """
    for item in document["items"]:
        relative_path = item["media"]["relativePath"]
        asset = media._asset_model(media._absolute_path(relative_path), relative_path, db)
        # The flat file omits the checksum, because no one should have to type
        # one, but the canonical document records what was actually resolved.
        # Without this the same schedule authored in the UI and read back from
        # the file would hash differently, and every start would republish.
        item["media"]["sha256"] = asset.sha256
    db.commit()


def _is_current(rundown_row: Rundown, checksum: str, db: Session) -> bool:
    """True when both the document and the projected queue already match.

    A checksum comparison alone is not enough: operators can reorder or delete
    `playlist_items` straight from the Playlist panel without ever creating a
    rundown version, so a matching active version does not imply a matching
    queue.
    """
    if not rundown_row.active_version_id:
        return False
    active = db.get(RundownVersion, rundown_row.active_version_id)
    if not active or active.state != "published" or active.checksum != checksum:
        return False
    projected = [
        (item.stable_item_id, item.media_id) for item in rundown.version_items(active.id, db)
    ]
    queued = [
        (item.id, item.media_id) for item in media.queue_items(rundown_row.playlist_id, db)
    ]
    return projected == queued


def apply_file(path: pathlib.Path, db: Session) -> BootstrapResult:
    """Reconcile the database to the declarative channel file.

    All destructive work happens inside one immediate transaction, so a failure
    anywhere leaves the previous playlist byte-identical and still on air.
    """
    raw = path.read_text()
    if len(raw.encode("utf-8")) > rundown.MAX_YAML_BYTES:
        raise UnprocessableException("channel file exceeds the 1 MiB authoring limit")
    try:
        document = yaml.safe_load(raw)
    except yaml.YAMLError as exc:
        raise UnprocessableException(f"channel file is not valid YAML: {exc}") from exc

    channel_id, lowered, warnings = lower_to_rundown_v1(document, db)
    _register_media(lowered, db)

    db.rollback()
    db.execute(text("BEGIN IMMEDIATE"))
    try:
        source_yaml = yaml.safe_dump(lowered, sort_keys=False, allow_unicode=True)
        parsed = rundown.parse_yaml(source_yaml, channel_id, db)
        # The HTTP YAML routes skip this; a declarative file gets the same
        # clip-fit and shared-squeeze checks the operator UI enforces.
        rundown._validate_timeline_constraints(parsed, db)
        checksum = hashlib.sha256(parsed.source_yaml.encode("utf-8")).hexdigest()

        existing = db.get(Rundown, channel_id)
        if existing is not None and _is_current(existing, checksum, db):
            active_version_id = existing.active_version_id
            db.rollback()
            # Recorded so `GET /rundowns/status` reports a fresh outcome rather
            # than the timestamp of whichever earlier start last rewrote the DB.
            rundown._audit(
                db, channel_id, "bootstrap_unchanged", "rundown_version",
                active_version_id or channel_id, checksum=checksum, warnings=warnings,
            )
            db.commit()
            return BootstrapResult(
                state="unchanged", checksum=checksum,
                version_id=active_version_id, warnings=warnings,
            )

        version = rundown._materialize_version(channel_id, parsed.source_yaml, db)
        rundown_row = rundown.get_rundown(channel_id, db)
        revision = rundown.publish_bootstrap(version, rundown_row, db, warnings=warnings)
        version_id = version.id
        db.commit()
        return BootstrapResult(
            state="applied", checksum=checksum, version_id=version_id,
            revision=revision, warnings=warnings,
        )
    except Exception:
        db.rollback()
        raise


def record_failure(channel_id: str, path: pathlib.Path, message: str, db: Session) -> None:
    """Persist a rejection so the API and operator UI can surface it."""
    try:
        rundown._audit(
            db, None, "bootstrap_failed", "channel_file", str(path),
            channel=channel_id, error=message[:1000],
        )
        db.commit()
    except Exception:  # pragma: no cover - auditing must never mask the cause
        db.rollback()
        log.exception("could not record the channel bootstrap failure")


def status(db: Session) -> dict[str, Any]:
    """Latest bootstrap outcome, for `GET /rundowns/status`.

    The top-level fields describe the default channel, which is what the single
    playlist deployments this endpoint was written for still expect.
    `bootstrapChannels` lists every declared channel, including that one.
    """
    paths = bootstrap_paths()
    files = channel_files()
    result: dict[str, Any] = {
        "bootstrapManaged": bool(paths),
        "bootstrapFile": str(paths[0]) if paths else None,
        # Tells the operator UI whether a publish persists.  With writeback on,
        # the only impermanent thing is an unpublished draft.
        "bootstrapWriteback": writeback_enabled(),
        "bootstrapState": "absent",
        "bootstrapAppliedAt": None,
        "bootstrapChecksum": None,
        "bootstrapError": None,
        "bootstrapWarnings": [],
        "bootstrapChannels": [],
    }
    if not paths or not rundown.enabled():
        return result

    default_id = default_channel_id()

    # One scan for every channel. A rejection cannot carry `rundown_id` - the
    # foreign key would not resolve for a channel that never applied - so it
    # records the channel in its details instead, and both forms are matched.
    recent = (
        db.query(RundownAuditLog)
        .filter(RundownAuditLog.action.startswith("bootstrap_"))
        .order_by(RundownAuditLog.id.desc())
        .limit(200)
        .all()
    )

    def _latest(channel_id: str) -> RundownAuditLog | None:
        for candidate in recent:
            if candidate.rundown_id == channel_id:
                return candidate
            if candidate.rundown_id is None:
                try:
                    if json.loads(candidate.details_json or "{}").get("channel") == channel_id:
                        return candidate
                except json.JSONDecodeError:
                    continue
        return None

    for channel_id, path in files.items():
        row = _latest(channel_id)
        entry: dict[str, Any] = {
            "channel": channel_id,
            "file": str(path),
            "state": "absent",
            "appliedAt": None,
            "checksum": None,
            "error": None,
            "warnings": [],
        }
        if row is not None:
            try:
                details = json.loads(row.details_json or "{}")
            except json.JSONDecodeError:
                details = {}
            entry["state"] = {
                "bootstrap_failed": "failed", "bootstrap_unchanged": "unchanged"
            }.get(row.action, "applied")
            entry["appliedAt"] = row.created_at
            entry["checksum"] = details.get("checksum")
            entry["error"] = details.get("error")
            entry["warnings"] = details.get("warnings") or []

        result["bootstrapChannels"].append(entry)

        if channel_id == default_id:
            result["bootstrapState"] = entry["state"]
            result["bootstrapAppliedAt"] = entry["appliedAt"]
            result["bootstrapChecksum"] = entry["checksum"]
            result["bootstrapError"] = entry["error"]
            result["bootstrapWarnings"] = entry["warnings"]
            result["bootstrapFile"] = entry["file"]

    return result


def channel_id_for(path: pathlib.Path) -> str:
    """The channel a file declares, or a stable stand-in when it cannot be read.

    A file too malformed to parse still needs an identity, or its rejection
    would be filed against - and would overwrite the result of - whichever
    channel happens to be the default. The file stem is used instead: it is
    stable, and in a deployment that names its files after its channels it is
    also the right answer.
    """
    for channel_id, declared in channel_files().items():
        if declared == path:
            return channel_id
    return rundown._key(path.stem or "channel", "channel")


def run_one(path: pathlib.Path, db: Session) -> BootstrapResult:
    """Apply one declarative channel file.  Never raises unless strict mode is on."""
    if not rundown.enabled():
        message = (
            "PLAYLIST_BOOTSTRAP_FILES is set but ENABLE_RUNDOWN_SERVICE is off; "
            "the rundown tables the bootstrap writes do not exist"
        )
        log.error("channel bootstrap: %s", message)
        if strict():
            raise SystemExit(1)
        return BootstrapResult(state="failed", error=message)
    if not path.is_file():
        message = f"channel file does not exist: {path}"
        log.error("channel bootstrap: %s", message)
        if strict():
            raise SystemExit(1)
        return BootstrapResult(state="failed", error=message)

    try:
        result = apply_file(path, db)
    except (TranscoderApiException, OSError, ValueError) as exc:
        message = getattr(exc, "message", str(exc))
        log.error("channel bootstrap: %s was rejected: %s", path, message)
        record_failure(channel_id_for(path), path, message, db)
        if strict():
            raise SystemExit(1) from exc
        log.warning("channel bootstrap: keeping the previously saved playlist")
        return BootstrapResult(state="failed", error=message)

    for warning in result.warnings:
        log.warning("channel bootstrap: %s", warning)
    if result.state == "unchanged":
        log.info("channel bootstrap: %s already matches the database", path)
    else:
        log.info(
            "channel bootstrap: applied %s as revision %s (version %s)",
            path, result.revision, result.version_id,
        )
    return result


def run_all(db: Session) -> dict[str, BootstrapResult]:
    """Apply every declared channel file, keyed by channel id.

    One file failing does not stop the others: a runtime whose break reel is
    malformed should still bring its programme to air.
    """
    results: dict[str, BootstrapResult] = {}
    for path in bootstrap_paths():
        # Keyed by the file's own channel, so one file's rejection can never be
        # recorded against another file's channel.
        results[channel_id_for(path)] = run_one(path, db)
    return results


def run(db: Session) -> BootstrapResult:
    """Entry point for a single-channel caller: the default channel's outcome.

    `preload_db` uses `run_all`, because it has to materialize one M3U8 per
    managed playlist. This wrapper keeps the original one-result contract for
    everything else.
    """
    if not bootstrap_paths():
        return BootstrapResult(state="absent")
    results = run_all(db)
    if not results:
        return BootstrapResult(state="absent")
    return results.get(default_channel_id()) or next(iter(results.values()))
