from __future__ import annotations

from datetime import datetime, timedelta, timezone
from uuid import uuid4

import pytest

from transcoder_api.exceptions import ConflictException
from transcoder_api.model.db import (
    GraphicsCue,
    PlaylistItem,
    PlaylistState,
    RundownEventDefinition,
    RundownEventExecution,
    RundownOutbox,
    RundownVersion,
)
from transcoder_api.services import media, rundown


def _register(client, tmp_path, name: str):
    (tmp_path / name).write_bytes(b"media")
    response = client.post("/media/import", json={"relativePath": name})
    assert response.status_code == 201
    return response.json()


def _yaml(rundown_id: str, relative_path: str, sha256: str) -> str:
    return f"""version: 1
rundown:
  id: {rundown_id}
  name: Test rundown
  timezone: UTC
  playlistId: main
  loop: true
items:
  - key: opening
    media:
      relativePath: {relative_path}
      sha256: {sha256}
    graphics:
      - key: opening-logo
        assetId: logo-default
        mode: snap
        durationMs: 5000
        trigger:
          type: item-start
          offsetMs: 0
scheduledGraphics: []
"""


def _seed(client, monkeypatch, tmp_path, rundown_id: str, duration: str = "30"):
    """Register media and publish version 1 — the usual test entry point.

    Uses the cold-start publish, which needs no running engine, so a test can
    choose for itself whether an on-air item exists.
    """
    from .conftest import TestingSessionLocal

    monkeypatch.setenv("ENABLE_RUNDOWN_SERVICE", "true")
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(
        media,
        "_probe",
        lambda path: {"streams": [{"codec_type": "video"}], "format": {"duration": duration}},
    )
    asset = _register(client, tmp_path, "clip.mp4")
    source = _yaml(rundown_id, "clip.mp4", "").replace("      sha256: \n", "")

    db = TestingSessionLocal()
    parsed = rundown.parse_yaml(source, rundown_id, db)
    rundown._validate_timeline_constraints(parsed, db)
    rundown._ensure_rundown(rundown_id, parsed, db)
    version = rundown._materialize_version(rundown_id, parsed.source_yaml, db)
    rundown.publish_bootstrap(version, rundown.get_rundown(rundown_id, db), db)
    db.commit()
    version_id = version.id
    db.close()

    document = client.get(f"/rundowns/{rundown_id}/versions/{version_id}").json()
    return asset, source, {"versionId": version_id, "document": document}


def _go_live(monkeypatch, asset, item_id: str | None = None) -> str:
    """Put something on air so a projection has a confirmed current item.

    The seed already projected the rundown into the queue, so by default the
    on-air item is that projection's first row.  Passing an `item_id` that is
    not part of the rundown is how a test asks for the bridge path, and it is
    appended rather than placed at position 1, which is already taken.
    """
    from .conftest import TestingSessionLocal

    db = TestingSessionLocal()
    if db.get(PlaylistState, "main") is None:
        db.add(
            PlaylistState(
                playlist_id="main", run_state="running",
                revision=1, applied_revision=1, skip_seq=0,
            )
        )
        db.flush()
    queue = media.queue_items("main", db)
    on_air = item_id or (queue[0].id if queue else str(uuid4()))
    if db.get(PlaylistItem, on_air) is None:
        db.add(
            PlaylistItem(
                id=on_air, playlist_id="main", media_id=asset["id"],
                position=max((row.position for row in queue), default=0) + 1,
                state="on_air",
            )
        )
    state = db.get(PlaylistState, "main")
    state.applied_revision = state.revision
    db.commit()
    db.close()
    monkeypatch.setattr(media, "_live_input_and_current", lambda *_: ("input", on_air))
    return on_air


def _mark_applied(playlist_id: str = "main") -> None:
    """Pretend the engine caught up: a projection is refused until it has."""
    from .conftest import TestingSessionLocal

    db = TestingSessionLocal()
    state = db.get(PlaylistState, playlist_id)
    assert state is not None
    state.applied_revision = state.revision
    db.commit()
    db.close()


def _sponsor(key: str, asset_id: str, offset_ms: int, duration_ms: int = 5000) -> dict:
    return {
        "op": "addGraphic", "itemKey": "opening", "assetId": asset_id, "key": key,
        "mode": "reveal", "durationMs": duration_ms, "fadeMs": 0, "offsetMs": offset_ms,
    }


def test_rundown_routes_are_hidden_until_feature_is_enabled(client):
    response = client.post("/rundowns/main/apply", json={"ops": [{"op": "removeItem", "key": "x"}]})
    assert response.status_code == 404


def test_rundown_status_is_available_while_optional_service_is_disabled(client):
    response = client.get("/rundowns/status")
    assert response.status_code == 200
    assert response.json()["enabled"] is False
    assert response.json()["observationIntervalMs"] == 250


