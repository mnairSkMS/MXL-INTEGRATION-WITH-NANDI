"""Setup-time input probing.

Three classes of invariant are worth protecting here, and each one has bitten somewhere:

* **Argument construction.** ffprobe's timeout options disagree with each other about units -
  libsrt's connect_timeout is milliseconds while its listen_timeout is microseconds - and
  forcing a demuxer with -f makes a dead source exit successfully with no streams. The argv
  assertions below encode the combination that actually terminates on a dead source.

* **The protocol whitelist.** The HLS invocation must not permit `file`, or a hostile manifest
  can list file:///etc/passwd as a segment and have the API read it back. That single assertion
  is the reason the whitelist is built per transport rather than shared.

* **PIDs are MPEG-TS only.** MP4 reports an `id` too, but those are track numbers. Emitting
  them as packet identifiers would produce a plausible, wrong answer that an operator would
  then type into an ad-insertion system.
"""

import pathlib
import subprocess

import pytest

from transcoder_api.services import probe


# Captured verbatim from ffprobe against a real MPEG-TS segment written by the engine:
# runtime/hls.playlist-backup.164011/variant_0_section_0.ts
TS_PROBE = {
    "programs": [
        {
            "program_id": 1,
            "program_num": 1,
            "pmt_pid": 32,
            "pcr_pid": 65,
            "tags": {"service_name": "Service01", "service_provider": "FFmpeg"},
            "streams": [{"index": 0}, {"index": 1}, {"index": 2}],
        }
    ],
    "streams": [
        {
            "index": 0,
            "id": "0x41",
            "codec_name": "aac",
            "codec_type": "audio",
            "sample_rate": "48000",
            "channels": 2,
            "channel_layout": "stereo",
            "bit_rate": "322639",
            "disposition": {"default": 1},
            "tags": {"language": "eng"},
        },
        {
            "index": 1,
            "id": "0x42",
            "codec_name": "h264",
            "codec_type": "video",
            "width": 1920,
            "height": 1080,
            "avg_frame_rate": "25/1",
            "r_frame_rate": "25/1",
            "field_order": "progressive",
            "pix_fmt": "yuv420p",
            "display_aspect_ratio": "16:9",
        },
        {
            "index": 2,
            "id": "0x10ad",
            "codec_name": "scte_35",
            "codec_type": "data",
            "avg_frame_rate": "0/0",
            "r_frame_rate": "0/0",
        },
    ],
    "format": {
        "format_name": "mpegts",
        "bit_rate": "6444462",
        "duration": "10.005333",
        "filename": "/runtime/should-not-be-echoed.ts",
    },
}

MP4_PROBE = {
    "programs": [],
    "streams": [
        {
            "index": 0,
            "id": "0x1",
            "codec_name": "h264",
            "codec_type": "video",
            "width": 1280,
            "height": 720,
            "avg_frame_rate": "30000/1001",
            "r_frame_rate": "30000/1001",
            "field_order": "progressive",
            "bit_rate": "2500000",
        },
        {
            "index": 1,
            "id": "0x2",
            "codec_name": "aac",
            "codec_type": "audio",
            "channels": 2,
            "sample_rate": "44100",
            "tags": {"language": "und"},
        },
    ],
    "format": {
        "format_name": "mov,mp4,m4a,3gp,3g2,mj2",
        "bit_rate": "2700000",
        "duration": "60.5",
    },
}


@pytest.fixture()
def recorder(monkeypatch):
    """Capture the argv the service would have executed, and answer with a fixture."""
    calls = []

    def fake(argv, timeout):
        calls.append((argv, timeout))
        return 0, fake.document, ""

    fake.document = TS_PROBE
    monkeypatch.setattr(probe, "_run_ffprobe", fake)
    monkeypatch.setattr(probe.shutil, "which", lambda name: "/usr/bin/ffprobe")
    return calls, fake


@pytest.fixture()
def media_dir(tmp_path, monkeypatch):
    root = tmp_path / "queue"
    root.mkdir()
    monkeypatch.setenv("MEDIA_DIR", str(root))
    monkeypatch.setenv("PLAYLIST_ROOT", str(tmp_path / "runtime"))
    (tmp_path / "runtime").mkdir()
    return root


