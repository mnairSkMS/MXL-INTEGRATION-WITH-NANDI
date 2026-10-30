"""Optional versioned rundown authoring routes."""

from __future__ import annotations

import json
from datetime import datetime

from fastapi import (
    APIRouter,
    Depends,
    File,
    Form,
    HTTPException,
    UploadFile,
    status,
)
from pydantic import BaseModel, Field
from sqlalchemy.orm import Session

from ..db import get_db
from ..model.db import (
    PlaybackOccurrence,
    RundownAuditLog,
    RundownEventDefinition,
    RundownEventExecution,
    RundownVersion,
)
from ..services import bootstrap, rundown


router = APIRouter(tags=["rundown"])


class ApplyRequest(BaseModel):
    """One atomic, validated change to the live schedule.

    The operations apply in order to an in-memory copy of the live version and
    the result is validated once, so an edit that is only legal as a whole never
    has to pass through a rejected intermediate state.
    """

    # Exactly one of these. `ops` is the surgical path; `document` is for a
    # client that already holds the complete end state, as the operator UI does.
    ops: list[dict] | None = None
    document: dict | None = None
    # Null forces the change through; any other value must match what is on air.
    expectedVersionId: str | None = None
    dryRun: bool = False


class SeedRequest(BaseModel):
    name: str | None = Field(default=None, max_length=256)


class ResetRequest(BaseModel):
    expectedVersionId: str = Field(min_length=1)


class TimeResolutionRequest(BaseModel):
    date: str = Field(pattern=r"^\d{4}-\d{2}-\d{2}$")
    time: str = Field(min_length=5, max_length=12)


class TimeResolutionResponse(BaseModel):
    startsAt: datetime
    localTime: datetime
    timezone: str
    abbreviation: str
    utcOffset: str


class RundownStatus(BaseModel):
    enabled: bool
    observationIntervalMs: int
    timingCapability: str
    # Governance of the declarative channel file.  Defaulted so that clients
    # written against the original three-field contract keep working.
    bootstrapManaged: bool = False
    bootstrapFile: str | None = None
    bootstrapWriteback: bool = False
    bootstrapState: str = "absent"
    bootstrapAppliedAt: datetime | None = None
    bootstrapChecksum: str | None = None
    bootstrapError: str | None = None
    bootstrapWarnings: list[str] = []


class VersionSummary(BaseModel):
    id: str
    number: int
    state: str
    checksum: str
    createdAt: datetime | None
    publishedAt: datetime | None

    @classmethod
    def of(cls, row: RundownVersion) -> "VersionSummary":
        return cls(
            id=row.id,
            number=row.number,
            state=row.state,
            checksum=row.checksum,
            createdAt=row.created_at,
            publishedAt=row.published_at,
        )


class RundownItemModel(BaseModel):
    key: str
    stableItemId: str
    mediaId: str
    position: int
    relativePath: str | None = None
    displayName: str | None = None
    durationMs: int | None = None
    timelineStartMs: int | None = None


class RundownEventModel(BaseModel):
    definitionId: str | None = None
    key: str
    itemKey: str | None = None
    kind: str | None = None
    source: str | None = None
    assetId: str
    mode: str
    durationMs: int | None
    fadeMs: int
    triggerType: str
    offsetMs: int | None
    startsAt: datetime | None


class VersionDetail(VersionSummary):
    items: list[RundownItemModel]
    graphics: list[RundownEventModel]


class PublishResult(VersionDetail):
    # Reports the writeback separately from the publish itself: the schedule can
    # be on air while the channel file swap failed, and an operator needs to
    # know that the change will not survive a restart.
    channelFile: dict


class ApplyResult(BaseModel):
    """The outcome of one atomic change.

    `version` is null for a dry run, which validates the merged document and
    rolls back without minting a version or touching the channel file.
    """

    version: VersionDetail | None = None
    applied: dict[str, int]
    channelFile: dict = {}
    warnings: list[str] = []


