#!/usr/bin/env bash
# The main engine is killed and comes back (Step 5, WP5, plan T9; Q8, fail open). A channel split into a main engine and a worker runs; the
# main engine is killed with SIGKILL, left dead for 6 s, and started again. Expected: the worker keeps running and its output shows no
# live picture and tone while the programme is gone (it treats the flows as a lost source), and it shows them again within 10 s of the
# main engine publishing again (0.3 to 1 s was measured in Step 4); no error is logged by the worker.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
. ${SCRIPT_DIR}/../../../../framework/mxl_worker.sh
worker_domain_in_memory
mxl_test_begin "$SCRIPT_DIR"
worker_trap
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

SIZE=1280x720
worker_configs $SIZE no
mxl_start_source tone $SIZE 25
mxl_start_engine $MXL_TMP/main.json main
pid_m=$MXL_ENGINE_PID
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
mxl_start_engine $MXL_TMP/w.json w
pid_w=$MXL_ENGINE_PID
sleep 10
[ "$(worker_probe_until before 4)" != "no" ] || mxl_abort "the worker shows no picture and tone before the main engine is killed"

kill -9 $pid_m
mxl_stop $pid_m
sleep 6
mxl_alive $pid_w || mxl_abort "the worker stopped when the main engine was killed"
down=$(worker_probe during)
echo "RESULT while the main engine is dead, the worker's output shows the picture and tone: ${down}"
[ "$down" = "no" ] || mxl_fail "the worker still shows the picture and tone while the main engine is dead"

mxl_start_engine $MXL_TMP/main.json main2
pid_m2=$MXL_ENGINE_PID
published=$(worker_wait_writer 40)
[ "$published" != "no" ] || mxl_abort "the restarted main engine did not write its flows again (see engine-main2.log)"
secs=$(worker_probe_until after 12)
echo "RESULT the worker shows the picture and tone again ${secs} s after the restarted main engine writes (coarse: the probe works in steps of about 4 s and lands at a random place in the group of pictures)"
[ "$secs" != "no" ] || mxl_fail "the worker did not show the picture and tone again"
mxl_alive $pid_w || mxl_fail "the worker stopped"

# the same time from the log, which does not depend on the steps of the probe
line_w=$(grep "the flows reach the channel" $MXL_TMP/engine-w.log | tail -n 1)
if [ -n "$line_w" ]; then
    logged=$(awk -v a="$published" -v b="$(worker_log_epoch "$line_w")" 'BEGIN { printf "%.1f", b - a }')
    echo "RESULT by the logs, the worker has the flows again ${logged} s after the restarted main engine writes (negative: it saw the writer before the check did)"
    awk -v t="$logged" 'BEGIN { exit !(t < 10) }' || mxl_fail "the worker took ${logged} s to attach the restarted flows (10 s is the limit set for this test)"
else
    mxl_fail "the worker's log does not say that it attached the flows"
fi
echo "RESULT $(grep -c 'no data on the .* flow' $MXL_TMP/engine-w.log) reattach lines in the worker's log"
errors=$(grep -E 'rror' $MXL_TMP/engine-w.log | grep -v 'no such table' || true)
[ -z "$errors" ] || { echo "$errors" | head -5 >> $MXL_LOG; mxl_fail "the worker logged errors"; }
mxl_alive $pid_m2 || mxl_fail "the restarted main engine stopped"

mxl_test_end
