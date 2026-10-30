from __future__ import annotations

import uuid
from datetime import datetime, timedelta, timezone

import pytest
import yaml  # type: ignore[import-untyped]

from transcoder_api.exceptions import TranscoderApiException
from transcoder_api.model.db import (
    MediaAsset,
    PlaylistItem,
    PlaylistState,
    Rundown,
    RundownAuditLog,
    RundownEventDefinition,
    RundownEventExecution,
    RundownOutbox,
    RundownVersion,
)
from transcoder_api.services import bootstrap, media, rundown

PROBE = {"streams": [{"codec_type": "video"}], "format": {"duration": "30"}}


@pytest.fixture()
def env(monkeypatch, tmp_path):
    monkeypatch.setenv("ENABLE_RUNDOWN_SERVICE", "true")
    monkeypatch.setenv("ENABLE_VIDEO_GRAPHICS", "false")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_STRICT", "false")
    monkeypatch.delenv("PLAYLIST_BOOTSTRAP_FILE", raising=False)
    monkeypatch.setenv("MEDIA_DIR", str(tmp_path))
    monkeypatch.setattr(media, "_probe", lambda path: PROBE)

    # preload_db runs before the engine, so nothing in this path may depend on
    # a live gRPC input.  Failing loudly here is the regression guard.
    def _no_engine(*_):
        raise RuntimeError("transcoder reported no inputs")

    monkeypatch.setattr(media, "_live_input_and_current", _no_engine)
    return tmp_path


def _clip(root, name: str) -> str:
    (root / name).write_bytes(b"media-bytes")
    return name


def _write(root, text: str):
    path = root / "channel.yml"
    path.write_text(text)
    return path


def test_cold_start_publishes_without_a_running_engine(env, test_db):
    _clip(env, "one.mp4")
    _clip(env, "two.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n  - media: two.mp4\n")

    result = bootstrap.apply_file(path, test_db)

    assert result.state == "applied"
    items = media.queue_items("main", test_db)
    assert [item.position for item in items] == [1, 2]
    assert [item.id for item in items] == [
        rundown._uuid("main", "item", "one"), rundown._uuid("main", "item", "two")
    ]
    state = test_db.get(PlaylistState, "main")
    assert state.revision >= 1
    # Forces exactly one snapshot replay once the engine registers its input.
    assert state.applied_revision == 0
    assert state.apply_error is None
    assert state.active_input_id == "bootstrap"
    versions = test_db.query(RundownVersion).all()
    assert len(versions) == 1 and versions[0].state == "published"


def test_bootstrap_replaces_operator_edits(env, test_db):
    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")
    ghost = MediaAsset(
        id=str(uuid.uuid4()), relative_path="ghost.mp4", display_name="ghost.mp4",
        size_bytes=1, sha256="deadbeef", probe_json="{}",
    )
    test_db.add(ghost)
    test_db.add(PlaylistState(
        playlist_id="main", run_state="running", revision=4, applied_revision=4, skip_seq=0
    ))
    test_db.add(PlaylistItem(
        id=str(uuid.uuid4()), playlist_id="main", media_id=ghost.id, position=1, state="on_air"
    ))
    test_db.commit()

    bootstrap.apply_file(path, test_db)

    assert [item.id for item in media.queue_items("main", test_db)] == [
        rundown._uuid("main", "item", "one")
    ]


