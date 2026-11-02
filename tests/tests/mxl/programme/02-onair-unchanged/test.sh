#!/usr/bin/env bash
# The on-air output must be the same with and without the MXL output: same streams, same frame count,
# no decode errors. The engine's CPU use is measured in both cases and printed (the cost of the
# conversion to v210 and of the extra branch).

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

summary() {  # summary <ts file>: streams and frame counts
    ffprobe -v error -count_frames -show_entries stream=codec_type,codec_name,width,height,nb_read_frames -of csv=p=0 "$1" | grep -v '^$' | sort | tr '\n' ';'
}

run() {  # run <label> <config>: starts the engine, captures 10 s of its output, measures its CPU
    local label="$1" config="$2"
    mxl_start_source tone; local src=$MXL_SRC_PID
    mxl_start_engine "$config"; local eng=$MXL_ENGINE_PID
    sleep 16
    mxl_bg ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t 10 -c copy $MXL_TMP/$label.ts
    local cap=$MXL_LAST_PID
    local cpu0=$(mxl_cpu_seconds $eng); sleep 10; local cpu1=$(mxl_cpu_seconds $eng)
    wait $cap 2>/dev/null || true
    echo "RESULT engine CPU $label: $(awk -v a=$cpu0 -v b=$cpu1 'BEGIN { printf "%.2f", (b - a) / 10 }') cores"
    mxl_stop $eng; mxl_stop $src
    sleep 1
}

mxl_write_config $MXL_TMP/notap.json ""
run notap $MXL_TMP/notap.json

rm -rf $MXL_DOMAIN; mkdir -p $MXL_DOMAIN
export MXL_EXTRA_OUTPUT="$(mxl_programme_json)"
mxl_write_config $MXL_TMP/mxl.json ""
run mxl $MXL_TMP/mxl.json

s0=$(summary $MXL_TMP/notap.ts); s1=$(summary $MXL_TMP/mxl.ts)
mxl_log "without: $s0"; mxl_log "with:    $s1"
[ -n "$s0" ] || mxl_fail "no on-air output without the MXL output"

# Same codecs and picture size on both; the frame counts of two ten-second captures can differ by a few frames.
kinds() { echo "$1" | tr ';' '\n' | awk -F, 'NF { print $1 "," $2 "," $3 "," $4 }' | sort | tr '\n' ';'; }
[ "$(kinds "$s0")" = "$(kinds "$s1")" ] || mxl_fail "the on-air streams differ: without [$(kinds "$s0")] with [$(kinds "$s1")]"
frames() { echo "$1" | tr ';' '\n' | awk -F, '$1 == "video" { print $5 }'; }
awk -v a="$(frames "$s0")" -v b="$(frames "$s1")" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 25) }' \
    || mxl_fail "the video frame counts differ: $(frames "$s0") without, $(frames "$s1") with"
[ "$(mxl_decode_errors $MXL_TMP/notap.ts)" = "0" ] || mxl_fail "the on-air output without the MXL output has decode errors"
[ "$(mxl_decode_errors $MXL_TMP/mxl.ts)" = "0" ]   || mxl_fail "the on-air output with the MXL output has decode errors"
[ "$(mxl-info -d $MXL_DOMAIN -l 2>/dev/null | grep -ci "$MXL_FLOW_VIDEO\|$MXL_FLOW_AUDIO")" -ge 2 ] || mxl_fail "the MXL flows are not present after the run"

mxl_test_end
