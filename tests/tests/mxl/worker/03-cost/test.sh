#!/usr/bin/env bash
# What splitting a channel costs in CPU (Step 5, Gate A: main plus worker at most 1.25 times the in-process channel for the same outputs),
# at 1080p25 with a busy picture, because encoding a flat picture costs almost nothing and would make the copying through the flows look
# larger than it is. The source is testsrc2 and a tone; both runs use the same encoder settings. The cores are over 24 s.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
. ${SCRIPT_DIR}/../../../../framework/mxl_worker.sh
worker_domain_in_memory
mxl_test_begin "$SCRIPT_DIR"
worker_trap
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info tsp

SIZE=${SIZE:-1920x1080}
WINDOW=24

worker_configs $SIZE no
worker_run_p tone $SIZE 25 12 $WINDOW
worker_run_split tone $SIZE 25 12 $WINDOW no

total=$(awk -v a=$WORKER_CPU_M -v b=$WORKER_CPU_W 'BEGIN { printf "%.2f", a + b }')
ratio=$(awk -v t=$total -v p=$WORKER_CPU_P 'BEGIN { printf "%.2f", t / p }')
echo "RESULT $SIZE engine CPU, cores: in process $WORKER_CPU_P; main $WORKER_CPU_M + worker $WORKER_CPU_W = $total ($ratio times)"
n=$(worker_frames $MXL_TMP/w.ts | cut -d' ' -f1)
[ "${n:-0}" -ge $(( WINDOW * 25 - 25 )) ] || mxl_fail "the worker's output has only $n video frames in $WINDOW s"
awk -v t=$total -v p=$WORKER_CPU_P 'BEGIN { exit !(t <= 1.25 * p) }' || mxl_fail "main plus worker use $total cores, over 1.25 times the in-process channel's $WORKER_CPU_P (Gate A)"

mxl_test_end
