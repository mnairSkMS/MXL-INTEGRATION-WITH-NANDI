from __future__ import annotations

import json
import pathlib
from typing import Literal

from fastapi import APIRouter, Depends, File, HTTPException, Query, Response, UploadFile, status
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field
from sqlalchemy import text
from sqlalchemy.orm import Session

from ..db import get_db
from ..exceptions import TranscoderApiException, UnprocessableException
from ..model.db import MediaAsset, PlaylistItem, PlaylistState
from ..services import media
from ..services import rundown as rundown_service

router = APIRouter(tags=["playout"])


def _lock_mutation(db: Session) -> None:
    """Serialize revision check + mutation for SQLite optimistic concurrency."""
    db.execute(text("BEGIN IMMEDIATE"))


class MediaModel(BaseModel):
    id: str
    name: str
    relativePath: str
    sizeBytes: int
    probe: dict

    @classmethod
    def of(cls, asset: MediaAsset) -> "MediaModel":
        return cls(
            id=asset.id,
            name=asset.display_name,
            relativePath=asset.relative_path,
            sizeBytes=asset.size_bytes,
            probe=json.loads(asset.probe_json),
        )


class MediaUploadResult(BaseModel):
    filename: str
    status: int  # the status this file would have got had it been uploaded alone
    media: MediaModel | None = None
    error: str | None = None


class MediaBatchResult(BaseModel):
    created: int
    failed: int
    results: list[MediaUploadResult]


class ImportRequest(BaseModel):
    relativePath: str


class EnqueueRequest(BaseModel):
    mediaId: str
    position: int | None = Field(default=None, ge=0)
    expectedRevision: int = Field(ge=0)


class MoveRequest(BaseModel):
    position: int = Field(ge=0)
    expectedRevision: int = Field(ge=0)


class PlaylistItemModel(BaseModel):
    id: str
    media: MediaModel
    position: int
    state: str
    error: str | None


class PlaylistSnapshot(BaseModel):
    id: str
    runState: str
    desiredRevision: int
    appliedRevision: int
    skipSequence: int
    currentItemId: str | None
    applyError: str | None
    items: list[PlaylistItemModel]


def _snapshot(playlist_id: str, db: Session) -> PlaylistSnapshot:
    state = media.state_for(playlist_id, db)
    items = media.queue_items(playlist_id, db)
    asset_ids = [item.media_id for item in items]
    assets = (
        {
            asset.id: asset
            for asset in db.query(MediaAsset).filter(MediaAsset.id.in_(asset_ids)).all()
        }
        if asset_ids
        else {}
    )
    models = [
        PlaylistItemModel(
            id=item.id,
            media=MediaModel.of(assets[item.media_id]),
            position=item.position,
            state=item.state,
            error=item.error,
        )
        for item in items
    ]
    current = next((item.id for item in items if item.state == "on_air"), None)
    return PlaylistSnapshot(
        id=playlist_id,
        runState=state.run_state,
        desiredRevision=state.revision,
        appliedRevision=state.applied_revision,
        skipSequence=state.skip_seq,
        currentItemId=current,
        items=models,
        applyError=state.apply_error,
    )


@router.get("/media", response_model=list[MediaModel])
def list_media(db: Session = Depends(get_db)):
    return [MediaModel.of(asset) for asset in media.list_assets(db)]


@router.post("/media/import", response_model=MediaModel, status_code=status.HTTP_201_CREATED)
def import_media(body: ImportRequest, db: Session = Depends(get_db)):
    return MediaModel.of(media.import_asset(body.relativePath, db))


def _chunks(file: UploadFile):
    while True:
        chunk = file.file.read(1024 * 1024)
        if not chunk:
            break
        yield chunk


@router.post(
    "/media",
    response_model=MediaBatchResult,
    status_code=status.HTTP_201_CREATED,
    responses={207: {"model": MediaBatchResult, "description": "At least one file was rejected"}},
)
async def upload_media(
    response: Response, file: list[UploadFile] = File(...), db: Session = Depends(get_db)
):
    """Upload one or more files in one multipart request (repeat the `file` field).

    Each file is validated and stored on its own. A rejected file does not undo the others:
    the reply lists every file's outcome, and the call answers 207 instead of 201 when any
    file failed.
    """
    if len(file) > media.MAX_UPLOAD_FILES:
        raise UnprocessableException(
            f"{len(file)} files in one call; the limit is {media.MAX_UPLOAD_FILES} (MEDIA_MAX_UPLOAD_FILES)"
        )
    results = []
    for upload in file:
        name = upload.filename or "upload.bin"
        try:
            asset = media.store_upload(name, _chunks(upload), db)
            results.append(MediaUploadResult(filename=name, status=201, media=MediaModel.of(asset)))
        except TranscoderApiException as exc:
            db.rollback()
            results.append(MediaUploadResult(filename=name, status=exc.http_code, error=exc.message))
        except OSError as exc:
            db.rollback()
            results.append(MediaUploadResult(filename=name, status=500, error=f"could not store file: {exc.strerror}"))
    failed = sum(1 for result in results if result.media is None)
    if failed:
        response.status_code = status.HTTP_207_MULTI_STATUS
    return MediaBatchResult(created=len(results) - failed, failed=failed, results=results)


