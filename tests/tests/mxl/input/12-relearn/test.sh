#!/usr/bin/env bash
# The shift of an MXL input is learned from the first buffers of its flows. A flow that is not there then (its writer
# starts later) or that delivers its first buffer late is not what it was learned from, and may be later than the delay
# allows for as long as the writer runs. The input must notice and learn the shift again.
#
# The sound is written from the start, the input is started, and the picture is written 6 s later, through a queue that
# holds every frame back for 2 s (a writer that lags 2 s more on its picture than on its sound). The input first learns
# the shift from the sound alone. When the picture arrives it is 2 s later than that allows, and 3 s of that make the
# input learn the shift again, from both flows. After that the picture must have room, and the output must hold a moving
# picture and the tone.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

mxl_write_audio $MXL_FLOW_AUDIO 2 0 200000
mxl_wait_flow $MXL_FLOW_AUDIO 20 || mxl_abort "the audio writer did not start"

MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/b.json ""
mxl_start_engine $MXL_TMP/b.json b
pid_b=$MXL_ENGINE_PID
sleep 6
mxl_alive $pid_b || mxl_abort "channel B did not start (see engine-b.log)"
grep -q "the flows reach the channel this long after the time they carry: audio [0-9-]* ms;" $MXL_TMP/engine-b.log \
    || mxl_fail "B did not learn its shift from the audio flow alone before the picture was there"

# the picture, every frame held back for 2 s before it is written
mxl_bg gst-launch-1.0 -q videotestsrc is-live=true do-timestamp=true pattern=ball num-buffers=200000 \
    ! videoconvert ! "video/x-raw,format=v210,width=1280,height=720,framerate=25/1" \
    ! queue max-size-buffers=0 max-size-bytes=0 max-size-time=0 min-threshold-time=2000000000 \
    ! mxlsink flow-id="$MXL_FLOW_VIDEO" domain="$MXL_DOMAIN"
mxl_wait_flow $MXL_FLOW_VIDEO 20 || mxl_abort "the video writer did not start"

# B must say that it learns again (the picture is 3 s late by then, and the watch looks once a second)
again=no
for i in $(seq 1 300); do
    if grep -q "the shift is learned again" $MXL_TMP/engine-b.log; then again=yes; break; fi
    sleep 0.1
done
echo "RESULT B learned the shift again after the late picture: ${again}"
[ "$again" = "yes" ] || mxl_fail "B did not learn its shift again although the picture was later than the delay allows"
sleep 4

line=$(grep "the flows reach the channel" $MXL_TMP/engine-b.log | tail -n 1)
echo "RESULT the last shift B learned: $(sed -E 's/.*carry: //' <<< "$line")"
grep -q "video [0-9-]* ms, audio [0-9-]* ms" <<< "$line" || grep -q "audio [0-9-]* ms, video [0-9-]* ms" <<< "$line" \
    || mxl_fail "the shift B learned the second time does not name both flows"
video_age=$(grep -o "video [0-9-]* ms" <<< "$line" | head -n 1 | grep -o "[0-9-]*")
[ "${video_age:-0}" -ge 1500 ] || mxl_fail "the picture should have been seen as at least 1500 ms old, was ${video_age:-nothing} ms"

# after that the picture has room: the debug line is written once a second, so take a late one
sleep 4
room=$(grep "the video flow's buffers had" $MXL_TMP/engine-b.log | tail -n 1 | sed -E "s/.*had (-?[0-9]+) to (-?[0-9]+) ms.*/\1/")
echo "RESULT the least room the picture's buffers have now: ${room:-unknown} ms"
[ "${room:--9999}" -ge 0 ] || mxl_fail "the picture's buffers are still later than the shift allows"

mxl_capture_output 8 $MXL_TMP/out.ts || mxl_fail "B's output could not be captured"
kinds=$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 $MXL_TMP/out.ts | grep -v '^$' | sort -u | tr '\n' ' ')
pics=$(ffmpeg -v error -y -i $MXL_TMP/out.ts -an -vf "fps=5,scale=160:90,format=gray" -f framemd5 - 2>/dev/null | awk '!/^#/ { print $NF }' | sort -u | wc -l)
echo "RESULT B's output: streams ${kinds}, ${pics} different pictures of about 40 sampled"
[ "$kinds" = "audio video " ] || mxl_fail "B's output should hold a picture and sound, has: ${kinds}"
[ "$pics" -ge 20 ] || mxl_fail "B's output has no moving picture (${pics} different pictures)"
mxl_alive $pid_b || mxl_fail "channel B stopped"

mxl_test_end