def test_the_sponsor_swap_that_separate_writes_cannot_express(
    client, monkeypatch, tmp_path
):
    """The headline guard, and the whole reason the draft could be removed.

    L-band and Aston share one squeeze resource, so moving one out of the way
    and another into its place is legal only as a single edit.  Applied as
    separate calls the first one is rejected; applied as one operation list it
    succeeds, because validation runs once on the merged document.
    """
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "swap")
    _go_live(monkeypatch, asset)

    # Put an L-band on air at 0-5 s.
    first = client.post(
        "/rundowns/swap/apply",
        json={"ops": [_sponsor("lband-a", "lband-voltedge", 0)]},
    )
    assert first.status_code == 202, first.text
    _mark_applied()

    # Adding the Aston at the same offset on its own collides with it.
    blocked = client.post(
        "/rundowns/swap/apply",
        json={"ops": [_sponsor("aston-b", "aston-static", 0)]},
    )
    assert blocked.status_code == 422
    assert "squeeze" in blocked.json()["message"].lower()

    # The same end state, expressed as one atomic list, is accepted.
    swapped = client.post(
        "/rundowns/swap/apply",
        json={
            "ops": [
                {"op": "removeGraphic", "key": "lband-a"},
                _sponsor("aston-b", "aston-static", 0),
                _sponsor("lband-a", "lband-voltedge", 12000),
            ]
        },
    )
    assert swapped.status_code == 202, swapped.text
    keys = {event["key"] for event in swapped.json()["version"]["graphics"]}
    assert keys == {"opening-logo", "aston-b", "lband-a"}


def test_a_document_that_breaks_timing_constraints_cannot_be_applied(
    client, monkeypatch, tmp_path
):
    """Every write path validates; there is no route that defers the check."""
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    _go_live(monkeypatch, asset)
    before = client.get("/rundowns/main").json()["activeVersionId"]

    # The clip is 30 s, and a graphic must also fit its reveal tail within it.
    rejected = client.post(
        "/rundowns/main/apply",
        json={"ops": [_sponsor("too-long", "lband-voltedge", 0, duration_ms=60000)]},
    )
    assert rejected.status_code == 422
    assert client.get("/rundowns/main").json()["activeVersionId"] == before


def test_a_stale_expected_version_is_rejected_and_changes_nothing(
    client, monkeypatch, tmp_path
):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "stale")
    _go_live(monkeypatch, asset)

    applied = client.post(
        "/rundowns/stale/apply",
        json={
            "ops": [_sponsor("first", "lband-voltedge", 0)],
            "expectedVersionId": seeded["versionId"],
        },
    )
    assert applied.status_code == 202, applied.text
    _mark_applied()

    stale = client.post(
        "/rundowns/stale/apply",
        json={
            "ops": [_sponsor("second", "aston-static", 12000)],
            "expectedVersionId": seeded["versionId"],
        },
    )
    assert stale.status_code == 409
    assert client.get("/rundowns/stale").json()["activeVersionId"] == applied.json()["version"]["id"]


def test_a_dry_run_validates_without_publishing(client, monkeypatch, tmp_path):
    from .conftest import TestingSessionLocal

    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "dry")
    _go_live(monkeypatch, asset)

    db = TestingSessionLocal()
    versions_before = db.query(RundownVersion).count()
    db.close()

    checked = client.post(
        "/rundowns/dry/apply",
        json={"ops": [_sponsor("probe", "lband-voltedge", 0)], "dryRun": True},
    )
    assert checked.status_code == 202, checked.text
    assert checked.json()["version"] is None
    assert checked.json()["applied"] == {"addGraphic": 1}

    db = TestingSessionLocal()
    assert db.query(RundownVersion).count() == versions_before
    db.close()
    assert client.get("/rundowns/dry").json()["activeVersionId"] == seeded["versionId"]

    # A dry run reports the same rejection a real apply would.
    bad = client.post(
        "/rundowns/dry/apply",
        json={
            "ops": [_sponsor("probe", "lband-voltedge", 0, duration_ms=60000)],
            "dryRun": True,
        },
    )
    assert bad.status_code == 422


def test_apply_requires_exactly_one_of_ops_or_document(client, monkeypatch, tmp_path):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "either")
    _go_live(monkeypatch, asset)

    assert client.post("/rundowns/either/apply", json={}).status_code == 422
    both = client.post(
        "/rundowns/either/apply",
        json={"ops": [_sponsor("x", "lband-voltedge", 0)], "document": {"items": []}},
    )
    assert both.status_code == 422

    # An *empty* document is still a document. Testing truthiness rather than
    # presence let this pair through and applied the ops, which a live demo
    # caught: the response was 202.
    empty = client.post(
        "/rundowns/either/apply",
        json={"ops": [_sponsor("y", "lband-voltedge", 0)], "document": {}},
    )
    assert empty.status_code == 422, empty.text

    # And an empty ops list alongside a document is refused the same way.
    empty_ops = client.post(
        "/rundowns/either/apply",
        json={"ops": [], "document": {"items": []}},
    )
    assert empty_ops.status_code == 422, empty_ops.text


def test_a_whole_document_can_be_applied(client, monkeypatch, tmp_path):
    """The operator UI holds the end state, not the operations that produced it."""
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "doc")
    _go_live(monkeypatch, asset)

    document = client.get(f"/rundowns/doc/versions/{seeded['versionId']}").json()
    document["items"][0]["graphics"][0]["durationMs"] = 7000
    applied = client.post(
        "/rundowns/doc/apply",
        json={"document": document, "expectedVersionId": seeded["versionId"]},
    )
    assert applied.status_code == 202, applied.text
    assert applied.json()["applied"] == {"items": 1, "graphics": 1}
    graphics = applied.json()["version"]["graphics"]
    assert [event["durationMs"] for event in graphics] == [7000]


