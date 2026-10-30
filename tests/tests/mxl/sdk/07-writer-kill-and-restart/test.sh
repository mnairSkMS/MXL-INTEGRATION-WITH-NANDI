#!/usr/bin/env bash
# Failure behaviour of a writer. Recorded findings from the v1.1.0 plugin:
#   * a killed writer leaves its flow listed with "Active: false";
#   * a reader attached to that flow does not error: it waits, so a consumer needs its own
#     watchdog (this test asserts the reader is still waiting after 5 s);
#   * `mxl-info -g` removes the dead flow;
#   * a new writer with the same flow id works and readers get frames again.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require mxlsink mxlsrc mxl-info

mxl_write_video $MXL_FLOW_VIDEO 30000/1001 1920 1080 3000
writer=$MXL_LAST_PID
mxl_wait_flow $MXL_FLOW_VIDEO 10 || mxl_abort "flow did not appear"
[ "$(mxl_flow_field $MXL_FLOW_VIDEO Active)" = "true" ] || mxl_fail "a running writer's flow is not Active"

mxl_stop $writer KILL
sleep 1
[ "$(mxl_flow_field $MXL_FLOW_VIDEO Active)" = "false" ] || mxl_fail "a killed writer's flow is still Active ($(mxl_flow_field $MXL_FLOW_VIDEO Active))"

# Reader on the dead flow: expected to still be waiting, not failed and not producing frames.
rc=0
timeout 5 gst-launch-1.0 -q mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=30 ! fakesink sync=false >> $MXL_LOG 2>&1 || rc=$?
[ $rc -eq 124 ] || mxl_fail "reader on a dead flow returned before the 5 s timeout (behaviour changed, update the finding)"

mxl-info -d "$MXL_DOMAIN" -g >> $MXL_LOG 2>&1
mxl-info -d "$MXL_DOMAIN" -l 2>/dev/null | grep -qi $MXL_FLOW_VIDEO && mxl_fail "mxl-info -g did not remove the dead flow"

mxl_write_video $MXL_FLOW_VIDEO 30000/1001 1920 1080 200
writer=$MXL_LAST_PID
mxl_wait_flow $MXL_FLOW_VIDEO 10 || mxl_abort "flow did not reappear after the restart"
[ "$(mxl_flow_field $MXL_FLOW_VIDEO Active)" = "true" ] || mxl_fail "restarted flow is not Active"
timeout 15 gst-launch-1.0 -q mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=30 ! fakesink sync=false >> $MXL_LOG 2>&1 \
    || mxl_fail "reader got no frames from the restarted writer"

mxl_test_end
