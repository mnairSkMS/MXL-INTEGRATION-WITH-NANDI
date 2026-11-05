#!/usr/bin/env bash
# An MXL input may name only one flow. With the video flow alone the channel's output has the picture and no
# audio; with the audio flow alone it has the sound and no picture; that is what a UDP input without audio or video
# gives too. The flows are written by gst-launch (a moving ball; a 1 kHz tone on the left channel).

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

kinds() { ffprobe -v error -show_entries stream=codec_type -of csv=p=0 $1 | grep -v '^$' | sort -u | tr '\n' ' '; }

# 1. video only
MXL_DOMAIN="$MXL_TMP/domain-video"; mkdir -p "$MXL_DOMAIN"
mxl_write_video $MXL_FLOW_VIDEO 25/1 1280 720 100000 ball
writer=$MXL_LAST_PID
mxl_wait_flow $MXL_FLOW_VIDEO 20 || mxl_abort "the video writer did not start"
MXL_INPUT_TRANSPORT="$(mxl_input_json "$MXL_FLOW_VIDEO" "")" mxl_write_config $MXL_TMP/video.json ""
mxl_start_engine $MXL_TMP/video.json video
engine=$MXL_ENGINE_PID
sleep 8
mxl_alive $engine || mxl_abort "the channel with a video flow only did not start (see engine-video.log)"
mxl_capture_output 8 $MXL_TMP/video.ts
echo "RESULT video flow only: output streams: $(kinds $MXL_TMP/video.ts)"
[ "$(kinds $MXL_TMP/video.ts)" = "video " ] || mxl_fail "video flow only: the output should have a video stream only"
pics=$(ffmpeg -v error -y -i $MXL_TMP/video.ts -an -vf "fps=5,scale=160:90,format=gray" -f framemd5 - 2>/dev/null | awk '!/^#/ { print $NF }' | sort -u | wc -l)
echo "RESULT video flow only: ${pics} different pictures in 40 sampled"
[ "$pics" -ge 25 ] || mxl_fail "video flow only: the picture does not move ($pics different pictures in 40 samples)"
[ "$(mxl_decode_errors $MXL_TMP/video.ts)" = "0" ] || mxl_fail "video flow only: the output has decode errors"
mxl_stop $engine; mxl_stop $writer

# 2. audio only
MXL_DOMAIN="$MXL_TMP/domain-audio"; mkdir -p "$MXL_DOMAIN"
mxl_write_audio $MXL_FLOW_AUDIO 2 0 100000
writer=$MXL_LAST_PID
mxl_wait_flow $MXL_FLOW_AUDIO 20 || mxl_abort "the audio writer did not start"
MXL_INPUT_TRANSPORT="$(mxl_input_json "" "$MXL_FLOW_AUDIO")" mxl_write_config $MXL_TMP/audio.json ""
mxl_start_engine $MXL_TMP/audio.json audio
engine=$MXL_ENGINE_PID
sleep 8
mxl_alive $engine || mxl_abort "the channel with an audio flow only did not start (see engine-audio.log)"
mxl_capture_output 8 $MXL_TMP/audio.ts
echo "RESULT audio flow only: output streams: $(kinds $MXL_TMP/audio.ts)"
[ "$(kinds $MXL_TMP/audio.ts)" = "audio " ] || mxl_fail "audio flow only: the output should have an audio stream only"
ffmpeg -v error -y -i $MXL_TMP/audio.ts -vn -af "pan=mono|c0=c0" $MXL_TMP/left.wav >> $MXL_LOG 2>&1 || true
ffmpeg -v error -y -i $MXL_TMP/audio.ts -vn -af "pan=mono|c0=c1" $MXL_TMP/right.wav >> $MXL_LOG 2>&1 || true
l=$(mxl_tone_db $MXL_TMP/left.wav 1000); r=$(mxl_tone_db $MXL_TMP/right.wav 1000)
echo "RESULT audio flow only: 1 kHz peak ${l} dB on the left channel, ${r} dB on the right"
awk -v l="$l" -v r="$r" 'BEGIN { exit !(l > -15 && r < l - 25) }' || mxl_fail "audio flow only: the tone should be on the left channel only"
mxl_alive $engine || mxl_fail "the channel stopped"

mxl_test_end