def _value_after(argv, flag):
    return argv[argv.index(flag) + 1]


# --------------------------------------------------------------------------------------
# argv construction
# --------------------------------------------------------------------------------------


def test_every_transport_asks_for_programs_and_never_forces_a_demuxer(recorder, media_dir):
    calls, _ = recorder
    (media_dir / "clip.ts").write_bytes(b"x")

    probe.probe(str(media_dir / "clip.ts"), probe.InputTransportType.file)
    probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)
    probe.probe("srt://10.0.0.1:9000", probe.InputTransportType.srt)
    probe.probe("http://127.0.0.1:8080/master.m3u8", probe.InputTransportType.hls)

    for argv, _timeout in calls:
        assert "-show_programs" in argv
        assert "-show_streams" in argv
        assert "-show_format" in argv
        assert _value_after(argv, "-of") == "json"
        # Forcing mpegts makes a dead UDP port exit 0 with an empty stream list.
        assert "-f" not in argv


def test_hls_whitelist_excludes_file(recorder):
    calls, _ = recorder
    probe.probe("http://127.0.0.1:8080/master.m3u8", probe.InputTransportType.hls)

    whitelist = _value_after(calls[0][0], "-protocol_whitelist").split(",")
    assert "file" not in whitelist
    assert "https" in whitelist and "crypto" in whitelist and "hls" in whitelist


def test_dash_whitelist_excludes_file_and_uses_http_timeouts(recorder):
    calls, _ = recorder
    probe.probe("https://127.0.0.1:8443/live/manifest.mpd", probe.InputTransportType.dash)

    argv = calls[0][0]
    whitelist = _value_after(argv, "-protocol_whitelist").split(",")
    # A hostile MPD could otherwise point a SegmentTemplate at file:///etc/passwd.
    assert "file" not in whitelist
    assert "https" in whitelist
    assert "-rw_timeout" in argv and "-timeout" in argv
    assert _value_after(argv, "-i") == "https://127.0.0.1:8443/live/manifest.mpd"


def test_file_whitelist_is_only_file(recorder, media_dir):
    calls, _ = recorder
    (media_dir / "clip.ts").write_bytes(b"x")

    probe.probe("clip.ts", probe.InputTransportType.file)

    assert _value_after(calls[0][0], "-protocol_whitelist") == "file"


def test_udp_carries_its_timeout_in_microseconds_and_the_multicast_interface(recorder):
    calls, _ = recorder
    probe.probe(
        "udp://239.1.1.1:5000",
        probe.InputTransportType.udp,
        timeout_seconds=4,
        interface_address="10.0.0.5",
    )

    argv = calls[0][0]
    assert _value_after(argv, "-timeout") == "4000000"
    target = _value_after(argv, "-i")
    assert "overrun_nonfatal=1" in target
    assert "localaddr=10.0.0.5" in target


def test_srt_caller_connect_timeout_is_milliseconds(recorder):
    calls, _ = recorder
    probe.probe("srt://10.0.0.1:9000", probe.InputTransportType.srt, timeout_seconds=6)

    argv = calls[0][0]
    # libsrt's connect_timeout is milliseconds while its own listen_timeout is microseconds.
    assert _value_after(argv, "-connect_timeout") == "6000"
    assert _value_after(argv, "-timeout") == "6000000"
    assert "-listen_timeout" not in argv


def test_srt_listener_waits_in_microseconds_and_says_so(recorder):
    calls, _ = recorder
    result = probe.probe(
        "srt://0.0.0.0:9000?mode=listener", probe.InputTransportType.srt
    )

    argv = calls[0][0]
    assert _value_after(argv, "-listen_timeout") == "20000000"
    assert "-connect_timeout" not in argv
    assert result.waitedForCaller is True


def test_srt_latency_is_converted_from_milliseconds(recorder):
    calls, _ = recorder
    result = probe.probe("srt://10.0.0.1:9000?latency=125", probe.InputTransportType.srt)

    # 125 ms in the engine's configuration vocabulary is 125000 us to libsrt. Passing 125
    # through unchanged would give a 125 microsecond buffer, which fails on any lossy link.
    assert "latency=125000" in _value_after(calls[0][0], "-i")
    assert "latency=125000" in result.probedUri


