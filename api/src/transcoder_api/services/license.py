"""The engine's installed license file: inspect it, replace it, remove it.

The license is encrypted and signed with keys that exist only inside the transcoder binary
(embedded at build time, bound to the binary's own hash - see doc/codebase/22). Nothing in
Python can decrypt one, so every inspection shells out to `transcoder -l <file> --license-info`,
which runs the same load/verify/decrypt path the engine runs at startup and prints one JSON
object. That keeps the engine the only authority on whether a license is good.

The engine reads its license once, at start, so like the config this is a provisioning surface:
a new license does nothing until the engine restarts. The router does that restart.

A replacement is staged next to the live file and inspected there before the atomic rename, so
a file the engine would refuse never displaces a working one. As with the engine config, that
rename needs the license to live inside a bind-mounted *directory*; a single-file mount is
pinned to one inode and os.replace fails EBUSY against it.
"""

from __future__ import annotations

import errno
import json
import os
import pathlib
import subprocess
from contextlib import suppress
from typing import Any

from ..exceptions import (
    ConflictException,
    NotFoundException,
    TranscoderApiException,
    UnprocessableException,
)
from ..model.rest_api import ErrorCode
from . import engine_config

DEFAULT_PATH = "/opt/utpl/etc/transcoder.lic"
DEFAULT_BINARY = "/opt/utpl/bin/transcoder"

# Generous. A real license is under 1 KiB; this only stops an operator uploading a video by
# mistake and having it read into memory.
MAX_BYTES = 64 * 1024

_INSPECT_TIMEOUT_SECONDS = 15


def license_path() -> pathlib.Path:
    return pathlib.Path(os.environ.get("TRANSCODER_LICENSE_PATH", DEFAULT_PATH))


def _binary() -> str:
    return os.environ.get("TRANSCODER_BINARY", DEFAULT_BINARY)


def inspect(path: pathlib.Path) -> dict[str, Any]:
    """Ask the engine binary what it makes of a license file.

    Returns the engine's report, whose status is valid, expired, notYetValid or invalid. Raises
    only when the binary itself could not be asked - an unusable license is a report, not an
    error, because GET /license must be able to describe a bad installed file.
    """
    try:
        completed = subprocess.run(
            [_binary(), "-l", str(path), "--license-info"],
            capture_output=True,
            text=True,
            timeout=_INSPECT_TIMEOUT_SECONDS,
            check=False,
        )
    except FileNotFoundError as exc:
        raise TranscoderApiException(
            f"the transcoder binary is not available at {_binary()}, so no license can be "
            "inspected",
            ErrorCode.UNAVAILABLE,
            http_code=503,
        ) from exc
    except subprocess.TimeoutExpired as exc:
        raise TranscoderApiException(
            "the transcoder did not finish inspecting the license in time",
            ErrorCode.UNAVAILABLE,
            http_code=503,
        ) from exc

    # The report is the last line. Taking the last one rather than the whole stream keeps any
    # log line the engine's libraries print on startup from breaking the parse.
    for line in reversed(completed.stdout.splitlines()):
        line = line.strip()
        if line.startswith("{"):
            try:
                report = json.loads(line)
            except json.JSONDecodeError:
                break
            if isinstance(report, dict) and "status" in report:
                return report
            break

    # No report at all means this binary predates --license-info, or crashed.
    raise TranscoderApiException(
        "the transcoder did not report on the license (exit "
        f"{completed.returncode}): {(completed.stderr or completed.stdout).strip()[:300]}",
        ErrorCode.UNAVAILABLE,
        http_code=503,
    )


def read() -> dict[str, Any]:
    """The installed license as the engine sees it, or 404 when none is installed."""
    path = license_path()
    if not path.is_file():
        raise NotFoundException(f"no license is installed at {path}")
    return {"path": str(path), **inspect(path)}


def _refusal(report: dict[str, Any]) -> str | None:
    """Why the engine would refuse to start on this license, or None if it would not."""
    status = report.get("status")
    if status == "invalid":
        return (
            "not a license this transcoder accepts - it is corrupted, or was not signed for "
            f"this build: {report.get('error', 'unknown error')}"
        )
    if status == "expired":
        return "the license has expired"
    if status == "notYetValid":
        return "the license is not valid yet"
    if status != "valid":
        return f"the transcoder reported an unknown license status '{status}'"

    # Mirrors Application::configure, which refuses an online license without an HTTP(S)
    # license server - the engine would read the license, then exit before going on air.
    if not report.get("offline", False):
        try:
            document = engine_config.read()
        except TranscoderApiException:
            document = {}
        application = document.get("application")
        server = application.get("licenseServer") if isinstance(application, dict) else None
        uri = server.get("uri") if isinstance(server, dict) else None
        if not isinstance(uri, str) or not uri.startswith(("http://", "https://")):
            return (
                "this is an online license, and the engine config has no "
                "application.licenseServer.uri with an http:// or https:// scheme - set one "
                "with PUT /config first"
            )

    return None