def test_apply_is_refused_while_a_prior_revision_is_still_applying(
    client, monkeypatch, tmp_path
):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "busy")
    on_air = _go_live(monkeypatch, asset)

    from .conftest import TestingSessionLocal

    db = TestingSessionLocal()
    state = db.get(PlaylistState, "main")
    state.revision = state.applied_revision + 1
    db.commit()
    db.close()

    busy = client.post(
        "/rundowns/busy/apply",
        json={"ops": [_sponsor("late", "lband-voltedge", 0)]},
    )
    assert busy.status_code == 409
    assert "applying" in busy.json()["message"].lower()


def test_wall_clock_time_resolution_uses_the_rundown_timezone(
    client, monkeypatch, tmp_path
):
    monkeypatch.setattr(rundown, "utc_now", lambda: datetime(2026, 8, 6, tzinfo=timezone.utc))
    _seed(client, monkeypatch, tmp_path, "clock")
    resolved = client.post(
        "/rundowns/clock/time/resolve",
        json={"date": "2026-08-07", "time": "14:30:15.25"},
    )
    assert resolved.status_code == 200
    assert resolved.json()["startsAt"].startswith("2026-08-07T14:30:15.250")
    assert resolved.json()["timezone"] == "UTC"
    # A native <input type="time"> with a hundredths step still serialises three
    # decimals, and refusing those was what made the field unusable from the UI.
    # They are accepted and quantised to the hundredth the schedule stores.
    rounded = client.post(
        "/rundowns/clock/time/resolve",
        json={"date": "2026-08-07", "time": "14:30:15.123"},
    )
    assert rounded.status_code == 200
    assert rounded.json()["startsAt"].startswith("2026-08-07T14:30:15.120")
    # Rounding up has to carry rather than produce an impossible time.
    carried = client.post(
        "/rundowns/clock/time/resolve",
        json={"date": "2026-08-07", "time": "14:30:59.999"},
    )
    assert carried.status_code == 200
    assert carried.json()["startsAt"].startswith("2026-08-07T14:31:00")
    # Tolerating the third decimal is not the same as tolerating anything: a
    # malformed time is still refused rather than quietly rounded into something.
    assert client.post(
        "/rundowns/clock/time/resolve",
        json={"date": "2026-08-07", "time": "14:30:1x"},
    ).status_code == 422
    # Beyond milliseconds the request model refuses it on length, before the service.
    assert client.post(
        "/rundowns/clock/time/resolve",
        json={"date": "2026-08-07", "time": "14:30:15.1234"},
    ).status_code == 400


def test_superseding_a_version_cancels_its_pending_graphics_command(
    client, monkeypatch, tmp_path
):
    from .conftest import TestingSessionLocal

    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "cancel")
    _go_live(monkeypatch, asset)

    version_id = seeded["versionId"]
    db = TestingSessionLocal()
    definition = db.query(RundownEventDefinition).filter_by(version_id=version_id).one()
    execution = RundownEventExecution(
        id=str(uuid4()), execution_key="pending-cancel", definition_id=definition.id,
        occurrence_id=None, planned_at=datetime.now(timezone.utc), state="pending",
    )
    db.add(execution)
    db.add(RundownOutbox(
        id=str(uuid4()), dedupe_key=execution.execution_key, command_type="graphics_cue",
        payload_json='{"executionId":"unused"}', state="pending", attempts=0,
        next_attempt_at=datetime.now(timezone.utc),
    ))
    db.commit()
    rundown._cancel_superseded_commands("cancel", version_id, "replacement", db)
    db.commit()
    assert db.get(RundownEventExecution, execution.id).state == "canceled"
    assert db.query(RundownOutbox).filter_by(dedupe_key="pending-cancel").one().state == "canceled"
    db.close()


def test_apply_keeps_on_air_item_as_one_cycle_bridge_then_cleans_it(
    client, monkeypatch, tmp_path
):
    from .conftest import TestingSessionLocal

    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "live")
    stable_id = seeded["document"]["items"][0]["stableItemId"]

    # Something unrelated is on air, so the projection has to bridge it.
    old_id = str(uuid4())
    _go_live(monkeypatch, asset, old_id)

    applied = client.post(
        "/rundowns/live/apply",
        json={"ops": [{"op": "updateGraphic", "key": "opening-logo", "durationMs": 6000}]},
    )
    assert applied.status_code == 202, applied.text
    # Writeback is inert without PLAYLIST_BOOTSTRAP_FILE, which tests never set.
    assert applied.json()["channelFile"]["state"] == "skipped"

    db = TestingSessionLocal()
    state = db.get(PlaylistState, "main")
    queue = media.queue_items("main", db)
    assert [item.id for item in queue] == [old_id, stable_id]
    assert state.revision == 2
    assert state.applied_revision == 1
    db.close()

    # Once the engine is on a stable rundown item, cleanup issues one normal
    # follow-up managed snapshot and the temporary bridge cannot air again.
    monkeypatch.setattr(media, "_live_input_and_current", lambda *_: ("input", stable_id))
    db = TestingSessionLocal()
    assert rundown.sync_active_rundowns(db)
    queue = media.queue_items("main", db)
    assert [item.id for item in queue] == [stable_id]
    assert db.get(PlaylistState, "main").revision == 3
    # The observed item start creates one idempotent outbox command; dispatch
    # uses the existing catalog cue API rather than a second graphics path.
    assert rundown.dispatch_outbox(db)
    assert db.query(RundownEventExecution).one().state == "dispatched"
    assert db.query(GraphicsCue).count() == 1
    db.close()


