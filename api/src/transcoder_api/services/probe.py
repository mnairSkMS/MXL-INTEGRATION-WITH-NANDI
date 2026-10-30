"""Setup-time input probing with ffprobe.

The channel creation screen shows an "Input Data" readout - resolution, frame rate, bitrate,
scan type, audio tracks, PIDs - for a source the operator has typed in but not yet committed
to a configuration. None of that is available from the engine: `Input::probe()` reports a bare
bool, the gRPC `InputStream` message carries only id/uri/state, and neither bitrate nor PID
exists anywhere in the media graph. ffprobe reports all of it, and already ships in the runtime
image, so this discovery lives in the API rather than in the transcoder.

Two properties of this module are load-bearing and easy to break:

* It opens the source itself. That is safe only because the channel is not running yet.
  Probing a single-subscriber source - an SRT caller target, or a UDP port the engine already
  holds - while that channel is on air can steal or duplicate the subscription.

* The URI comes from the client, so the server can be made to dial anything. `transport` pins
  the scheme that will be accepted, file paths are confined to MEDIA_DIR through the helpers in
  `media`, and every ffprobe invocation carries a `-protocol_whitelist` narrow enough that a
  hostile container cannot redirect the probe somewhere else. Removing any one of those three
  turns this route into an SSRF and file-disclosure primitive.

`_run_ffprobe` is deliberately separate from `media._probe`: that function is monkeypatched by
the playout and bootstrap tests, and this module needs different failure handling anyway.
"""

from __future__ import annotations

import ipaddress
import json
import logging
import os
import pathlib
import shutil
import socket
import subprocess
import threading
from typing import Any, Optional
from urllib.parse import parse_qsl, unquote, urlencode, urlsplit, urlunsplit

from ..exceptions import (
    ConflictException,
    InvalidRequestException,
    NotFoundException,
    UnprocessableException,
)
from ..model.rest_api import (
    InputProbe,
    InputProbeAudio,
    InputProbeData,
    InputProbeProgram,
    InputProbeVideo,
    InputScanType,
    InputTransportType,
)
from . import media

logger = logging.getLogger(__name__)


DEFAULT_TIMEOUT_SECONDS = 10
# An SRT listener probe binds the port and waits for an encoder to call in. That is a human
# walking to another machine, not a network round trip, so it gets a longer default.
DEFAULT_LISTENER_TIMEOUT_SECONDS = 20
MIN_TIMEOUT_SECONDS = 1
MAX_TIMEOUT_SECONDS = 30

# ffprobe is given a little longer than the transport timeout so that its own bounded wait is
# what fails, producing a useful message, rather than our subprocess kill.
SUBPROCESS_GRACE_SECONDS = 5

# On a seekable local file analyzeduration is stream time, so five seconds costs nothing and
# buys a reliable field_order. On a live source it is wall clock and must stay well under the
# transport timeout, or the probe spends its whole budget analyzing and never reports.
FILE_ANALYZE_DURATION_US = 5_000_000
FILE_PROBE_SIZE = 10_000_000
LIVE_PROBE_SIZE = 8_000_000
LIVE_ANALYZE_CEILING_US = 5_000_000

# libsrt's latency is microseconds; transport.srt.latency in the engine configuration is
# milliseconds. Callers speak the configuration's units - see _normalize_srt.
DEFAULT_SRT_LATENCY_MS = 125
MAX_SRT_LATENCY_MS = 8000
_SRT_LATENCY_KEYS = ("latency", "rcvlatency", "peerlatency")

LIVE_TRANSPORTS = frozenset(
    {
        InputTransportType.srt,
        InputTransportType.udp,
        InputTransportType.hls,
        InputTransportType.dash,
        InputTransportType.rtmp,
    }
)

_PROTOCOL_WHITELIST = {
    InputTransportType.file: "file",
    InputTransportType.playlist: "file",
    InputTransportType.udp: "udp",
    InputTransportType.srt: "srt",
    # No "file" here, deliberately. It is what stops a hostile manifest listing
    # file:///etc/passwd as a segment. crypto covers AES-128 keyed HLS.
    InputTransportType.hls: "http,https,tcp,tls,crypto,hls",
    # Same reasoning as hls. DASH segments are fetched through the http protocols themselves.
    InputTransportType.dash: "http,https,tcp,tls",
    InputTransportType.rtmp: "rtmp,rtmps,tcp,tls",
}

