#!/usr/bin/env bash
# A 1080i25 source (top field first). The video flow must be 1920x1080 at 25 fps, and the cost of
# deinterlacing for the tap is measured: engine CPU with and without the tap, printed as RESULT
# lines. The tap keeps one field per frame, so the flow is progressive.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

cpu_of() {  # cpu_of <name> <tap json or empty>: engine cores over 20 s
    mxl_write_config $MXL_TMP/$1.json "$2"
    mxl_start_source interlaced
    local src=$MXL_SRC_PID
    mxl_start_engine $MXL_TMP/$1.json
    local eng=$MXL_ENGINE_PID
    sleep 20
    local c0; c0=$(mxl_cpu_seconds $eng)
    sleep 20
    local c1; c1=$(mxl_cpu_seconds $eng)
    mxl_alive $eng || mxl_fail "$1: the engine stopped"
    echo "RESULT engine CPU, 1080i25 source, $1: $(awk -v a=$c0 -v b=$c1 'BEGIN { printf "%.2f", (b - a) / 20 }') cores"
    if [ -n "$2" ]; then
        timeout 30 gst-launch-1.0 -q -e mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=50 \
            ! videoconvert ! "video/x-raw,format=I420" ! y4menc ! filesink location=$MXL_TMP/v.y4m >> $MXL_LOG 2>&1 || mxl_fail "the video reader did not finish"
        local v; v=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate -of csv=p=0 $MXL_TMP/v.y4m | head -1)
        mxl_log "flow: $v"
        [ "$v" = "1920,1080,25/1" ] || mxl_fail "the flow is '$v', expected 1920,1080,25/1"
    fi
    mxl_stop $eng; mxl_stop $src
    sleep 2
}

cpu_of notap ""
rm -rf "$MXL_DOMAIN"; mkdir -p "$MXL_DOMAIN"
cpu_of tap "$(mxl_tap_json)"

mxl_test_end
