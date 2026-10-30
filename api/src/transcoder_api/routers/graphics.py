from __future__ import annotations

from datetime import datetime, timezone

from fastapi import APIRouter, Depends, File, UploadFile, status
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field, confloat
from sqlalchemy.orm import Session

from ..db import get_db
from ..services import graphics, graphics_upload

router = APIRouter(tags=["graphics"])


class Rect(BaseModel):
    x: confloat(ge=0.0, le=1.0)
    y: confloat(ge=0.0, le=1.0)
    width: confloat(gt=0.0, le=1.0)
    height: confloat(gt=0.0, le=1.0)


class UploadResponse(BaseModel):
    uploadId: str
    mediaType: str
    width: int
    height: int
    previewUrl: str
    expiresAt: datetime
    # The artwork's transparent region, when it has one: the exact programme window.
    contentBox: Rect | None = None


class AssetRequest(BaseModel):
    uploadId: str
    id: str
    kind: str
    label: str | None = None
    geometry: Rect | None = None
    squeezeWindow: Rect | None = None


class CueRequest(BaseModel):
    assetId: str
    mode: str
    durationMs: int | None = Field(default=None, gt=0)
    startInMs: int = Field(default=0, ge=0)
    fadeMs: int = Field(default=0, ge=0)


class CueResponse(BaseModel):
    id: str
    assetId: str
    kind: str
    mode: str
    phase: str
    startsAt: datetime
    endsAt: datetime | None
    restoreUntil: datetime | None
    cancelable: bool = True
    createdAt: datetime | None = None


def _response(cue: graphics.CueStatus) -> CueResponse:
    return CueResponse(
        id=cue.cue.id,
        assetId=cue.cue.asset_id,
        kind=cue.kind,
        mode=cue.mode,
        phase=cue.phase,
        startsAt=cue.starts_at,
        endsAt=cue.ends_at,
        restoreUntil=cue.restore_until,
        createdAt=cue.cue.created_at,
    )


@router.get("/graphics/catalog")
def get_catalog():
    return graphics.catalog()


@router.post(
    "/graphics/uploads",
    response_model=UploadResponse,
    status_code=status.HTTP_201_CREATED,
)
async def stage_upload(file: UploadFile = File(...)):
    def chunks():
        while True:
            chunk = file.file.read(1024 * 1024)
            if not chunk:
                break
            yield chunk

    staged = graphics_upload.stage_upload(file.filename or "upload.bin", chunks())
    return UploadResponse(
        uploadId=staged.upload_id,
        mediaType=staged.media_type,
        width=staged.width,
        height=staged.height,
        previewUrl=f"/graphics/uploads/{staged.upload_id}/preview",
        expiresAt=datetime.fromtimestamp(staged.expires_at, tz=timezone.utc),
        contentBox=Rect(**staged.content_box) if staged.content_box else None,
    )


@router.get("/graphics/uploads/{upload_id}/preview")
def get_upload_preview(upload_id: str):
    return FileResponse(
        graphics_upload.staged_file(upload_id), headers={"Cache-Control": "no-store"}
    )


@router.delete("/graphics/uploads/{upload_id}", status_code=status.HTTP_204_NO_CONTENT)
def discard_upload(upload_id: str):
    graphics_upload.discard_upload(upload_id)


@router.post("/graphics/assets", status_code=status.HTTP_201_CREATED)
def create_asset(body: AssetRequest):
    return graphics_upload.commit_asset(
        body.uploadId,
        body.id,
        body.kind,
        geometry=body.geometry.model_dump() if body.geometry else None,
        squeeze_window=body.squeezeWindow.model_dump() if body.squeezeWindow else None,
        label=body.label,
    )


@router.get("/graphics/assets/{asset_id}/thumbnail")
def get_asset_thumbnail(asset_id: str):
    return FileResponse(graphics_upload.asset_media(asset_id))


@router.get("/graphics/cues", response_model=list[CueResponse])
def list_cues(db: Session = Depends(get_db)):
    return [_response(cue) for cue in graphics.list_cues(db)]


@router.post("/graphics/cues", response_model=CueResponse, status_code=status.HTTP_201_CREATED)
def create_cue(body: CueRequest, db: Session = Depends(get_db)):
    cue = graphics.create_cue(
        body.assetId, body.mode, body.durationMs, body.startInMs, body.fadeMs, db
    )
    return _response(cue)


@router.delete("/graphics/cues/{cue_id}", status_code=status.HTTP_204_NO_CONTENT)
def cancel_cue(cue_id: str, db: Session = Depends(get_db)):
    graphics.cancel_cue(cue_id, db)