def test_bootstrap_supersedes_prior_versions_and_cancels_pending_commands(env, test_db):
    _clip(env, "one.mp4")
    _clip(env, "two.mp4")
    document = (
        "channel: main\n"
        "playlist:\n"
        "  - media: one.mp4\n"
        "    graphics:\n"
        "      - use: logo-default\n"
        "        mode: snap\n"
        "        duration: 5s\n"
    )
    path = _write(env, document)
    first = bootstrap.apply_file(path, test_db)

    definition = test_db.query(RundownEventDefinition).filter_by(
        version_id=first.version_id
    ).one()
    execution = RundownEventExecution(
        id=str(uuid.uuid4()), execution_key="pending-x", definition_id=definition.id,
        occurrence_id=None, planned_at=datetime.now(timezone.utc), state="pending",
    )
    test_db.add(execution)
    test_db.add(RundownOutbox(
        id=str(uuid.uuid4()), dedupe_key="pending-x", command_type="graphics_cue",
        payload_json='{"executionId":"unused"}', state="pending", attempts=0,
        next_attempt_at=datetime.now(timezone.utc),
    ))
    test_db.commit()
    path.write_text(document + "  - media: two.mp4\n")
    second = bootstrap.apply_file(path, test_db)

    assert second.state == "applied"
    assert test_db.get(RundownEventExecution, execution.id).state == "canceled"
    assert test_db.query(RundownOutbox).filter_by(dedupe_key="pending-x").one().state == "canceled"
    states = {row.id: row.state for row in test_db.query(RundownVersion).all()}
    assert states.pop(second.version_id) == "published"
    assert set(states.values()) == {"superseded"}


def test_expired_and_far_future_wall_clock_graphics_are_skipped(env, test_db):
    _clip(env, "one.mp4")
    now = rundown.utc_now()
    path = _write(env, f"""channel: main
playlist:
  - media: one.mp4
scheduled:
  - use: logo-default
    mode: snap
    duration: 5s
    at: "{(now - timedelta(hours=1)).isoformat()}"
  - use: bug-default
    mode: snap
    duration: 5s
    at: "{(now + timedelta(hours=1)).isoformat()}"
  - use: logo-default
    mode: snap
    duration: 5s
    at: "{(now + timedelta(days=30)).isoformat()}"
""")

    result = bootstrap.apply_file(path, test_db)

    assert result.state == "applied"
    wall_clock = test_db.query(RundownEventDefinition).filter_by(
        version_id=result.version_id, trigger_type="wall-clock"
    ).all()
    assert [row.asset_id for row in wall_clock] == ["bug-default"]
    assert len(result.warnings) == 2
    assert any("already past" in warning for warning in result.warnings)
    assert any("14-day" in warning for warning in result.warnings)
    audit = test_db.query(RundownAuditLog).filter_by(action="bootstrap_published").one()
    assert "already past" in audit.details_json


def test_naive_wall_clock_resolves_in_the_channel_timezone(env, test_db):
    _clip(env, "one.mp4")
    local = (rundown.utc_now() + timedelta(hours=6)).astimezone(
        timezone(timedelta(hours=5, minutes=30))
    )
    path = _write(env, f"""channel: main
timezone: Asia/Kolkata
playlist:
  - media: one.mp4
scheduled:
  - use: logo-default
    mode: snap
    duration: 5s
    at: "{local.replace(tzinfo=None).isoformat(timespec='seconds')}"
""")

    result = bootstrap.apply_file(path, test_db)

    definition = test_db.query(RundownEventDefinition).filter_by(
        version_id=result.version_id, trigger_type="wall-clock"
    ).one()
    assert rundown._utc(definition.starts_at).replace(microsecond=0) == local.astimezone(
        timezone.utc
    ).replace(microsecond=0)


def test_duration_grammar_requires_a_unit(env):
    assert bootstrap.parse_duration("5s", "f") == 5000
    assert bootstrap.parse_duration("600ms", "f") == 600
    assert bootstrap.parse_duration("1.5s", "f") == 1500
    assert bootstrap.parse_duration(0, "f") == 0
    # A bare integer would silently mean milliseconds, so `duration: 5` has to
    # be a hard error rather than a five-millisecond graphic.
    for bad in (5000, "5", "1m30s", "-1s", True, None):
        with pytest.raises(TranscoderApiException):
            bootstrap.parse_duration(bad, "f")


