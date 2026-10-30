#!/usr/bin/env bash
# A reader that stops for longer than the ring holds (about 167 ms at 29.97 fps) must not slow the
# writer, and must be able to carry on afterwards. This is the property that lets an optional
# consumer be attached without risk to the on-air path.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require mxlsink mxlsrc mxl-info

FRAMES=300   # 10.01 s at 29.97 fps

start=$(date +%s.%N)
mxl_write_video $MXL_FLOW_VIDEO 30000/1001 1920 1080 $FRAMES
writer=$MXL_LAST_PID
mxl_wait_flow $MXL_FLOW_VIDEO 10 || mxl_abort "flow did not appear"

mxl_bg gst-launch-1.0 -q mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN ! fakesink sync=false
reader=$MXL_LAST_PID
sleep 1
kill -STOP $reader; sleep 4; kill -CONT $reader

wait $writer
took=$(mxl_seconds_since $start)
mxl_log "writer finished in ${took}s for $FRAMES frames (10.01 s of media)"
awk -v t=$took 'BEGIN { exit !(t < 11.0) }' || mxl_fail "writer took ${took}s: the stalled reader slowed it down"

kill -0 $reader 2>/dev/null || mxl_fail "the reader died after the stall"

# A fresh reader still works on the same domain.
mxl_write_video $MXL_FLOW_VIDEO 30000/1001 1920 1080 100
mxl_wait_flow $MXL_FLOW_VIDEO 10
timeout 15 gst-launch-1.0 -q mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=30 ! fakesink sync=false >> $MXL_LOG 2>&1 \
    || mxl_fail "a new reader got no frames after the stall test"

mxl_test_end
