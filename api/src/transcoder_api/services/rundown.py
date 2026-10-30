"""Versioned rundown authoring and safe projection into the managed playlist.

The rundown database is an optional control-plane layer.  Its only media-engine
write is a normal managed-playlist revision, so static M3U8 inputs and existing
queue clients retain their current behavior.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import uuid
from dataclasses import dataclass
from datetime import date, datetime, time, timedelta, timezone
from typing import Any, Callable
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

import yaml  # type: ignore[import-untyped]
from sqlalchemy import func, text
from sqlalchemy.orm import Session

from .. import db
from ..exceptions import (
    ConflictException,
    NotFoundException,
    TranscoderApiException,
    UnprocessableException,
)
from ..model.db import (
    GraphicsCue,
    MediaAsset,
    PlaybackOccurrence,
    PlaylistItem,
    Rundown,
    RundownAuditLog,
    RundownEventDefinition,
    RundownEventExecution,
    RundownOutbox,
    RundownVersion,
    RundownVersionItem,
)
from ..model.rest_api import ErrorCode
from . import graphics, media


RUNDOWN_NAMESPACE = uuid.UUID("e7c7e8f1-13fe-4e65-93fa-70c7eb9d3c93")
KEY_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")
MAX_YAML_BYTES = 1024 * 1024
MAX_OUTBOX_ATTEMPTS = 5
# Commands are created shortly before their planned time.  Creating a future
# sponsor cue at item observation time would reserve the shared squeeze layer
# and incorrectly block an earlier, non-overlapping sponsor.
DISPATCH_LEAD_MS = 750


def enabled() -> bool:
    return os.environ.get("ENABLE_RUNDOWN_SERVICE", "false").lower() in {"1", "true", "yes", "on"}


def require_enabled() -> None:
    if not enabled():
        raise NotFoundException("the rundown service is disabled")
    # Only blocks on a schema that was checked and found stale; an unchecked schema
    # (None) is left alone so this cannot fire in tests or outside the served app.
    if db.SCHEMA_READY is False:
        raise NotFoundException(
            "the rundown service is unavailable: this database has not been migrated"
        )


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


# When this control plane came up, which is when the channel it fronts came up:
# supervisord starts the API and the transcoder together.  The operator grid counts
# its Start column from the moment playout started, and "started" has to mean this
# run of it -- occurrences live in the database and outlast any restart, so without
# this the count would keep climbing across a stop/start of the whole stack.
PROCESS_STARTED_AT = utc_now()


def _process_epoch() -> datetime:
    """When this API process started counting.

    Occurrences older than this belong to a previous run: the grid must not carry on
    projecting from an anchor the current process never observed. Read through an
    accessor rather than the constant directly so the meaning is explicit and tests
    can place the epoch where the scenario they describe requires it.
    """
    return PROCESS_STARTED_AT


def _utc(value: datetime) -> datetime:
    return (
        value.replace(tzinfo=timezone.utc)
        if value.tzinfo is None
        else value.astimezone(timezone.utc)
    )


def _uuid(*parts: str) -> str:
    return str(uuid.uuid5(RUNDOWN_NAMESPACE, ":".join(parts)))


def _audit(
    db: Session,
    rundown_id: str | None,
    action: str,
    subject_type: str,
    subject_id: str,
    **details: Any,
) -> None:
    db.add(
        RundownAuditLog(
            rundown_id=rundown_id,
            action=action,
            subject_type=subject_type,
            subject_id=subject_id,
            details_json=json.dumps(details, sort_keys=True, separators=(",", ":")),
        )
    )


def _enqueue_outbox(
    db: Session,
    command_type: str,
    dedupe_key: str,
    payload: dict[str, Any],
    *,
    next_attempt_at: datetime | None = None,
) -> None:
    if db.query(RundownOutbox).filter(RundownOutbox.dedupe_key == dedupe_key).first():
        return
    db.add(
        RundownOutbox(
            id=str(uuid.uuid4()),
            dedupe_key=dedupe_key,
            command_type=command_type,
            payload_json=json.dumps(payload, sort_keys=True, separators=(",", ":")),
            state="pending",
            attempts=0,
            next_attempt_at=next_attempt_at or utc_now(),
        )
    )


def _parse_datetime(value: Any, field: str) -> datetime:
    if not isinstance(value, str):
        raise UnprocessableException(f"{field} must be an ISO-8601 timestamp")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise UnprocessableException(f"{field} must be an ISO-8601 timestamp") from exc
    if parsed.tzinfo is None:
        raise UnprocessableException(f"{field} must include a timezone")
    return parsed.astimezone(timezone.utc)


def _resolve_civil_in_zone(civil: datetime, zone: ZoneInfo) -> tuple[datetime, datetime]:
    """Map a naive civil datetime onto a unique instant in `zone`.

    Daylight saving makes this a partial function: the skipped hour has no
    instant and the repeated hour has two.  Both are rejected rather than
    silently resolved, because either guess would air a graphic an hour wrong.
    """
    candidates: dict[datetime, datetime] = {}
    for fold in (0, 1):
        candidate = civil.replace(tzinfo=zone, fold=fold)
        round_trip = candidate.astimezone(timezone.utc).astimezone(zone)
        if round_trip.replace(tzinfo=None) == civil:
            candidates[candidate.astimezone(timezone.utc)] = candidate
    if not candidates:
        raise UnprocessableException("this local time does not exist because of a daylight-saving change")
    if len(candidates) > 1:
        raise UnprocessableException(
            "this local time occurs twice because of a daylight-saving change; choose a different time"
        )
    return next(iter(candidates.items()))


def resolve_civil_datetime(zone_name: str, value: str, field: str) -> datetime:
    """Parse an ISO-8601 timestamp that may omit its offset.

    Declarative bootstrap files are written by hand, so a bare local time is the
    natural thing to type.  An explicit offset still wins; only a naive value is
    interpreted in the channel's declared zone.
    """
    try:
        zone = ZoneInfo(zone_name)
    except (ZoneInfoNotFoundError, ValueError) as exc:
        raise UnprocessableException(f"{zone_name} is not a valid IANA timezone") from exc
    if not isinstance(value, str):
        raise UnprocessableException(f"{field} must be an ISO-8601 timestamp")
    try:
        civil = datetime.fromisoformat(value.strip().replace("Z", "+00:00"))
    except ValueError as exc:
        raise UnprocessableException(f"{field} must be an ISO-8601 timestamp") from exc
    if civil.tzinfo is not None:
        return civil.astimezone(timezone.utc)
    return _resolve_civil_in_zone(civil, zone)[0]


def _to_hundredths(value: datetime) -> datetime:
    """Round a civil datetime to the hundredth of a second the schedule stores.

    A native ``<input type="time">`` with a hundredths step still serialises three
    decimals - "12:00:00.120" - so the third digit has to be absorbed here rather
    than refused. Rounding through ``timedelta`` carries across the second, minute,
    hour and day boundaries instead of producing an impossible time.
    """
    remainder = value.microsecond % 10_000
    rounded = value.replace(microsecond=value.microsecond - remainder)
    return rounded + timedelta(microseconds=10_000) if remainder >= 5_000 else rounded


def resolve_local_wall_clock_time(
    rundown_id: str,
    local_date: str,
    local_time: str,
    db: Session,
) -> tuple[datetime, datetime, str]:
    """Resolve operator-entered civil time in the rundown's declared timezone.

    Browsers cannot reliably convert arbitrary IANA zones (especially DST
    boundaries) with a native input. Keeping this resolution server-side makes
    the canonical stored ISO timestamp independent of the operator's laptop.
    """
    rundown = get_rundown(rundown_id, db)
    try:
        parsed_date = date.fromisoformat(local_date)
    except (TypeError, ValueError) as exc:
        raise UnprocessableException("date must use YYYY-MM-DD") from exc
    if not isinstance(local_time, str) or not re.fullmatch(
        r"\d{2}:\d{2}(?::\d{2}(?:\.\d{1,3})?)?", local_time
    ):
        raise UnprocessableException("time must use HH:MM:SS.ss")
    try:
        parsed_time = time.fromisoformat(local_time)
    except ValueError as exc:
        raise UnprocessableException("time is invalid") from exc
    try:
        zone = ZoneInfo(rundown.timezone)
    except ZoneInfoNotFoundError as exc:
        raise UnprocessableException("the rundown has an invalid IANA timezone") from exc

    utc_value, local_value = _resolve_civil_in_zone(
        _to_hundredths(datetime.combine(parsed_date, parsed_time)), zone
    )
    now = utc_now()
    if utc_value < now or utc_value > now + timedelta(days=14):
        raise UnprocessableException("wall-clock time must be within the next 14 days")
    return utc_value, local_value, rundown.timezone


def _mapping(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise UnprocessableException(f"{field} must be an object")
    return value


def _list(value: Any, field: str) -> list[Any]:
    if not isinstance(value, list):
        raise UnprocessableException(f"{field} must be an array")
    return value


def _key(value: Any, field: str) -> str:
    if not isinstance(value, str) or not KEY_RE.fullmatch(value):
        raise UnprocessableException(f"{field} must be 1-128 URL-safe identifier characters")
    return value


def _integer(value: Any, field: str, *, allow_none: bool = False) -> int | None:
    if value is None and allow_none:
        return None
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise UnprocessableException(f"{field} must be a non-negative integer")
    return value


@dataclass(frozen=True)
class ParsedEvent:
    key: str
    item_key: str | None
    asset_id: str
    mode: str
    duration_ms: int | None
    fade_ms: int
    trigger_type: str
    offset_ms: int | None
    starts_at: datetime | None


@dataclass(frozen=True)
class ParsedItem:
    key: str
    media_id: str
    position: int


@dataclass(frozen=True)
class ParsedRundown:
    id: str
    name: str
    timezone: str
    playlist_id: str
    source_yaml: str
    items: list[ParsedItem]
    events: list[ParsedEvent]


def _asset_by_path(db: Session, value: Any, context: str) -> MediaAsset:
    media_spec = _mapping(value, f"{context}.media")
    relative_path = media_spec.get("relativePath")
    if not isinstance(relative_path, str):
        raise UnprocessableException(f"{context}.media.relativePath is required")
    safe_path = media._safe_relative_path(relative_path).as_posix()
    asset = db.query(MediaAsset).filter(MediaAsset.relative_path == safe_path).one_or_none()
    if not asset:
        raise UnprocessableException(f"{context}.media.relativePath is not registered media")
    expected_sha = media_spec.get("sha256")
    if expected_sha is not None and expected_sha != asset.sha256:
        raise UnprocessableException(f"{context}.media.sha256 does not match registered media")
    return asset


def _parse_graphic(
    raw: Any,
    *,
    context: str,
    item_key: str | None,
    seen_keys: set[str],
    enforce_schedule_window: bool = True,
) -> ParsedEvent:
    value = _mapping(raw, context)
    key = _key(value.get("key"), f"{context}.key")
    if key in seen_keys:
        raise UnprocessableException(f"duplicate graphics key {key}")
    seen_keys.add(key)
    asset_id = value.get("assetId")
    mode = value.get("mode")
    if not isinstance(asset_id, str) or not isinstance(mode, str):
        raise UnprocessableException(f"{context}.assetId and {context}.mode are required")
    _, asset = graphics._asset(asset_id)
    if not asset.get("enabled", True):
        raise UnprocessableException(
            f"{context}.assetId is disabled by the video graphics safety gate"
        )
    if mode not in asset["supportedModes"]:
        raise UnprocessableException(f"{context}.mode is not supported by {asset_id}")
    duration = _integer(value.get("durationMs"), f"{context}.durationMs", allow_none=True)
    if duration is None and asset["kind"] not in {"logo", "bug"}:
        duration = asset.get("defaultDurationMs")
    if duration is None and asset["kind"] not in {"logo", "bug"}:
        raise UnprocessableException(f"{context}.durationMs is required for {asset['kind']}")
    if duration == 0:
        raise UnprocessableException(f"{context}.durationMs must be greater than zero")
    fade = _integer(value.get("fadeMs", 0), f"{context}.fadeMs")
    assert fade is not None
    if duration is not None and mode in {"fade", "shrink_overlay", "reveal"} and 2 * fade >= duration:
        raise UnprocessableException(f"{context}.durationMs must leave time for both animation legs")
    trigger = _mapping(value.get("trigger"), f"{context}.trigger")
    trigger_type = trigger.get("type")
    if item_key is not None:
        if trigger_type != "item-start":
            raise UnprocessableException(f"{context}.trigger.type must be item-start")
        offset = _integer(trigger.get("offsetMs", 0), f"{context}.trigger.offsetMs")
        starts_at = None
    else:
        if trigger_type != "wall-clock":
            raise UnprocessableException(f"{context}.trigger.type must be wall-clock")
        starts_at = _parse_datetime(trigger.get("startsAt"), f"{context}.trigger.startsAt")
        now = utc_now()
        # Only a write is refused for being out of window.  A published version
        # is an immutable as-run record whose scheduled times inevitably fall
        # into the past, and refusing to *read* it would lock an operator out of
        # their own live schedule the moment one wall-clock trigger elapsed.
        if enforce_schedule_window and (
            starts_at < now or starts_at > now + timedelta(days=14)
        ):
            raise UnprocessableException(
                f"{context}.trigger.startsAt must be within the next 14 days"
            )
        offset = None
    return ParsedEvent(
        key, item_key, asset_id, mode, duration, int(fade), trigger_type, offset, starts_at
    )


def parse_yaml(
    source_yaml: str,
    rundown_id: str,
    db: Session,
    *,
    enforce_schedule_window: bool = True,
) -> ParsedRundown:
    if not isinstance(source_yaml, str) or not source_yaml.strip():
        raise UnprocessableException("yaml is required")
    if len(source_yaml.encode("utf-8")) > MAX_YAML_BYTES:
        raise UnprocessableException("yaml exceeds the 1 MiB authoring limit")
    try:
        document = yaml.safe_load(source_yaml)
    except yaml.YAMLError as exc:
        raise UnprocessableException("yaml is invalid") from exc
    root = _mapping(document, "document")
    if root.get("version") != 1:
        raise UnprocessableException("document.version must be 1")
    info = _mapping(root.get("rundown"), "rundown")
    declared_id = info.get("id", rundown_id)
    if declared_id != rundown_id:
        raise UnprocessableException("rundown.id must match the URL rundown ID")
    name = info.get("name", rundown_id)
    timezone_name = info.get("timezone", "UTC")
    playlist_id = info.get("playlistId", "main")
    if not all(isinstance(value, str) and value for value in (name, timezone_name, playlist_id)):
        raise UnprocessableException(
            "rundown name, timezone, and playlistId must be non-empty strings"
        )

    items: list[ParsedItem] = []
    item_keys: set[str] = set()
    event_keys: set[str] = set()
    events: list[ParsedEvent] = []
    for position, raw_item in enumerate(_list(root.get("items"), "items"), start=1):
        item = _mapping(raw_item, f"items[{position - 1}]")
        key = _key(item.get("key"), f"items[{position - 1}].key")
        if key in item_keys:
            raise UnprocessableException(f"duplicate item key {key}")
        item_keys.add(key)
        asset = _asset_by_path(db, item.get("media"), f"items[{position - 1}]")
        items.append(ParsedItem(key, asset.id, position))
        for graphic_index, raw_graphic in enumerate(
            _list(item.get("graphics", []), f"items[{position - 1}].graphics")
        ):
            events.append(
                _parse_graphic(
                    raw_graphic,
                    context=f"items[{position - 1}].graphics[{graphic_index}]",
                    item_key=key,
                    seen_keys=event_keys,
                    enforce_schedule_window=enforce_schedule_window,
                )
            )

    if not items:
        raise UnprocessableException("items must contain at least one media item")

    for index, raw_graphic in enumerate(
        _list(root.get("scheduledGraphics", []), "scheduledGraphics")
    ):
        events.append(
            _parse_graphic(
                raw_graphic,
                context=f"scheduledGraphics[{index}]",
                item_key=None,
                seen_keys=event_keys,
                enforce_schedule_window=enforce_schedule_window,
            )
        )

    canonical = yaml.safe_dump(root, sort_keys=False, allow_unicode=True)
    return ParsedRundown(rundown_id, name, timezone_name, playlist_id, canonical, items, events)


def _asset_duration_ms(asset: MediaAsset) -> int | None:
    """Return a probed media duration without treating unknown as zero."""
    try:
        probe = json.loads(asset.probe_json or "{}")
        duration = probe.get("format", {}).get("duration")
        value = float(duration)
        return int(value * 1000) if value > 0 else None
    except (TypeError, ValueError, json.JSONDecodeError):
        return None


def _validate_timeline_constraints(parsed: ParsedRundown, db: Session) -> None:
    """Extra constraints for UI-authored automatic graphics.

    YAML stays backwards compatible; this is also intentionally applied when
    publishing any document so an automatic event cannot extend beyond a clip.
    """
    assets = {
        asset.id: asset
        for asset in db.query(MediaAsset)
        .filter(MediaAsset.id.in_([item.media_id for item in parsed.items]))
        .all()
    }
    item_by_key = {item.key: item for item in parsed.items}
    lengths: dict[str, int] = {}
    for key, item in item_by_key.items():
        length = _asset_duration_ms(assets[item.media_id])
        if length is None:
            # Only item-relative events require the duration.
            continue
        lengths[key] = length

    sponsors: list[tuple[int, int, str]] = []
    timeline_start = 0
    starts: dict[str, int] = {}
    for item in parsed.items:
        starts[item.key] = timeline_start
        timeline_start += lengths.get(item.key, 0)
    loop_length = timeline_start
    for event in parsed.events:
        _, catalog = graphics._asset(event.asset_id)
        kind = catalog["kind"]
        if event.item_key is not None:
            duration = lengths.get(event.item_key)
            if duration is None:
                raise UnprocessableException(
                    f"item-relative graphics require a known media duration ({event.item_key})"
                )
            if event.duration_ms is None:
                raise UnprocessableException("item-relative Logo/Bug graphics require a finite duration")
            restore_margin = 1200 if kind in {"lband", "aston"} else 0
            assert event.offset_ms is not None
            if event.offset_ms + event.duration_ms + restore_margin > duration:
                raise UnprocessableException(
                    f"graphic {event.key} does not fit inside owning clip including restoration"
                )
            if kind in {"lband", "aston"}:
                start = starts[event.item_key] + event.offset_ms
                sponsors.append((start, start + event.duration_ms + restore_margin, event.key))
        elif kind in {"lband", "aston"} and event.duration_ms is not None:
            # Absolute schedules only need mutual-exclusion validation if their
            # intervals overlap in real time.  Their own finite timing was
            # already validated by parse_yaml.
            pass
    if loop_length and sponsors:
        # Check normal and loop-boundary intersections.  An event is never
        # compared with itself in the next loop.
        for index, (start, end, key) in enumerate(sponsors):
            for other_start, other_end, other_key in sponsors[index + 1 :]:
                if start < other_end and other_start < end:
                    raise UnprocessableException(f"shared squeeze overlap: {key} and {other_key}")
                if start < other_end + loop_length and other_start + loop_length < end:
                    raise UnprocessableException(
                        f"shared squeeze overlap across loop boundary: {key} and {other_key}"
                    )
        for start, end, key in sponsors:
            for other_start, other_end, other_key in sponsors:
                if key == other_key:
                    continue
                if start < other_end + loop_length and other_start + loop_length < end:
                    raise UnprocessableException(
                        f"shared squeeze overlap across loop boundary: {key} and {other_key}"
                    )


def document_from_yaml(
    source_yaml: str, rundown_id: str, db: Session, *, version_id: str | None = None
) -> dict[str, Any]:
    """Structured authoring view; browser clients never need to parse YAML.

    `version_id` is supplied only for a published or superseded version, whose
    persisted event definitions carry the IDs that executions reference.  A
    draft has no such rows — its identity is the YAML alone — so it reports a
    null `definitionId` and derives each stable item ID from the item key.
    """
    parsed = parse_yaml(source_yaml, rundown_id, db, enforce_schedule_window=False)
    assets = {asset.id: asset for asset in db.query(MediaAsset).filter(
        MediaAsset.id.in_([item.media_id for item in parsed.items])
    )}
    definition_by_key = (
        {event.key: event for event in version_events(version_id, db)} if version_id else {}
    )

    def definition_id(key: str) -> str | None:
        definition = definition_by_key.get(key)
        return definition.id if definition else None

    def stable_id(key: str) -> str:
        return _uuid(rundown_id, "item", key)

    timeline = 0
    items: list[dict[str, Any]] = []
    for parsed_item in parsed.items:
        asset = assets[parsed_item.media_id]
        duration = _asset_duration_ms(asset)
        item: dict[str, Any] = {
            "key": parsed_item.key, "stableItemId": stable_id(parsed_item.key),
            "mediaId": asset.id, "relativePath": asset.relative_path,
            "displayName": asset.display_name, "durationMs": duration,
            "timelineStartMs": timeline, "position": parsed_item.position, "graphics": [],
        }
        for event in parsed.events:
            if event.item_key == parsed_item.key:
                _, catalog = graphics._asset(event.asset_id)
                item["graphics"].append({
                    "definitionId": definition_id(event.key), "key": event.key, "assetId": event.asset_id,
                    "kind": catalog["kind"], "mode": event.mode, "durationMs": event.duration_ms,
                    "fadeMs": event.fade_ms, "trigger": {"type": "item-start", "offsetMs": event.offset_ms or 0},
                    "itemKey": parsed_item.key, "source": "rundown",
                })
        items.append(item)
        timeline += duration or 0
    scheduled: list[dict[str, Any]] = []
    for event in parsed.events:
        if event.item_key is None:
            _, catalog = graphics._asset(event.asset_id)
            scheduled.append({
                "definitionId": definition_id(event.key), "key": event.key, "assetId": event.asset_id,
                "kind": catalog["kind"], "mode": event.mode, "durationMs": event.duration_ms,
                "fadeMs": event.fade_ms,
                "trigger": {"type": "wall-clock", "startsAt": event.starts_at.isoformat() if event.starts_at else None},
                "source": "rundown",
            })
    return {"version": 1,
            "rundown": {"id": parsed.id, "name": parsed.name, "timezone": parsed.timezone,
                        "playlistId": parsed.playlist_id, "loop": True},
            "items": items, "scheduledGraphics": scheduled, "timelineDurationMs": timeline}


def document_from_version(version: RundownVersion, db: Session) -> dict[str, Any]:
    document = document_from_yaml(
        version.source_yaml, version.rundown_id, db, version_id=version.id
    )
    document["versionId"] = version.id
    document["state"] = version.state
    return document


def document_to_yaml(document: dict[str, Any], rundown_id: str, db: Session) -> str:
    root = _mapping(document, "document")
    info = _mapping(root.get("rundown"), "rundown")
    items: list[dict[str, Any]] = []
    for raw in _list(root.get("items"), "items"):
        item = _mapping(raw, "items[]")
        media_id = item.get("mediaId")
        if not isinstance(media_id, str):
            raise UnprocessableException("items[].mediaId is required")
        asset = db.get(MediaAsset, media_id)
        if not asset:
            raise UnprocessableException("items[].mediaId is not registered media")
        graphics_rows = []
        for raw_event in _list(item.get("graphics", []), "items[].graphics"):
            event = _mapping(raw_event, "items[].graphics[]")
            graphics_rows.append({"key": event.get("key"), "assetId": event.get("assetId"),
                "mode": event.get("mode"), "durationMs": event.get("durationMs"),
                "fadeMs": event.get("fadeMs", 0), "trigger": event.get("trigger")})
        items.append({"key": item.get("key"), "media": {"relativePath": asset.relative_path,
            "sha256": asset.sha256}, "graphics": graphics_rows})
    scheduled = []
    for raw_event in _list(root.get("scheduledGraphics", []), "scheduledGraphics"):
        event = _mapping(raw_event, "scheduledGraphics[]")
        scheduled.append({"key": event.get("key"), "assetId": event.get("assetId"),
            "mode": event.get("mode"), "durationMs": event.get("durationMs"),
            "fadeMs": event.get("fadeMs", 0), "trigger": event.get("trigger")})
    canonical = {"version": root.get("version", 1), "rundown": {"id": info.get("id", rundown_id),
        "name": info.get("name", rundown_id), "timezone": info.get("timezone", "UTC"),
        "playlistId": info.get("playlistId", "main"), "loop": bool(info.get("loop", True))},
        "items": items, "scheduledGraphics": scheduled}
    return yaml.safe_dump(canonical, sort_keys=False, allow_unicode=True)


def validate_document(document: dict[str, Any], rundown_id: str, db: Session) -> ParsedRundown:
    parsed = parse_yaml(document_to_yaml(document, rundown_id, db), rundown_id, db)
    _validate_timeline_constraints(parsed, db)
    return parsed


def _drop_elapsed_triggers(document: dict[str, Any], warnings: list[str]) -> None:
    """Remove wall-clock triggers whose time has already passed.

    A published version is an immutable as-run record, so its scheduled times
    inevitably fall into the past.  Carrying an elapsed trigger into a document
    that is about to be re-validated would fail `parse_yaml`'s future-window
    check and lock the operator out of editing their own live schedule until a
    restart.  Dropping it matches what the bootstrap does with a stale
    `scheduled:` entry — nothing re-runs a time that has passed.
    """
    horizon = utc_now() + timedelta(milliseconds=DISPATCH_LEAD_MS)
    retained: list[dict[str, Any]] = []
    for event in document.get("scheduledGraphics", []) or []:
        starts_at = (event.get("trigger") or {}).get("startsAt")
        if starts_at and _parse_datetime(
            starts_at, "scheduledGraphics[].trigger.startsAt"
        ) > horizon:
            retained.append(event)
        else:
            warnings.append(
                f"dropped elapsed wall-clock graphic {event.get('key')} ({starts_at})"
            )
    document["scheduledGraphics"] = retained


def seed_from_playlist(
    rundown_id: str, db: Session, *, name: str | None = None
) -> tuple[RundownVersion, dict[str, Any]]:
    """Publish a first version for a channel that has none.

    `apply_to_live` mutates the live version, so a channel with no channel file
    and no published version would otherwise have no way in.  This projects the
    playlist that is already running into version 1 and publishes it.
    """
    require_enabled()
    _require_live_input(rundown_id)
    from . import bootstrap

    bootstrap.require_writable_channel_file(rundown_id)
    db.rollback()
    db.execute(text("BEGIN IMMEDIATE"))
    try:
        existing = db.get(Rundown, rundown_id)
        if existing is not None and existing.active_version_id:
            raise ConflictException(
                "the rundown already has a published version; edit it with apply"
            )
        parsed = parse_yaml(
            _yaml_from_current_playlist(rundown_id, name or rundown_id, db), rundown_id, db
        )
        _validate_timeline_constraints(parsed, db)
        _ensure_rundown(rundown_id, parsed, db)
        rundown = get_rundown(rundown_id, db)
        version = _materialize_version(rundown_id, parsed.source_yaml, db)
        _publish_locked(version, rundown, db)
        _audit(db, rundown_id, "seeded", "rundown_version", version.id, source="playlist")
    except Exception:
        db.rollback()
        raise
    return version, _finalize_publish(version, db)


def _latest_version(
    rundown_id: str, db: Session, state: str | None = None
) -> RundownVersion | None:
    query = db.query(RundownVersion).filter(RundownVersion.rundown_id == rundown_id)
    if state:
        query = query.filter(RundownVersion.state == state)
    return query.order_by(RundownVersion.number.desc()).first()


def _ensure_rundown(rundown_id: str, parsed: ParsedRundown, db: Session) -> Rundown:
    """Upsert the rundown's identity row.

    Seeding reaches this as well as publishing, so the rundown is a real
    resource from its first version rather than springing into existence
    partway through a projection.
    """
    rundown = db.get(Rundown, rundown_id)
    if rundown is None:
        rundown = Rundown(
            id=rundown_id,
            name=parsed.name,
            timezone=parsed.timezone,
            playlist_id=parsed.playlist_id,
        )
        db.add(rundown)
        db.flush()
        return rundown
    if rundown.playlist_id != parsed.playlist_id:
        raise ConflictException("a rundown cannot change playlistId after it is created")
    rundown.name = parsed.name
    rundown.timezone = parsed.timezone
    return rundown


def _materialize_version(
    rundown_id: str, source_yaml: str, db: Session
) -> RundownVersion:
    """Write the immutable record of a schedule that is about to go on air.

    Only publish paths reach this, so a version number counts on-air
    generations rather than keystrokes: a rejected or dry-run change never
    allocates one. Does not commit; the caller owns the transaction.
    """
    parsed = parse_yaml(source_yaml, rundown_id, db)
    _ensure_rundown(rundown_id, parsed, db)

    previous = _latest_version(rundown_id, db)
    number = (previous.number if previous else 0) + 1
    version = RundownVersion(
        id=str(uuid.uuid4()),
        rundown_id=rundown_id,
        number=number,
        state="pending",
        source_yaml=parsed.source_yaml,
        checksum=hashlib.sha256(parsed.source_yaml.encode("utf-8")).hexdigest(),
    )
    db.add(version)
    db.flush()
    version_items: dict[str, RundownVersionItem] = {}
    for item in parsed.items:
        row = RundownVersionItem(
            id=str(uuid.uuid4()),
            version_id=version.id,
            stable_item_id=_uuid(rundown_id, "item", item.key),
            key=item.key,
            media_id=item.media_id,
            position=item.position,
        )
        db.add(row)
        version_items[item.key] = row
    db.flush()
    for event in parsed.events:
        db.add(
            RundownEventDefinition(
                id=str(uuid.uuid4()),
                version_id=version.id,
                item_id=version_items[event.item_key].id if event.item_key else None,
                key=event.key,
                asset_id=event.asset_id,
                mode=event.mode,
                duration_ms=event.duration_ms,
                fade_ms=event.fade_ms,
                trigger_type=event.trigger_type,
                offset_ms=event.offset_ms,
                starts_at=event.starts_at,
            )
        )
    db.flush()
    return version


def _yaml_from_current_playlist(rundown_id: str, name: str, db: Session) -> str:
    """Seed a schedule from whatever the managed queue is already playing."""
    playlist_id = "main"
    rows = media.queue_items(playlist_id, db)
    if not rows:
        raise ConflictException("the managed playlist has no items to seed a rundown")
    assets = {
        asset.id: asset
        for asset in db.query(MediaAsset)
        .filter(MediaAsset.id.in_([row.media_id for row in rows]))
        .all()
    }
    document = {
        "version": 1,
        "rundown": {
            "id": rundown_id,
            "name": name,
            "timezone": "UTC",
            "playlistId": playlist_id,
            "loop": True,
        },
        "items": [
            {
                "key": f"seed-{position}",
                "media": {
                    "relativePath": assets[row.media_id].relative_path,
                    "sha256": assets[row.media_id].sha256,
                },
            }
            for position, row in enumerate(rows, start=1)
        ],
        "scheduledGraphics": [],
    }
    return yaml.safe_dump(document, sort_keys=False)


def get_rundown(rundown_id: str, db: Session) -> Rundown:
    rundown = db.get(Rundown, rundown_id)
    if not rundown:
        raise NotFoundException("rundown does not exist")
    return rundown


def get_version(version_id: str, db: Session) -> RundownVersion:
    version = db.get(RundownVersion, version_id)
    if not version:
        raise NotFoundException("rundown version does not exist")
    return version


def _queue_projection(version: RundownVersion, rundown: Rundown, db: Session) -> tuple[int, bool]:
    """Project a published rundown safely, retaining the current item as a bridge.

    PlaylistInput deliberately refuses to remove its on-air stable ID.  If a new
    rundown does not contain that ID, retain the old clip at queue head for one
    final play, then the post-transition cleanup removes that bridge revision.
    """
    state = media.state_for(rundown.playlist_id, db)
    if state.revision != state.applied_revision:
        raise ConflictException("the managed playlist is still applying a prior revision")
    try:
        _, current_id = media._live_input_and_current(rundown.playlist_id)
    except Exception as exc:
        raise TranscoderApiException(
            "cannot confirm the on-air item", ErrorCode.UNAVAILABLE, 503
        ) from exc
    if not current_id:
        raise ConflictException("the managed playlist has no confirmed on-air item")
    existing = db.get(PlaylistItem, current_id)
    if not existing or existing.playlist_id != rundown.playlist_id:
        raise ConflictException("the on-air item is not the current managed playlist")

    target = (
        db.query(RundownVersionItem)
        .filter(RundownVersionItem.version_id == version.id)
        .order_by(RundownVersionItem.position)
        .all()
    )
    target_ids = {item.stable_item_id for item in target}
    current_is_target = current_id in target_ids
    if current_is_target:
        matching = next(item for item in target if item.stable_item_id == current_id)
        if matching.media_id != existing.media_id:
            raise ConflictException(
                "a stable rundown item ID cannot change its media asset while on air"
            )

    # Temporarily move the surviving on-air row out of the public position range
    # before removing/recreating the rest of the unique playlist positions.
    existing.position = 1000000
    db.query(PlaylistItem).filter(
        PlaylistItem.playlist_id == rundown.playlist_id, PlaylistItem.id != existing.id
    ).delete(synchronize_session=False)
    db.flush()

    position = 1
    if not current_is_target:
        existing.position = position
        existing.state = "on_air"
        existing.error = None
        position += 1
    for item in target:
        if item.stable_item_id == current_id:
            existing.position = position
            existing.state = "on_air"
            existing.error = None
        else:
            db.add(
                PlaylistItem(
                    id=item.stable_item_id,
                    playlist_id=rundown.playlist_id,
                    media_id=item.media_id,
                    position=position,
                    state="queued",
                )
            )
        position += 1
    state.revision += 1
    state.apply_error = None
    return state.revision, not current_is_target


def _create_wall_clock_commands(version: RundownVersion, db: Session) -> None:
    for definition in db.query(RundownEventDefinition).filter(
        RundownEventDefinition.version_id == version.id,
        RundownEventDefinition.trigger_type == "wall-clock",
        RundownEventDefinition.enabled.is_(True),
    ):
        assert definition.starts_at is not None
        execution_key = f"{definition.id}:wall-clock"
        execution = RundownEventExecution(
            id=str(uuid.uuid4()),
            execution_key=execution_key,
            definition_id=definition.id,
            occurrence_id=None,
            planned_at=_utc(definition.starts_at),
            state="pending",
        )
        db.add(execution)
        db.flush()
        _enqueue_outbox(
            db, "graphics_cue", execution_key, {"executionId": execution.id},
            next_attempt_at=max(utc_now(), execution.planned_at - timedelta(milliseconds=DISPATCH_LEAD_MS)),
        )


def _cancel_superseded_commands(
    rundown_id: str, previous_version_id: str, replacement_version_id: str, db: Session
) -> None:
    """Stop future work from an old version without interrupting live graphics."""
    reason = f"superseded by rundown version {replacement_version_id}"
    executions = (
        db.query(RundownEventExecution)
        .join(
            RundownEventDefinition,
            RundownEventExecution.definition_id == RundownEventDefinition.id,
        )
        .filter(RundownEventDefinition.version_id == previous_version_id)
        .all()
    )
    canceled = 0
    scheduled_cues = 0
    now = utc_now()
    for execution in executions:
        outbox = (
            db.query(RundownOutbox)
            .filter(RundownOutbox.dedupe_key == execution.execution_key)
            .one_or_none()
        )
        if execution.state in {"pending", "retrying"}:
            execution.state = "canceled"
            execution.error = reason
            if outbox and outbox.state == "pending":
                outbox.state = "canceled"
                outbox.last_error = reason
            canceled += 1
            continue
        if execution.state == "dispatched" and execution.cue_id:
            cue = db.get(GraphicsCue, execution.cue_id)
            if cue and graphics._status(cue, now).phase == "scheduled":
                graphics.cancel_cue(cue.id, db, commit=False)
                execution.state = "canceled"
                execution.error = reason
                scheduled_cues += 1
    if canceled or scheduled_cues:
        _audit(
            db,
            rundown_id,
            "superseded_commands_canceled",
            "rundown_version",
            previous_version_id,
            replacement_version_id=replacement_version_id,
            pending_executions=canceled,
            scheduled_cues=scheduled_cues,
        )


def _cancel_all_pending_commands(rundown_id: str, keep_version_id: str, db: Session) -> None:
    """Retire queued work from every version except the one being published.

    `_cancel_superseded_commands` only sweeps the previously active version.  A
    declarative bootstrap makes the file the sole authority, so a command left
    pending by any older version must not fire after the rewrite.
    """
    for version in db.query(RundownVersion).filter(
        RundownVersion.rundown_id == rundown_id, RundownVersion.id != keep_version_id
    ):
        _cancel_superseded_commands(rundown_id, version.id, keep_version_id, db)


def publish_bootstrap(
    version: RundownVersion, rundown: Rundown, db: Session, *, warnings: list[str] | None = None
) -> int:
    """Project a draft onto the managed queue during cold start.

    `_publish_locked` cannot be used here.  `preload_db` runs before the engine,
    so there is no gRPC socket to confirm an on-air item with, and nothing is
    playing that would need the `_queue_projection` bridge.  The queue is
    therefore replaced outright and left with `applied_revision = 0` so the
    normal reconciler replays it once the engine registers its input.

    Does not commit: the caller owns the enclosing BEGIN IMMEDIATE transaction.
    """
    if version.state != "pending":
        raise ConflictException("bootstrap version changed before it could be published")
    state = media.state_for(rundown.playlist_id, db)
    target = version_items(version.id, db)

    # Query.delete() emits SQL immediately.  The unit of work would order the
    # inserts first and collide with UNIQUE(playlist_id, position).
    db.query(PlaylistItem).filter(
        PlaylistItem.playlist_id == rundown.playlist_id
    ).delete(synchronize_session=False)
    db.flush()
    for position, item in enumerate(target, start=1):
        db.add(
            PlaylistItem(
                id=item.stable_item_id,
                playlist_id=rundown.playlist_id,
                media_id=item.media_id,
                position=position,
                state="queued",
            )
        )
    db.flush()

    _cancel_all_pending_commands(rundown.id, version.id, db)
    for prior in db.query(RundownVersion).filter(
        RundownVersion.rundown_id == rundown.id,
        RundownVersion.id != version.id,
        RundownVersion.state == "published",
    ):
        prior.state = "superseded"

    version.state = "published"
    version.published_at = utc_now()
    rundown.active_version_id = version.id
    _create_wall_clock_commands(version, db)

    state.revision += 1
    # A fresh engine process starts from the materialized M3U8 with generated
    # stream IDs, so the snapshot must be replayed even though this bootstrap
    # just wrote the desired revision itself.
    state.applied_revision = 0
    state.apply_error = None
    state.active_input_id = "bootstrap"
    _enqueue_outbox(
        db,
        "playlist_snapshot",
        f"rundown-bootstrap:{rundown.id}:{version.id}:{state.revision}",
        {"playlistId": rundown.playlist_id, "revision": state.revision, "versionId": version.id},
    )
    _audit(
        db, rundown.id, "bootstrap_published", "rundown_version", version.id,
        revision=state.revision, item_count=len(target), checksum=version.checksum,
        warnings=warnings or [],
    )
    return state.revision


def _require_live_input(playlist_id: str | None = None) -> None:
    try:
        media._live_input_and_current(playlist_id)
    except Exception as exc:
        raise TranscoderApiException(
            "cannot confirm the on-air item", ErrorCode.UNAVAILABLE, 503
        ) from exc


def _publish_locked(
    version: RundownVersion, rundown: Rundown, db: Session,
    *, reset_source_version_id: str | None = None, reset_removed_event_count: int | None = None,
) -> RundownVersion:
    """Project a materialized version while an immediate transaction is held."""
    rundown_id = rundown.id
    if version.state != "pending":
        raise ConflictException("rundown version changed before it could be published")
    revision, bridged = _queue_projection(version, rundown, db)
    previous = rundown.active_version_id
    if previous and previous != version.id:
        prior = db.get(RundownVersion, previous)
        if prior:
            _cancel_superseded_commands(rundown.id, previous, version.id, db)
            prior.state = "superseded"
    version.state = "published"
    version.published_at = utc_now()
    rundown.active_version_id = version.id
    _create_wall_clock_commands(version, db)
    _enqueue_outbox(
        db,
        "playlist_snapshot",
        f"rundown-publish:{rundown_id}:{version.id}:{revision}",
        {"playlistId": rundown.playlist_id, "revision": revision, "versionId": version.id},
    )
    _audit(
        db,
        rundown_id,
        "published",
        "rundown_version",
        version.id,
        revision=revision,
        retained_on_air_bridge=bridged,
    )
    if reset_source_version_id is not None:
        _audit(
            db, rundown_id, "schedule_reset", "rundown_version", version.id,
            removed_event_count=reset_removed_event_count or 0,
            source_version_id=reset_source_version_id,
        )
    # The caller commits, so that the declarative channel file can be staged
    # inside the same window and swapped in only once the database agrees.
    return version


def _finalize_publish(version: RundownVersion, db: Session) -> dict[str, Any]:
    """Commit a publish and mirror it into the declarative channel file.

    Ordering matters: the replacement file is staged before the commit but is
    swapped in after it, so a crash can leave a stale file (which the next start
    simply reapplies) but never a file describing a publish that did not happen.
    """
    from . import bootstrap

    staged = bootstrap.stage_channel_file(document_from_version(version, db))
    db.commit()
    db.refresh(version)
    outcome = bootstrap.commit_channel_file(staged)
    if outcome.get("state") == "failed":
        _audit(
            db, version.rundown_id, "channel_file_write_failed", "rundown_version", version.id,
            path=outcome.get("path"), error=outcome.get("error"),
        )
        db.commit()
    return outcome


def apply_to_live(
    rundown_id: str,
    db: Session,
    expected_version_id: str | None,
    mutate: Callable[[dict[str, Any]], Any],
    *,
    dry_run: bool = False,
    publish_kwargs: Callable[[], dict[str, Any]] | None = None,
) -> tuple[RundownVersion | None, Any, dict[str, Any], list[str]]:
    """Mutate the live version, validate the result, and publish it atomically.

    This is the single write path for the schedule.  A caller expresses the end
    state it wants; validation runs once on the merged document, so an edit that
    is only legal as a whole — swapping which sponsor owns the shared squeeze,
    for instance — never has to pass through an intermediate state that
    `_validate_timeline_constraints` would reject.

    The ordering is load-bearing: both the live-input check and the channel-file
    writability probe happen before the immediate transaction, because refusing
    after the queue has moved would leave a publish on air that the next restart
    would silently revert.
    """
    require_enabled()
    _require_live_input(rundown_id)
    from . import bootstrap

    bootstrap.require_writable_channel_file(rundown_id)
    warnings: list[str] = []
    db.rollback()
    db.execute(text("BEGIN IMMEDIATE"))
    try:
        rundown = get_rundown(rundown_id, db)
        if not rundown.active_version_id:
            raise ConflictException("the rundown has no published version to change")
        if expected_version_id is not None and rundown.active_version_id != expected_version_id:
            raise ConflictException("the schedule changed in another operator session")
        document = document_from_version(get_version(rundown.active_version_id, db), db)
        _drop_elapsed_triggers(document, warnings)
        result = mutate(document)
        parsed = validate_document(document, rundown_id, db)
        if dry_run:
            db.rollback()
            return None, result, {}, warnings
        version = _materialize_version(rundown_id, parsed.source_yaml, db)
        _publish_locked(version, rundown, db, **(publish_kwargs() if publish_kwargs else {}))
    except Exception:
        db.rollback()
        raise
    return version, result, _finalize_publish(version, db), warnings


def _slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", str(value).lower()).strip("-") or "graphic"


def _all_graphic_keys(document: dict[str, Any]) -> set[str]:
    keys = {
        event.get("key")
        for item in document.get("items", []) or []
        for event in item.get("graphics", []) or []
    }
    keys.update(event.get("key") for event in document.get("scheduledGraphics", []) or [])
    return {key for key in keys if key}


def _find_graphic(document: dict[str, Any], key: str) -> tuple[list[dict[str, Any]], int]:
    for item in document.get("items", []) or []:
        for index, event in enumerate(item.get("graphics", []) or []):
            if event.get("key") == key:
                return item["graphics"], index
    for index, event in enumerate(document.get("scheduledGraphics", []) or []):
        if event.get("key") == key:
            return document["scheduledGraphics"], index
    raise NotFoundException(f"no graphic with key {key}")


_GRAPHIC_FIELDS = ("mode", "durationMs", "fadeMs")


def _apply_operations(
    document: dict[str, Any], ops: list[dict[str, Any]], db: Session
) -> dict[str, int]:
    """Apply an ordered operation list to a rundown document in memory.

    Pure dictionary manipulation: nothing here touches the database, so the
    whole list either produces a document that validates or is discarded.
    """
    from . import bootstrap

    if not ops:
        raise UnprocessableException("ops must contain at least one operation")
    counts: dict[str, int] = {}
    for index, raw in enumerate(ops):
        op = _mapping(raw, f"ops[{index}]")
        name = op.get("op")
        if name == "addGraphic":
            item_key = op.get("itemKey")
            starts_at = op.get("startsAt")
            if bool(item_key) == bool(starts_at):
                raise UnprocessableException(
                    f"ops[{index}] requires exactly one of itemKey or startsAt"
                )
            asset_id = op.get("assetId")
            if not isinstance(asset_id, str) or not asset_id:
                raise UnprocessableException(f"ops[{index}].assetId is required")
            _, catalog = graphics._asset(asset_id)
            taken = _all_graphic_keys(document)
            base = _slug(f"{item_key or 'sched'}-{asset_id}")[:120]
            candidate = op.get("key") or base
            event = {
                "key": bootstrap._unique(_key(candidate, f"ops[{index}].key"), taken),
                "assetId": asset_id,
                "mode": op.get("mode") or catalog.get("defaultMode"),
                "durationMs": op.get("durationMs", catalog.get("defaultDurationMs")),
                "fadeMs": op.get("fadeMs", catalog.get("defaultAnimationMs", 0)),
            }
            if item_key:
                target = next(
                    (
                        item
                        for item in document.get("items", []) or []
                        if item.get("key") == item_key
                    ),
                    None,
                )
                if target is None:
                    raise NotFoundException(f"no rundown item with key {item_key}")
                event["trigger"] = {
                    "type": "item-start",
                    "offsetMs": _integer(op.get("offsetMs", 0), f"ops[{index}].offsetMs") or 0,
                }
                target.setdefault("graphics", []).append(event)
            else:
                event["trigger"] = {"type": "wall-clock", "startsAt": starts_at}
                document.setdefault("scheduledGraphics", []).append(event)
        elif name == "updateGraphic":
            key = _key(op.get("key"), f"ops[{index}].key")
            container, position = _find_graphic(document, key)
            event = container[position]
            for field in _GRAPHIC_FIELDS:
                if field in op:
                    event[field] = op[field]
            if "offsetMs" in op:
                if event.get("trigger", {}).get("type") != "item-start":
                    raise UnprocessableException(
                        f"ops[{index}]: offsetMs applies only to an item-relative graphic"
                    )
                event["trigger"]["offsetMs"] = _integer(
                    op["offsetMs"], f"ops[{index}].offsetMs"
                )
            if "startsAt" in op:
                if event.get("trigger", {}).get("type") != "wall-clock":
                    raise UnprocessableException(
                        f"ops[{index}]: startsAt applies only to a wall-clock graphic"
                    )
                event["trigger"]["startsAt"] = op["startsAt"]
        elif name == "removeGraphic":
            key = _key(op.get("key"), f"ops[{index}].key")
            container, position = _find_graphic(document, key)
            container.pop(position)
        elif name == "removeItem":
            key = _key(op.get("key"), f"ops[{index}].key")
            items = document.get("items", []) or []
            remaining = [item for item in items if item.get("key") != key]
            if len(remaining) == len(items):
                raise NotFoundException(f"no rundown item with key {key}")
            document["items"] = remaining
        elif name == "insertItems":
            new_items = _list(op.get("items"), f"ops[{index}].items")
            if not new_items:
                raise UnprocessableException(f"ops[{index}].items must not be empty")
            items = document.get("items", []) or []
            raw_position = op.get("position")
            position = len(items) if raw_position is None else max(
                0, min(int(raw_position), len(items))
            )
            document["items"] = items[:position] + list(new_items) + items[position:]
        else:
            raise UnprocessableException(f"ops[{index}].op is not a supported operation: {name}")
        counts[name] = counts.get(name, 0) + 1
    return counts


# --- queue edits, expressed as schedule edits -------------------------------
#
# While a rundown has a published version, that version — not the queue table —
# is the authority on what the queue contains: `_remove_completed_bridge` runs
# every reconciler tick and deletes every queue row whose stable id is absent
# from it. That check cannot tell a leftover on-air bridge row from a row an
# operator has just inserted, so a direct queue write is accepted and then
# silently undone within ~250 ms.
#
# The three functions below therefore express insert/remove/reorder as edits to
# the schedule and publish them, which is what makes them durable: the queue is
# reprojected from the new version, the change survives a restart because the
# channel file is rewritten, and the removed item stops reappearing as a
# schedule row with no queue row behind it.


def governs_playlist(playlist_id: str, db: Session) -> Rundown | None:
    """The published rundown that owns this playlist's queue, or None."""
    if not enabled():
        return None
    rundown = (
        db.query(Rundown)
        .filter(
            Rundown.playlist_id == playlist_id,
            Rundown.active_version_id.is_not(None),
        )
        .first()
    )
    if rundown is None:
        return None
    version = db.get(RundownVersion, rundown.active_version_id)
    return rundown if version is not None and version.state == "published" else None


