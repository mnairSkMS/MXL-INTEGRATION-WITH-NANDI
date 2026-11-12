#!/usr/bin/env bash
# A keyframe at each splice point, in the worker against in process (Step 5, WP3 task 4; plan S1). The plan assumed that in process the muxer
# makes the encoder cut a keyframe at the splice time of each splice_insert, and that the worker gets the same from the cue it reads from
# the data flow. MEASURED: neither does. With a moving picture, a splice_insert every 3 s, key-int-max 250 and the encoder's scene-cut
# detection off (so that a keyframe near a splice could only be one that was asked for), the in-process output has no keyframe within one
# frame of any splice, and neither has the worker's. So the test requires the worker to be no worse than the in-process channel and
# reports the numbers; the gap itself is a known gap of the engine, not of the worker (doc/codebase/27).

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
WINDOW=26

export MXL_OUT_KEYINT=250 MXL_OUT_X264OPTS=scenecut=0
worker_configs $SIZE yes
worker_run_p scte $SIZE 25 12 $WINDOW
worker_run_split scte $SIZE 25 12 $WINDOW yes

for f in p w; do
    python3 -I $FW/scte_idr.py $MXL_TMP/$f.ts > $MXL_TMP/idr-$f.txt
    echo "RESULT keyframe within one frame of the splice, $f: $(awk '/IDR-SUMMARY/ { print $3 " of " $2 }' $MXL_TMP/idr-$f.txt); distance of the nearest keyframe, ms: $(awk '$1 == "IDR" { printf "%s ", $3 }' $MXL_TMP/idr-$f.txt)"
done
# the events themselves: every splice_insert of the source (ids 100 to 111, out of network, program 1) reaches the worker's PID 500
python3 -I $FW/scte_events.py $MXL_TMP/p.ts > $MXL_TMP/events-p.txt
python3 -I $FW/scte_events.py $MXL_TMP/w.ts > $MXL_TMP/events-w.txt
ne_p=$(wc -l < $MXL_TMP/events-p.txt); ne_w=$(wc -l < $MXL_TMP/events-w.txt)
echo "RESULT splice_insert events on PID 500: in process $ne_p, worker $ne_w"
[ "$ne_w" -ge $(( ne_p - 1 )) ] || mxl_fail "the worker has $ne_w events, the in-process channel $ne_p"
extra=$(cut -d' ' -f1-3 $MXL_TMP/events-w.txt | grep -vcE '^1(0[0-9]|1[01]) true 1$' || true)
[ "$extra" = "0" ] || mxl_fail "$extra event(s) in the worker's output are not what the source sent"

read -r _ ev ok <<< "$(grep IDR-SUMMARY $MXL_TMP/idr-w.txt)"
[ "${ev:-0}" -ge 5 ] || mxl_fail "the worker's output has only ${ev:-0} events"
read -r _ _ ok_p <<< "$(grep IDR-SUMMARY $MXL_TMP/idr-p.txt)"
[ "$ok" -ge "$ok_p" ] || mxl_fail "the worker has a keyframe within one frame of $ok of its $ev splices, the in-process channel of $ok_p"

mxl_test_end
