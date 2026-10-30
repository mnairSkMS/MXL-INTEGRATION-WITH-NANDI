import logging
import os
import pathlib
import sqlite3
from datetime import datetime, timezone

from sqlalchemy import create_engine, event
from sqlalchemy.orm import sessionmaker
from .model.db import Base, RUNDOWN_TABLE_NAMES

log = logging.getLogger(__name__)

DATABASE_URL = f"sqlite:///{os.environ.get('DB_PATH', './events.sqlite')}"
# Create the database engine
engine = create_engine(DATABASE_URL)


@event.listens_for(engine, "connect")
def _configure_sqlite(dbapi_connection, _connection_record):
    cursor = dbapi_connection.cursor()
    cursor.execute("PRAGMA busy_timeout=5000")
    cursor.execute("PRAGMA journal_mode=WAL")
    cursor.close()


# Create a configured session class
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)


def _rundown_enabled() -> bool:
    return os.environ.get("ENABLE_RUNDOWN_SERVICE", "false").lower() in {"1", "true", "yes", "on"}


RUNDOWN_SCHEMA_VERSION = "2026-08-20-rundown-v3"
# v3 removed `rundown_drafts`: the schedule is edited by one atomic validated
# apply against the live version, so there is no working copy to store.
RUNDOWN_SCHEMA_V2 = "2026-08-11-rundown-v2"


def _backup_rundown_database(suffix: str) -> None:
    """Take a consistent SQLite backup before a destructive schema change.

    Never skips. An earlier run leaving a backup of the same name used to make this
    return early, so a second migration proceeded with no backup of the data it was
    about to drop - the one moment the safety net exists for. A taken name now gets a
    timestamped sibling instead, and a backup that cannot be written raises rather
    than letting the migration continue unprotected.
    """
    db_path = pathlib.Path(os.environ.get("DB_PATH", "./events.sqlite"))
    if not db_path.is_file():
        return
    backup = db_path.with_name(db_path.name + suffix)
    if backup.exists():
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")
        backup = db_path.with_name(f"{db_path.name}{suffix}.{stamp}")
    source = sqlite3.connect(db_path)
    destination = sqlite3.connect(backup)
    try:
        source.backup(destination)
    finally:
        destination.close()
        source.close()
    log.info("wrote pre-migration backup to %s", backup)


def _reset_rundown_schema(connection) -> bool:
    """Bring the authoring tables up to the current schema.

    Two very different paths meet here, and the difference matters:

    * **v2 to v3 is a real migration.** v3 only removes `rundown_drafts`, and
      every other table keeps its shape, so the drafts table is dropped in place
      and publish history, occurrences, executions and audit rows all survive.
    * **Anything older is a reset.** v1 stored drafts as immutable
      `rundown_versions` rows, so its history is a mix of publishes and
      abandoned saves that no later schema has a shape for. The tables are
      dropped and the declarative channel file republishes on the same start.

    Each path takes its own backup first, so whatever is discarded stays
    recoverable from disk.
    """
    tables = {
        row[0]
        for row in connection.exec_driver_sql(
            "SELECT name FROM sqlite_master WHERE type='table'"
        )
    }
    if "schema_migrations" not in tables:
        return False
    applied = {
        row[0] for row in connection.exec_driver_sql("SELECT version FROM schema_migrations")
    }
    if RUNDOWN_SCHEMA_VERSION in applied or not applied:
        return False
    if applied == {RUNDOWN_SCHEMA_V2}:
        # Deliberately not the reset below: falling through to it would drop ten
        # tables and lose as-run history for a change that only removes one.
        _backup_rundown_database(".before-rundown-v3")
        connection.exec_driver_sql("DROP TABLE IF EXISTS rundown_drafts")
        connection.exec_driver_sql("DELETE FROM schema_migrations")
        connection.exec_driver_sql(
            "INSERT OR IGNORE INTO schema_migrations(version) VALUES (?)",
            (RUNDOWN_SCHEMA_VERSION,),
        )
        return False
    _backup_rundown_database(".before-rundown-v2")
    for name in (RUNDOWN_TABLE_NAMES | {"rundown_drafts"}) - {"schema_migrations"}:
        connection.exec_driver_sql(f"DROP TABLE IF EXISTS {name}")
    connection.exec_driver_sql("DELETE FROM schema_migrations")
    return True


