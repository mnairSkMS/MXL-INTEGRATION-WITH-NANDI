#!/usr/bin/env bash
# The same channel with and without mxlTap: the on-air output must be the same (streams, no decode
# errors, same length and frame count). The engine's CPU in both runs is printed as RESULT lines.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

run() {  # run <name> <tap json or empty>
    local name="$1"
    mxl_write_config $MXL_TMP/$name.json "$2"
    mxl_start_source tone
    local src=$MXL_SRC_PID
    mxl_start_engine $MXL_TMP/$name.json
    local eng=$MXL_ENGINE_PID
    sleep 15
    local c0; c0=$(mxl_cpu_seconds $eng)
    mxl_capture_output 20 $MXL_TMP/$name.ts
    local c1; c1=$(mxl_cpu_seconds $eng)
    mxl_alive $eng || mxl_fail "$name: the engine died"
    echo "RESULT engine CPU ${name}: $(awk -v a=$c0 -v b=$c1 'BEGIN { printf "%.2f", (b - a) / 20 }') cores"
    [ -n "$2" ] && mxl-info -d "$MXL_DOMAIN" -l 2> /dev/null | grep -c 'Media Function' | sed 's/^/RESULT MXL flows present: /'
    mxl_stop $eng; mxl_stop $src
    sleep 2
}

run notap ""
rm -rf "$MXL_DOMAIN"; mkdir -p "$MXL_DOMAIN"
run tap "$(mxl_tap_json)"

streams() { ffprobe -v error -show_entries stream=codec_type,codec_name,width,height,r_frame_rate -of compact $1 | sort -u | tr '\n' ' '; }
[ "$(streams $MXL_TMP/notap.ts)" = "$(streams $MXL_TMP/tap.ts)" ] || mxl_fail "the streams differ: $(streams $MXL_TMP/notap.ts) / $(streams $MXL_TMP/tap.ts)"
for n in notap tap; do
    [ "$(mxl_decode_errors $MXL_TMP/$n.ts)" = "0" ] || mxl_fail "$n: the output has decode errors"
done
frames() { ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of default=nw=1:nk=1 $1 | head -1; }
fa=$(frames $MXL_TMP/notap.ts); fb=$(frames $MXL_TMP/tap.ts)
mxl_log "video frames: without tap $fa, with tap $fb"
awk -v a=$fa -v b=$fb 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 3) }' || mxl_fail "frame counts differ: $fa without the tap, $fb with it"

mxl_test_end