def test_srt_stream_id_colon_is_not_percent_encoded(recorder):
    calls, _ = recorder
    result = probe.probe(
        "srt://10.0.0.1:9000?streamid=read:crowd", probe.InputTransportType.srt
    )

    # SRT stream IDs are colon-delimited ("read:path"). urlencode percent-encodes ":" by
    # default, and an SRT origin rejects "read%3Apath" as a malformed stream ID - so the
    # probe failed against a source the engine itself reads without trouble.
    assert "streamid=read:crowd" in _value_after(calls[0][0], "-i")
    assert "streamid=read%3Acrowd" not in _value_after(calls[0][0], "-i")
    assert "streamid=read:crowd" in result.probedUri


def test_srt_latency_defaults_to_the_configuration_default(recorder):
    calls, _ = recorder
    probe.probe("srt://10.0.0.1:9000", probe.InputTransportType.srt)

    assert "latency=125000" in _value_after(calls[0][0], "-i")


# --------------------------------------------------------------------------------------
# mapping
# --------------------------------------------------------------------------------------


def test_mpegts_reports_pids_programs_and_the_multiplex_bitrate(recorder):
    _, fake = recorder
    fake.document = TS_PROBE

    result = probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert result.programs[0].programNumber == 1
    assert result.programs[0].pmtPid == 32
    assert result.programs[0].pcrPid == 65
    assert result.programs[0].serviceName == "Service01"
    assert result.programs[0].streamIndexes == [0, 1, 2]

    assert result.video.pid == 0x42 == 66
    assert result.audio[0].pid == 0x41 == 65
    assert result.data[0].pid == 0x10AD == 4269
    assert result.data[0].codec == "scte_35"

    assert result.audio[0].language == "eng"
    assert result.audio[0].sampleRateHz == 48000
    assert result.audio[0].bitrateBps == 322639
    assert result.audio[0].isDefault is True

    assert result.bitrateBps == 6444462
    # Per-stream video bitrate is absent on a live TS and must not be faked from the mux rate.
    assert result.video.bitrateBps is None
    assert any("bitrate" in warning for warning in result.warnings)

    # A live transport has no meaningful duration even when the container claims one.
    assert result.durationSeconds is None
    assert result.isLive is True


def test_mp4_track_ids_are_not_reported_as_pids(recorder, media_dir):
    _, fake = recorder
    fake.document = MP4_PROBE
    (media_dir / "clip.mp4").write_bytes(b"x")

    result = probe.probe("clip.mp4", probe.InputTransportType.file)

    assert result.programs == []
    # MP4 emits id 0x1/0x2, but those are track numbers. Reporting them as PIDs would be a
    # plausible-looking lie that an operator could act on.
    assert result.video.pid is None
    assert result.audio[0].pid is None
    # "und" is ffprobe saying it does not know; it must not reach the operator as a language.
    assert result.audio[0].language is None
    assert result.audio[0].sampleRateHz == 44100
    assert result.isLive is False
    assert result.durationSeconds == pytest.approx(60.5)


def test_the_container_path_is_never_echoed(recorder):
    _, fake = recorder
    fake.document = TS_PROBE

    result = probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert "should-not-be-echoed" not in result.probedUri


@pytest.mark.parametrize(
    "field_order,expected,expected_raw",
    [
        ("progressive", "progressive", "progressive"),
        ("tt", "interlaced", "tt"),
        ("bb", "interlaced", "bb"),
        ("unknown", "unknown", "unknown"),
        (None, "unknown", None),
    ],
)
def test_scan_type_never_guesses_progressive(recorder, field_order, expected, expected_raw):
    _, fake = recorder
    document = {
        "programs": [],
        "streams": [
            {
                "index": 0,
                "codec_name": "h264",
                "codec_type": "video",
                "width": 720,
                "height": 576,
                "avg_frame_rate": "25/1",
            }
        ],
        "format": {"format_name": "mpegts"},
    }
    if field_order is not None:
        document["streams"][0]["field_order"] = field_order
    fake.document = document

    result = probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert result.video.scanType.value == expected
    assert result.video.fieldOrder == expected_raw