@router.get(
    "/media/{media_id}/thumbnail",
    response_class=FileResponse,
    responses={200: {"content": {"image/jpeg": {}}}},
)
def get_media_thumbnail(
    media_id: str,
    at: float | None = Query(default=None, ge=0, description="Seconds from the start; default 10% in, at most 10 s"),
    width: int | None = Query(default=None, ge=16, le=1920, description="Pixels; snapped up to 160/320/480/640/960/1280"),
    db: Session = Depends(get_db),
):
    """One JPEG frame of a media asset, cut once and cached. Cheap enough for library grids."""
    path = media.thumbnail(media_id, at, width, db)
    return FileResponse(path, media_type="image/jpeg", headers={"Cache-Control": "public, max-age=86400"})


@router.delete("/media/{media_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_media(media_id: str, db: Session = Depends(get_db)):
    media.delete_asset(media_id, db)


@router.get("/playlists/{playlist_id}", response_model=PlaylistSnapshot)
def get_playlist(playlist_id: str, db: Session = Depends(get_db)):
    return _snapshot(playlist_id, db)


@router.post(
    "/playlists/{playlist_id}/items",
    response_model=PlaylistSnapshot,
    status_code=status.HTTP_202_ACCEPTED,
)
def add_playlist_item(playlist_id: str, body: EnqueueRequest, db: Session = Depends(get_db)):
    _lock_mutation(db)
    state = media.state_for(playlist_id, db)
    if state.revision != body.expectedRevision:
        raise HTTPException(status_code=409, detail="playlist revision is stale")
    # A published rundown owns the queue: a row inserted straight into the queue
    # table is deleted by the next reconciler tick, because it is absent from the
    # active version. Edit the schedule instead, which reprojects the queue.
    governing = rundown_service.governs_playlist(playlist_id, db)
    if governing is not None:
        rundown_service.enqueue_media(governing, body.mediaId, body.position, db)
    else:
        media.enqueue(playlist_id, body.mediaId, db, body.position)
    return _snapshot(playlist_id, db)


@router.delete(
    "/playlists/{playlist_id}/items/{item_id}",
    response_model=PlaylistSnapshot,
    status_code=status.HTTP_202_ACCEPTED,
)
def remove_playlist_item(
    playlist_id: str, item_id: str, expectedRevision: int, db: Session = Depends(get_db)
):
    _lock_mutation(db)
    if media.state_for(playlist_id, db).revision != expectedRevision:
        raise HTTPException(status_code=409, detail="playlist revision is stale")
    # Do not rely solely on the mirrored SQLite state: a clip can transition
    # between the periodic reconciler polls.  Reject it before changing the
    # desired queue, otherwise an engine-side rejection would strand a revision.
    current_item_id = None
    try:
        _, current_item_id = media._live_input_and_current(playlist_id)
    except Exception as error:
        raise HTTPException(status_code=503, detail="cannot confirm the on-air item") from error
    if current_item_id == item_id:
        raise HTTPException(status_code=409, detail="cannot delete the item on air; use skip")
    governing = rundown_service.governs_playlist(playlist_id, db)
    # A bridge row has no scheduled item behind it, so it can only be deleted
    # directly; everything else goes through the schedule, because removing only
    # the queue row would leave the item scheduled with nothing playing it, and
    # bring it back outright on the next restart from the channel file.
    if governing is not None and rundown_service.schedules_item(governing, item_id, db):
        rundown_service.remove_queue_item(governing, item_id, db)
    else:
        media.delete_item(playlist_id, item_id, db)
    return _snapshot(playlist_id, db)


@router.patch(
    "/playlists/{playlist_id}/items/{item_id}",
    response_model=PlaylistSnapshot,
    status_code=status.HTTP_202_ACCEPTED,
)
def reorder_playlist_item(
    playlist_id: str, item_id: str, body: MoveRequest, db: Session = Depends(get_db)
):
    _lock_mutation(db)
    if media.state_for(playlist_id, db).revision != body.expectedRevision:
        raise HTTPException(status_code=409, detail="playlist revision is stale")
    current_item_id = None
    try:
        _, current_item_id = media._live_input_and_current(playlist_id)
    except Exception as error:
        raise HTTPException(status_code=503, detail="cannot confirm the on-air item") from error
    if current_item_id == item_id:
        raise HTTPException(status_code=409, detail="the item on air cannot be reordered")
    governing = rundown_service.governs_playlist(playlist_id, db)
    if governing is not None and rundown_service.schedules_item(governing, item_id, db):
        rundown_service.move_queue_item(governing, item_id, body.position, db)
    else:
        media.move_item(playlist_id, item_id, body.position, body.expectedRevision, db)
    return _snapshot(playlist_id, db)


@router.post(
    "/playlists/{playlist_id}/{action_name}",
    response_model=PlaylistSnapshot,
    status_code=status.HTTP_202_ACCEPTED,
)
def playlist_action(
    playlist_id: str, action_name: Literal["start", "pause", "skip"], db: Session = Depends(get_db)
):
    if action_name in {"start", "pause"}:
        raise HTTPException(
            status_code=410,
            detail="managed playlists run continuously; Start and Pause are unavailable",
        )
    media.skip(playlist_id, db)
    return _snapshot(playlist_id, db)
