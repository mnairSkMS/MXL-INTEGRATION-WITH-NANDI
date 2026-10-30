import logging
import os
import pathlib
import shutil
import sys
import uuid
from datetime import timezone

from transcoder_api.db import SessionLocal, initialize_schema
from transcoder_api.exceptions import TranscoderApiException
from transcoder_api.model.db import Event, MediaAsset, PlaylistItem, PlaylistState, db_json_serialize
from transcoder_api.model.rest_api import EventList
from transcoder_api.services import bootstrap, media
from transcoder_api.services.schedule import validate_event

logging.basicConfig(stream=sys.stdout, level=logging.INFO)
log = logging.getLogger(__name__)


def _seed_uris(path: pathlib.Path) -> list[str]:
    return [line.strip() for line in path.read_text().splitlines() if line.strip() and not line.startswith("#")]


def _managed_playlist_file(playlist_id: str) -> pathlib.Path:
    """Where the engine reads this playlist from.

    The default channel keeps MANAGED_PLAYLIST_FILE, because that is the path
    existing transcoder configurations already point at.  Every other channel
    is materialized beside it under its own id, which is what a second input -
    an ad break reel, say - is configured to read.
    """
    default = pathlib.Path(os.environ.get("MANAGED_PLAYLIST_FILE", "/runtime/main.m3u8"))
    if playlist_id == os.environ.get("MANAGED_PLAYLIST_ID", "main"):
        return default
    return default.parent / f"{playlist_id}.m3u8"


def _bootstrap_managed_playlist(db, playlist_id: str, *, skip_legacy_seed: bool = False) -> None:
    """One-time, recoverable migration from the static on-air M3U8 to SQLite.

    The tail of this function is unconditional and is shared with the
    declarative channel bootstrap: it forces one snapshot replay per engine
    process and materializes the managed M3U8 the engine actually boots from.
    """
    seed = pathlib.Path(os.environ.get("PLAYLIST_SEED_FILE", "/media/queue/playlist.m3u8"))
    output = _managed_playlist_file(playlist_id)
    state = db.get(PlaylistState, playlist_id)
    if state is None:
        state = PlaylistState(playlist_id=playlist_id, run_state="running", revision=0, applied_revision=0, skip_seq=0)
        db.add(state)
        db.flush()

    # Managed playlists are continuous loops. Older desired-only prototypes could
    # leave this field paused even while output was running.
    state.run_state = "running"

    # `bootstrap` records that the old detached desired queue was safely archived and
    # replaced from the actual on-air source. Never repeat this destructive migration.
    # The legacy static M3U8 only ever described the default channel; any other
    # channel exists because a declarative file created it, and has nothing to
    # migrate from.
    if state.active_input_id is None and not skip_legacy_seed and playlist_id == os.environ.get("MANAGED_PLAYLIST_ID", "main"):
        db_path = pathlib.Path(os.environ.get("DB_PATH", "./events.sqlite"))
        if db_path.is_file():
            backup = db_path.with_name(db_path.name + ".before-managed-playlist")
            if not backup.exists():
                shutil.copy2(db_path, backup)
                log.info("saved pre-migration playlist database to %s", backup)

        db.query(PlaylistItem).filter(PlaylistItem.playlist_id == playlist_id).delete()
        if not seed.is_file():
            raise RuntimeError(
                "the managed playlist has no source: PLAYLIST_BOOTSTRAP_FILE "
                f"({bootstrap.bootstrap_path() or 'unset'}) did not apply and "
                f"PLAYLIST_SEED_FILE ({seed}) does not exist"
            )

        for position, uri in enumerate(_seed_uris(seed), start=1):
            if not uri.startswith("file:///media/queue/"):
                raise RuntimeError(f"managed seed only supports MEDIA_DIR files: {uri}")
            relative = uri.removeprefix("file:///media/queue/")
            asset = media._asset_model(media._absolute_path(relative), relative, db)
            db.add(PlaylistItem(
                id=str(uuid.uuid4()),
                playlist_id=playlist_id,
                media_id=asset.id,
                position=position,
                state="queued",
            ))
        state.revision = 1
        state.applied_revision = 0
        state.apply_error = None
        state.active_input_id = "bootstrap"
        db.commit()

    # Early migration builds used UUID hex strings. gRPC/API UUID values are canonical
    # hyphenated strings, so normalize once before the managed IDs reach the engine.
    normalized = False
    managed_items = media.queue_items(playlist_id, db)
    for item in managed_items:
        canonical = str(uuid.UUID(item.id))
        if item.id != canonical:
            item.id = canonical
            normalized = True
    if normalized:
        state.revision += 1
        state.applied_revision = 0
        db.commit()

    # Older revisions could leave gaps after a remove.  Positions are a public
    # ordering field, so repair them once at startup without changing order.
    if [item.position for item in managed_items] != list(range(1, len(managed_items) + 1)):
        media._renumber(managed_items, db)
        state.revision += 1
        state.applied_revision = 0
        db.commit()

    # `applied_revision` acknowledges one particular engine process.  A fresh
    # process starts from the materialized M3U8 and therefore has generated
    # stream IDs until the managed snapshot is replayed.  Force one idempotent
    # reconciliation on every process start; preserving the desired revision
    # retains normal optimistic-concurrency semantics for operators.
    if state.applied_revision == state.revision:
        state.applied_revision = 0
        state.apply_error = None
        db.commit()

    items = media.queue_items(playlist_id, db)
    assets = {asset.id: asset for asset in db.query(MediaAsset).filter(MediaAsset.id.in_([i.media_id for i in items])).all()}
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    lines = ["#EXTM3U"]
    # Pinned live sources come first, matching every snapshot media.reconcile sends.
    for entry in media.live_entries(playlist_id):
        lines.extend(["#EXTINF:-1,", entry["uri"]])
    for item in items:
        lines.extend(["#EXTINF:-1,", media._asset_uri(assets[item.media_id])])
    temporary.write_text("\n".join(lines) + "\n")
    os.replace(temporary, output)
    log.info("materialized %d managed playlist items for %s to %s", len(items), playlist_id, output)


