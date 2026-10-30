import json
from datetime import datetime, timezone
from typing import Optional

from fastapi.encoders import jsonable_encoder
from pydantic import BaseModel
from sqlalchemy import Boolean, ForeignKey, Integer, String, Text, UniqueConstraint, func
from sqlalchemy.orm import DeclarativeBase, mapped_column, Mapped
from sqlalchemy.dialects.sqlite import DATETIME


ALCHEMY_DATE_FORMAT = (
    "%(year)04d-%(month)02d-%(day)02d " "%(hour)02d:%(minute)02d:%(second)02d.%(microsecond)06dZ"
)
SQLITE_DATE_FORMAT = "%Y-%m-%d %H:%M:%fZ"

dt = DATETIME(storage_format=ALCHEMY_DATE_FORMAT)


RUNDOWN_TABLE_NAMES = {
    "schema_migrations",
    "rundowns",
    "rundown_versions",
    "rundown_version_items",
    "rundown_event_definitions",
    "playback_occurrences",
    "rundown_event_executions",
    "rundown_outbox",
    "rundown_audit_log",
}


class Base(DeclarativeBase):
    pass


def db_json_serialize(value: BaseModel) -> str:
    value_dict = value.dict()
    value_dict = jsonable_encoder(
        value_dict,
        custom_encoder={
            datetime: lambda x: x.astimezone(timezone.utc).strftime("%Y-%m-%d %H:%M:%S.%fZ")
        },
    )
    return json.dumps(value_dict)


class Event(Base):
    __tablename__ = "events"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, index=True)
    type: Mapped[str] = mapped_column(String, index=True, nullable=False)
    time: Mapped[datetime] = mapped_column(dt, index=True, nullable=False)
    end_time: Mapped[Optional[datetime]] = mapped_column(dt, index=True, nullable=True)
    details: Mapped[str] = mapped_column(String, nullable=False)
    creation_time: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )
    read_time: Mapped[datetime] = mapped_column(dt, nullable=True)


class MediaAsset(Base):
    __tablename__ = "media_assets"

    id: Mapped[str] = mapped_column(String, primary_key=True)
    relative_path: Mapped[str] = mapped_column(String, unique=True, nullable=False)
    display_name: Mapped[str] = mapped_column(String, nullable=False)
    size_bytes: Mapped[int] = mapped_column(Integer, nullable=False)
    sha256: Mapped[str] = mapped_column(String, nullable=False)
    probe_json: Mapped[str] = mapped_column(Text, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )


class PlaylistState(Base):
    __tablename__ = "playlist_state"

    playlist_id: Mapped[str] = mapped_column(String, primary_key=True)
    run_state: Mapped[str] = mapped_column(String, nullable=False, default="running")
    revision: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    applied_revision: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    skip_seq: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    apply_error: Mapped[Optional[str]] = mapped_column(Text, nullable=True)
    active_input_id: Mapped[Optional[str]] = mapped_column(String, nullable=True)


class PlaylistItem(Base):
    __tablename__ = "playlist_items"
    __table_args__ = (UniqueConstraint("playlist_id", "position", name="playlist_position"),)

    id: Mapped[str] = mapped_column(String, primary_key=True)
    playlist_id: Mapped[str] = mapped_column(String, index=True, nullable=False)
    media_id: Mapped[str] = mapped_column(ForeignKey("media_assets.id"), index=True, nullable=False)
    position: Mapped[int] = mapped_column(Integer, nullable=False)
    state: Mapped[str] = mapped_column(String, index=True, nullable=False, default="queued")
    error: Mapped[Optional[str]] = mapped_column(String, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )
    started_at: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)
    completed_at: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)


class GraphicsCue(Base):
    __tablename__ = "graphics_cues"

    id: Mapped[str] = mapped_column(String, primary_key=True)
    asset_id: Mapped[str] = mapped_column(String, nullable=False)
    event_ids: Mapped[str] = mapped_column(Text, nullable=False)
    kind: Mapped[Optional[str]] = mapped_column(String, nullable=True)
    mode: Mapped[Optional[str]] = mapped_column(String, nullable=True)
    starts_at: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)
    ends_at: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)
    restore_until: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )


# The rundown service is intentionally additive.  It never changes the schema
# or meaning of the pre-existing events/media_assets/playlist_* tables: those
# remain the execution projection consumed by the current transcoder.
class SchemaMigration(Base):
    __tablename__ = "schema_migrations"

    version: Mapped[str] = mapped_column(String, primary_key=True)
    applied_at: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )


class Rundown(Base):
    __tablename__ = "rundowns"

    id: Mapped[str] = mapped_column(String, primary_key=True)
    name: Mapped[str] = mapped_column(String, nullable=False)
    timezone: Mapped[str] = mapped_column(String, nullable=False, default="UTC")
    playlist_id: Mapped[str] = mapped_column(String, nullable=False, default="main")
    active_version_id: Mapped[Optional[str]] = mapped_column(String, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )
    updated_at: Mapped[datetime] = mapped_column(
        dt,
        server_default=func.strftime(SQLITE_DATE_FORMAT, "now"),
        onupdate=func.strftime(SQLITE_DATE_FORMAT, "now"),
    )


