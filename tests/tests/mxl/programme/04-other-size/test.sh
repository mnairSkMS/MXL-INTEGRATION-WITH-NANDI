#!/usr/bin/env bash
# The MXL output has a size and a rate of its own. A 720p25 source with an on-air output of 720p25 and
# an MXL output of 1920x1080 at 25: the programme flow is 1920x1080 (a canvas of its own, with the
# source scaled to it), shows the source's picture and not black, and the on-air output is unchanged.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

export MXL_EXTRA_OUTPUT="$(mxl_programme_json 1920 1080 25)"
mxl_write_config $MXL_TMP/config.json ""
mxl_start_source tone
mxl_start_engine $MXL_TMP/config.json

mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
sleep 4

mxl_read_video 30 50 $MXL_TMP/v.y4m || mxl_fail "video reader did not finish"
pic=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x $MXL_TMP/v.y4m | head -1)
echo "RESULT programme picture: $pic"
[ "$pic" = "1920x1080" ] || mxl_fail "the programme picture is $pic, expected 1920x1080"
[ "$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')" = "25/1" ] || mxl_fail "video flow rate is not 25/1"

# The source is the testsrc2 pattern, bright and colourful: black would average near zero luma.
avg=$(ffmpeg -v info -i $MXL_TMP/v.y4m -vf "select=eq(n\,10),signalstats,metadata=print:key=lavfi.signalstats.YAVG" -frames:v 1 -f null - 2>&1 | grep -o 'YAVG=[0-9.]*' | head -1 | cut -d= -f2)
echo "RESULT mean luma of a programme frame: ${avg:-none}"
awk -v y="${avg:-0}" 'BEGIN { exit !(y > 40) }' || mxl_fail "the programme picture looks black (mean luma ${avg:-none})"
pics=$(mxl_distinct_frames $MXL_TMP/v.y4m)
[ "$pics" -ge 40 ] || mxl_fail "the programme video shows only $pics different pictures in 50 frames"

mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine is not running"
mxl_capture_output 6 $MXL_TMP/out.ts
sz=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x $MXL_TMP/out.ts | head -1)
[ "$sz" = "1280x720" ] || mxl_fail "the on-air picture is $sz, expected 1280x720"
[ "$(mxl_decode_errors $MXL_TMP/out.ts)" = "0" ] || mxl_fail "the on-air output has decode errors"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "the tap reported a side pipeline error"

mxl_test_end
