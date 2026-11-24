#!/usr/bin/env bash
# One writer per flow, and abandoned flows are cleaned up (Step 5, flow registry).
#  1. A configuration in which two writers use one flow, or in which the channel writes a flow it also reads, is
#     refused when the engine starts, with a message that names the flow.
#  2. A flow that a writer in another process holds is not written by the engine's tap: the tap is switched off and
#     logged, the other writer keeps its flow, the other flow of the tap is written, and the channel runs.
#  3. A flow left behind by a writer that was killed is gone after the engine starts (the engine runs MXL's
#     clean-up when it starts, and the GStreamer plugin runs it too when a writer or reader starts); a flow held by
#     a live writer is not removed.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

STALE_FLOW=0c6f2d4a-3b8e-4f57-9a21-7d1e5c9b8a44

check_refused() {  # check_refused <name> <text the message must contain>   (uses the config written before)
    : > $MXL_TMP/e.sqlite
    local rc=0
    timeout 15 "$MXL_ENGINE" -c $MXL_TMP/$1.json -l "$MXL_LICENSE" -b $MXL_TMP/e.sqlite > $MXL_TMP/$1.log 2>&1 || rc=$?
    if [ $rc -eq 0 ] || [ $rc -eq 124 ]; then
        mxl_fail "$1: the engine did not refuse the configuration (exit status $rc)"
    elif ! grep -q "$2" $MXL_TMP/$1.log; then
        mxl_fail "$1: refused, but the message does not contain '$2': $(grep -m1 'InvalidArgument' $MXL_TMP/$1.log | cut -c1-240)"
    else
        mxl_log "ok: $1 refused with exit status $rc"
    fi
}

# 1. Two writers on one flow: the tap and the programme output use the same ids.
MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config $MXL_TMP/two-writers.json "$(mxl_tap_json)"
check_refused two-writers "a flow has one writer"

#    The channel reads the flow that its programme output writes.
MXL_INPUT_TRANSPORT="$(mxl_input_json)" MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config $MXL_TMP/read-write.json ""
check_refused read-write "read by an MXL input of the same channel"

# 2 and 3. A live writer of the video flow, and a flow left by a writer that was killed.
mxl_write_video $MXL_FLOW_VIDEO 25 320 240 7500
live=$MXL_LAST_PID
mxl_write_video $STALE_FLOW 25 320 240 7500
stale=$MXL_LAST_PID
mxl_wait_flow $MXL_FLOW_VIDEO 20 || mxl_abort "the live writer's flow did not appear"
mxl_wait_flow $STALE_FLOW 20     || mxl_abort "the second writer's flow did not appear"
sleep 1
kill -9 $stale
wait $stale 2> /dev/null || true
mxl-info -d "$MXL_DOMAIN" -l 2>/dev/null | grep -qi "$STALE_FLOW" || mxl_abort "precondition: the flow of the killed writer is gone already"
mxl_log "the flow of the killed writer is left in the domain"

mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_source tone
mxl_start_engine $MXL_TMP/config.json
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the tap did not write its audio flow"
sleep 6

mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped"
mxl_alive $live           || mxl_fail "the other writer stopped"

grep -q "MXL tap disabled for flow $MXL_FLOW_VIDEO: a writer in another process already holds it" $MXL_TMP/engine.log \
    || mxl_fail "the tap on a flow held by another writer was not switched off with a message"
grep -q "MXL tap disabled for flow $MXL_FLOW_AUDIO" $MXL_TMP/engine.log \
    && mxl_fail "the audio tap was switched off, though nobody else writes its flow"
grep -q "the clean-up of flows that no writer holds ran" $MXL_TMP/engine.log \
    || mxl_fail "the clean-up at start was not logged"

mxl-info -d "$MXL_DOMAIN" -l 2>/dev/null | grep -qi "$STALE_FLOW" && mxl_fail "the abandoned flow is still in the domain"
mxl-info -d "$MXL_DOMAIN" -l 2>/dev/null | grep -qi "$MXL_FLOW_VIDEO" || mxl_fail "the flow of the live writer was removed"

# The other writer still owns the flow: its description (320 wide), not the engine's (1280), is what a reader gets.
width=$(jq -r '.frame_width' "$MXL_DOMAIN/$MXL_FLOW_VIDEO.mxl-flow/flow_def.json" 2> /dev/null || true)
mxl_log "video flow frame width: ${width:-unknown}"
[ "$width" = "320" ] || mxl_fail "the video flow is not the other writer's (width '${width}')"

mxl_capture_output 6 $MXL_TMP/out.ts
[ "$(mxl_decode_errors $MXL_TMP/out.ts)" = "0" ] || mxl_fail "the on-air output has decode errors"

mxl_test_end