class RundownDetail(BaseModel):
    id: str
    name: str
    timezone: str
    playlistId: str
    activeVersionId: str | None
    versions: list[VersionSummary]


class ClockModel(BaseModel):
    """The loop clock the operator grid projects program start times from.

    Every field is nullable or zero before the observer has seen a transition:
    a freshly published version has no occurrence yet, and a rundown whose media
    has no probed duration has no loop length to count cycles against.
    """

    syncedAt: datetime | None
    currentItemId: str | None
    currentStartedAt: datetime | None
    loopDurationMs: int
    cycle: int
    serverNow: datetime


class ResetResult(BaseModel):
    version: VersionDetail
    removedEventCount: int
    channelFile: dict


class OccurrenceModel(BaseModel):
    id: str
    versionId: str
    playlistItemId: str
    sequence: int
    observedAt: datetime
    endedAt: datetime | None


class ExecutionModel(BaseModel):
    id: str
    definitionId: str
    occurrenceId: str | None
    plannedAt: datetime
    dispatchedAt: datetime | None
    cueId: str | None
    state: str
    error: str | None


class AuditModel(BaseModel):
    id: int
    action: str
    subjectType: str
    subjectId: str
    details: dict
    createdAt: datetime | None


def _version_detail(row: RundownVersion, db: Session) -> VersionDetail:
    structured = rundown.document_from_version(row, db)
    items_by_key = {item["key"]: item for item in structured["items"]}
    event_item = {
        event["key"]: item["key"]
        for item in structured["items"] for event in item["graphics"]
    }
    return VersionDetail(
        **VersionSummary.of(row).model_dump(),
        items=[
            RundownItemModel(
                key=item.key,
                stableItemId=item.stable_item_id,
                mediaId=item.media_id,
                position=item.position,
                relativePath=items_by_key[item.key]["relativePath"],
                displayName=items_by_key[item.key]["displayName"],
                durationMs=items_by_key[item.key]["durationMs"],
                timelineStartMs=items_by_key[item.key]["timelineStartMs"],
            )
            for item in rundown.version_items(row.id, db)
        ],
        graphics=[
            RundownEventModel(
                definitionId=event.id,
                key=event.key,
                itemKey=event_item.get(event.key),
                kind=graphics_kind(event.asset_id),
                source="rundown",
                assetId=event.asset_id,
                mode=event.mode,
                durationMs=event.duration_ms,
                fadeMs=event.fade_ms,
                triggerType=event.trigger_type,
                offsetMs=event.offset_ms,
                startsAt=event.starts_at,
            )
            for event in rundown.version_events(row.id, db)
        ],
    )


def _apply_result(
    version: RundownVersion | None,
    counts: dict[str, int],
    channel_file: dict,
    warnings: list[str],
    db: Session,
) -> ApplyResult:
    return ApplyResult(
        version=_version_detail(version, db) if version is not None else None,
        applied=counts,
        channelFile=channel_file,
        warnings=warnings,
    )


def _enabled() -> None:
    rundown.require_enabled()


def graphics_kind(asset_id: str) -> str:
    return rundown.graphics._asset(asset_id)[1]["kind"]


@router.get("/rundowns/status", response_model=RundownStatus)
def rundown_status(db: Session = Depends(get_db)):
    # Deliberately reachable while the service is disabled, so an operator can
    # always discover why the rundown workspace is unavailable.
    return RundownStatus(
        enabled=rundown.enabled(), observationIntervalMs=250,
        timingCapability="observed playlist transitions; not frame-accurate timecode",
        **bootstrap.status(db),
    )


@router.post("/rundowns/{rundown_id}/time/resolve", response_model=TimeResolutionResponse)
def resolve_time(
    rundown_id: str, body: TimeResolutionRequest, db: Session = Depends(get_db)
):
    _enabled()
    starts_at, local_value, timezone_name = rundown.resolve_local_wall_clock_time(
        rundown_id, body.date, body.time, db
    )
    offset = local_value.strftime("%z")
    return TimeResolutionResponse(
        startsAt=starts_at,
        localTime=local_value,
        timezone=timezone_name,
        abbreviation=local_value.tzname() or timezone_name,
        utcOffset=f"{offset[:3]}:{offset[3:]}",
    )