def schedules_item(rundown: Rundown, stable_item_id: str, db: Session) -> bool:
    """Is this queue row backed by a scheduled item?

    False for a bridge row — the on-air item of a previous version that the
    current one dropped. `_queue_projection` keeps it at the queue head for one
    last play, so it exists in the queue with nothing in the schedule behind it,
    and it has to be edited directly rather than through the schedule.
    """
    return (
        db.query(RundownVersionItem)
        .filter(
            RundownVersionItem.version_id == rundown.active_version_id,
            RundownVersionItem.stable_item_id == stable_item_id,
        )
        .first()
        is not None
    )


def _item_by_stable_id(document: dict[str, Any], stable_item_id: str) -> dict[str, Any]:
    for item in document.get("items", []) or []:
        if item.get("stableItemId") == stable_item_id:
            return item
    raise NotFoundException("playlist item is not in the published schedule")


def _schedule_index(
    document: dict[str, Any], playlist_id: str, queue_position: int | None, db: Session
) -> int | None:
    """Translate a 0-based queue index into the matching schedule index.

    The two lists are identical except while a bridge row sits at the queue head,
    when the queue is one longer. Counting how many scheduled items fall before
    the target queue row keeps an insert landing where the operator pointed
    instead of one row out.
    """
    if queue_position is None:
        return None
    before = {row.id for row in media.queue_items(playlist_id, db)[: max(0, queue_position)]}
    return sum(
        1 for item in document.get("items", []) or [] if item.get("stableItemId") in before
    )


