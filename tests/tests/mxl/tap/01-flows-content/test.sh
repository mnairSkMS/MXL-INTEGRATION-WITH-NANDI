#!/usr/bin/env bash
# An input with mxlTap publishes its decoded video and audio as MXL flows: video v210 at the source
# size and rate, audio 48 kHz Float32 stereo with the source's tone. The on-air output is checked too.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_source tone
mxl_start_engine $MXL_TMP/config.json

mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear (see engine.log)"
sleep 3

[ "$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')" = "25/1" ]    || mxl_fail "video flow rate is not 25/1"
[ "$(mxl_flow_field $MXL_FLOW_AUDIO 'Grain/sample rate')" = "48000/1" ] || mxl_fail "audio flow rate is not 48000"

timeout 30 gst-launch-1.0 -q -e mxlsrc video-flow-id=$MXL_FLOW_VIDEO domain=$MXL_DOMAIN num-buffers=50 \
    ! videoconvert ! "video/x-raw,format=I420" ! y4menc ! filesink location=$MXL_TMP/v.y4m >> $MXL_LOG 2>&1 || mxl_fail "video reader did not finish"
v() { ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=$1 -of default=nw=1:nk=1 $MXL_TMP/v.y4m; }
[ "$(v width)" = "1280" ]          || mxl_fail "video width is not 1280"
[ "$(v height)" = "720" ]          || mxl_fail "video height is not 720"
[ "$(v r_frame_rate)" = "25/1" ]   || mxl_fail "video frame rate is not 25/1"
[ "$(v nb_read_frames)" = "50" ]   || mxl_fail "expected 50 frames, got $(v nb_read_frames)"

# mxlsrc audio buffers are 1 ms each: 3000 buffers are 3 s.
timeout 30 gst-launch-1.0 -q -e mxlsrc audio-flow-id=$MXL_FLOW_AUDIO domain=$MXL_DOMAIN num-buffers=3000 \
    ! audioconvert ! wavenc ! filesink location=$MXL_TMP/a.wav >> $MXL_LOG 2>&1 || mxl_fail "audio reader did not finish"
a() { ffprobe -v error -show_entries stream=$1 -of default=nw=1:nk=1 $MXL_TMP/a.wav; }
[ "$(a sample_rate)" = "48000" ] || mxl_fail "audio rate is not 48000"
[ "$(a channels)" = "2" ]        || mxl_fail "audio is not stereo"
awk -v d="$(a duration)" 'BEGIN { exit !(d >= 2.9 && d <= 3.1) }' || mxl_fail "audio duration $(a duration) s, expected 3 s"

band() { ffmpeg -v info -i $MXL_TMP/a.wav -af "bandpass=f=$1:width_type=h:w=200,volumedetect" -f null - 2>&1 | awk '/max_volume/ { print $(NF-1) }'; }
on=$(band 1000); off=$(band 4000)
mxl_log "1 kHz band peak ${on} dB, 4 kHz band peak ${off} dB"
awk -v a="$on" -v b="$off" 'BEGIN { exit !(a > -35 && b < a - 25) }' || mxl_fail "the 1 kHz tone is not in the audio flow (1 kHz ${on} dB, 4 kHz ${off} dB)"

mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine is not running"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "the tap reported a side pipeline error"

mxl_capture_output 6 $MXL_TMP/out.ts
kinds=$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 $MXL_TMP/out.ts | grep -v '^$' | sort -u | tr '\n' ' ')
[ "$kinds" = "audio video " ] || mxl_fail "on-air output streams: $kinds"
[ "$(mxl_decode_errors $MXL_TMP/out.ts)" = "0" ] || mxl_fail "the on-air output has decode errors"

mxl_test_end