class RundownVersion(Base):
    __tablename__ = "rundown_versions"
    __table_args__ = (UniqueConstraint("rundown_id", "number", name="rundown_version_number"),)

    id: Mapped[str] = mapped_column(String, primary_key=True)
    rundown_id: Mapped[str] = mapped_column(ForeignKey("rundowns.id"), index=True, nullable=False)
    # Publish generation. Authoring saves no longer consume numbers, so this
    # counts on-air changes rather than keystrokes.
    number: Mapped[int] = mapped_column(Integer, nullable=False)
    state: Mapped[str] = mapped_column(String, index=True, nullable=False, default="published")
    source_yaml: Mapped[str] = mapped_column(Text, nullable=False)
    checksum: Mapped[str] = mapped_column(String, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )
    published_at: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)


class RundownVersionItem(Base):
    __tablename__ = "rundown_version_items"
    __table_args__ = (
        UniqueConstraint("version_id", "key", name="rundown_version_item_key"),
        UniqueConstraint("version_id", "position", name="rundown_version_item_position"),
    )

    id: Mapped[str] = mapped_column(String, primary_key=True)
    version_id: Mapped[str] = mapped_column(
        ForeignKey("rundown_versions.id"), index=True, nullable=False
    )
    # This ID is projected into playlist_items when a version is published.
    # It is UUIDv5(rundown-id, item-key), therefore survives a YAML re-import.
    stable_item_id: Mapped[str] = mapped_column(String, index=True, nullable=False)
    key: Mapped[str] = mapped_column(String, nullable=False)
    media_id: Mapped[str] = mapped_column(ForeignKey("media_assets.id"), index=True, nullable=False)
    position: Mapped[int] = mapped_column(Integer, nullable=False)


class RundownEventDefinition(Base):
    __tablename__ = "rundown_event_definitions"
    __table_args__ = (UniqueConstraint("version_id", "key", name="rundown_event_key"),)

    id: Mapped[str] = mapped_column(String, primary_key=True)
    version_id: Mapped[str] = mapped_column(
        ForeignKey("rundown_versions.id"), index=True, nullable=False
    )
    item_id: Mapped[Optional[str]] = mapped_column(
        ForeignKey("rundown_version_items.id"), index=True, nullable=True
    )
    key: Mapped[str] = mapped_column(String, nullable=False)
    asset_id: Mapped[str] = mapped_column(String, nullable=False)
    mode: Mapped[str] = mapped_column(String, nullable=False)
    duration_ms: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    fade_ms: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    trigger_type: Mapped[str] = mapped_column(String, nullable=False)
    offset_ms: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    starts_at: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)
    enabled: Mapped[bool] = mapped_column(Boolean, nullable=False, default=True)


class PlaybackOccurrence(Base):
    __tablename__ = "playback_occurrences"
    __table_args__ = (
        UniqueConstraint("version_id", "playlist_item_id", "sequence", name="rundown_occurrence"),
    )

    id: Mapped[str] = mapped_column(String, primary_key=True)
    rundown_id: Mapped[str] = mapped_column(ForeignKey("rundowns.id"), index=True, nullable=False)
    version_id: Mapped[str] = mapped_column(
        ForeignKey("rundown_versions.id"), index=True, nullable=False
    )
    playlist_item_id: Mapped[str] = mapped_column(String, index=True, nullable=False)
    sequence: Mapped[int] = mapped_column(Integer, nullable=False)
    observed_at: Mapped[datetime] = mapped_column(dt, nullable=False)
    ended_at: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)


class RundownEventExecution(Base):
    __tablename__ = "rundown_event_executions"

    id: Mapped[str] = mapped_column(String, primary_key=True)
    execution_key: Mapped[str] = mapped_column(String, unique=True, nullable=False)
    definition_id: Mapped[str] = mapped_column(
        ForeignKey("rundown_event_definitions.id"), index=True, nullable=False
    )
    occurrence_id: Mapped[Optional[str]] = mapped_column(
        ForeignKey("playback_occurrences.id"), index=True, nullable=True
    )
    planned_at: Mapped[datetime] = mapped_column(dt, nullable=False)
    dispatched_at: Mapped[Optional[datetime]] = mapped_column(dt, nullable=True)
    cue_id: Mapped[Optional[str]] = mapped_column(ForeignKey("graphics_cues.id"), nullable=True)
    state: Mapped[str] = mapped_column(String, index=True, nullable=False, default="pending")
    error: Mapped[Optional[str]] = mapped_column(Text, nullable=True)


class RundownOutbox(Base):
    __tablename__ = "rundown_outbox"

    id: Mapped[str] = mapped_column(String, primary_key=True)
    dedupe_key: Mapped[str] = mapped_column(String, unique=True, nullable=False)
    command_type: Mapped[str] = mapped_column(String, index=True, nullable=False)
    payload_json: Mapped[str] = mapped_column(Text, nullable=False)
    state: Mapped[str] = mapped_column(String, index=True, nullable=False, default="pending")
    attempts: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    next_attempt_at: Mapped[datetime] = mapped_column(dt, nullable=False)
    last_error: Mapped[Optional[str]] = mapped_column(Text, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )


class RundownAuditLog(Base):
    __tablename__ = "rundown_audit_log"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, index=True)
    rundown_id: Mapped[Optional[str]] = mapped_column(
        ForeignKey("rundowns.id"), index=True, nullable=True
    )
    action: Mapped[str] = mapped_column(String, nullable=False)
    subject_type: Mapped[str] = mapped_column(String, nullable=False)
    subject_id: Mapped[str] = mapped_column(String, nullable=False)
    details_json: Mapped[str] = mapped_column(Text, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        dt, server_default=func.strftime(SQLITE_DATE_FORMAT, "now")
    )
