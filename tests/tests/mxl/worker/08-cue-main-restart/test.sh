#!/usr/bin/env bash
# The worker is killed and restarted while the source's SCTE-35 events go by (Step 5, WP5, plan T10 = S5; Q8, fail open). The source sends a
# splice_insert every 3 s (ids 100 to 111, each once). 14 s after it starts, the worker is killed and started again at once; it takes a few
# seconds to come back, so some events go by while it is down and are lost (the source sent them once, and nothing carries them to a
# worker that is not there). Expected: the events that come after the worker is back are carried, in the source's order, and each on the
# frame the source put it (the repeated cue in the data flow is what a worker that has just joined reads); no event is lost that came after
# it was back; where the splice falls against the picture is that of the source, within one frame (as input/14).

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
. ${SCRIPT_DIR}/../../../../framework/mxl_worker.sh
worker_domain_in_memory
mxl_test_begin "$SCRIPT_DIR"
worker_trap
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info tsp tstables

FW=${SCRIPT_DIR}/../../../../framework
SIZE=1280x720
worker_configs $SIZE yes
worker_cue_restart main
worker_cue_report > $MXL_TMP/report.txt || true
grep -E '^(RESULT|FAIL)' $MXL_TMP/report.txt | cut -c1-300
grep -q '^FAIL' $MXL_TMP/report.txt && mxl_fail "$(grep -m3 '^FAIL' $MXL_TMP/report.txt | tr '\n' ';')"

# where the splices fall against the picture, against where the source put them, in the output after the restart
python3 -I $FW/scte_align.py $MXL_TMP/scte.ts $MXL_TMP/w2.ts $MXL_TMP/w2.ts 45 > $MXL_TMP/align.txt || true
grep '^RESULT' $MXL_TMP/align.txt | cut -c1-200
grep -q '^FAIL' $MXL_TMP/align.txt && mxl_fail "$(grep -m3 '^FAIL' $MXL_TMP/align.txt | tr '\n' ';')"

mxl_test_end
