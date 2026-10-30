"""The engine's configuration document, read and replaced whole.

This is a provisioning surface, not a runtime one: the transcoder parses its config once at
start, so a write here does nothing until the engine is restarted. That shapes everything
below. There is no partial update, no revision history and no rollback copy, because the
document is written once while a channel is being brought up rather than edited while it runs.

Two constraints are worth stating because neither is visible from the code:

Validation is structural only. transcoder/config/doc/openapi.yaml describes this document, but
doc/codebase/27 records that it diverges from the C++ parser - bandwidth/bandwith,
mulitcast/multicast, logger fatal/none, and omitted NDI, playlist and S3 objects - so
validating against it would refuse configs the engine accepts. The parser is the authority;
what is checked here is only what makes a document structurally unusable.

The write is an atomic rename, following services/bootstrap.py. That means the config must
live inside a bind-mounted *directory*: a single-file bind mount is pinned to one inode and
os.replace fails EBUSY against it.
"""

from __future__ import annotations

import errno
import json
import os
import pathlib
from contextlib import suppress
from typing import Any

from ..exceptions import (
    ConflictException,
    NotFoundException,
    TranscoderApiException,
    UnprocessableException,
)
from ..model.rest_api import ErrorCode

DEFAULT_PATH = "/opt/utpl/etc/config.json"


def config_path() -> pathlib.Path:
    return pathlib.Path(os.environ.get("TRANSCODER_CONFIG_PATH", DEFAULT_PATH))


def read() -> dict[str, Any]:
    """The document on disk, or a typed failure naming what is wrong with it."""
    path = config_path()
    try:
        document = json.loads(path.read_text())
    except FileNotFoundError as exc:
        raise NotFoundException(f"no engine config at {path}") from exc
    except json.JSONDecodeError as exc:
        raise UnprocessableException(f"engine config at {path} is not valid JSON: {exc}") from exc
    except OSError as exc:
        raise UnprocessableException(
            f"engine config at {path} could not be read: {exc.strerror}"
        ) from exc

    if not isinstance(document, dict):
        raise UnprocessableException(f"engine config at {path} is not a JSON object")

    return document


def validate(document: Any) -> None:
    """Refuse only what the engine could not start from.

    Unknown keys pass through untouched - see the module docstring for why rejecting them
    would be wrong. What is caught here is the shape: an object where an array belongs is the
    failure the retired setters used to write, and it produces a file that parses as JSON and
    then fails at engine start with nothing pointing back at the write.
    """
    if not isinstance(document, dict):
        raise UnprocessableException("config must be a JSON object")

    if "version" not in document:
        raise UnprocessableException("config is missing 'version'")

    transcoder = document.get("transcoder")
    if not isinstance(transcoder, dict):
        raise UnprocessableException("config is missing a 'transcoder' object")

    for key in ("inputs", "outputs"):
        value = transcoder.get(key)
        if not isinstance(value, list):
            raise UnprocessableException(f"'transcoder.{key}' must be an array")
        if not value:
            raise UnprocessableException(f"'transcoder.{key}' must not be empty")
        for index, entry in enumerate(value):
            if not isinstance(entry, dict):
                raise UnprocessableException(f"'transcoder.{key}[{index}]' must be an object")

    application = document.get("application")
    if application is not None and not isinstance(application, dict):
        raise UnprocessableException("'application' must be an object")


def require_writable() -> pathlib.Path:
    """Refuse a write that could not land, before touching anything."""
    path = config_path()
    directory = path.parent

    if not directory.is_dir():
        raise ConflictException(f"the engine config directory does not exist: {directory}")

    # Probed rather than inspected with os.access: the container runs as root, for whom
    # access() reports success on a directory it cannot in fact write through. Creating the
    # file is the only answer that is never wrong, and it also catches a read-only bind mount.
    probe = directory / f".{path.name}.writable"
    try:
        probe.touch()
        probe.unlink()
    except OSError as exc:
        raise ConflictException(
            f"the engine config directory is not writable: {directory} ({exc.strerror}). "
            "Mount it read-write."
        ) from exc

    return path


def check(document: dict[str, Any]) -> dict[str, Any]:
    """Validate a document and report where it would go, without writing it."""
    validate(document)
    path = require_writable()
    return {"state": "validated", "path": str(path), "restartRequired": False}


def write(document: dict[str, Any]) -> dict[str, Any]:
    """Replace the config with an atomic rename, or raise saying why not.

    Mirrors stage_channel_file/commit_channel_file in services/bootstrap.py, which does the
    same job for the channel file. Those two are kept separate there so the rundown publish
    can stage before its transaction commits; there is no transaction to straddle here, so the
    two halves are inlined. Unlike commit_channel_file, which reports failure rather than
    raising because its publish is already on air, this must raise: nothing is live yet, and a
    write that silently did not happen is the bug this whole route exists to fix.
    """
    validate(document)
    path = require_writable()

    # Captured before the write: os.replace installs a new inode, so the container's
    # root-owned default would otherwise leave the operator needing sudo to hand-edit their
    # own file on the host side of the bind mount.
    try:
        existing = path.stat()
        uid, gid, mode = existing.st_uid, existing.st_gid, existing.st_mode & 0o7777
    except FileNotFoundError:
        directory = path.parent.stat()
        uid, gid, mode = directory.st_uid, directory.st_gid, 0o644

    body = json.dumps(document, indent=4) + "\n"
    temporary = path.with_name(path.name + ".tmp")

    try:
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        try:
            os.write(descriptor, body.encode("utf-8"))
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    except OSError as exc:
        with suppress(OSError):
            temporary.unlink(missing_ok=True)
        raise TranscoderApiException(
            f"could not stage a replacement for {path}: {exc.strerror}",
            ErrorCode.UNAVAILABLE,
            http_code=500,
        ) from exc

    try:
        os.replace(temporary, path)
        os.chmod(path, mode)
        if os.geteuid() == 0:
            os.chown(path, uid, gid)
    except OSError as exc:
        with suppress(OSError):
            temporary.unlink(missing_ok=True)
        if exc.errno in (errno.EBUSY, errno.ETXTBSY):
            # The documented single-file bind mount trap (doc/codebase/28). Reachable through
            # the docker run -v form in doc/misc/docker-deployment.md even though the shipped
            # compose profile mounts the directory.
            raise ConflictException(
                f"{path} is bind-mounted as a single file, so it cannot be replaced. "
                "Mount the containing directory read-write and point "
                "TRANSCODER_CONFIG_PATH at a file inside it."
            ) from exc
        raise TranscoderApiException(
            f"could not replace {path}: {exc.strerror}",
            ErrorCode.UNAVAILABLE,
            http_code=500,
        ) from exc

    return {"state": "written", "path": str(path), "restartRequired": True}