def enqueue_media(
    rundown: Rundown, media_id: str, position: int | None, db: Session
) -> tuple[RundownVersion | None, Any, dict[str, Any], list[str]]:
    """Insert media into the schedule. `position` is a 0-based queue index."""
    asset = db.get(MediaAsset, media_id)
    if not asset:
        raise NotFoundException("media asset does not exist")
    playlist_id = rundown.playlist_id

    def mutate(document: dict[str, Any]) -> str:
        from . import bootstrap

        items = list(document.get("items", []) or [])
        # Keys must stay distinct: stable_item_id is _uuid(rundown, "item", key),
        # so two items sharing a key would collide on PlaylistItem's primary key
        # the moment the same clip is queued twice.
        taken = {item.get("key") for item in items if item.get("key")}
        stem = asset.relative_path.rsplit("/", 1)[-1].rsplit(".", 1)[0]
        key = bootstrap._unique(_slug(stem)[:120] or "item", taken)
        index = _schedule_index(document, playlist_id, position, db)
        index = len(items) if index is None else max(0, min(index, len(items)))
        items.insert(index, {"key": key, "mediaId": media_id, "graphics": []})
        document["items"] = items
        return key

    return apply_to_live(rundown.id, db, rundown.active_version_id, mutate)


def remove_queue_item(
    rundown: Rundown, stable_item_id: str, db: Session
) -> tuple[RundownVersion | None, Any, dict[str, Any], list[str]]:
    def mutate(document: dict[str, Any]) -> str:
        target = _item_by_stable_id(document, stable_item_id)
        remaining = [
            item for item in document.get("items", []) or [] if item is not target
        ]
        # A managed playlist the engine can be handed is never empty; refusing
        # here is clearer than failing later in reconciliation.
        if not remaining:
            raise ConflictException("the schedule cannot be left empty")
        document["items"] = remaining
        return target.get("key")

    return apply_to_live(rundown.id, db, rundown.active_version_id, mutate)