@router.post(
    "/rundowns/{rundown_id}/apply",
    response_model=ApplyResult,
    status_code=status.HTTP_202_ACCEPTED,
)
def apply_rundown(rundown_id: str, body: ApplyRequest, db: Session = Depends(get_db)):
    """Change the live schedule in one atomic, validated call.

    Every operation lands or none does.  `dryRun` validates the merged document
    and rolls back, which is how a client checks an edit before committing to it.
    """
    _enabled()
    # Presence, not truthiness: an empty `document: {}` is still a document the
    # caller meant to send, and testing it for truth let it through alongside
    # `ops`.  Emptiness is the service layer's problem to reject, not this one's.
    if (body.ops is None) == (body.document is None):
        raise HTTPException(
            status_code=422, detail="provide exactly one of ops or document"
        )
    if body.ops is not None:
        outcome = rundown.apply_operations(
            rundown_id,
            db,
            body.ops,
            expected_version_id=body.expectedVersionId,
            dry_run=body.dryRun,
        )
    else:
        outcome = rundown.apply_document(
            rundown_id,
            db,
            body.document or {},
            expected_version_id=body.expectedVersionId,
            dry_run=body.dryRun,
        )
    return _apply_result(*outcome, db)


@router.post(
    "/rundowns/{rundown_id}/programs/import",
    response_model=ApplyResult,
    status_code=status.HTTP_202_ACCEPTED,
)
async def import_programs(
    rundown_id: str,
    file: UploadFile = File(...),
    position: int | None = Form(default=None),
    expectedVersionId: str | None = Form(default=None),
    dryRun: bool = Form(default=False),
    db: Session = Depends(get_db),
):
    """Splice a channel YAML's programs into the live rundown.

    The file is the same format as the declarative channel file, and it names
    media by relative path: this imports a program *list*, not the media itself,
    so upload the clips with `POST /media` first.
    """
    _enabled()
    raw = await file.read()
    if len(raw) > rundown.MAX_YAML_BYTES:
        raise HTTPException(status_code=422, detail="file exceeds the 1 MiB authoring limit")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise HTTPException(status_code=422, detail="file must be UTF-8 text") from exc
    version, counts, channel_file, warnings = rundown.import_programs(
        rundown_id,
        db,
        text,
        position=position,
        expected_version_id=expectedVersionId,
        dry_run=dryRun,
    )
    return _apply_result(version, counts, channel_file, warnings, db)


@router.post(
    "/rundowns/{rundown_id}/seed",
    response_model=PublishResult,
    status_code=status.HTTP_201_CREATED,
)
def seed_rundown(
    rundown_id: str, body: SeedRequest | None = None, db: Session = Depends(get_db)
):
    """Publish a first version from the playlist that is already running.

    `apply` edits the live version, so a channel that has never published one
    needs this way in.
    """
    _enabled()
    version, channel_file = rundown.seed_from_playlist(
        rundown_id, db, name=body.name if body else None
    )
    return PublishResult(
        **_version_detail(version, db).model_dump(), channelFile=channel_file
    )


@router.get("/rundowns/{rundown_id}/versions/{version_id}")
def get_version_document(rundown_id: str, version_id: str, db: Session = Depends(get_db)):
    _enabled()
    version = rundown.get_version(version_id, db)
    if version.rundown_id != rundown_id:
        from ..exceptions import NotFoundException
        raise NotFoundException("rundown version does not belong to rundown")
    return rundown.document_from_version(version, db)


