#!/usr/bin/env bash
# The worker is starved of CPU (Step 5, WP5, plan T6; Q8, fail open). The worker is held on one CPU, at the lowest priority (nice 19),
# together with three busy loops, so that it gets next to nothing of that CPU for 12 s (at normal priority it still got a fifth of a CPU, kept
# its output and never read late: its reading thread sleeps most of the time, and is woken first); the main engine and everything else run on
# the other CPUs. Expected: the main engine does
# not notice (running, flow lag and CPU as before, no error logged); the worker keeps running and says that its flows arrive late (the
# input's lateness report, and the shift being learned again, if it lasts); once the load is gone the worker shows the picture and tone
# again. What the worker's output looks like while it is starved is printed, not judged: it is what the starved encoder manages.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
. ${SCRIPT_DIR}/../../../../framework/mxl_worker.sh
worker_domain_in_memory
mxl_test_begin "$SCRIPT_DIR"
worker_trap
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info taskset

worker_cpus
[ -n "$WORKER_CPU_REST" ] || { echo "skip: this test needs three CPUs" >&2; exit $TEST_RC_SKIP; }
# everything this test starts runs on the other CPUs, except the worker and the busy loops
taskset -p -c $WORKER_CPU_REST $$ > /dev/null

SIZE=1280x720
worker_configs $SIZE no
mxl_start_source tone $SIZE 25
mxl_start_engine $MXL_TMP/main.json main
WORKER_PID_M=$MXL_ENGINE_PID
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
MXL_ENGINE_WRAP="taskset -c $WORKER_CPU_ONE nice -n 19" mxl_start_engine $MXL_TMP/w.json w
pid_w=$MXL_ENGINE_PID
sleep 10
[ "$(worker_probe_until before 4)" != "no" ] || mxl_abort "the worker shows no picture and tone before it is starved"

read -r lag0 _ <<< "$(worker_main_state)"
c0=$(mxl_cpu_seconds $WORKER_PID_M); sleep 5; cpu_before=$(awk -v a=$c0 -v b=$(mxl_cpu_seconds $WORKER_PID_M) 'BEGIN { printf "%.2f", (b - a) / 5 }')

spin=()
for i in 1 2 3; do
    mxl_bg taskset -c $WORKER_CPU_ONE sh -c 'while :; do :; done'
    spin+=($MXL_LAST_PID)
done
c0=$(mxl_cpu_seconds $WORKER_PID_M); wc0=$(mxl_cpu_seconds $pid_w); sleep 12; wcpu=$(awk -v a=$wc0 -v b=$(mxl_cpu_seconds $pid_w) 'BEGIN { printf "%.2f", (b - a) / 12 }')
cpu_starved=$(awk -v a=$c0 -v b=$(mxl_cpu_seconds $WORKER_PID_M) 'BEGIN { printf "%.2f", (b - a) / 12 }')
echo "RESULT the worker got ${wcpu} of a CPU in 12 s of starvation"
during=$(worker_probe starved)
echo "RESULT while starved, the worker's output shows the picture and tone: ${during}"
worker_main_unaffected "worker starved" $lag0 $cpu_before $cpu_starved
mxl_alive $pid_w || mxl_fail "the worker stopped while it was starved"

for p in "${spin[@]}"; do mxl_stop $p; done
secs=$(worker_probe_until after 12)
echo "RESULT once the load is gone, the worker shows the picture and tone again after ${secs} s (coarse: the probe works in steps of about 4 s)"
[ "$secs" != "no" ] || mxl_fail "the worker did not show the picture and tone again once the load was gone"
mxl_alive $pid_w || mxl_fail "the worker stopped"

late=$(grep -c 'later than the .* ms delay allows' $MXL_TMP/engine-w.log || true)
relearn=$(grep -c 'the shift is learned again' $MXL_TMP/engine-w.log || true)
echo "RESULT the worker's log: ${late} late-flow reports, ${relearn} times the shift was learned again"
[ "$late" -ge 1 ] || mxl_fail "the starved worker did not report a flow arriving late"
read -r lag1 _ <<< "$(worker_main_state)"
awk -v a="$lag1" -v b="$lag0" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 80) }' || mxl_fail "the main engine's flow lag moved from ${lag0} to ${lag1} ms"
errors=$(grep -E 'rror' $MXL_TMP/engine-main.log | grep -v 'no such table' || true)
[ -z "$errors" ] || { echo "$errors" | head -5 >> $MXL_LOG; mxl_fail "the main engine logged errors"; }

mxl_test_end