def test_an_elapsed_wall_clock_trigger_does_not_lock_the_schedule(
    client, monkeypatch, tmp_path
):
    """A published version must stay editable after its trigger time passes.

    `parse_yaml` enforces the 14-day future window, which is right on a write
    and wrong on a read: every wall-clock trigger eventually falls into the
    past, and refusing it locked the operator out of their own live schedule
    until the next restart dropped it.
    """
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "clock")
    _go_live(monkeypatch, asset)

    scheduled = client.post(
        "/rundowns/clock/apply",
        json={
            "ops": [{
                "op": "addGraphic", "key": "promo", "assetId": "logo-default",
                "mode": "snap", "durationMs": 5000, "fadeMs": 0,
                "startsAt": (rundown.utc_now() + timedelta(hours=1)).isoformat(),
            }]
        },
    )
    assert scheduled.status_code == 202, scheduled.text
    _mark_applied()
    version_id = scheduled.json()["version"]["id"]

    # Two hours later the trigger has fired and is permanently in the past.
    monkeypatch.setattr(
        rundown, "utc_now", lambda: datetime.now(timezone.utc) + timedelta(hours=2)
    )

    assert client.get(f"/rundowns/clock/versions/{version_id}").status_code == 200
    later = client.post(
        "/rundowns/clock/apply",
        json={"ops": [{"op": "updateGraphic", "key": "opening-logo", "durationMs": 6000}]},
    )
    assert later.status_code == 202, later.text
    # The elapsed trigger is dropped rather than carried forward, and said so.
    assert later.json()["version"]["graphics"] == [
        event for event in later.json()["version"]["graphics"]
        if event["key"] == "opening-logo"
    ]
    assert any("promo" in warning for warning in later.json()["warnings"])


# ---------------------------------------------------------------------------
# Program import
# ---------------------------------------------------------------------------


def _import(client, rundown_id: str, body: str, **fields):
    data = {key: str(value) for key, value in fields.items() if value is not None}
    return client.post(
        f"/rundowns/{rundown_id}/programs/import",
        files={"file": ("channel.yml", body, "application/yaml")},
        data=data,
    )


def test_importing_a_program_list_appends_by_default(client, monkeypatch, tmp_path):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "imp")
    _go_live(monkeypatch, asset)
    _register(client, tmp_path, "second.mp4")

    before = client.get(f"/rundowns/imp/versions/{seeded['versionId']}").json()
    original = [item["stableItemId"] for item in before["items"]]

    imported = _import(client, "imp", "playlist:\n  - media: second.mp4\n")
    assert imported.status_code == 202, imported.text
    items = imported.json()["version"]["items"]
    assert [item["key"] for item in items] == ["opening", "second"]
    # Existing identity is untouched: as-run history joins on these.
    assert items[0]["stableItemId"] == original[0]


def test_importing_at_a_position_inserts_there(client, monkeypatch, tmp_path):
    asset, _, _ = _seed(client, monkeypatch, tmp_path, "pos")
    _go_live(monkeypatch, asset)
    _register(client, tmp_path, "second.mp4")

    imported = _import(client, "pos", "playlist:\n  - media: second.mp4\n", position=0)
    assert imported.status_code == 202, imported.text
    assert [item["key"] for item in imported.json()["version"]["items"]] == ["second", "opening"]


def test_an_imported_key_collision_renames_only_the_newcomer(
    client, monkeypatch, tmp_path
):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "collide")
    _go_live(monkeypatch, asset)

    before = client.get(f"/rundowns/collide/versions/{seeded['versionId']}").json()
    original = before["items"][0]["stableItemId"]

    # The same media, so the slug collides with the live item's key.
    imported = _import(client, "collide", "playlist:\n  - media: clip.mp4\n    key: opening\n")
    assert imported.status_code == 202, imported.text
    items = imported.json()["version"]["items"]
    assert [item["key"] for item in items] == ["opening", "opening-2"]
    assert items[0]["stableItemId"] == original
    assert items[1]["stableItemId"] != original


def test_importing_a_file_for_another_channel_is_refused(client, monkeypatch, tmp_path):
    asset, _, _ = _seed(client, monkeypatch, tmp_path, "guard")
    _go_live(monkeypatch, asset)

    refused = _import(client, "guard", "channel: ads\nplaylist:\n  - media: clip.mp4\n")
    assert refused.status_code == 422
    assert "declares channel" in refused.json()["message"]


def test_importing_unregistered_media_is_refused(client, monkeypatch, tmp_path):
    asset, _, _ = _seed(client, monkeypatch, tmp_path, "missing")
    _go_live(monkeypatch, asset)

    refused = _import(client, "missing", "playlist:\n  - media: nowhere.mp4\n")
    assert refused.status_code == 422


def test_an_oversized_import_is_refused(client, monkeypatch, tmp_path):
    asset, _, _ = _seed(client, monkeypatch, tmp_path, "big")
    _go_live(monkeypatch, asset)

    refused = _import(client, "big", "# padding\n" * 200000)
    assert refused.status_code == 422


