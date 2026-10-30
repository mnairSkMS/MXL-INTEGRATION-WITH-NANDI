from __future__ import annotations

from fastapi import APIRouter
from fastapi.responses import FileResponse
from pydantic import BaseModel

from ..services import slates

router = APIRouter(tags=["slates"])


class SlateAsset(BaseModel):
    id: str
    label: str
    mediaType: str
    hasAudio: bool
    mediaUri: str


@router.get("/slates/catalog", response_model=list[SlateAsset])
def list_slates():
    """The slates an operator may fire, each with the URL to put in the event."""
    return slates.assets()


@router.get("/slates/media/{asset_id}")
def get_slate_media(asset_id: str):
    """Serves the bytes the engine downloads when the slate fires."""
    return FileResponse(slates.resolve(asset_id))