@router.get("/rundowns/{rundown_id}", response_model=RundownDetail)
def get_rundown(rundown_id: str, db: Session = Depends(get_db)):
    _enabled()
    record = rundown.get_rundown(rundown_id, db)
    # `versions` is a publish history: every row in it was on air at some point.
    versions = (
        db.query(RundownVersion)
        .filter(RundownVersion.rundown_id == rundown_id)
        .order_by(RundownVersion.number.desc())
        .all()
    )
    return RundownDetail(
        id=record.id,
        name=record.name,
        timezone=record.timezone,
        playlistId=record.playlist_id,
        activeVersionId=record.active_version_id,
        versions=[VersionSummary.of(version) for version in versions],
    )


@router.get("/rundowns/{rundown_id}/clock", response_model=ClockModel)
def rundown_clock(rundown_id: str, db: Session = Depends(get_db)):
    """Anchor for the operator grid: when this schedule synced and what is on now.

    `serverNow` is returned so the browser can correct for clock skew between the
    container and the operator's machine; without it every projected row time is
    wrong by that difference.
    """
    _enabled()
    return ClockModel(**rundown.clock_for(rundown_id, db))


@router.post(
    "/rundowns/{rundown_id}/reset",
    response_model=ResetResult,
    status_code=status.HTTP_202_ACCEPTED,
)
def reset_rundown(rundown_id: str, body: ResetRequest, db: Session = Depends(get_db)):
    """Immediately publish a programs-only replacement of what is on air."""
    _enabled()
    version, removed, channel_file = rundown.reset_and_publish(
        rundown_id, db, body.expectedVersionId
    )
    return ResetResult(
        version=_version_detail(version, db),
        removedEventCount=removed,
        channelFile=channel_file,
    )


@router.get("/rundowns/{rundown_id}/occurrences", response_model=list[OccurrenceModel])
def list_occurrences(rundown_id: str, db: Session = Depends(get_db)):
    _enabled()
    rundown.get_rundown(rundown_id, db)
    rows = (
        db.query(PlaybackOccurrence)
        .filter(PlaybackOccurrence.rundown_id == rundown_id)
        .order_by(PlaybackOccurrence.observed_at.desc())
        .limit(200)
        .all()
    )
    return [
        OccurrenceModel(
            id=row.id,
            versionId=row.version_id,
            playlistItemId=row.playlist_item_id,
            sequence=row.sequence,
            observedAt=row.observed_at,
            endedAt=row.ended_at,
        )
        for row in rows
    ]


@router.get("/rundowns/{rundown_id}/executions", response_model=list[ExecutionModel])
def list_executions(rundown_id: str, db: Session = Depends(get_db)):
    _enabled()
    rundown.get_rundown(rundown_id, db)
    rows = (
        db.query(RundownEventExecution)
        .join(
            RundownEventDefinition, RundownEventExecution.definition_id == RundownEventDefinition.id
        )
        .join(RundownVersion, RundownEventDefinition.version_id == RundownVersion.id)
        .filter(RundownVersion.rundown_id == rundown_id)
        .order_by(RundownEventExecution.planned_at.desc())
        .limit(200)
        .all()
    )
    return [
        ExecutionModel(
            id=row.id,
            definitionId=row.definition_id,
            occurrenceId=row.occurrence_id,
            plannedAt=row.planned_at,
            dispatchedAt=row.dispatched_at,
            cueId=row.cue_id,
            state=row.state,
            error=row.error,
        )
        for row in rows
    ]


@router.get("/rundowns/{rundown_id}/audit", response_model=list[AuditModel])
def list_audit(rundown_id: str, db: Session = Depends(get_db)):
    _enabled()
    rundown.get_rundown(rundown_id, db)
    rows = (
        db.query(RundownAuditLog)
        .filter(RundownAuditLog.rundown_id == rundown_id)
        .order_by(RundownAuditLog.id.desc())
        .limit(200)
        .all()
    )
    return [
        AuditModel(
            id=row.id,
            action=row.action,
            subjectType=row.subject_type,
            subjectId=row.subject_id,
            details=json.loads(row.details_json),
            createdAt=row.created_at,
        )
        for row in rows
    ]
