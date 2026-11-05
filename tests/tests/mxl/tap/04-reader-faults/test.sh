#!/usr/bin/env bash
# Readers that stall or are killed must not disturb the channel. Two readers (video, audio) attach to
# the tap flows; one is stopped for 6 s, then both are killed with SIGKILL, while the on-air output
# is recorded. The output must keep its frame spacing (no gap over 120 ms) and the engine must run.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_source tone
mxl_start_engine $MXL_TMP/config.json
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear"
sleep 5

mxl_bg ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t 26 -c copy $MXL_TMP/out.ts
capture=$MXL_LAST_PID
sleep 2

mxl_bg gst-launch-1.0 -q mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN ! fakesink sync=false
rv=$MXL_LAST_PID
mxl_bg gst-launch-1.0 -q mxlsrc audio-flow-id=$MXL_FLOW_AUDIO domain=$MXL_DOMAIN ! fakesink sync=false
ra=$MXL_LAST_PID
sleep 4
kill -STOP $rv $ra
sleep 6
kill -CONT $rv $ra
sleep 4
kill -9 $rv $ra
sleep 2
mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped"
# A new reader must still be able to join afterwards.
timeout 20 gst-launch-1.0 -q mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=25 ! fakesink sync=false >> $MXL_LOG 2>&1 \
    || mxl_fail "a new reader could not join after the faults"
wait $capture 2>/dev/null || true

vgap=$(mxl_max_gap_ms $MXL_TMP/out.ts v:0); agap=$(mxl_max_gap_ms $MXL_TMP/out.ts a:0)
echo "RESULT largest gap between video packets: ${vgap} ms, audio packets: ${agap} ms"
mxl_log "gaps: video ${vgap} ms, audio ${agap} ms"
awk -v g="$vgap" 'BEGIN { exit !(g <= 120) }' || mxl_fail "the on-air video has a ${vgap} ms gap"
awk -v g="$agap" 'BEGIN { exit !(g <= 120) }' || mxl_fail "the on-air audio has a ${agap} ms gap"
[ "$(mxl_decode_errors $MXL_TMP/out.ts)" = "0" ] || mxl_fail "the on-air output has decode errors"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "the tap reported a side pipeline error"

mxl_test_end
