#!/usr/bin/env bash
# One channel split into a main engine and a worker (Step 5, WP3), against the same channel done in process, at 1080p25 (see
# framework/mxl_worker.sh for the two runs). The source is a live picture that flashes and beeps once a second. The worker's output
# must be the same programme (Gate A, agreed 2026-11-11, and "the same streams" of the plan):
#   - the same streams (codec, size, frame rate, audio layout), and decoding without errors;
#   - no frame lost: the frame rate over the capture is the in-process channel's, and no gap between video packets over two frames;
#   - lip-sync within one frame (40 ms) of the in-process channel's.
# The SCTE-35 events across a split are test 02, and `input/13` and `input/14`; the CPU is test 03. The source is a live one (ffmpeg,
# paced) and not the file played by tsp that the SCTE tests need: that one reaches the main engine with a skew of its own (24 ms
# at 1080p) when the channel has only MXL outputs; see doc/codebase/41. The limits that Gate A sets over an hour (drops) are checked
# here over the capture only. In process the offset is steady (within 1 ms from run to run); the worker's moves by up to 60 ms from run
# to run, so the 40 ms limit has little room.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
. ${SCRIPT_DIR}/../../../../framework/mxl_worker.sh
worker_domain_in_memory
mxl_test_begin "$SCRIPT_DIR"
worker_trap
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info tsp

FW=${SCRIPT_DIR}/../../../../framework
SIZE=1920x1080
WINDOW=24

worker_configs $SIZE no
worker_run_p sync $SIZE 25 12 $WINDOW
worker_run_split sync $SIZE 25 12 $WINDOW no

# The same streams
worker_streams $MXL_TMP/p.ts > $MXL_TMP/streams-p.txt
worker_streams $MXL_TMP/w.ts > $MXL_TMP/streams-w.txt
echo "RESULT streams of the in-process channel: $(tr '\n' ' ' < $MXL_TMP/streams-p.txt)"
echo "RESULT streams of the worker:             $(tr '\n' ' ' < $MXL_TMP/streams-w.txt)"
[ -s $MXL_TMP/streams-p.txt ] || mxl_abort "the reference capture has no streams"
cmp -s $MXL_TMP/streams-p.txt $MXL_TMP/streams-w.txt || mxl_fail "the worker's streams differ from the in-process channel's"

errs=$(worker_decode_errors $MXL_TMP/w.ts $(( WINDOW - 3 )))
[ "$errs" = "0" ] || mxl_fail "the worker's output has $errs decode error lines"

# Frames: the rate over the capture, and the largest gap
read -r np fp lp <<< "$(worker_frames $MXL_TMP/p.ts)"
read -r nw fw lw <<< "$(worker_frames $MXL_TMP/w.ts)"
fps_p=$(awk -v n=$np -v f=$fp -v l=$lp 'BEGIN { printf "%.3f", (n - 1) / (l - f) }')
fps_w=$(awk -v n=$nw -v f=$fw -v l=$lw 'BEGIN { printf "%.3f", (n - 1) / (l - f) }')
gap_p=$(mxl_max_gap_ms $MXL_TMP/p.ts v:0); gap_w=$(mxl_max_gap_ms $MXL_TMP/w.ts v:0)
echo "RESULT video frames per second over the capture: in process $fps_p ($np frames, largest gap $gap_p ms), worker $fps_w ($nw frames, largest gap $gap_w ms)"
awk -v a=$fps_w -v b=$fps_p 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 0.05) }' || mxl_fail "the worker's frame rate $fps_w is not the in-process channel's $fps_p"
[ "$gap_w" -le 80 ] || mxl_fail "the worker's largest gap between video packets is $gap_w ms (limit 80 ms)"

# Lip-sync
read -r ls_p n_p <<< "$(mxl_lipsync_file $MXL_TMP/p.ts)"
read -r ls_w n_w <<< "$(mxl_lipsync_file $MXL_TMP/w.ts)"
echo "RESULT flash-minus-beep: in process ${ls_p} ms (${n_p} pairs), worker ${ls_w} ms (${n_w} pairs), difference $(awk -v a=$ls_w -v b=$ls_p 'BEGIN { print a - b }') ms"
[ "${n_p:-0}" -ge 5 ] && [ "${n_w:-0}" -ge 5 ] || mxl_abort "could not measure lip-sync (pairs: in process ${n_p:-0}, worker ${n_w:-0})"
awk -v a="$ls_w" -v b="$ls_p" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 40) }' \
    || mxl_fail "the worker's lip-sync is $(awk -v a=$ls_w -v b=$ls_p 'BEGIN { print a - b }') ms from the in-process channel's; limit 40 ms (Gate A)"

for tag in main w; do
    errors=$(grep -E 'rror' $MXL_TMP/engine-$tag.log | grep -v 'no such table' || true)
    [ -z "$errors" ] || { echo "$errors" | head -5 >> $MXL_LOG; mxl_fail "the $tag engine logged errors"; }
done

mxl_test_end
