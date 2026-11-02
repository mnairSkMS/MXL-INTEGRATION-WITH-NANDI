#!/usr/bin/env bash
# An output of type "mxl" publishes the finished programme (the picture and sound the channel puts on
# air, after its compositor and audio processing) as MXL flows: video v210 at the processor's size and
# rate, audio 48 kHz Float32 stereo with the source's tone. The channel's ordinary output is checked too.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

export MXL_EXTRA_OUTPUT="$(mxl_programme_json)"
mxl_write_config $MXL_TMP/config.json ""
mxl_start_source tone
mxl_start_engine $MXL_TMP/config.json

mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear (see engine.log)"
sleep 4

[ "$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')" = "25/1" ]    || mxl_fail "video flow rate is not 25/1"
[ "$(mxl_flow_field $MXL_FLOW_AUDIO 'Grain/sample rate')" = "48000/1" ] || mxl_fail "audio flow rate is not 48000"

mxl_read_video 30 50 $MXL_TMP/v.y4m || mxl_fail "video reader did not finish"
v() { ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=$1 -of default=nw=1:nk=1 $MXL_TMP/v.y4m; }
[ "$(v width)" = "1280" ]        || mxl_fail "video width is not 1280"
[ "$(v height)" = "720" ]        || mxl_fail "video height is not 720"
[ "$(v r_frame_rate)" = "25/1" ] || mxl_fail "video frame rate is not 25/1"
[ "$(v nb_read_frames)" = "50" ] || mxl_fail "expected 50 frames, got $(v nb_read_frames)"
pics=$(mxl_distinct_frames $MXL_TMP/v.y4m)
[ "$pics" -ge 40 ] || mxl_fail "the programme video shows only $pics different pictures in 50 frames"

mxl_read_audio 3 $MXL_TMP/a.wav || mxl_fail "audio reader did not finish"
a() { ffprobe -v error -show_entries stream=$1 -of default=nw=1:nk=1 $MXL_TMP/a.wav; }
[ "$(a sample_rate)" = "48000" ] || mxl_fail "audio rate is not 48000"
[ "$(a channels)" = "2" ]        || mxl_fail "audio is not stereo"
on=$(mxl_tone_db $MXL_TMP/a.wav 1000); off=$(mxl_tone_db $MXL_TMP/a.wav 4000)
echo "RESULT programme audio: 1 kHz peak ${on} dB, 4 kHz peak ${off} dB"
awk -v a="$on" -v b="$off" 'BEGIN { exit !(a > -35 && b < a - 25) }' || mxl_fail "the 1 kHz tone is not in the programme audio (1 kHz ${on} dB, 4 kHz ${off} dB)"

mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine is not running"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "the tap reported a side pipeline error"

mxl_capture_output 6 $MXL_TMP/out.ts
kinds=$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 $MXL_TMP/out.ts | grep -v '^$' | sort -u | tr '\n' ' ')
[ "$kinds" = "audio video " ] || mxl_fail "on-air output streams: $kinds"
[ "$(mxl_decode_errors $MXL_TMP/out.ts)" = "0" ] || mxl_fail "the on-air output has decode errors"

mxl_test_end
