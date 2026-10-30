#!/usr/bin/env bash
# The frame rates Nandi channels use. Each flow must report the exact rational rate, and 90 frames
# must arrive in the matching real time (within 5 % + 0.3 s start-up), so fractional rates do not
# drift or run fast.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require mxlsink mxlsrc mxl-info

FRAMES=90

for rate in 25/1 30000/1001 50/1 60000/1001 60/1; do
    rm -rf "$MXL_DOMAIN"; mkdir -p "$MXL_DOMAIN"
    mxl_write_video $MXL_FLOW_VIDEO $rate 1920 1080 400
    writer=$MXL_LAST_PID
    mxl_wait_flow $MXL_FLOW_VIDEO 10 || { mxl_fail "$rate: flow did not appear"; continue; }

    [ "$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')" = "$rate" ] || mxl_fail "$rate: flow reports $(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')"

    start=$(date +%s.%N)
    timeout 20 gst-launch-1.0 -q mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=$FRAMES ! fakesink sync=false >> $MXL_LOG 2>&1 \
        || mxl_fail "$rate: reader did not finish"
    took=$(mxl_seconds_since $start)
    expected=$(awk -v r=$rate -v n=$FRAMES 'BEGIN { split(r, a, "/"); printf "%.2f", n * a[2] / a[1] }')
    mxl_log "$rate: $FRAMES frames in ${took}s (expected ${expected}s)"
    awk -v t=$took -v e=$expected 'BEGIN { exit !(t >= e * 0.95 - 0.1 && t <= e * 1.05 + 0.3) }' \
        || mxl_fail "$rate: $FRAMES frames took ${took}s, expected about ${expected}s"

    mxl_stop $writer
done

mxl_test_end