def move_queue_item(
    rundown: Rundown, stable_item_id: str, position: int, db: Session
) -> tuple[RundownVersion | None, Any, dict[str, Any], list[str]]:
    playlist_id = rundown.playlist_id

    def mutate(document: dict[str, Any]) -> str:
        items = list(document.get("items", []) or [])
        target = _item_by_stable_id(document, stable_item_id)
        index = _schedule_index(document, playlist_id, position, db)
        items.remove(target)
        items.insert(max(0, min(index if index is not None else len(items), len(items))), target)
        document["items"] = items
        return target.get("key")

    return apply_to_live(rundown.id, db, rundown.active_version_id, mutate)


def apply_operations(
    rundown_id: str,
    db: Session,
    ops: list[dict[str, Any]],
    *,
    expected_version_id: str | None = None,
    dry_run: bool = False,
) -> tuple[RundownVersion | None, dict[str, int], dict[str, Any], list[str]]:
    return apply_to_live(
        rundown_id,
        db,
        expected_version_id,
        lambda document: _apply_operations(document, ops, db),
        dry_run=dry_run,
    )


def apply_document(
    rundown_id: str,
    db: Session,
    document: dict[str, Any],
    *,
    expected_version_id: str | None = None,
    dry_run: bool = False,
) -> tuple[RundownVersion | None, dict[str, int], dict[str, Any], list[str]]:
    """Apply a whole edited schedule rather than an operation list.

    A client that already holds the complete end state — the operator UI does,
    because it edits a copy of the live document — should not have to synthesize
    the operations that would produce it.  Both paths share one validation.
    """
    dropped: list[str] = []

    def mutate(live: dict[str, Any]) -> dict[str, int]:
        incoming = _mapping(document, "document")
        live["items"] = _list(incoming.get("items"), "document.items")
        live["scheduledGraphics"] = _list(
            incoming.get("scheduledGraphics", []) or [], "document.scheduledGraphics"
        )
        # The replacement came from the client, so it can carry a trigger that
        # elapsed while the tab was open; the live copy was already filtered.
        _drop_elapsed_triggers(live, dropped)
        return {
            "items": len(live["items"]),
            "graphics": sum(
                len(item.get("graphics", []) or []) for item in live["items"]
            )
            + len(live["scheduledGraphics"]),
        }

    version, counts, channel_file, warnings = apply_to_live(
        rundown_id, db, expected_version_id, mutate, dry_run=dry_run
    )
    return version, counts, channel_file, warnings + dropped


