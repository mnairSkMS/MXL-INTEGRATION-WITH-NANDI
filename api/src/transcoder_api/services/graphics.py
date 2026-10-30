"""Catalog-driven graphics cues built on the existing scheduled-event API."""

from __future__ import annotations

import json
import os
import pathlib
import uuid
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Any

from sqlalchemy.orm import Session

from ..exceptions import ConflictException, NotFoundException, UnprocessableException
from ..model.db import GraphicsCue
from ..model.rest_api import EventList
from . import schedule


DEFAULT_CATALOG = (
    pathlib.Path(__file__).resolve().parent.parent / "assets" / "graphics_catalog.json"
)


def catalog_path() -> pathlib.Path:
    return pathlib.Path(os.environ.get("GRAPHICS_CATALOG_PATH", str(DEFAULT_CATALOG)))


def _validate_rect(rect: dict, field: str, asset_id: str) -> None:
    if any(not 0.0 <= rect[key] <= 1.0 for key in ("x", "y", "width", "height") if key in rect):
        raise UnprocessableException(
            f"graphics asset {asset_id} has a {field} outside 0..1; "
            "geometry is a fraction of the output frame, not pixels"
        )


def catalog() -> dict:
    try:
        data = json.loads(catalog_path().read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise UnprocessableException("graphics catalog is unavailable or invalid") from exc
    if not isinstance(data.get("assets"), list):
        raise UnprocessableException("graphics catalog must contain an assets array")
    video_enabled = os.environ.get("ENABLE_VIDEO_GRAPHICS", "false").lower() in {
        "1", "true", "yes", "on"
    }
    for asset in data["assets"]:
        default_mode = asset.get("defaultMode")
        if default_mode is not None and default_mode not in asset.get("supportedModes", []):
            raise UnprocessableException(
                f"graphics asset {asset.get('id', '<unknown>')} has an unsupported defaultMode"
            )
        for field in ("geometry", "squeezeWindow"):
            if asset.get(field) is not None:
                _validate_rect(asset[field], field, asset.get("id", "<unknown>"))
        asset["enabled"] = asset.get("mediaType", "image") != "video" or video_enabled
    return data


def _asset(asset_id: str) -> tuple[dict, dict]:
    data = catalog()
    asset = next((value for value in data["assets"] if value.get("id") == asset_id), None)
    if not asset:
        raise NotFoundException("graphics asset does not exist")
    required = {"id", "kind", "mediaUri", "geometry", "supportedModes"}
    if not required.issubset(asset):
        raise UnprocessableException("graphics asset is incomplete")
    return data, asset


@dataclass(frozen=True)
class CueStatus:
    cue: GraphicsCue
    kind: str
    mode: str
    phase: str
    starts_at: datetime
    ends_at: datetime | None
    restore_until: datetime | None


def _utc(value: datetime | None) -> datetime | None:
    if value is None:
        return None
    return value.replace(tzinfo=timezone.utc) if value.tzinfo is None else value.astimezone(timezone.utc)


def _cleanup_expired(db: Session, now: datetime) -> None:
    changed = False
    for cue in db.query(GraphicsCue).all():
        restore_until = _utc(cue.restore_until)
        # Rows created by the earlier cue implementation carry no lifecycle
        # information and cannot safely own the sponsor lock after an API restart.
        if restore_until is None and cue.starts_at is None and cue.kind is None:
            db.delete(cue)
            changed = True
        elif restore_until is not None and restore_until <= now:
            db.delete(cue)
            changed = True
    if changed:
        db.commit()


def _status(cue: GraphicsCue, now: datetime) -> CueStatus:
    starts_at = _utc(cue.starts_at) or _utc(cue.created_at) or now
    ends_at = _utc(cue.ends_at)
    restore_until = _utc(cue.restore_until)
    if now < starts_at:
        phase = "scheduled"
    elif ends_at is not None and now >= ends_at:
        phase = "restoring"
    else:
        phase = "on_air"
    return CueStatus(
        cue=cue,
        kind=cue.kind or "overlay",
        mode=cue.mode or "snap",
        phase=phase,
        starts_at=starts_at,
        ends_at=ends_at,
        restore_until=restore_until,
    )


def list_cues(db: Session) -> list[CueStatus]:
    now = datetime.now(timezone.utc)
    _cleanup_expired(db, now)
    return [
        _status(cue, now)
        for cue in db.query(GraphicsCue).order_by(GraphicsCue.created_at.desc()).all()
    ]


def create_cue(
    asset_id: str, mode: str, duration_ms: int | None, start_in_ms: int, fade_ms: int,
    db: Session
) -> CueStatus:
    _, asset = _asset(asset_id)
    if not asset.get("enabled", True):
        raise UnprocessableException("video graphics are disabled until their runtime gate passes")
    if mode not in asset["supportedModes"]:
        raise UnprocessableException("animation mode is not supported by this asset")
    kind = asset["kind"]
    if duration_ms is None:
        duration_ms = asset.get("defaultDurationMs")
    if kind in {"lband", "aston"} and duration_ms is None:
        duration_ms = asset.get("clipDurationMs", 15000)
    if (duration_ms is not None and duration_ms <= 0) or fade_ms < 0 or start_in_ms < 0:
        raise UnprocessableException("duration, fade, and start delay must be valid")
    if duration_ms is None and kind not in {"logo", "bug"}:
        raise UnprocessableException("only Logo and Bug may hold without a duration")
    if (
        duration_ms is not None
        and mode in {"fade", "shrink_overlay", "reveal"}
        and 2 * fade_ms >= duration_ms
    ):
        raise UnprocessableException("duration must leave time for both animation legs")

    now = datetime.now(timezone.utc)
    _cleanup_expired(db, now)
    if kind in {"lband", "aston"}:
        active_sponsor = (
            db.query(GraphicsCue)
            .filter(GraphicsCue.kind.in_(["lband", "aston"]))
            .first()
        )
        if active_sponsor:
            raise ConflictException("an L-band or Aston cue is already active; stop it first")
    # A zero delay is represented by an omitted event time. The scheduler interprets
    # this as immediate without racing an explicitly generated `now` timestamp.
    starts_at = (
        None
        if start_in_ms == 0
        else now + timedelta(milliseconds=start_in_ms)
    )
    effective_start = starts_at or now
    overlay: dict[str, Any] = {
        "geometry_norm": asset["geometry"],
        "image_urls": [asset["mediaUri"]],
        "opacity": 1.0,
        # Static images otherwise default to one buffer per second.  Feed them at
        # the configured 25 fps output cadence so the compositor remains supplied
        # while the programme pad is resized for an L-band/Aston cue.
        "delay_between_images": 40,
        "fade_in": fade_ms if mode in {"fade", "shrink_overlay"} else 0,
        "fade_out": fade_ms if mode in {"fade", "shrink_overlay"} else 0,
    }
    # Engine event duration is the point at which fade-out starts.  The UI/API
    # duration is the complete visual interval, including fade/reveal in and
    # out, so subtract the closing leg from the engine event where applicable.
    engine_duration_ms = duration_ms
    if duration_ms is not None and mode in {"fade", "shrink_overlay", "reveal"}:
        engine_duration_ms = duration_ms - fade_ms
    if engine_duration_ms is not None:
        overlay["duration"] = engine_duration_ms
    events: list[dict[str, Any]] = []
    visual_life_ms = duration_ms
    if mode == "reveal":
        assert duration_ms is not None
        overlay["z_order"] = 0
        # The behind-program layer remains only through the restoration margin;
        # the visible reveal itself still ends at duration_ms.
        overlay["duration"] = duration_ms + 1200
    events.append(
        {
            "type": "image_overlay",
            "time": starts_at.isoformat() if starts_at is not None else None,
            "details": {"image_overlay": overlay},
        }
    )
    if mode in {"snap", "shrink_overlay", "reveal"} and asset["kind"] in {"lband", "aston"}:
        assert duration_ms is not None
        squeeze_duration = duration_ms - fade_ms if mode in {"shrink_overlay", "reveal"} else duration_ms
        squeeze = {"duration": squeeze_duration, "fade_in": 0, "fade_out": 0}
        if mode in {"shrink_overlay", "reveal"}:
            squeeze["fade_in"] = fade_ms
            squeeze["fade_out"] = fade_ms
        if asset.get("squeezeName"):
            squeeze["squeeze_name"] = asset["squeezeName"]
        elif asset.get("squeezeWindow"):
            squeeze["squeeze_name"] = ""
            squeeze["input_window_norm"] = {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0}
            squeeze["output_window_norm"] = asset["squeezeWindow"]
        else:
            raise UnprocessableException("squeeze graphics need squeezeName or squeezeWindow")
        events.append(
            {
                "type": "squeeze",
                "time": starts_at.isoformat() if starts_at is not None else None,
                "details": {"squeeze": squeeze},
            }
        )
    created = schedule.create_or_update_events(EventList.model_validate(events), db)
    cue = GraphicsCue(
        id=str(uuid.uuid4()),
        asset_id=asset_id,
        event_ids=json.dumps([event.id.root for event in created]),
        kind=kind,
        mode=mode,
        starts_at=effective_start,
        ends_at=(
            effective_start + timedelta(milliseconds=visual_life_ms)
            if visual_life_ms is not None else None
        ),
        restore_until=(
            effective_start + timedelta(milliseconds=visual_life_ms + 1200)
            if visual_life_ms is not None and kind in {"lband", "aston"}
            else (
                effective_start + timedelta(milliseconds=visual_life_ms)
                if visual_life_ms is not None else None
            )
        ),
    )
    db.add(cue)
    db.commit()
    db.refresh(cue)
    return _status(cue, now)


def cancel_cue(cue_id: str, db: Session, *, commit: bool = True) -> None:
    cue = db.get(GraphicsCue, cue_id)
    if not cue:
        raise NotFoundException("graphics cue does not exist")
    for event_id in json.loads(cue.event_ids):
        try:
            schedule.delete_event(event_id, db, commit=False)
        except Exception:
            # An event may already have expired or been removed by another operator.
            pass
    db.delete(cue)
    if commit:
        db.commit()
    else:
        db.flush()