def test_frame_rate_falls_back_and_keeps_the_exact_fraction(recorder):
    _, fake = recorder
    fake.document = {
        "programs": [],
        "streams": [
            {
                "index": 0,
                "codec_name": "h264",
                "codec_type": "video",
                "width": 1920,
                "height": 1080,
                "avg_frame_rate": "0/0",
                "r_frame_rate": "30000/1001",
            }
        ],
        "format": {"format_name": "mpegts"},
    }

    result = probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert result.video.frameRateExact == "30000/1001"
    assert result.video.frameRate == pytest.approx(29.97, abs=0.01)
    assert any("r_frame_rate" in warning for warning in result.warnings)


def test_a_second_video_stream_is_flagged_rather_than_silently_dropped(recorder):
    _, fake = recorder
    fake.document = {
        "programs": [],
        "streams": [
            {
                "index": 0,
                "codec_name": "h264",
                "codec_type": "video",
                "width": 1920,
                "height": 1080,
                "avg_frame_rate": "25/1",
                "field_order": "progressive",
            },
            {
                "index": 1,
                "codec_name": "h264",
                "codec_type": "video",
                "width": 640,
                "height": 360,
                "avg_frame_rate": "25/1",
            },
        ],
        "format": {"format_name": "mpegts"},
    }

    result = probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert result.video.width == 1920
    assert any("video streams present" in warning for warning in result.warnings)


def test_cover_art_is_not_mistaken_for_the_video_stream(recorder, media_dir):
    _, fake = recorder
    (media_dir / "clip.mp4").write_bytes(b"x")
    fake.document = {
        "programs": [],
        "streams": [
            {
                "index": 0,
                "codec_name": "mjpeg",
                "codec_type": "video",
                "width": 300,
                "height": 300,
                "disposition": {"attached_pic": 1},
            },
            {
                "index": 1,
                "codec_name": "h264",
                "codec_type": "video",
                "width": 1920,
                "height": 1080,
                "avg_frame_rate": "25/1",
            },
        ],
        "format": {"format_name": "mov,mp4,m4a,3gp,3g2,mj2"},
    }

    result = probe.probe("clip.mp4", probe.InputTransportType.file)

    assert result.video.codec == "h264"


def test_an_audio_only_source_is_a_result_not_an_error(recorder):
    _, fake = recorder
    fake.document = {
        "programs": [],
        "streams": [
            {"index": 0, "codec_name": "aac", "codec_type": "audio", "channels": 2}
        ],
        "format": {"format_name": "mpegts"},
    }

    result = probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert result.video is None
    assert result.audio[0].codec == "aac"


# --------------------------------------------------------------------------------------
# rejections - none of these may reach ffprobe
# --------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "uri,transport",
    [
        ("file:///etc/passwd", "file"),
        ("../../etc/passwd", "file"),
        ("/etc/passwd", "file"),
        ("http://example.net/x.m3u8", "file"),
        ("file:///etc/passwd", "hls"),
        ("hls+file:///runtime/hls", "hls"),
        ("http://169.254.169.254/latest/meta-data/", "hls"),
        ("http://user:pass@example.net/x.m3u8", "hls"),
        ("file:///srv/manifest.mpd", "dash"),
        ("http://user:pass@example.net/x.mpd", "dash"),
        ("http://169.254.169.254/latest/manifest.mpd", "dash"),
        ("-show_streams", "file"),
        ("srt://10.0.0.1", "srt"),
        ("udp://239.1.1.1", "udp"),
        ("srt://10.0.0.1:9000?mode=rendezvous", "srt"),
    ],
)
def test_rejected_before_execution(recorder, media_dir, uri, transport):
    calls, _ = recorder

    with pytest.raises(Exception) as caught:
        probe.probe(uri, probe.InputTransportType(transport))

    assert caught.value.http_code == 400, caught.value.message
    assert calls == [], f"{uri} reached ffprobe"


def test_ndi_is_rejected_with_an_explanation(recorder):
    calls, _ = recorder

    with pytest.raises(Exception) as caught:
        probe.probe("ndi://studio-a", probe.InputTransportType.ndi)

    assert caught.value.http_code == 422
    assert "NDI" in caught.value.message
    assert calls == []