# Cloud instance metadata is the only realistic SSRF prize on an appliance whose legitimate
# sources genuinely live on RFC1918 LANs. Blocking private ranges wholesale would break the
# product; blocking link-local costs nothing, because it is never a video source.
_DENIED_NETWORKS = (
    ipaddress.ip_network("169.254.0.0/16"),
    ipaddress.ip_network("fe80::/10"),
    ipaddress.ip_network("fd00:ec2::254/128"),
)

# Two concurrent thirty-second probes are enough for an operator filling in a form. More would
# let a caller occupy the whole FastAPI threadpool on a box that is also encoding.
_PROBE_SLOTS = threading.BoundedSemaphore(2)

_STDERR_LIMIT = 200


def playlist_root() -> pathlib.Path:
    """Where the engine writes the playlist an operator would paste in."""
    return pathlib.Path(os.environ.get("PLAYLIST_ROOT", "/runtime")).resolve()


def _host_allowlist() -> list[str]:
    raw = os.environ.get("PROBE_HOST_ALLOWLIST", "").strip()
    return [entry.strip() for entry in raw.split(",") if entry.strip()]


# --------------------------------------------------------------------------------------
# timeouts
# --------------------------------------------------------------------------------------


def _resolve_timeout(requested: Optional[int], is_listener: bool) -> int:
    if requested is None:
        return DEFAULT_LISTENER_TIMEOUT_SECONDS if is_listener else DEFAULT_TIMEOUT_SECONDS
    if requested < MIN_TIMEOUT_SECONDS or requested > MAX_TIMEOUT_SECONDS:
        raise InvalidRequestException(
            f"timeoutSeconds must be between {MIN_TIMEOUT_SECONDS} and {MAX_TIMEOUT_SECONDS}"
        )
    return requested