def import_programs(
    rundown_id: str,
    db: Session,
    raw_yaml: str,
    *,
    position: int | None = None,
    expected_version_id: str | None = None,
    dry_run: bool = False,
) -> tuple[RundownVersion | None, dict[str, int], dict[str, Any], list[str]]:
    """Splice the programs declared by a channel YAML into the live rundown.

    The file is the same format as `config/channel.yml`, lowered by the very
    same code the declarative bootstrap uses, so an imported list inherits
    catalog validation, duration-unit grammar and stable item identity for free.
    """
    require_enabled()
    from . import bootstrap

    if len(raw_yaml.encode("utf-8")) > MAX_YAML_BYTES:
        raise UnprocessableException("channel file exceeds the 1 MiB authoring limit")
    try:
        document = yaml.safe_load(raw_yaml)
    except yaml.YAMLError as exc:
        raise UnprocessableException(f"file is not valid YAML: {exc}") from exc

    root = _mapping(document, "document")
    # Importing another channel's file would silently move the wrong media onto
    # this rundown, so a declared channel has to match the one being imported to.
    declared = root.get("channel")
    if isinstance(declared, str) and declared and _key(declared, "channel") != rundown_id:
        raise UnprocessableException(
            f"this file declares channel '{declared}'; import it into that channel "
            "or remove the key"
        )

    # The live rundown's own name and timezone win; only the items are used.
    _, lowered, warnings = bootstrap.lower_to_rundown_v1({**root, "channel": rundown_id}, db)
    # Must happen before the write lock: it shells out to ffprobe and hashes
    # whole files, which has no business holding an immediate transaction.
    try:
        bootstrap._register_media(lowered, db)
    except NotFoundException as exc:
        # The bootstrap treats a missing file as a missing resource, but here it
        # is a fault in the document the caller just submitted, and the fix is
        # to upload the media first rather than to retry the import.
        db.rollback()
        raise UnprocessableException(
            f"{exc.message} — this imports a program list, not the media; "
            "upload the clips with POST /media first"
        ) from exc

    assets = {
        asset.relative_path: asset.id
        for asset in db.query(MediaAsset).filter(
            MediaAsset.relative_path.in_(
                [item["media"]["relativePath"] for item in lowered["items"]]
            )
        )
    }

    def mutate(live: dict[str, Any]) -> dict[str, int]:
        # Only the newcomers are re-keyed.  A stable item ID is uuid5 of its
        # key, and it is what the on-air row and every as-run occurrence join
        # on, so renaming an item that already exists would silently rewrite
        # history for a clip nobody touched.
        taken = {item.get("key") for item in live.get("items", []) or []}
        taken.update(_all_graphic_keys(live))
        prepared: list[dict[str, Any]] = []
        for item in lowered["items"]:
            relative_path = item["media"]["relativePath"]
            media_id = assets.get(relative_path)
            if not media_id:
                raise UnprocessableException(
                    f"{relative_path} is not registered media; upload it first"
                )
            events = [
                {**event, "key": bootstrap._unique(event["key"], taken)}
                for event in item.get("graphics", []) or []
            ]
            prepared.append(
                {
                    "key": bootstrap._unique(item["key"], taken),
                    "mediaId": media_id,
                    "graphics": events,
                }
            )
        return _apply_operations(
            live, [{"op": "insertItems", "position": position, "items": prepared}], db
        )

    version, counts, channel_file, apply_warnings = apply_to_live(
        rundown_id, db, expected_version_id, mutate, dry_run=dry_run
    )
    return version, counts, channel_file, warnings + apply_warnings