def run():
    # supervisord runs this to completion before the API or the transcoder opens the
    # database, so it is the one place the schema can be created and migrated without
    # racing a reader.
    initialize_schema()

    db = SessionLocal()
    preload_data_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else None
    # The declarative file must win before the M3U8 is materialized below.  A
    # fresh engine rebinds its on-air item by URI on the first snapshot
    # (PlaylistInput::applyPlaylistSnapshot, allowInitialUriRebind), so an M3U8
    # written from the pre-bootstrap queue would make the engine refuse the new
    # snapshot outright.
    results = bootstrap.run_all(db)

    # Which playlists to materialize:
    #   - the default channel, which exists even with no declarative file at
    #     all, because that is the pre-bootstrap layout;
    #   - every channel whose file applied cleanly;
    #   - every channel already in the database, so a channel whose file is
    #     broken today keeps playing what it was given yesterday, which is the
    #     promise the bootstrap failure path makes.
    default_id = os.environ.get("MANAGED_PLAYLIST_ID", "main")
    playlist_ids = [default_id]
    playlist_ids.extend(
        channel_id for channel_id, result in results.items()
        if result.ok and channel_id not in playlist_ids
    )
    playlist_ids.extend(
        state.playlist_id for state in db.query(PlaylistState).all()
        if state.playlist_id not in playlist_ids
    )

    for playlist_id in playlist_ids:
        applied = results.get(playlist_id)
        _bootstrap_managed_playlist(
            db, playlist_id, skip_legacy_seed=bool(applied and applied.ok)
        )
    if preload_data_path is None or not preload_data_path.is_file():
        log.warning(f"Cannot open events file {preload_data_path}; playlist bootstrap completed")
        db.close()
        return

    events = EventList.parse_raw(preload_data_path.read_text())
    try:
        for event in events.root:
            validate_event(event)
            new_event = Event(
                type=event.type.value,
                time=event.time.astimezone(timezone.utc),
                details=db_json_serialize(event.details),
            )
            db.add(new_event)
        log.info(f"{len(events.root)} events loaded")
    except TranscoderApiException as e:
        log.error(f"data validation exception: {e.message}", exc_info=e)
        exit(1)

    db.commit()
    db.close()
    log.info(f"preloading finish")


if __name__ == "__main__":
    run()