def _live_analyze_duration_us(timeout_seconds: int) -> int:
    return min((timeout_seconds * 1_000_000 * 6) // 10, LIVE_ANALYZE_CEILING_US)


# --------------------------------------------------------------------------------------
# URI normalization
# --------------------------------------------------------------------------------------


def _reject_flag_like(uri: str) -> None:
    # We always pass the URI as the value of -i, so argv injection is not possible. A leading
    # dash is still never a legitimate input and always signals someone probing the boundary.
    if uri.startswith("-"):
        raise InvalidRequestException("uri must not begin with '-'")


def _require_host_and_port(parts, scheme: str) -> None:
    if not parts.hostname:
        raise InvalidRequestException(f"{scheme} uri must include a host")
    try:
        port = parts.port
    except ValueError:
        raise InvalidRequestException(f"{scheme} uri has an invalid port") from None
    if port is None:
        raise InvalidRequestException(f"{scheme} uri must include a port")


def _local_path_from(uri: str) -> pathlib.Path:
    """Accept file:///abs, /abs, or a MEDIA_DIR-relative path and return an absolute path."""
    parts = urlsplit(uri)
    if parts.scheme == "file":
        raw = unquote(parts.path)
    elif parts.scheme:
        raise InvalidRequestException(
            f"expected a file path or file:// uri, got scheme '{parts.scheme}'"
        )
    else:
        raw = uri

    if not raw:
        raise InvalidRequestException("uri does not name a file")

    root = media.media_root()
    candidate = pathlib.Path(raw)
    if candidate.is_absolute():
        try:
            relative = candidate.resolve().relative_to(root)
        except ValueError:
            raise InvalidRequestException(f"path is outside MEDIA_DIR ({root})") from None
        raw = str(relative)

    # media._absolute_path resolves after joining, so a symlink inside MEDIA_DIR pointing out
    # of it is rejected too. It raises 422; from here a bad path is client input, so 400.
    try:
        return media._absolute_path(raw)
    except UnprocessableException as exc:
        raise InvalidRequestException(str(exc.message)) from exc


def _confine(path: pathlib.Path, roots: tuple[pathlib.Path, ...]) -> pathlib.Path:
    resolved = path.resolve()
    for root in roots:
        if resolved == root or root in resolved.parents:
            return resolved
    named = " or ".join(str(root) for root in roots)
    raise InvalidRequestException(f"path is outside {named}")


def _resolve_file(uri: str) -> pathlib.Path:
    path = _local_path_from(uri)
    if not path.is_file():
        raise NotFoundException("no such file")
    return path


def _resolve_playlist(uri: str) -> pathlib.Path:
    """Return the first media entry of a playlist.

    The engine's playlist is a UTPL M3U8 - `#EXTINF:-1` lines followed by `file://` entries -
    and ffprobe cannot open it: it is a schedule, not a container. Probing the playlist means
    probing what it plays first.
    """
    raw = uri
    if raw.startswith("pls+"):
        raw = raw[len("pls+") :]

    parts = urlsplit(raw)
    if parts.scheme and parts.scheme != "file":
        raise InvalidRequestException(
            f"playlist uri must be a file or pls+file uri, got scheme '{parts.scheme}'"
        )
    manifest_raw = unquote(parts.path) if parts.scheme == "file" else raw
    if not manifest_raw:
        raise InvalidRequestException("uri does not name a playlist")

    manifest = _confine(pathlib.Path(manifest_raw), (media.media_root(), playlist_root()))
    if not manifest.is_file():
        raise NotFoundException("no such playlist")

    try:
        lines = manifest.read_text(errors="replace").splitlines()
    except OSError as exc:
        raise UnprocessableException("the playlist could not be read") from exc

    for line in lines:
        entry = line.strip()
        if not entry or entry.startswith("#"):
            continue
        return _resolve_file(entry)

    raise UnprocessableException("the playlist contains no media entries")


def _deny_link_local(host: str) -> None:
    try:
        infos = socket.getaddrinfo(host, None)
    except socket.gaierror as exc:
        raise UnprocessableException(f"could not resolve host '{host}'") from exc

    for info in infos:
        address = ipaddress.ip_address(info[4][0])
        for network in _DENIED_NETWORKS:
            if address.version == network.version and address in network:
                raise InvalidRequestException(
                    f"host '{host}' resolves to a link-local address and will not be probed"
                )

    allowlist = _host_allowlist()
    if allowlist and host not in allowlist:
        raise InvalidRequestException(f"host '{host}' is not in PROBE_HOST_ALLOWLIST")


def _normalize_http(uri: str, label: str) -> str:
    parts = urlsplit(uri)
    if parts.scheme not in ("http", "https"):
        raise InvalidRequestException(
            f"{label} uri must be http or https, got scheme '{parts.scheme or 'none'}'"
        )
    if parts.username or parts.password:
        # Credentials would end up in ffprobe's stderr and then in our logs.
        raise InvalidRequestException(f"{label} uri must not carry credentials in the userinfo")
    if not parts.hostname:
        raise InvalidRequestException(f"{label} uri must include a host")

    _deny_link_local(parts.hostname)
    return uri


def _normalize_rtmp(uri: str) -> str:
    parts = urlsplit(uri)
    if parts.scheme not in ("rtmp", "rtmps"):
        raise InvalidRequestException(
            f"rtmp uri must be rtmp or rtmps, got scheme '{parts.scheme or 'none'}'"
        )
    if parts.username or parts.password:
        raise InvalidRequestException("rtmp uri must not carry credentials in the userinfo")
    if not parts.hostname:
        raise InvalidRequestException("rtmp uri must include a host")
    if not parts.path.strip("/"):
        raise InvalidRequestException("rtmp uri must include an application and stream key")

    _deny_link_local(parts.hostname)
    return uri


def _rtmp_stream_key(uri: str) -> str:
    return urlsplit(uri).path.rstrip("/").rsplit("/", 1)[-1]


def _normalize_udp(uri: str, timeout_seconds: int, interface: Optional[str]) -> str:
    parts = urlsplit(uri)
    if parts.scheme != "udp":
        raise InvalidRequestException(
            f"udp uri must use the udp scheme, got '{parts.scheme or 'none'}'"
        )
    _require_host_and_port(parts, "udp")

    query = dict(parse_qsl(parts.query, keep_blank_values=True))
    query["overrun_nonfatal"] = "1"
    query["timeout"] = str(timeout_seconds * 1_000_000)
    if interface:
        # Mirrors transport.udp.multicast.interfaces. Without it a multicast probe times out on
        # a multi-homed appliance where the engine itself works, and the source gets the blame.
        query["localaddr"] = interface

    return urlunsplit(
        (parts.scheme, parts.netloc, parts.path, urlencode(query, safe=":"), "")
    )


def _normalize_srt(uri: str, timeout_seconds: Optional[int]) -> tuple[str, int, bool]:
    parts = urlsplit(uri)
    if parts.scheme != "srt":
        raise InvalidRequestException(
            f"srt uri must use the srt scheme, got '{parts.scheme or 'none'}'"
        )
    _require_host_and_port(parts, "srt")

    query = dict(parse_qsl(parts.query, keep_blank_values=True))
    mode = query.get("mode", "caller").lower()
    if mode not in ("caller", "listener"):
        raise InvalidRequestException(f"unsupported srt mode '{mode}'")
    query["mode"] = mode
    query["transtype"] = "live"

    # The caller speaks the engine configuration's units, where transport.srt.latency is
    # milliseconds. libsrt wants microseconds. Getting this wrong turns a 125 ms buffer into
    # 125 us, which fails on any lossy link, so convert here and echo the result as probedUri.
    for key in _SRT_LATENCY_KEYS:
        if key not in query and key != "latency":
            continue
        raw = query.get(key, str(DEFAULT_SRT_LATENCY_MS))
        try:
            milliseconds = int(raw)
        except ValueError:
            raise InvalidRequestException(f"srt {key} must be an integer number of ms") from None
        if milliseconds < 0 or milliseconds > MAX_SRT_LATENCY_MS:
            raise InvalidRequestException(
                f"srt {key} must be between 0 and {MAX_SRT_LATENCY_MS} ms"
            )
        query[key] = str(milliseconds * 1000)

    is_listener = mode == "listener"
    resolved = _resolve_timeout(timeout_seconds, is_listener)
    # safe=":" keeps the colon literal. RFC 3986 permits ":" in a query, and SRT stream IDs are
    # colon-delimited ("read:path", "publish:path"). Percent-encoding it yields "read%3Apath",
    # which an SRT origin rejects as a malformed stream ID, so the probe fails against a source
    # the engine itself reads fine.
    normalized = urlunsplit(
        (parts.scheme, parts.netloc, parts.path, urlencode(query, safe=":"), "")
    )
    return normalized, resolved, is_listener


# --------------------------------------------------------------------------------------
# argv
# --------------------------------------------------------------------------------------


def _build_argv(
    executable: str,
    transport: InputTransportType,
    target: str,
    timeout_seconds: int,
    is_listener: bool,
) -> list[str]:
    argv = [executable, "-hide_banner", "-v", "error"]
    argv += ["-protocol_whitelist", _PROTOCOL_WHITELIST[transport]]

    timeout_us = timeout_seconds * 1_000_000

    if transport in (InputTransportType.file, InputTransportType.playlist):
        argv += ["-analyzeduration", str(FILE_ANALYZE_DURATION_US)]
        argv += ["-probesize", str(FILE_PROBE_SIZE)]
    else:
        if transport is InputTransportType.udp:
            argv += ["-timeout", str(timeout_us)]
        elif transport is InputTransportType.srt:
            if is_listener:
                # listen_timeout is MICROSECONDS, while connect_timeout below is MILLISECONDS.
                # Both belong to libsrt. This asymmetry is upstream's, not a typo here.
                argv += ["-listen_timeout", str(timeout_us)]
            else:
                argv += ["-connect_timeout", str(timeout_seconds * 1000)]
                argv += ["-timeout", str(timeout_us)]
        elif transport in (InputTransportType.hls, InputTransportType.dash):
            argv += ["-rw_timeout", str(timeout_us)]
            argv += ["-timeout", str(timeout_us)]
            argv += ["-user_agent", "skandha-transcoder-probe/1.0"]
        elif transport is InputTransportType.rtmp:
            argv += ["-rw_timeout", str(timeout_us)]

        argv += ["-analyzeduration", str(_live_analyze_duration_us(timeout_seconds))]
        argv += ["-probesize", str(LIVE_PROBE_SIZE)]

    # No -f. Forcing the demuxer makes a dead UDP port exit 0 with an empty stream list after
    # about fourteen seconds, ignoring -timeout - a probe that silently succeeds on a dead
    # source. Autodetection fails properly instead.
    argv += ["-of", "json", "-show_format", "-show_streams", "-show_programs"]
    argv += ["-i", target]
    return argv


# --------------------------------------------------------------------------------------
# execution - the test seam
# --------------------------------------------------------------------------------------


def _run_ffprobe(argv: list[str], timeout: float) -> tuple[int, dict, str]:
    try:
        result = subprocess.run(
            argv, capture_output=True, text=True, timeout=timeout, check=False
        )
    except subprocess.TimeoutExpired as exc:
        # media._probe lets this escape as a 500. Here a slow source is an expected outcome.
        raise UnprocessableException(
            f"the source did not respond within {int(timeout)} seconds"
        ) from exc

    try:
        document = json.loads(result.stdout) if result.stdout.strip() else {}
    except json.JSONDecodeError:
        document = {}

    return result.returncode, document, (result.stderr or "").strip()


def _classify_failure(
    transport: InputTransportType, stderr: str, secret: Optional[str] = None
) -> None:
    if secret:
        # An RTMP stream key is a publish credential and ffprobe echoes the URL on failure.
        stderr = stderr.replace(secret, "***")
    lowered = stderr.lower()

    if "address already in use" in lowered:
        raise ConflictException(
            "the probe could not bind the listener port; something else is already listening"
        )
    if "no such file" in lowered:
        raise NotFoundException("no such file")

    if transport in (InputTransportType.file, InputTransportType.playlist):
        # Distinguishing "missing" from "not media" here would make the route a filesystem
        # oracle for anything under MEDIA_DIR. The detail goes to the log, not the client.
        logger.warning("ffprobe failed for a local %s probe: %s", transport.value, stderr)
        raise UnprocessableException("the file could not be probed")

    detail = stderr.splitlines()[0][:_STDERR_LIMIT] if stderr else ""
    message = "the source could not be probed"
    raise UnprocessableException(f"{message}: {detail}" if detail else message)


# --------------------------------------------------------------------------------------
# mapping
# --------------------------------------------------------------------------------------


def _int_or_none(value: Any) -> Optional[int]:
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _float_or_none(value: Any) -> Optional[float]:
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _pid_of(stream: dict, is_mpegts: bool) -> Optional[int]:
    """MPEG-TS packet identifier, or None.

    MP4 also carries an `id` (0x1, 0x2) but those are track numbers. Reporting them as PIDs
    would be a plausible-looking lie, so this is gated on the container.
    """
    if not is_mpegts:
        return None
    raw = stream.get("id")
    if not isinstance(raw, str):
        return None
    try:
        return int(raw, 16)
    except ValueError:
        return None


def _frame_rate(stream: dict) -> tuple[Optional[float], Optional[str], bool]:
    """Return (float, exact fraction, whether the r_frame_rate fallback was used).

    Both fields are fraction strings and either can be "0/0" - audio streams report that for
    both, and a live TS probed for a few seconds often has an unusable avg_frame_rate.
    """
    for index, key in enumerate(("avg_frame_rate", "r_frame_rate")):
        text = stream.get(key) or ""
        numerator, _, denominator = text.partition("/")
        try:
            value = int(numerator) / int(denominator)
        except (ValueError, ZeroDivisionError):
            continue
        if 0 < value <= 1000:
            return value, text, index == 1
    return None, None, False


def _scan_type(stream: dict) -> tuple[InputScanType, Optional[str]]:
    field_order = stream.get("field_order")
    if field_order == "progressive":
        return InputScanType.progressive, field_order
    if field_order in ("tt", "bb", "tb", "bt"):
        return InputScanType.interlaced, field_order
    # The key is omitted entirely when ffprobe could not determine it. Defaulting to
    # progressive would show "Progressive" for an unanalyzed source and get the deinterlacer
    # configured wrong, so an unknown field order stays unknown.
    return InputScanType.unknown, field_order


def _map_video(stream: dict, is_mpegts: bool, warnings: list[str]) -> InputProbeVideo:
    scan_type, field_order = _scan_type(stream)
    rate, exact, used_fallback = _frame_rate(stream)
    if used_fallback:
        warnings.append("frame rate taken from r_frame_rate; avg_frame_rate was unusable")

    bitrate = _int_or_none(stream.get("bit_rate"))
    if bitrate is None:
        # Never synthesize this from format.bit_rate: that is the whole multiplex.
        warnings.append("per-stream video bitrate was not reported; see the container bitrate")

    return InputProbeVideo(
        index=int(stream.get("index", 0)),
        codec=stream.get("codec_name") or "unknown",
        codecLongName=stream.get("codec_long_name"),
        profile=stream.get("profile"),
        width=int(stream.get("width") or 0),
        height=int(stream.get("height") or 0),
        displayAspectRatio=stream.get("display_aspect_ratio"),
        pixelFormat=stream.get("pix_fmt"),
        frameRate=rate,
        frameRateExact=exact,
        scanType=scan_type,
        fieldOrder=field_order,
        bitrateBps=bitrate,
        pid=_pid_of(stream, is_mpegts),
    )


def _map_audio(stream: dict, is_mpegts: bool) -> InputProbeAudio:
    language = (stream.get("tags") or {}).get("language")
    if language in (None, "", "und"):
        # "und" is ffprobe's way of saying it does not know. Showing it to an operator as if it
        # were a language is worse than showing nothing.
        language = None

    return InputProbeAudio(
        index=int(stream.get("index", 0)),
        codec=stream.get("codec_name") or "unknown",
        codecLongName=stream.get("codec_long_name"),
        profile=stream.get("profile"),
        channels=int(stream.get("channels") or 0),
        channelLayout=stream.get("channel_layout"),
        sampleRateHz=_int_or_none(stream.get("sample_rate")),
        bitrateBps=_int_or_none(stream.get("bit_rate")),
        language=language,
        isDefault=(stream.get("disposition") or {}).get("default") == 1,
        pid=_pid_of(stream, is_mpegts),
    )


def _map_data(stream: dict, is_mpegts: bool) -> InputProbeData:
    return InputProbeData(
        index=int(stream.get("index", 0)),
        codec=stream.get("codec_name") or "unknown",
        pid=_pid_of(stream, is_mpegts),
    )


def _map_programs(document: dict) -> list[InputProbeProgram]:
    programs = []
    for program in document.get("programs") or []:
        number = program.get("program_num")
        if number is None:
            number = program.get("program_id")
        if number is None:
            continue
        tags = program.get("tags") or {}
        programs.append(
            InputProbeProgram(
                programNumber=int(number),
                programId=_int_or_none(program.get("program_id")),
                serviceName=tags.get("service_name"),
                serviceProvider=tags.get("service_provider"),
                pmtPid=_int_or_none(program.get("pmt_pid")),
                pcrPid=_int_or_none(program.get("pcr_pid")),
                # -show_programs repeats each stream object inside the program. Only the index
                # is taken here; the top-level streams array is the one that gets mapped.
                streamIndexes=[
                    int(s["index"]) for s in (program.get("streams") or []) if "index" in s
                ],
            )
        )
    return programs


def _map(
    document: dict,
    uri: str,
    transport: InputTransportType,
    probed_uri: str,
    waited_for_caller: bool,
) -> InputProbe:
    fmt = document.get("format") or {}
    container = fmt.get("format_name") or "unknown"
    is_mpegts = "mpegts" in container
    is_live = transport in LIVE_TRANSPORTS

    warnings: list[str] = []
    video_streams = [
        stream
        for stream in document.get("streams") or []
        if stream.get("codec_type") == "video"
        and (stream.get("disposition") or {}).get("attached_pic") != 1
    ]
    if len(video_streams) > 1:
        warnings.append(f"{len(video_streams)} video streams present; reported the first")

    container_bitrate = _int_or_none(fmt.get("bit_rate"))
    if is_live and container_bitrate is None:
        # ffmpeg derives format.bit_rate from size over duration, and a live socket has
        # neither, so the field is absent entirely rather than zero. Measuring it would take a
        # second pass with -read_intervals, which truncates stream analysis badly enough to
        # cost us width and field_order - the two fields an operator needs more than bitrate.
        warnings.append(
            "no container bitrate: ffprobe does not report one for a non-seekable live source"
        )

    video = _map_video(video_streams[0], is_mpegts, warnings) if video_streams else None
    audio = [
        _map_audio(stream, is_mpegts)
        for stream in document.get("streams") or []
        if stream.get("codec_type") == "audio"
    ]
    data = [
        _map_data(stream, is_mpegts)
        for stream in document.get("streams") or []
        if stream.get("codec_type") == "data"
    ]

    return InputProbe(
        uri=uri,
        transport=transport,
        # Never format.filename - that is the absolute container path on our filesystem.
        probedUri=probed_uri,
        container=container,
        isLive=is_live,
        durationSeconds=None if is_live else _float_or_none(fmt.get("duration")),
        bitrateBps=container_bitrate,
        waitedForCaller=waited_for_caller,
        video=video,
        audio=audio,
        data=data,
        programs=_map_programs(document) if is_mpegts else [],
        warnings=warnings,
    )


# --------------------------------------------------------------------------------------
# entry point
# --------------------------------------------------------------------------------------


def probe(
    uri: str,
    transport: InputTransportType,
    timeout_seconds: Optional[int] = None,
    interface_address: Optional[str] = None,
) -> InputProbe:
    """Probe a prospective input. Blocking; call it off the event loop."""
    if not uri or not uri.strip():
        raise InvalidRequestException("uri is required")
    uri = uri.strip()
    _reject_flag_like(uri)

    if transport is InputTransportType.ndi:
        raise UnprocessableException(
            "NDI inputs cannot be probed: ffmpeg has no NDI input protocol in this build"
        )

    waited_for_caller = False
    is_listener = False

    if transport is InputTransportType.file:
        target = str(_resolve_file(uri))
        resolved_timeout = _resolve_timeout(timeout_seconds, False)
    elif transport is InputTransportType.playlist:
        target = str(_resolve_playlist(uri))
        resolved_timeout = _resolve_timeout(timeout_seconds, False)
    elif transport is InputTransportType.udp:
        resolved_timeout = _resolve_timeout(timeout_seconds, False)
        target = _normalize_udp(uri, resolved_timeout, interface_address)
    elif transport is InputTransportType.srt:
        target, resolved_timeout, is_listener = _normalize_srt(uri, timeout_seconds)
        waited_for_caller = is_listener
    elif transport in (InputTransportType.hls, InputTransportType.dash):
        resolved_timeout = _resolve_timeout(timeout_seconds, False)
        target = _normalize_http(uri, transport.value)
    elif transport is InputTransportType.rtmp:
        resolved_timeout = _resolve_timeout(timeout_seconds, False)
        target = _normalize_rtmp(uri)
    else:
        raise InvalidRequestException(f"unsupported transport '{transport}'")

    executable = shutil.which("ffprobe")
    if not executable:
        raise UnprocessableException("ffprobe is required to probe an input")

    argv = _build_argv(executable, transport, target, resolved_timeout, is_listener)

    if not _PROBE_SLOTS.acquire(blocking=False):
        raise ConflictException("another probe is already in progress; try again shortly")
    try:
        returncode, document, stderr = _run_ffprobe(
            argv, resolved_timeout + SUBPROCESS_GRACE_SECONDS
        )
    finally:
        _PROBE_SLOTS.release()

    if returncode != 0:
        secret = _rtmp_stream_key(uri) if transport is InputTransportType.rtmp else None
        _classify_failure(transport, stderr, secret)

    if not document.get("streams"):
        # Reachable even on success: see the note about -f in _build_argv.
        raise UnprocessableException("the source carried no streams that could be identified")

    return _map(document, uri, transport, target, waited_for_caller)
