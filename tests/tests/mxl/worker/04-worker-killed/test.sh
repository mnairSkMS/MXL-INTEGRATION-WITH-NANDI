#!/usr/bin/env bash
# The worker is killed and comes back (Step 5, WP5, plan T5; Q8, fail open). A channel split into a main engine and a worker runs; the worker
# is killed with SIGKILL, left dead for 6 s, and started again (in a deployment Docker does that: restart: unless-stopped).
# Expected: the main engine does not notice (still running, its programme flow keeps the time of the wall clock, same CPU, no error logged),
# and the restarted worker shows the programme's picture and tone again; the time that takes is printed.

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
WORKER_PID_M=$MXL_ENGINE_PID
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
mxl_start_engine $MXL_TMP/w.json w
pid_w=$MXL_ENGINE_PID
sleep 10
[ "$(worker_probe_until before 4)" != "no" ] || mxl_abort "the worker shows no picture and tone before it is killed"

# the main engine, over 5 s with the worker running
read -r lag0 _ <<< "$(worker_main_state)"
c0=$(mxl_cpu_seconds $WORKER_PID_M); sleep 5; cpu_before=$(awk -v a=$c0 -v b=$(mxl_cpu_seconds $WORKER_PID_M) 'BEGIN { printf "%.2f", (b - a) / 5 }')

kill -9 $pid_w
mxl_stop $pid_w
sleep 1
c0=$(mxl_cpu_seconds $WORKER_PID_M); sleep 5; cpu_down=$(awk -v a=$c0 -v b=$(mxl_cpu_seconds $WORKER_PID_M) 'BEGIN { printf "%.2f", (b - a) / 5 }')
worker_main_unaffected "worker dead" $lag0 $cpu_before $cpu_down
down=$(worker_probe during)
echo "RESULT while the worker is dead, its output shows the picture and tone: ${down}"
[ "$down" = "no" ] || mxl_fail "the worker's output is still there while it is dead"

mxl_start_engine $MXL_TMP/w.json w2
pid_w2=$MXL_ENGINE_PID
secs=$(worker_probe_until after 12)
echo "RESULT the restarted worker shows the picture and tone again ${secs} s after it was started (the probe works in steps of about 4 s and lands at a random place in the group of pictures, so this is a coarse upper bound; the engine's start is part of it)"
[ "$secs" != "no" ] || mxl_fail "the restarted worker did not show the picture and tone again"
mxl_alive $pid_w2 || mxl_fail "the restarted worker stopped"

c0=$(mxl_cpu_seconds $WORKER_PID_M); sleep 5; cpu_after=$(awk -v a=$c0 -v b=$(mxl_cpu_seconds $WORKER_PID_M) 'BEGIN { printf "%.2f", (b - a) / 5 }')
worker_main_unaffected "worker back" $lag0 $cpu_before $cpu_after
errors=$(grep -E 'rror' $MXL_TMP/engine-w2.log | grep -v 'no such table' || true)
[ -z "$errors" ] || { echo "$errors" | head -5 >> $MXL_LOG; mxl_fail "the restarted worker logged errors"; }

mxl_test_end
