#!/usr/bin/env bash
# The delay of an MXL input (transport.mxl.delayMs). The flows carry the time at which their pictures and sound were
# made; the writer's processing has delayed them since, and by more on the picture than on the sound. The channel
# plays them a set time after they arrive, which is the room the writer's lag has to stay inside.
#
# Channel A publishes its programme as MXL flows; channel B reads them, three times with a different delay each:
#   not set   the engine says 500 ms in its log and the channel runs
#   1000 ms   the engine says so, the channel runs, and nothing arrives later than the delay allows
#   2000 ms   the same, at the longest delay allowed
#   0 ms      the engine says so, and reports that a flow arrived later than the delay allows, naming the setting
# The last shows that the delay is what the check is made against: with no room at all the lag of the writer, which
# moves by tens of milliseconds even on an idle host, is always more than there is.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config $MXL_TMP/a.json ""
mxl_start_source tone
mxl_start_engine $MXL_TMP/a.json a
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "channel A's video flow did not appear (see engine-a.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "channel A's audio flow did not appear (see engine-a.log)"

# run_b <tag> <delay in ms, or empty for none>: channel B for about 20 s; sets FRAMES to what its output held in 8 s
# (a capture loses about a second at its start while it waits for a keyframe) and GAP to the largest spacing of its
# video packets in ms
run_b() {
    local tag="$1" delay="$2"
    MXL_INPUT_TRANSPORT="$(MXL_INPUT_DELAY_MS="$delay" mxl_input_json)" mxl_write_config $MXL_TMP/b-$tag.json ""
    mxl_start_engine $MXL_TMP/b-$tag.json b-$tag
    local pid=$MXL_ENGINE_PID
    sleep 8
    mxl_alive $pid || { tail -20 $MXL_TMP/engine-b-$tag.log >> $MXL_LOG; mxl_abort "channel B ($tag) is not running (see engine-b-$tag.log)"; }
    mxl_capture_output 8 $MXL_TMP/out-$tag.ts || { tail -20 $MXL_TMP/engine-b-$tag.log >> $MXL_LOG; mxl_abort "channel B ($tag): its output could not be captured (see test.log and engine-b-$tag.log)"; }
    FRAMES=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of default=nw=1:nk=1 $MXL_TMP/out-$tag.ts | head -n 1)
    GAP=$(mxl_max_gap_ms $MXL_TMP/out-$tag.ts v:0)
    sleep 2
    mxl_stop $pid
}
says()  { grep -c "plays it $2 ms after it arrives" $MXL_TMP/engine-b-$1.log || true; }
late()  { grep -c "later than the .* ms delay allows; raise transport.mxl.delayMs" $MXL_TMP/engine-b-$1.log || true; }

run_b default ""
echo "RESULT delay not set: ${FRAMES:-0} frames in 8 s (largest gap ${GAP} ms), the engine said 500 ms $(says default 500) time(s), late reports: $(late default)"
[ "$(says default 500)" -ge 1 ] || mxl_fail "default: the engine did not say that it plays the flows 500 ms after they arrive"
[ "${FRAMES:-0}" -ge 140 ] && [ "${GAP:-9999}" -le 120 ] || mxl_fail "default: B's output is not continuous (${FRAMES:-0} frames in 8 s, largest gap ${GAP} ms)"

run_b d1000 1000
echo "RESULT delay 1000 ms: ${FRAMES:-0} frames in 8 s (largest gap ${GAP} ms), the engine said 1000 ms $(says d1000 1000) time(s), late reports: $(late d1000)"
[ "$(says d1000 1000)" -ge 1 ] || mxl_fail "1000 ms: the engine did not say that it plays the flows 1000 ms after they arrive"
[ "${FRAMES:-0}" -ge 140 ] && [ "${GAP:-9999}" -le 120 ] || mxl_fail "1000 ms: B's output is not continuous (${FRAMES:-0} frames in 8 s, largest gap ${GAP} ms)"
[ "$(late d1000)" = "0" ]      || mxl_fail "1000 ms: the engine reported a flow later than the delay allows"

run_b d2000 2000
echo "RESULT delay 2000 ms: ${FRAMES:-0} frames in 8 s (largest gap ${GAP} ms), the engine said 2000 ms $(says d2000 2000) time(s), late reports: $(late d2000)"
[ "$(says d2000 2000)" -ge 1 ] || mxl_fail "2000 ms: the engine did not say that it plays the flows 2000 ms after they arrive"
[ "${FRAMES:-0}" -ge 140 ] && [ "${GAP:-9999}" -le 120 ] || mxl_fail "2000 ms: B's output is not continuous (${FRAMES:-0} frames in 8 s, largest gap ${GAP} ms)"
[ "$(late d2000)" = "0" ]      || mxl_fail "2000 ms: the engine reported a flow later than the delay allows"

run_b d0 0
echo "RESULT delay 0 ms: the engine said 0 ms $(says d0 0) time(s), late reports: $(late d0)"
[ "$(says d0 0)" -ge 1 ] || mxl_fail "0 ms: the engine did not say that it plays the flows 0 ms after they arrive"
[ "$(late d0)" -ge 1 ]   || mxl_fail "0 ms: the engine did not report that a flow arrived later than the delay allows"
grep -q "transport.mxl.delayMs" $MXL_TMP/engine-b-d0.log || mxl_fail "0 ms: the report does not name transport.mxl.delayMs"

mxl_test_end