def test_item_keys_slug_from_media_and_survive_reordering(env, test_db):
    _clip(env, "Ad Spot.mp4")
    _clip(env, "other.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: Ad Spot.mp4\n  - media: other.mp4\n")
    first = bootstrap.apply_file(path, test_db)
    before = {row.key: row.stable_item_id for row in rundown.version_items(first.version_id, test_db)}
    assert set(before) == {"ad-spot", "other"}

    path.write_text("channel: main\nplaylist:\n  - media: other.mp4\n  - media: Ad Spot.mp4\n")
    second = bootstrap.apply_file(path, test_db)

    after = {row.key: row.stable_item_id for row in rundown.version_items(second.version_id, test_db)}
    # Identity is what as-run history joins on, so a reorder must not mint new IDs.
    assert before == after
    assert [item.id for item in media.queue_items("main", test_db)] == [
        after["other"], after["ad-spot"]
    ]


def test_duplicate_media_disambiguates_keys(env, test_db):
    _clip(env, "clip.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: clip.mp4\n  - media: clip.mp4\n")
    result = bootstrap.apply_file(path, test_db)
    assert [row.key for row in rundown.version_items(result.version_id, test_db)] == [
        "clip", "clip-2"
    ]


def test_timeline_violation_leaves_the_database_untouched(env, test_db):
    _clip(env, "one.mp4")
    # 30 s of L-band plus the 1200 ms restore does not fit a 30 s clip.  The
    # HTTP YAML routes would accept this; the bootstrap must not.
    path = _write(env, """channel: main
playlist:
  - media: one.mp4
    graphics:
      - use: lband-playboxtv
        mode: reveal
        at: 0
        duration: 30s
        fade: 600ms
""")

    with pytest.raises(TranscoderApiException):
        bootstrap.apply_file(path, test_db)

    assert media.queue_items("main", test_db) == []
    assert test_db.query(RundownVersion).count() == 0


def test_invalid_file_is_non_fatal_and_strict_mode_exits(env, test_db, monkeypatch):
    path = _write(env, "channel: main\nplaylist: [oops\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILE", str(path))

    result = bootstrap.run(test_db)

    assert result.state == "failed"
    assert test_db.query(RundownVersion).count() == 0
    assert test_db.query(RundownAuditLog).filter_by(action="bootstrap_failed").count() == 1

    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_STRICT", "true")
    with pytest.raises(SystemExit):
        bootstrap.run(test_db)


def test_unregistered_media_is_imported_and_missing_media_is_reported(env, test_db):
    _clip(env, "fresh.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: fresh.mp4\n")
    assert test_db.query(MediaAsset).count() == 0

    bootstrap.apply_file(path, test_db)

    asset = test_db.query(MediaAsset).one()
    assert asset.relative_path == "fresh.mp4" and asset.probe_json

    path.write_text("channel: main\nplaylist:\n  - media: nope.mp4\n")
    with pytest.raises(TranscoderApiException) as excinfo:
        bootstrap.apply_file(path, test_db)
    assert "does not exist" in excinfo.value.message


def test_bootstrap_requires_the_rundown_service(env, test_db, monkeypatch):
    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILE", str(path))
    monkeypatch.setenv("ENABLE_RUNDOWN_SERVICE", "false")

    result = bootstrap.run(test_db)

    assert result.state == "failed"
    assert "ENABLE_RUNDOWN_SERVICE" in (result.error or "")
    assert test_db.query(RundownVersion).count() == 0


def test_unchanged_file_does_not_create_a_new_version(env, test_db):
    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")
    assert bootstrap.apply_file(path, test_db).state == "applied"

    assert bootstrap.apply_file(path, test_db).state == "unchanged"
    assert test_db.query(RundownVersion).count() == 1
    # The skip still has to report a fresh outcome, or /rundowns/status would
    # keep showing the timestamp of an older start that did rewrite the queue.
    assert test_db.query(RundownAuditLog).filter_by(action="bootstrap_unchanged").count() == 1

    # The queue can be mutated straight from the Playlist panel without ever
    # creating a version, so a checksum match alone must not be trusted.
    test_db.query(PlaylistItem).delete()
    test_db.commit()
    assert bootstrap.apply_file(path, test_db).state == "applied"
    assert test_db.query(RundownVersion).count() == 2


