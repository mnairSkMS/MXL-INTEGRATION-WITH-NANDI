#!/usr/bin/env bash
# The source changes from 720p to 1080p while the channel runs (a new encoder, a different feed).
# The engine must keep running, and the video flow must follow: first 1280x720, then 1920x1080.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

size() {  # the size of the video flow, read through mxlsrc
    timeout 20 gst-launch-1.0 -q -e mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=5 \
        ! videoconvert ! "video/x-raw,format=I420" ! y4menc ! filesink location=$MXL_TMP/s.y4m >> $MXL_LOG 2>&1 || return 0
    ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x $MXL_TMP/s.y4m | head -1
}

mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_source tone 1280x720
src=$MXL_SRC_PID
mxl_start_engine $MXL_TMP/config.json
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear"
sleep 4
first=$(size)
[ "$first" = "1280x720" ] || mxl_fail "before the change the flow is '$first', expected 1280x720"

mxl_stop $src
mxl_start_source tone 1920x1080
sleep 20
mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped when the picture size changed"
second=$(size)
echo "RESULT video flow size after the source changed: ${second:-none} (was ${first:-none})"
mxl_log "after the change: ${second:-none}"
[ "$second" = "1920x1080" ] || mxl_fail "after the change the flow is '${second:-none}', expected 1920x1080"

mxl_test_end
