#!/usr/bin/env bash
# The writer of a flow is replaced by one that writes another picture size (and another picture), with the same
# flow id. The reader reattaches (nothing tells it that the writer is gone) and the channel must go on with the new
# picture at its own configured size: here a red 640x360 flow is replaced by a blue 1280x720 one. The output must
# keep its size, stay free of decode errors, and show blue after the change.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

# colour <file>: the mean of the U and V planes, which say whether the picture is red (V high) or blue (U high)
colour() {
    ffmpeg -v error -i "$1" -an -vf "signalstats,metadata=print:key=lavfi.signalstats.UAVG:file=$MXL_TMP/u.txt,metadata=print:key=lavfi.signalstats.VAVG:file=$MXL_TMP/v.txt" -f null - >> $MXL_LOG 2>&1 || true
    local u v
    u=$(awk -F= '/UAVG/ { s += $2; n++ } END { if (n) printf "%.0f", s / n }' $MXL_TMP/u.txt)
    v=$(awk -F= '/VAVG/ { s += $2; n++ } END { if (n) printf "%.0f", s / n }' $MXL_TMP/v.txt)
    echo "$u $v"
}

mxl_write_video $MXL_FLOW_VIDEO 25/1 640 360 100000 red
first=$MXL_LAST_PID
mxl_write_audio $MXL_FLOW_AUDIO 2 0 100000
mxl_wait_flow $MXL_FLOW_VIDEO 20 || mxl_abort "the video writer did not start"
mxl_wait_flow $MXL_FLOW_AUDIO 20 || mxl_abort "the audio writer did not start"
MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/b.json ""
mxl_start_engine $MXL_TMP/b.json b
pid_b=$MXL_ENGINE_PID
sleep 8
mxl_alive $pid_b || mxl_abort "the channel did not start (see engine-b.log)"

mxl_capture_output 4 $MXL_TMP/before.ts
read -r u v <<< "$(colour $MXL_TMP/before.ts)"
echo "RESULT before the change: output ${u:-?}/${v:-?} (U/V; red has V high)"
awk -v v="${v:-0}" -v u="${u:-0}" 'BEGIN { exit !(v > 180 && u < 128) }' || mxl_fail "the output should be red before the change (U $u, V $v)"

mxl_stop $first
mxl_write_video $MXL_FLOW_VIDEO 25/1 1280 720 100000 blue
sleep 12
mxl_alive $pid_b || mxl_abort "the channel stopped when the picture size changed (see engine-b.log)"

mxl_capture_output 4 $MXL_TMP/after.ts
read -r u v <<< "$(colour $MXL_TMP/after.ts)"
echo "RESULT after the change: output ${u:-?}/${v:-?} (U/V; blue has U high)"
awk -v v="${v:-255}" -v u="${u:-0}" 'BEGIN { exit !(u > 180 && v < 128) }' || mxl_fail "the output should be blue after the change (U $u, V $v)"
for f in before after; do
    size=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 $MXL_TMP/$f.ts | head -n 1)
    [ "$size" = "1280x720" ] || mxl_fail "the output size at '$f' the change is $size, not 1280x720"
    [ "$(mxl_decode_errors $MXL_TMP/$f.ts)" = "0" ] || mxl_fail "the output at '$f' the change has decode errors"
done

mxl_test_end