def test_an_out_of_range_timeout_is_refused(recorder, media_dir):
    calls, _ = recorder
    (media_dir / "clip.ts").write_bytes(b"x")

    with pytest.raises(Exception) as caught:
        probe.probe("clip.ts", probe.InputTransportType.file, timeout_seconds=99)

    assert caught.value.http_code == 400
    assert calls == []


def test_a_missing_file_is_a_404(recorder, media_dir):
    calls, _ = recorder

    with pytest.raises(Exception) as caught:
        probe.probe("absent.ts", probe.InputTransportType.file)

    assert caught.value.http_code == 404
    assert calls == []


# --------------------------------------------------------------------------------------
# playlist
# --------------------------------------------------------------------------------------


def _write_playlist(root: pathlib.Path, entries: list[str]) -> pathlib.Path:
    manifest = root / "main.m3u8"
    body = ["#EXTM3U"]
    for entry in entries:
        body += ["#EXTINF:-1,", entry]
    manifest.write_text("\n".join(body) + "\n")
    return manifest


def test_playlist_probes_its_first_entry_not_the_manifest(recorder, media_dir, tmp_path):
    _, fake = recorder
    fake.document = MP4_PROBE
    (media_dir / "first.mp4").write_bytes(b"x")
    (media_dir / "second.mp4").write_bytes(b"x")
    manifest = _write_playlist(
        tmp_path / "runtime",
        [f"file://{media_dir}/first.mp4", f"file://{media_dir}/second.mp4"],
    )

    result = probe.probe(f"pls+file://{manifest}", probe.InputTransportType.playlist)

    # The engine's playlist is a schedule, not a container; ffprobe cannot open it at all.
    assert result.probedUri == str(media_dir / "first.mp4")


def test_a_playlist_entry_outside_media_dir_is_refused(recorder, media_dir, tmp_path):
    calls, _ = recorder
    manifest = _write_playlist(tmp_path / "runtime", ["file:///etc/passwd"])

    with pytest.raises(Exception) as caught:
        probe.probe(f"pls+file://{manifest}", probe.InputTransportType.playlist)

    assert caught.value.http_code == 400
    assert calls == []


def test_an_empty_playlist_is_unprocessable(recorder, media_dir, tmp_path):
    calls, _ = recorder
    manifest = _write_playlist(tmp_path / "runtime", [])

    with pytest.raises(Exception) as caught:
        probe.probe(f"pls+file://{manifest}", probe.InputTransportType.playlist)

    assert caught.value.http_code == 422
    assert calls == []


# --------------------------------------------------------------------------------------
# failure handling
# --------------------------------------------------------------------------------------


def test_a_live_failure_returns_the_ffprobe_reason(monkeypatch):
    monkeypatch.setattr(probe.shutil, "which", lambda name: "/usr/bin/ffprobe")
    monkeypatch.setattr(
        probe,
        "_run_ffprobe",
        lambda argv, timeout: (1, {}, "udp://239.1.1.1:5000: Input/output error"),
    )

    with pytest.raises(Exception) as caught:
        probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert caught.value.http_code == 422
    assert "Input/output error" in caught.value.message


def test_a_local_failure_does_not_leak_the_filesystem(monkeypatch, media_dir):
    monkeypatch.setattr(probe.shutil, "which", lambda name: "/usr/bin/ffprobe")
    monkeypatch.setattr(
        probe,
        "_run_ffprobe",
        lambda argv, timeout: (1, {}, "/media/queue/secret.ts: Invalid data found"),
    )
    (media_dir / "clip.ts").write_bytes(b"x")

    with pytest.raises(Exception) as caught:
        probe.probe("clip.ts", probe.InputTransportType.file)

    assert caught.value.http_code == 422
    # Distinguishing "missing" from "not media" would turn the route into a filesystem oracle.
    assert "secret" not in caught.value.message
    assert "Invalid data" not in caught.value.message


def test_a_bound_listener_port_is_a_conflict(monkeypatch):
    monkeypatch.setattr(probe.shutil, "which", lambda name: "/usr/bin/ffprobe")
    monkeypatch.setattr(
        probe,
        "_run_ffprobe",
        lambda argv, timeout: (1, {}, "srt://0.0.0.0:9000: Address already in use"),
    )

    with pytest.raises(Exception) as caught:
        probe.probe("srt://0.0.0.0:9000?mode=listener", probe.InputTransportType.srt)

    assert caught.value.http_code == 409