# None until something has actually checked. Only an explicit False - a schema
# inspected and found stale - is allowed to disable routes.
SCHEMA_READY: bool | None = None


def initialize_schema() -> None:
    """Create and migrate the control-plane schema.

    Called from the `preload_db` entrypoint, which supervisord runs to completion
    before either the API or the transcoder opens this database
    (`dependent_startup_wait_for=preload_db:exited`). This deliberately does not run
    at import: DDL as a side effect of `import transcoder_api.db` fired on every
    `--reload` cycle and during test collection, and put a destructive
    `DROP TABLE` path one stray import away.
    """
    global SCHEMA_READY

    # Keep the feature completely inert when disabled: the existing control-plane
    # schema is created exactly as before and no new tables or migration marker are
    # written. A later restart with the feature enabled performs the backup below.
    if _rundown_enabled():
        _backup_rundown_database(".before-rundown-v1")
        with engine.begin() as connection:
            _reset_rundown_schema(connection)

    core_tables = [
        table for table in Base.metadata.sorted_tables if table.name not in RUNDOWN_TABLE_NAMES
    ]
    Base.metadata.create_all(bind=engine, tables=core_tables)

    # This repository intentionally has no migration runner. Keep this additive migration
    # local, idempotent, and safe for the existing Compose database.
    with engine.begin() as connection:
        columns = {row[1] for row in connection.exec_driver_sql("PRAGMA table_info(playlist_state)")}
        if "apply_error" not in columns:
            connection.exec_driver_sql("ALTER TABLE playlist_state ADD COLUMN apply_error TEXT")
        if "active_input_id" not in columns:
            connection.exec_driver_sql("ALTER TABLE playlist_state ADD COLUMN active_input_id VARCHAR")

        cue_columns = {row[1] for row in connection.exec_driver_sql("PRAGMA table_info(graphics_cues)")}
        for name, column_type in (
            ("kind", "VARCHAR"),
            ("mode", "VARCHAR"),
            ("starts_at", "DATETIME"),
            ("ends_at", "DATETIME"),
            ("restore_until", "DATETIME"),
        ):
            if name not in cue_columns:
                connection.exec_driver_sql(f"ALTER TABLE graphics_cues ADD COLUMN {name} {column_type}")

    if _rundown_enabled():
        rundown_tables = [
            table for table in Base.metadata.sorted_tables if table.name in RUNDOWN_TABLE_NAMES
        ]
        Base.metadata.create_all(bind=engine, tables=rundown_tables)
        with engine.begin() as connection:
            # This marker documents the additive control-plane expansion. Existing
            # execution tables are never altered, so an older application image can
            # still open this database.
            connection.exec_driver_sql(
                "INSERT OR IGNORE INTO schema_migrations(version) VALUES (?)",
                (RUNDOWN_SCHEMA_VERSION,),
            )

    SCHEMA_READY = True


def verify_schema() -> bool:
    """Report whether this database has been migrated. Issues no DDL.

    `preload_db` is configured `autorestart=false` with `startsecs=0`, so a failed
    run still releases the programs waiting on it - by design, so a bad preload does
    not take the channel dark. The API therefore cannot assume the schema is current
    and has to look.
    """
    global SCHEMA_READY

    try:
        with engine.connect() as connection:
            tables = {
                row[0]
                for row in connection.exec_driver_sql(
                    "SELECT name FROM sqlite_master WHERE type='table'"
                )
            }
            required = {
                table.name
                for table in Base.metadata.sorted_tables
                if table.name not in RUNDOWN_TABLE_NAMES
            }
            missing = required - tables
            if missing:
                log.error("control-plane schema is incomplete; missing %s", sorted(missing))
                SCHEMA_READY = False
                return False

            if _rundown_enabled():
                if "schema_migrations" not in tables:
                    log.error("rundown service is enabled but the schema has never been migrated")
                    SCHEMA_READY = False
                    return False
                applied = {
                    row[0]
                    for row in connection.exec_driver_sql("SELECT version FROM schema_migrations")
                }
                if RUNDOWN_SCHEMA_VERSION not in applied:
                    log.error(
                        "rundown schema is stale: found %s, expected %s",
                        sorted(applied) or "nothing",
                        RUNDOWN_SCHEMA_VERSION,
                    )
                    SCHEMA_READY = False
                    return False
    except Exception:
        log.exception("could not verify the control-plane schema")
        SCHEMA_READY = False
        return False

    SCHEMA_READY = True
    return True


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
