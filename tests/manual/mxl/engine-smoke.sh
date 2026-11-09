#!/usr/bin/env bash
# Engine regression smoke test for an image: transcode a generated file to MPEG-TS and inspect the
# result with ffprobe. Used to show that adding MXL to utpl/base did not change the transcoder:
# run it on the :baseline image and on the :mxl image and compare the printed streams.
# Config-level check as described in AGENTS.md; it does not exercise MXL itself. The engine keeps
# running after the end of the file, so it is stopped by the 60 s timeout (exit status 124).
#
#   tests/manual/mxl/engine-smoke.sh [image]       default image: utpl/transcoder-test:mxl

IMAGE="${1:-utpl/transcoder-test:mxl}"
ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )/../../.." &> /dev/null && pwd )
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/config.json" <<'JSON'
{
  "version": "1.0.7",
  "application": { "logger": { "transcoder": { "level": "trace", "console": { "enable": true, "colors": false, "stderr": false } } } },
  "transcoder": {
    "inputs": [ { "uri": "file:///work/in.ts" } ],
    "outputs": [ {
      "uri": "file:///work/out.ts",
      "container": "mpegts",
      "streams": [
        { "type": "audio", "codec": "ac3",
          "audio": { "rate": 48000, "channels": 6 },
          "ac3": { "encoder": "dlbpatenc",
                   "properties": { "dialnorm": "-24", "drc-line": "auto", "drc-rf": "auto", "surround-trim": "auto", "height-trim": "auto" },
                   "caps": {} } },
        { "type": "video", "codec": "h264",
          "video": { "width": 640, "height": 360 },
          "h264": { "encoder": "x264enc",
                    "properties": { "key-int-max": "15", "tune": "zerolatency", "speed-preset": "ultrafast", "bitrate": "1500" },
                    "caps": { "profile": "high" } } }
      ] } ]
  }
}
JSON

docker run --rm --user "$(id -u):$(id -g)" --entrypoint bash \
    -v "$WORK:/work" -v "$ROOT/transcoder/license/prod/licenses:/lic:ro" "$IMAGE" -c '
    ffmpeg -v error -f lavfi -i testsrc=size=640x360:rate=25 -f lavfi -i sine=frequency=440:sample_rate=48000 \
        -t 10 -c:v libx264 -g 25 -c:a aac -ac 2 -f mpegts /work/in.ts || exit 90
    mkdir -p /tmp/db && : > /tmp/db/e.sqlite
    # The engine keeps running after the end of its input, so the script stops it; that is status 124 and is the expected end.
    timeout 40 /opt/utpl/bin/transcoder -c /work/config.json -l /lic/offline.lic -b /tmp/db/e.sqlite > /work/engine.log 2>&1
    status=$?
    if [ "$status" = 124 ]; then
        echo "engine: still running when the script stopped it after 40 s (the expected end; its input is 10 s long)"
    else
        echo "engine exit status: $status (it should still be running when the script stops it)"
    fi
    echo "--- last engine log lines"; tail -n 6 /work/engine.log | cut -c1-200
    echo "--- ffprobe of the output"
    ffprobe -v error -show_entries stream=index,codec_type,codec_name,width,height,r_frame_rate,sample_rate,channels -of compact /work/out.ts
    echo "--- decode check"
    ffmpeg -v error -i /work/out.ts -f null - 2>&1 | head -5; echo "decode errors above (none = clean)"
'