def test_a_timeout_is_reported_rather_than_crashing(monkeypatch):
    monkeypatch.setattr(probe.shutil, "which", lambda name: "/usr/bin/ffprobe")

    # _run_ffprobe is the real one here on purpose: the point is that it catches this.
    monkeypatch.setattr(
        probe.subprocess,
        "run",
        lambda *a, **k: (_ for _ in ()).throw(subprocess.TimeoutExpired("ffprobe", 10)),
    )

    with pytest.raises(Exception) as caught:
        probe.probe("srt://10.0.0.1:9000", probe.InputTransportType.srt)

    # media._probe lets TimeoutExpired escape as a 500; a slow source is expected here.
    assert caught.value.http_code == 422


def test_a_successful_exit_with_no_streams_is_still_a_failure(monkeypatch):
    monkeypatch.setattr(probe.shutil, "which", lambda name: "/usr/bin/ffprobe")
    monkeypatch.setattr(probe, "_run_ffprobe", lambda argv, timeout: (0, {"streams": []}, ""))

    with pytest.raises(Exception) as caught:
        probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert caught.value.http_code == 422


def test_a_missing_ffprobe_is_reported_cleanly(monkeypatch):
    monkeypatch.setattr(probe.shutil, "which", lambda name: None)

    with pytest.raises(Exception) as caught:
        probe.probe("udp://239.1.1.1:5000", probe.InputTransportType.udp)

    assert caught.value.http_code == 422


# --------------------------------------------------------------------------------------
# routing
# --------------------------------------------------------------------------------------


def test_the_probe_route_is_not_swallowed_by_the_input_id_route(client, recorder):
    _, fake = recorder
    fake.document = TS_PROBE

    response = client.get("/inputs/probe", params={"uri": "udp://239.1.1.1:5000", "transport": "udp"})

    # /inputs/{id} takes a str path parameter, so it matches /inputs/probe outright and the
    # request dies on UUID validation. This asserts the probe route is registered first.
    assert response.status_code == 200, response.text
    assert response.json()["video"]["pid"] == 66


def test_the_route_reports_a_rejection_as_a_client_error(client, recorder):
    response = client.get(
        "/inputs/probe", params={"uri": "file:///etc/passwd", "transport": "hls"}
    )

    assert response.status_code == 400


def test_an_unknown_transport_is_refused_by_validation(client, recorder):
    response = client.get("/inputs/probe", params={"uri": "udp://1.2.3.4:5000", "transport": "rtp"})

    assert response.status_code == 400


def test_rtmp_whitelist_is_rtmp_and_transport_only(recorder):
    calls, _ = recorder
    probe.probe("rtmp://127.0.0.1/live/abcdefghijklmnop", probe.InputTransportType.rtmp)

    argv = calls[0][0]
    assert _value_after(argv, "-protocol_whitelist") == "rtmp,rtmps,tcp,tls"
    assert _value_after(argv, "-rw_timeout") == "10000000"
    assert _value_after(argv, "-i") == "rtmp://127.0.0.1/live/abcdefghijklmnop"


@pytest.mark.parametrize(
    "uri",
    [
        "srt://127.0.0.1:9000",
        "rtmp://user:pw@127.0.0.1/live/key",
        "rtmp:///live/key",
        "rtmp://127.0.0.1",
        "rtmp://169.254.169.254/live/key",
    ],
)
def test_rtmp_rejects_bad_uris(recorder, uri):
    with pytest.raises(probe.InvalidRequestException):
        probe.probe(uri, probe.InputTransportType.rtmp)


def test_rtmp_stream_key_is_redacted_from_failures(monkeypatch):
    key = "s3cretstreamkey0123"

    def fake(argv, timeout):
        return 1, {}, f"rtmp://127.0.0.1/live/{key}: Connection refused"

    monkeypatch.setattr(probe, "_run_ffprobe", fake)
    monkeypatch.setattr(probe.shutil, "which", lambda name: "/usr/bin/ffprobe")

    with pytest.raises(probe.UnprocessableException) as excinfo:
        probe.probe(f"rtmp://127.0.0.1/live/{key}", probe.InputTransportType.rtmp)

    assert key not in str(excinfo.value.message)