def test_an_import_is_validated_as_part_of_the_merged_document(
    client, monkeypatch, tmp_path
):
    """An imported file's graphics face the same constraints as authored ones.

    Note what is *not* asserted here.  Sponsor windows cannot conflict across
    items: item windows are disjoint, and clip-fit keeps every sponsor inside
    its own item, so `_validate_timeline_constraints`' cross-item and
    loop-boundary branches are unreachable while clip-fit holds.  The reachable
    conflict is two sponsors sharing one item, which an import can introduce.
    """
    asset, _, _ = _seed(client, monkeypatch, tmp_path, "merge")
    _go_live(monkeypatch, asset)
    _register(client, tmp_path, "second.mp4")

    refused = _import(
        client,
        "merge",
        "playlist:\n"
        "  - media: second.mp4\n"
        "    graphics:\n"
        "      - use: aston-static\n"
        "        key: imported-aston\n"
        "        at: 0\n"
        "        duration: 5s\n"
        "      - use: lband-voltedge\n"
        "        key: imported-lband\n"
        "        at: 3s\n"
        "        duration: 5s\n",
    )
    assert refused.status_code == 422
    assert "squeeze" in refused.json()["message"].lower()

    # A clip-fit violation in the imported fragment is rejected the same way.
    too_long = _import(
        client,
        "merge",
        "playlist:\n"
        "  - media: second.mp4\n"
        "    graphics:\n"
        "      - use: lband-voltedge\n"
        "        at: 0\n"
        "        duration: 60s\n",
    )
    assert too_long.status_code == 422


def test_a_dry_run_import_publishes_nothing(client, monkeypatch, tmp_path):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "impdry")
    _go_live(monkeypatch, asset)
    _register(client, tmp_path, "second.mp4")

    checked = _import(
        client, "impdry", "playlist:\n  - media: second.mp4\n", dryRun=True
    )
    assert checked.status_code == 202, checked.text
    assert checked.json()["version"] is None
    assert client.get("/rundowns/impdry").json()["activeVersionId"] == seeded["versionId"]


def test_seeding_publishes_a_first_version_and_refuses_a_second(
    client, monkeypatch, tmp_path
):
    from .conftest import TestingSessionLocal

    monkeypatch.setenv("ENABLE_RUNDOWN_SERVICE", "true")
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(
        media,
        "_probe",
        lambda path: {"streams": [{"codec_type": "video"}], "format": {"duration": "30"}},
    )
    asset = _register(client, tmp_path, "clip.mp4")

    on_air = str(uuid4())
    db = TestingSessionLocal()
    db.add(PlaylistState(
        playlist_id="main", run_state="running", revision=1, applied_revision=1, skip_seq=0
    ))
    db.add(PlaylistItem(
        id=on_air, playlist_id="main", media_id=asset["id"], position=1, state="on_air"
    ))
    db.commit()
    db.close()
    monkeypatch.setattr(media, "_live_input_and_current", lambda *_: ("input", on_air))

    seeded = client.post("/rundowns/main/seed", json={"name": "Main rundown"})
    assert seeded.status_code == 201, seeded.text
    assert len(seeded.json()["items"]) == 1

    again = client.post("/rundowns/main/seed", json={"name": "Main rundown"})
    assert again.status_code == 409


def _observe() -> None:
    """Drive the observer once, the way the reconciler task does."""
    from .conftest import TestingSessionLocal

    db = TestingSessionLocal()
    rundown.sync_active_rundowns(db)
    db.close()


def test_clock_reports_the_sync_origin_and_the_observed_current_start(
    client, monkeypatch, tmp_path
):
    """The grid projects every row from this; all three fields must be real."""
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    _go_live(monkeypatch, asset)
    _mark_applied()

    before = rundown.utc_now()
    _observe()
    after = rundown.utc_now()

    response = client.get("/rundowns/main/clock")
    assert response.status_code == 200, response.text
    body = response.json()

    assert body["currentItemId"] == seeded["document"]["items"][0]["stableItemId"]
    assert body["loopDurationMs"] == seeded["document"]["items"][0]["durationMs"] == 30000
    assert body["cycle"] == 1
    synced = datetime.fromisoformat(body["syncedAt"])
    started = datetime.fromisoformat(body["currentStartedAt"])
    assert before <= synced <= after
    # The first observation is both the sync origin and the current start.
    assert synced == started
    assert datetime.fromisoformat(body["serverNow"]) >= started


def test_clock_before_any_observation_falls_back_to_the_process_start(
    client, monkeypatch, tmp_path
):
    """A schedule can be published seconds before the observer sees a transition.

    With nothing observed there is no real origin to report, so the clock falls back
    to when this process began watching.  The two genuinely unknown fields stay null,
    and the first observation replaces the fallback within one program.
    """
    _seed(client, monkeypatch, tmp_path, "main")

    body = client.get("/rundowns/main/clock").json()
    assert body["syncedAt"] is not None
    assert body["currentItemId"] is None
    assert body["currentStartedAt"] is None
    assert body["cycle"] == 0
    # The loop length is known from the document even with nothing observed yet.
    assert body["loopDurationMs"] == 30000
    assert body["serverNow"] is not None