def test_rundown_status_reports_bootstrap_governance(env, test_db, client, monkeypatch):
    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILE", str(path))
    result = bootstrap.run(test_db)

    body = client.get("/rundowns/status").json()

    assert body["bootstrapManaged"] is True
    assert body["bootstrapState"] == "applied"
    assert body["bootstrapFile"] == str(path)
    assert body["bootstrapChecksum"] == result.checksum


def _publish_over(path, test_db, monkeypatch, document: str):
    """Bring a file on air, then publish an edited draft of it."""
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILE", str(path))
    applied = bootstrap.apply_file(path, test_db)
    assert applied.state == "applied"
    state = test_db.get(PlaylistState, "main")
    state.applied_revision = state.revision
    test_db.commit()
    # Publish needs a confirmed on-air item; the bootstrap runs without one.
    on_air = media.queue_items("main", test_db)[0].id
    monkeypatch.setattr(media, "_live_input_and_current", lambda *_: ("input", on_air))
    version, _, channel_file, _ = rundown.apply_operations(
        "main",
        test_db,
        [{
            "op": "addGraphic", "itemKey": "one", "assetId": "logo-default",
            "mode": "snap", "durationMs": 5000, "fadeMs": 0, "offsetMs": 2000,
            "key": "one-logo",
        }],
    )
    return version, channel_file


def test_publish_writes_the_channel_file_and_the_result_round_trips(
    env, test_db, monkeypatch
):
    """The invariant that makes writeback safe: a restart must be a no-op."""
    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")

    version, outcome = _publish_over(path, test_db, monkeypatch, "")

    assert outcome["state"] == "written"
    written = path.read_text()
    assert written.startswith("# Managed by the transcoder rundown service.")
    assert "use: logo-default" in written
    # Every field is explicit, so a catalog change cannot silently reinterpret
    # a file the service wrote.
    assert "mode: snap" in written and "duration: 5s" in written and "at: 2s" in written

    # Re-reading what we just wrote must reproduce the same canonical document,
    # or every restart would republish a schedule that is already on air.
    _, lowered, warnings = bootstrap.lower_to_rundown_v1(yaml.safe_load(written), test_db)
    assert warnings == []
    bootstrap._register_media(lowered, test_db)
    source = yaml.safe_dump(lowered, sort_keys=False, allow_unicode=True)
    assert rundown.parse_yaml(source, "main", test_db).source_yaml == version.source_yaml
    assert bootstrap.apply_file(path, test_db).state == "unchanged"


def test_writeback_preserves_the_original_ownership_and_mode(env, test_db, monkeypatch):
    """`os.replace` installs a new inode; the operator must still own the file."""
    import os

    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")
    os.chmod(path, 0o640)
    before = path.stat()

    _publish_over(path, test_db, monkeypatch, "")

    after = path.stat()
    assert after.st_mode & 0o7777 == 0o640
    assert (after.st_uid, after.st_gid) == (before.st_uid, before.st_gid)
    assert not path.with_name(path.name + ".tmp").exists()


def test_writeback_can_be_disabled_without_blocking_publish(env, test_db, monkeypatch):
    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_WRITEBACK", "false")
    original = path.read_text()

    _, outcome = _publish_over(path, test_db, monkeypatch, "")

    assert outcome["state"] == "skipped"
    assert path.read_text() == original