def _require_writable() -> pathlib.Path:
    path = license_path()
    directory = path.parent

    if not directory.is_dir():
        raise ConflictException(f"the license directory does not exist: {directory}")

    # Probed rather than checked with os.access - see engine_config.require_writable.
    probe = directory / f".{path.name}.writable"
    try:
        probe.touch()
        probe.unlink()
    except OSError as exc:
        raise ConflictException(
            f"the license directory is not writable: {directory} ({exc.strerror}). "
            "Mount it read-write."
        ) from exc

    return path


def _stage(content: bytes) -> pathlib.Path:
    path = _require_writable()
    staged = path.with_name(path.name + ".upload")
    try:
        descriptor = os.open(staged, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        try:
            os.write(descriptor, content)
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    except OSError as exc:
        with suppress(OSError):
            staged.unlink(missing_ok=True)
        raise TranscoderApiException(
            f"could not stage the license next to {path}: {exc.strerror}",
            ErrorCode.UNAVAILABLE,
            http_code=500,
        ) from exc
    return staged


def _commit(staged: pathlib.Path) -> None:
    path = license_path()

    # Keep the host-side owner and mode of the file being replaced, as engine_config.write does.
    try:
        existing = path.stat()
        uid, gid, mode = existing.st_uid, existing.st_gid, existing.st_mode & 0o7777
    except FileNotFoundError:
        directory = path.parent.stat()
        uid, gid, mode = directory.st_uid, directory.st_gid, 0o644

    try:
        os.replace(staged, path)
        os.chmod(path, mode)
        if os.geteuid() == 0:
            os.chown(path, uid, gid)
    except OSError as exc:
        with suppress(OSError):
            staged.unlink(missing_ok=True)
        if exc.errno in (errno.EBUSY, errno.ETXTBSY):
            raise ConflictException(
                f"{path} is bind-mounted as a single file, so it cannot be replaced. "
                "Mount the containing directory read-write and point "
                "TRANSCODER_LICENSE_PATH at a file inside it."
            ) from exc
        raise TranscoderApiException(
            f"could not replace {path}: {exc.strerror}",
            ErrorCode.UNAVAILABLE,
            http_code=500,
        ) from exc


def install(content: bytes, dry_run: bool = False) -> dict[str, Any]:
    """Validate an uploaded license with the engine, then put it in place.

    Refuses - leaving the installed license untouched - anything the engine would not start
    on. With dry_run the upload is inspected in place and discarded.
    """
    if not content:
        raise UnprocessableException("the uploaded license file is empty")
    if len(content) > MAX_BYTES:
        raise UnprocessableException(
            f"the uploaded file is {len(content)} bytes; a license is under {MAX_BYTES} bytes"
        )

    staged = _stage(content)
    try:
        report = inspect(staged)
        reason = _refusal(report)
        if reason is not None:
            raise UnprocessableException(f"license refused: {reason}")
        if dry_run:
            return {"state": "validated", "path": str(license_path()), "license": report}
        _commit(staged)
    finally:
        with suppress(OSError):
            staged.unlink(missing_ok=True)

    return {"state": "installed", "path": str(license_path()), "license": report}


def remove() -> dict[str, Any]:
    """Delete the installed license. The caller stops the engine first."""
    path = license_path()
    try:
        path.unlink()
    except FileNotFoundError as exc:
        raise NotFoundException(f"no license is installed at {path}") from exc
    except OSError as exc:
        if exc.errno in (errno.EBUSY, errno.EROFS, errno.EACCES, errno.EPERM):
            raise ConflictException(
                f"{path} cannot be removed ({exc.strerror}). The license must live inside a "
                "read-write bind-mounted directory, not be mounted as a file of its own."
            ) from exc
        raise TranscoderApiException(
            f"could not remove {path}: {exc.strerror}", ErrorCode.UNAVAILABLE, http_code=500
        ) from exc
    return {"state": "removed", "path": str(path)}