def test_clock_survives_a_publish_without_blanking_the_current_item(
    client, monkeypatch, tmp_path
):
    """The regression this exists to prevent.

    An occurrence records when a *playlist item* started, and a stable item ID
    survives a publish.  Scoping the lookup to the active version would report
    nothing for up to one whole program after every apply, blanking the operator
    grid's Start column exactly when they had just changed something.
    """
    asset, _, _ = _seed(client, monkeypatch, tmp_path, "main")
    on_air = _go_live(monkeypatch, asset)
    _mark_applied()
    _observe()

    before = client.get("/rundowns/main/clock").json()
    assert before["currentItemId"] == on_air

    _mark_applied()
    applied = client.post(
        "/rundowns/main/apply",
        json={"ops": [_sponsor("late-lband", "lband-voltedge", 0)]},
    )
    assert applied.status_code == 202, applied.text

    after = client.get("/rundowns/main/clock").json()
    assert after["currentItemId"] == on_air
    assert after["currentStartedAt"] == before["currentStartedAt"]


def test_publishing_does_not_move_the_clocks_origin(client, monkeypatch, tmp_path):
    """Playout does not restart on a publish, so the elapsed origin must not either.

    The origin used to be the first occurrence *of the active version*, so every
    apply minted a new version and restarted the Start column at zero.  An
    untethered graphic entered as elapsed time was then re-expressed against a new
    origin the moment anything else was applied, which is indistinguishable from the
    schedule having moved it.
    """
    asset, _, _ = _seed(client, monkeypatch, tmp_path, "main")
    _go_live(monkeypatch, asset)
    _mark_applied()
    _observe()

    origin = client.get("/rundowns/main/clock").json()["syncedAt"]
    assert origin is not None

    # Two publishes, because one could pass by accident: the origin has to hold
    # across every version this rundown gains, not merely the next one.
    for key, offset in (("first-lband", 0), ("second-lband", 10000)):
        _mark_applied()
        applied = client.post(
            "/rundowns/main/apply",
            json={"ops": [_sponsor(key, "lband-voltedge", offset, duration_ms=3000)]},
        )
        assert applied.status_code == 202, applied.text
        _observe()
        assert client.get("/rundowns/main/clock").json()["syncedAt"] == origin


def test_clock_with_unprobed_media_reports_a_zero_loop_and_does_not_divide_by_zero(
    client, monkeypatch, tmp_path
):
    """An item whose duration never probed leaves the loop length at zero."""
    asset, _, _ = _seed(client, monkeypatch, tmp_path, "main")
    _go_live(monkeypatch, asset)
    _mark_applied()
    _observe()

    from .conftest import TestingSessionLocal
    from transcoder_api.model.db import MediaAsset

    db = TestingSessionLocal()
    db.get(MediaAsset, asset["id"]).probe_json = "{}"
    db.commit()
    db.close()

    body = client.get("/rundowns/main/clock").json()
    assert body["loopDurationMs"] == 0
    assert body["cycle"] == 1
    assert body["currentStartedAt"] is not None


def test_clock_counts_a_cycle_per_completed_loop(client, monkeypatch, tmp_path):
    """`cycle` is how the header shows the loop advancing, so it must advance."""
    from .conftest import TestingSessionLocal
    from transcoder_api.model.db import PlaybackOccurrence

    # The scenario below is a channel that has been looping for 75 s, so the process
    # has to have been counting for at least that long. Occurrences older than the
    # epoch belong to a previous run and are deliberately ignored, which would
    # otherwise discard the backdated origin and collapse the count to one.
    monkeypatch.setattr(
        rundown, "PROCESS_STARTED_AT", rundown.utc_now() - timedelta(hours=1)
    )

    asset, _, _ = _seed(client, monkeypatch, tmp_path, "main")
    _go_live(monkeypatch, asset)
    _mark_applied()
    _observe()

    # Backdate the sync origin by two and a half loops of the 30 s item.
    db = TestingSessionLocal()
    origin = db.query(PlaybackOccurrence).one()
    started = rundown._utc(origin.observed_at)
    origin.observed_at = started - timedelta(milliseconds=75000)
    origin.ended_at = started - timedelta(milliseconds=60000)
    db.add(
        PlaybackOccurrence(
            id=str(uuid4()), rundown_id="main", version_id=origin.version_id,
            playlist_item_id=origin.playlist_item_id, sequence=2, observed_at=started,
        )
    )
    db.commit()
    db.close()

    body = client.get("/rundowns/main/clock").json()
    assert body["loopDurationMs"] == 30000
    # 75 s after the origin is part way through the third loop.
    assert body["cycle"] == 3


def test_clock_is_hidden_while_the_optional_service_is_disabled(client):
    assert client.get("/rundowns/main/clock").status_code == 404


# --- queue edits while a rundown is published --------------------------------
#
# A published rundown owns the queue: _remove_completed_bridge deletes every
# queue row absent from the active version, and it cannot tell a leftover
# on-air bridge row from one an operator just inserted. So insert/remove/reorder
# are written through the schedule; these pin that, and in particular that an
# inserted item still exists after the reconciler pass that used to delete it.

def _second_asset(client, tmp_path, name="second.mp4"):
    return _register(client, tmp_path, name)


def test_an_inserted_item_survives_the_reconciler_that_used_to_delete_it(
    client, monkeypatch, tmp_path
):
    from .conftest import TestingSessionLocal

    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    stable_id = seeded["document"]["items"][0]["stableItemId"]
    _go_live(monkeypatch, asset, stable_id)
    _mark_applied()

    extra = _second_asset(client, tmp_path)
    revision = client.get("/playlists/main").json()["desiredRevision"]
    added = client.post(
        "/playlists/main/items",
        json={"mediaId": extra["id"], "expectedRevision": revision},
    )
    assert added.status_code == 202, added.text

    # In the schedule, not merely in the queue: that is what makes it durable.
    detail = client.get("/rundowns/main").json()
    document = client.get(
        f"/rundowns/main/versions/{detail['activeVersionId']}"
    ).json()
    assert [item["mediaId"] for item in document["items"]] == [
        asset["id"],
        extra["id"],
    ]

    # The pass that previously removed it, because its id was not in the version.
    db = TestingSessionLocal()
    rundown.sync_active_rundowns(db)
    assert [row.media_id for row in media.queue_items("main", db)] == [
        asset["id"],
        extra["id"],
    ]
    db.close()