def test_publish_refuses_before_the_queue_moves_when_the_file_cannot_be_written(
    env, test_db, monkeypatch
):
    """Failing early is the safe direction: an unpersisted publish reverts.

    The unwritable case is provoked with a missing directory rather than a mode
    change, because the container runs as root and root ignores directory modes.
    """
    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILE", str(path))
    bootstrap.apply_file(path, test_db)
    state = test_db.get(PlaylistState, "main")
    state.applied_revision = state.revision
    test_db.commit()
    on_air = media.queue_items("main", test_db)[0].id
    monkeypatch.setattr(media, "_live_input_and_current", lambda *_: ("input", on_air))
    before_revision = test_db.get(PlaylistState, "main").revision
    before_version = test_db.get(Rundown, "main").active_version_id

    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILE", str(env / "gone" / "channel.yml"))
    with pytest.raises(TranscoderApiException) as excinfo:
        rundown.apply_operations(
            "main",
            test_db,
            [{"op": "addGraphic", "itemKey": "one", "assetId": "logo-default"}],
        )

    assert excinfo.value.http_code == 409
    assert "does not exist" in excinfo.value.message
    # Nothing moved: the refusal happens before the queue or the version does.
    assert test_db.get(PlaylistState, "main").revision == before_revision
    assert test_db.get(Rundown, "main").active_version_id == before_version


def test_video_graphics_gate_applies_to_the_bootstrap_file(env, test_db):
    _clip(env, "one.mp4")
    path = _write(env, """channel: main
playlist:
  - media: one.mp4
    graphics:
      - use: lband-video
        mode: reveal
        duration: 4s
""")

    with pytest.raises(TranscoderApiException) as excinfo:
        bootstrap.apply_file(path, test_db)
    assert "safety gate" in excinfo.value.message


def test_v2_to_v3_drops_only_the_drafts_table_and_keeps_history(tmp_path, monkeypatch):
    """Bumping the marker must not take the v1 reset path.

    v3 removes `rundown_drafts` and nothing else, so publish history and as-run
    rows have to survive. Falling through to `_reset_rundown_schema`'s reset
    would drop ten tables instead of one.
    """
    import importlib
    import sqlite3

    db_path = tmp_path / "events.sqlite"
    connection = sqlite3.connect(db_path)
    connection.executescript(
        """
        CREATE TABLE schema_migrations (version VARCHAR PRIMARY KEY);
        CREATE TABLE rundown_drafts (rundown_id VARCHAR PRIMARY KEY, source_yaml TEXT);
        CREATE TABLE rundowns (id VARCHAR PRIMARY KEY, name VARCHAR);
        CREATE TABLE rundown_versions (id VARCHAR PRIMARY KEY, rundown_id VARCHAR);
        CREATE TABLE playback_occurrences (id VARCHAR PRIMARY KEY, version_id VARCHAR);
        CREATE TABLE rundown_audit_log (id INTEGER PRIMARY KEY, action VARCHAR);
        INSERT INTO schema_migrations VALUES ('2026-08-11-rundown-v2');
        INSERT INTO rundown_drafts VALUES ('main', 'stale: yaml');
        INSERT INTO rundowns VALUES ('main', 'Main');
        INSERT INTO rundown_versions VALUES ('v1', 'main');
        INSERT INTO playback_occurrences VALUES ('o1', 'v1');
        INSERT INTO rundown_audit_log VALUES (1, 'published');
        """
    )
    connection.commit()
    connection.close()

    monkeypatch.setenv("DB_PATH", str(db_path))
    monkeypatch.setenv("ENABLE_RUNDOWN_SERVICE", "true")
    from transcoder_api import db as db_module

    # Reloading the module used to be how this migration was triggered, because the
    # DDL ran at import. It is now an explicit call, so the test names what it is
    # exercising -- but the reload still has to happen, because the engine is bound
    # to DB_PATH as it was when the module was first imported.
    importlib.reload(db_module)
    db_module.initialize_schema()

    check = sqlite3.connect(db_path)
    tables = {
        row[0] for row in check.execute("SELECT name FROM sqlite_master WHERE type='table'")
    }
    assert "rundown_drafts" not in tables
    # The history that a reset would have destroyed is still here.
    assert check.execute("SELECT count(*) FROM rundown_versions").fetchone()[0] == 1
    assert check.execute("SELECT count(*) FROM playback_occurrences").fetchone()[0] == 1
    assert check.execute("SELECT count(*) FROM rundown_audit_log").fetchone()[0] == 1
    assert [row[0] for row in check.execute("SELECT version FROM schema_migrations")] == [
        "2026-08-20-rundown-v3"
    ]
    check.close()
    assert (tmp_path / "events.sqlite.before-rundown-v3").is_file()