def reset_and_publish(
    rundown_id: str, db: Session, expected_version_id: str
) -> tuple[RundownVersion, int, dict[str, Any]]:
    """Atomically publish the live programs with every future graphic removed.

    Reset acts on what is on air: its purpose is to stop triggers that are
    already live.
    """
    removed = 0

    def mutate(document: dict[str, Any]) -> int:
        nonlocal removed
        removed = sum(len(item.get("graphics", []) or []) for item in document.get("items", []))
        removed += len(document.get("scheduledGraphics", []) or [])
        for item in document.get("items", []) or []:
            item["graphics"] = []
        document["scheduledGraphics"] = []
        return removed

    version, _, channel_file, _ = apply_to_live(
        rundown_id,
        db,
        expected_version_id,
        mutate,
        publish_kwargs=lambda: {
            "reset_source_version_id": expected_version_id,
            "reset_removed_event_count": removed,
        },
    )
    assert version is not None
    return version, removed, channel_file


def clock_for(rundown_id: str, db: Session) -> dict[str, Any]:
    """Report the loop clock that the operator grid projects start times from.

    The on-air item's observed start is the only exact timing the control plane
    has; every other row in the grid is projected from it.  Serving it here
    rather than letting the browser mine `GET /occurrences` keeps the 1 Hz poll
    to two indexed lookups, and answers "when did this schedule start" — which
    that endpoint cannot, being capped at the newest 200 rows.
    """
    require_enabled()
    rundown = get_rundown(rundown_id, db)
    now = utc_now()
    idle: dict[str, Any] = {
        "syncedAt": None,
        "currentItemId": None,
        "currentStartedAt": None,
        "loopDurationMs": 0,
        "cycle": 0,
        "serverNow": now,
    }
    if not rundown.active_version_id:
        return idle

    version = get_version(rundown.active_version_id, db)
    loop_length = document_from_version(version, db)["timelineDurationMs"] or 0

    # When this channel started playing out, which the grid counts its Start column
    # from and which untethered graphics are scheduled against.
    #
    # Deliberately NOT scoped to the active version.  Publishing mints a new version,
    # so a per-version anchor restarted the count at zero on every apply: an operator
    # who scheduled something for 01:00:00 saw it re-expressed against a new origin
    # the moment they applied anything at all.  Playout does not restart on a publish,
    # so neither does this.
    #
    # It is still scoped to this process run.  Occurrences persist in the database, so
    # without the lower bound a restart would inherit the previous run's anchor and the
    # grid would carry on counting as if the channel had never stopped.
    observed_sync = (
        db.query(func.min(PlaybackOccurrence.observed_at))
        .filter(
            PlaybackOccurrence.rundown_id == rundown_id,
            PlaybackOccurrence.observed_at >= _process_epoch(),
        )
        .scalar()
    )
    # Between start-up and the first observed transition there is no occurrence yet.
    # Anchoring on start-up keeps the column counting from something real instead of
    # blanking it, and the first observation replaces it within one program.
    synced_at = _utc(observed_sync) if observed_sync else _process_epoch()

    # The newest occurrence in this rundown is, by construction, the start of
    # whatever is on air: `_record_current_occurrence` writes one at every
    # observed transition.  Deliberately not filtered by version — an item's
    # stable ID survives a publish, so the occurrence that recorded its start is
    # still the truth about when it started.  Scoping this to the active version
    # would blank the entire Start column for up to one program after every apply.
    latest = (
        db.query(PlaybackOccurrence)
        .filter(PlaybackOccurrence.rundown_id == rundown_id)
        .order_by(PlaybackOccurrence.observed_at.desc())
        .first()
    )
    # A restart leaves the previous run's occurrences newest until the observer
    # catches its first transition, usually within one program.  Reporting one as
    # the current item would anchor every projected row to a start that already
    # happened, so stay idle and let the grid show em dashes until it is real.
    if latest is None or _utc(latest.observed_at) < _process_epoch():
        return {**idle, "syncedAt": synced_at, "loopDurationMs": loop_length}

    started_at = _utc(latest.observed_at)
    # A zero loop means no item has a probed duration, so there are no cycles to
    # count; report the first rather than dividing by zero.
    cycle = 1
    if loop_length > 0:
        elapsed_ms = max(0.0, (started_at - synced_at).total_seconds() * 1000)
        cycle = int(elapsed_ms // loop_length) + 1
    return {
        "syncedAt": synced_at,
        "currentItemId": latest.playlist_item_id,
        "currentStartedAt": started_at,
        "loopDurationMs": loop_length,
        "cycle": cycle,
        "serverNow": now,
    }


def _definitions_for_item(
    version_id: str, stable_item_id: str, db: Session
) -> list[RundownEventDefinition]:
    return (
        db.query(RundownEventDefinition)
        .join(RundownVersionItem, RundownEventDefinition.item_id == RundownVersionItem.id)
        .filter(
            RundownEventDefinition.version_id == version_id,
            RundownVersionItem.stable_item_id == stable_item_id,
            RundownEventDefinition.trigger_type == "item-start",
            RundownEventDefinition.enabled.is_(True),
        )
        .all()
    )


def _close_previous_occurrence(
    rundown_id: str, version_id: str, current_id: str, now: datetime, db: Session
) -> bool:
    changed = False
    for occurrence in db.query(PlaybackOccurrence).filter(
        PlaybackOccurrence.rundown_id == rundown_id,
        PlaybackOccurrence.version_id == version_id,
        PlaybackOccurrence.ended_at.is_(None),
        PlaybackOccurrence.playlist_item_id != current_id,
    ):
        occurrence.ended_at = now
        changed = True
    return changed


def _record_current_occurrence(
    rundown: Rundown, version: RundownVersion, current_id: str, db: Session
) -> bool:
    target_ids = {
        row.stable_item_id
        for row in db.query(RundownVersionItem).filter(RundownVersionItem.version_id == version.id)
    }
    if current_id not in target_ids:
        return False
    open_occurrence = (
        db.query(PlaybackOccurrence)
        .filter(
            PlaybackOccurrence.rundown_id == rundown.id,
            PlaybackOccurrence.version_id == version.id,
            PlaybackOccurrence.playlist_item_id == current_id,
            PlaybackOccurrence.ended_at.is_(None),
        )
        .first()
    )
    if open_occurrence:
        return False
    sequence = (
        db.query(func.max(PlaybackOccurrence.sequence))
        .filter(
            PlaybackOccurrence.version_id == version.id,
            PlaybackOccurrence.playlist_item_id == current_id,
        )
        .scalar()
        or 0
    ) + 1
    now = utc_now()
    occurrence = PlaybackOccurrence(
        id=str(uuid.uuid4()),
        rundown_id=rundown.id,
        version_id=version.id,
        playlist_item_id=current_id,
        sequence=sequence,
        observed_at=now,
    )
    db.add(occurrence)
    db.flush()
    for definition in _definitions_for_item(version.id, current_id, db):
        execution_key = f"{definition.id}:{occurrence.id}"
        execution = RundownEventExecution(
            id=str(uuid.uuid4()),
            execution_key=execution_key,
            definition_id=definition.id,
            occurrence_id=occurrence.id,
            planned_at=now + timedelta(milliseconds=definition.offset_ms or 0),
            state="pending",
        )
        db.add(execution)
        db.flush()
        _enqueue_outbox(
            db, "graphics_cue", execution_key, {"executionId": execution.id},
            next_attempt_at=max(utc_now(), execution.planned_at - timedelta(milliseconds=DISPATCH_LEAD_MS)),
        )
    _audit(
        db,
        rundown.id,
        "occurrence_observed",
        "playback_occurrence",
        occurrence.id,
        item_id=current_id,
        sequence=sequence,
    )
    return True


def _remove_completed_bridge(
    rundown: Rundown, version: RundownVersion, current_id: str, db: Session
) -> bool:
    target_ids = {
        row.stable_item_id
        for row in db.query(RundownVersionItem).filter(RundownVersionItem.version_id == version.id)
    }
    if current_id not in target_ids:
        return False
    old_rows = (
        db.query(PlaylistItem)
        .filter(PlaylistItem.playlist_id == rundown.playlist_id, PlaylistItem.id.notin_(target_ids))
        .all()
    )
    if not old_rows:
        return False
    for row in old_rows:
        db.delete(row)
    db.flush()
    state = media.state_for(rundown.playlist_id, db)
    media._renumber(media.queue_items(rundown.playlist_id, db), db)
    state.revision += 1
    state.apply_error = None
    _enqueue_outbox(
        db,
        "playlist_snapshot",
        f"rundown-bridge-cleanup:{rundown.id}:{version.id}:{state.revision}",
        {"playlistId": rundown.playlist_id, "revision": state.revision, "versionId": version.id},
    )
    _audit(
        db,
        rundown.id,
        "on_air_bridge_removed",
        "rundown_version",
        version.id,
        revision=state.revision,
    )
    return True


def sync_active_rundowns(db: Session) -> bool:
    """Observe live stable-ID transitions and create idempotent cue commands."""
    changed = False
    for rundown in db.query(Rundown).filter(Rundown.active_version_id.is_not(None)).all():
        version = db.get(RundownVersion, rundown.active_version_id)
        if not version or version.state != "published":
            continue
        try:
            _, current_id = media._live_input_and_current(rundown.playlist_id)
        except Exception:
            continue
        if not current_id:
            continue
        now = utc_now()
        changed = _close_previous_occurrence(rundown.id, version.id, current_id, now, db) or changed
        changed = _record_current_occurrence(rundown, version, current_id, db) or changed
        changed = _remove_completed_bridge(rundown, version, current_id, db) or changed
    if changed:
        db.commit()
    return changed


def _dispatch_graphics(outbox: RundownOutbox, db: Session) -> None:
    payload = json.loads(outbox.payload_json)
    execution = db.get(RundownEventExecution, payload["executionId"])
    if not execution:
        raise RuntimeError("rundown graphics execution no longer exists")
    definition = db.get(RundownEventDefinition, execution.definition_id)
    if not definition:
        raise RuntimeError("rundown graphics definition no longer exists")
    delay_ms = max(0, int((_utc(execution.planned_at) - utc_now()).total_seconds() * 1000))
    cue = graphics.create_cue(
        definition.asset_id,
        definition.mode,
        definition.duration_ms,
        delay_ms,
        definition.fade_ms,
        db,
    )
    execution.cue_id = cue.cue.id
    execution.dispatched_at = utc_now()
    execution.state = "dispatched"
    execution.error = None


def dispatch_outbox(db: Session) -> bool:
    """Dispatch pending commands with bounded retry; safe in the one-worker profile."""
    now = utc_now()
    commands = (
        db.query(RundownOutbox)
        .filter(RundownOutbox.state == "pending", RundownOutbox.next_attempt_at <= now)
        .order_by(RundownOutbox.created_at)
        .limit(20)
        .all()
    )
    changed = False
    for outbox in commands:
        payload = json.loads(outbox.payload_json)
        try:
            if outbox.command_type == "playlist_snapshot":
                state = media.state_for(payload["playlistId"], db)
                if state.applied_revision < int(payload["revision"]):
                    continue
            elif outbox.command_type == "graphics_cue":
                _dispatch_graphics(outbox, db)
            else:
                raise RuntimeError(f"unsupported rundown outbox command {outbox.command_type}")
            outbox.state = "dispatched"
            outbox.last_error = None
            changed = True
            db.commit()
        except Exception as exc:
            outbox.attempts += 1
            outbox.last_error = str(getattr(exc, "message", exc))[:1000]
            if outbox.attempts >= MAX_OUTBOX_ATTEMPTS:
                outbox.state = "failed"
            else:
                outbox.next_attempt_at = utc_now() + timedelta(seconds=min(30, 2**outbox.attempts))
            execution_id = payload.get("executionId")
            if execution_id:
                execution = db.get(RundownEventExecution, execution_id)
                if execution:
                    execution.state = "failed" if outbox.state == "failed" else "retrying"
                    execution.error = outbox.last_error
            db.commit()
            changed = True
    return changed


def version_items(version_id: str, db: Session) -> list[RundownVersionItem]:
    return (
        db.query(RundownVersionItem)
        .filter(RundownVersionItem.version_id == version_id)
        .order_by(RundownVersionItem.position)
        .all()
    )


def version_events(version_id: str, db: Session) -> list[RundownEventDefinition]:
    return (
        db.query(RundownEventDefinition)
        .filter(RundownEventDefinition.version_id == version_id)
        .order_by(RundownEventDefinition.key)
        .all()
    )