def test_removing_a_queue_item_also_removes_it_from_the_schedule(
    client, monkeypatch, tmp_path
):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    stable_id = seeded["document"]["items"][0]["stableItemId"]
    _go_live(monkeypatch, asset, stable_id)
    _mark_applied()

    extra = _second_asset(client, tmp_path)
    revision = client.get("/playlists/main").json()["desiredRevision"]
    client.post(
        "/playlists/main/items",
        json={"mediaId": extra["id"], "expectedRevision": revision},
    )
    _mark_applied()

    snapshot = client.get("/playlists/main").json()
    target = next(
        item for item in snapshot["items"] if item["media"]["id"] == extra["id"]
    )
    removed = client.delete(
        f"/playlists/main/items/{target['id']}"
        f"?expectedRevision={snapshot['desiredRevision']}"
    )
    assert removed.status_code == 202, removed.text

    # Left in the schedule it would render as a row with no queue row behind it,
    # and return outright on the next restart from the channel file.
    detail = client.get("/rundowns/main").json()
    document = client.get(
        f"/rundowns/main/versions/{detail['activeVersionId']}"
    ).json()
    assert [item["mediaId"] for item in document["items"]] == [asset["id"]]


def test_the_last_scheduled_item_cannot_be_removed(client, monkeypatch, tmp_path):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    stable_id = seeded["document"]["items"][0]["stableItemId"]
    _go_live(monkeypatch, asset, stable_id)
    _mark_applied()

    # Not the on-air guard: that returns 409 earlier. This is the empty-schedule
    # guard, which an engine cannot be handed.
    extra = _second_asset(client, tmp_path)
    revision = client.get("/playlists/main").json()["desiredRevision"]
    client.post(
        "/playlists/main/items",
        json={"mediaId": extra["id"], "expectedRevision": revision},
    )
    _mark_applied()
    snapshot = client.get("/playlists/main").json()
    keep = next(item for item in snapshot["items"] if item["media"]["id"] == extra["id"])
    client.delete(
        f"/playlists/main/items/{keep['id']}"
        f"?expectedRevision={snapshot['desiredRevision']}"
    )
    _mark_applied()

    # One item left, and it is on air, so removal is refused either way.
    snapshot = client.get("/playlists/main").json()
    assert len(snapshot["items"]) == 1
    refused = client.delete(
        f"/playlists/main/items/{snapshot['items'][0]['id']}"
        f"?expectedRevision={snapshot['desiredRevision']}"
    )
    assert refused.status_code == 409, refused.text


def test_queue_edits_stay_direct_when_no_rundown_governs_the_playlist(
    client, monkeypatch, tmp_path
):
    """Without the rundown service nothing deletes a plain queue row."""
    from .conftest import TestingSessionLocal

    monkeypatch.setenv("ENABLE_RUNDOWN_SERVICE", "false")
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(
        media,
        "_probe",
        lambda path: {"streams": [{"codec_type": "video"}], "format": {"duration": "30"}},
    )
    asset = _register(client, tmp_path, "clip.mp4")

    db = TestingSessionLocal()
    assert rundown.governs_playlist("main", db) is None
    db.close()

    revision = client.get("/playlists/main").json()["desiredRevision"]
    added = client.post(
        "/playlists/main/items",
        json={"mediaId": asset["id"], "expectedRevision": revision},
    )
    assert added.status_code == 202, added.text
    assert [item["media"]["id"] for item in added.json()["items"]] == [asset["id"]]


def test_inserting_the_same_clip_twice_gives_it_a_distinct_identity(
    client, monkeypatch, tmp_path
):
    """stable_item_id is _uuid(rundown, "item", key), so keys must not repeat."""
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    _go_live(monkeypatch, asset, seeded["document"]["items"][0]["stableItemId"])

    for _ in range(2):
        _mark_applied()
        revision = client.get("/playlists/main").json()["desiredRevision"]
        added = client.post(
            "/playlists/main/items",
            json={"mediaId": asset["id"], "expectedRevision": revision},
        )
        assert added.status_code == 202, added.text

    detail = client.get("/rundowns/main").json()
    document = client.get(f"/rundowns/main/versions/{detail['activeVersionId']}").json()
    keys = [item["key"] for item in document["items"]]
    stable = [item["stableItemId"] for item in document["items"]]
    assert len(document["items"]) == 3
    assert len(set(keys)) == 3, keys
    assert len(set(stable)) == 3, stable