def _write_named(root, name: str, text: str):
    path = root / name
    path.write_text(text)
    return path


def test_every_declared_channel_becomes_its_own_managed_playlist(env, test_db, monkeypatch):
    """A runtime holds more than one playlist: a programme and a break reel.

    Both are authored the same way, and each one owns a separate queue, so a
    cue-out can put the second on air without disturbing the first.
    """
    _clip(env, "programme.mp4")
    _clip(env, "spot.mp4")
    main = _write_named(env, "channel.yml", "channel: main\nplaylist:\n  - media: programme.mp4\n")
    ads = _write_named(env, "ads.yml", "channel: ads\nplaylist:\n  - media: spot.mp4\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILES", f"{main},{ads}")

    results = bootstrap.run_all(test_db)

    assert set(results) == {"main", "ads"}
    assert [result.state for result in results.values()] == ["applied", "applied"]

    # Separate queues, not one merged playlist.
    assert [item.media_id for item in media.queue_items("main", test_db)] != []
    assert len(media.queue_items("main", test_db)) == 1
    assert len(media.queue_items("ads", test_db)) == 1

    # Declaration order is the binding to the engine's inputs, so it has to be
    # the file order and not, say, alphabetical.
    assert media.managed_playlist_order() == ["main", "ads"]

    # The single-result entry point still describes the default channel.
    assert bootstrap.run(test_db).state == "unchanged"


def test_a_broken_second_channel_does_not_stop_the_first(env, test_db, monkeypatch):
    """A malformed break reel must not keep the programme off air."""
    _clip(env, "programme.mp4")
    main = _write_named(env, "channel.yml", "channel: main\nplaylist:\n  - media: programme.mp4\n")
    ads = _write_named(env, "ads.yml", "channel: ads\nplaylist: [oops\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILES", f"{main},{ads}")

    results = bootstrap.run_all(test_db)

    assert results["main"].state == "applied"
    assert results["ads"].state == "failed"
    assert len(media.queue_items("main", test_db)) == 1


def test_publish_writes_back_to_the_file_that_declares_the_channel(env, monkeypatch):
    """With several channel files, a publish must not overwrite the wrong one."""
    main = _write_named(env, "channel.yml", "channel: main\nplaylist: []\n")
    ads = _write_named(env, "ads.yml", "channel: ads\nplaylist: []\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILES", f"{main},{ads}")

    assert bootstrap.bootstrap_path("main") == main
    assert bootstrap.bootstrap_path("ads") == ads
    # No channel asked for: the first declared file, which is the programme.
    assert bootstrap.bootstrap_path() == main


def test_the_single_file_variable_still_works(env, test_db, monkeypatch):
    """PLAYLIST_BOOTSTRAP_FILE is the deployed spelling and stays supported."""
    _clip(env, "one.mp4")
    path = _write(env, "channel: main\nplaylist:\n  - media: one.mp4\n")
    monkeypatch.setenv("PLAYLIST_BOOTSTRAP_FILE", str(path))

    assert bootstrap.bootstrap_paths() == [path]
    assert bootstrap.run(test_db).state == "applied"
    # An unknown channel resolves to the only file there is, so a publish from a
    # differently-named rundown still persists.
    assert bootstrap.bootstrap_path("clock") == path