def test_an_insert_is_placed_by_queue_position_even_across_a_bridge_row(
    client, monkeypatch, tmp_path
):
    """The queue is one longer than the schedule while a bridge row is at its head.

    Taking the queue index as a schedule index would drop the new clip one row
    late, which is the row the operator did not click.
    """
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    scheduled_id = seeded["document"]["items"][0]["stableItemId"]

    # Something outside the rundown is on air, so the projection bridges it.
    bridge_id = str(uuid4())
    _go_live(monkeypatch, asset, bridge_id)
    client.post(
        "/rundowns/main/apply",
        json={"ops": [{"op": "updateGraphic", "key": "opening-logo", "durationMs": 6000}]},
    )
    _mark_applied()
    queue = [item["id"] for item in client.get("/playlists/main").json()["items"]]
    assert queue == [bridge_id, scheduled_id], queue

    extra = _register(client, tmp_path, "second.mp4")
    revision = client.get("/playlists/main").json()["desiredRevision"]
    # Queue index 1 is "before the first scheduled row", i.e. schedule index 0.
    added = client.post(
        "/playlists/main/items",
        json={"mediaId": extra["id"], "position": 1, "expectedRevision": revision},
    )
    assert added.status_code == 202, added.text

    detail = client.get("/rundowns/main").json()
    document = client.get(f"/rundowns/main/versions/{detail['activeVersionId']}").json()
    assert [item["mediaId"] for item in document["items"]] == [extra["id"], asset["id"]]


def test_a_bridge_row_is_removed_directly_because_nothing_schedules_it(
    client, monkeypatch, tmp_path
):
    from .conftest import TestingSessionLocal

    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    scheduled_id = seeded["document"]["items"][0]["stableItemId"]
    bridge_id = str(uuid4())
    _go_live(monkeypatch, asset, bridge_id)
    client.post(
        "/rundowns/main/apply",
        json={"ops": [{"op": "updateGraphic", "key": "opening-logo", "durationMs": 6000}]},
    )
    _mark_applied()

    db = TestingSessionLocal()
    governing = rundown.governs_playlist("main", db)
    assert governing is not None
    assert rundown.schedules_item(governing, scheduled_id, db) is True
    assert rundown.schedules_item(governing, bridge_id, db) is False
    db.close()

    # The engine has moved on and the reconciler has restated the rows, but the
    # sweep that drops the bridge has not run yet. That is the only window in
    # which a bridge row is both deletable and still present.
    monkeypatch.setattr(media, "_live_input_and_current", lambda *_: ("input", scheduled_id))
    db = TestingSessionLocal()
    db.get(PlaylistItem, bridge_id).state = "queued"
    db.get(PlaylistItem, scheduled_id).state = "on_air"
    db.commit()
    db.close()

    snapshot = client.get("/playlists/main").json()
    removed = client.delete(
        f"/playlists/main/items/{bridge_id}"
        f"?expectedRevision={snapshot['desiredRevision']}"
    )
    assert removed.status_code == 202, removed.text
    assert [item["id"] for item in removed.json()["items"]] == [scheduled_id]


def test_an_insert_against_a_superseded_schedule_is_refused(
    client, monkeypatch, tmp_path
):
    """apply_to_live rolls back the router's lock, so the schedule's own
    optimistic check is what keeps two concurrent editors apart."""
    from .conftest import TestingSessionLocal

    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    _go_live(monkeypatch, asset, seeded["document"]["items"][0]["stableItemId"])
    _mark_applied()

    db = TestingSessionLocal()
    governing = rundown.governs_playlist("main", db)
    stale_version = governing.active_version_id
    db.close()

    # Someone else publishes first.
    client.post(
        "/rundowns/main/apply",
        json={"ops": [{"op": "updateGraphic", "key": "opening-logo", "durationMs": 7000}]},
    )
    _mark_applied()

    extra = _register(client, tmp_path, "second.mp4")
    db = TestingSessionLocal()
    governing = rundown.governs_playlist("main", db)
    assert governing.active_version_id != stale_version
    governing.active_version_id = stale_version  # pretend we read the old one
    with pytest.raises(ConflictException):
        rundown.enqueue_media(governing, extra["id"], None, db)
    db.close()


def test_an_unknown_media_id_cannot_enter_the_schedule(client, monkeypatch, tmp_path):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "main")
    _go_live(monkeypatch, asset, seeded["document"]["items"][0]["stableItemId"])
    _mark_applied()

    revision = client.get("/playlists/main").json()["desiredRevision"]
    refused = client.post(
        "/playlists/main/items",
        json={"mediaId": str(uuid4()), "expectedRevision": revision},
    )
    assert refused.status_code == 404, refused.text
    detail = client.get("/rundowns/main").json()
    document = client.get(f"/rundowns/main/versions/{detail['activeVersionId']}").json()
    assert len(document["items"]) == 1


def test_reset_publishes_programs_only_from_the_live_version(client, monkeypatch, tmp_path):
    asset, _, seeded = _seed(client, monkeypatch, tmp_path, "reset")
    _go_live(monkeypatch, asset)
    live_id = seeded["versionId"]

    # Reset acts on what is on air, so it is addressed by the live version.
    reset = client.post("/rundowns/reset/reset", json={"expectedVersionId": live_id})
    assert reset.status_code == 202, reset.text
    assert reset.json()["removedEventCount"] == 1
    published = reset.json()["version"]
    assert published["state"] == "published"
    assert published["graphics"] == []
    document = client.get(f"/rundowns/reset/versions/{published['id']}").json()
    assert len(document["items"]) == 1
    assert document["items"][0]["graphics"] == []
    assert document["scheduledGraphics"] == []

    _mark_applied()
    stale = client.post("/rundowns/reset/reset", json={"expectedVersionId": live_id})
    assert stale.status_code == 409
