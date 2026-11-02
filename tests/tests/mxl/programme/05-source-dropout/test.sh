#!/usr/bin/env bash
# The programme is the channel's output, not the source's, so it goes on when the source stops: the channel
# holds or fills the picture and the flows keep being written. This test cuts the source for 8 s and
# reports what the programme flows did (RESULT lines), then checks only what must hold: the engine
# runs, no tap error, and a reader that starts after the source returns gets picture and sound.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

head_of() { mxl_flow_field $1 'Head index'; }

export MXL_EXTRA_OUTPUT="$(mxl_programme_json)"
mxl_write_config $MXL_TMP/config.json ""
mxl_start_source tone; src=$MXL_SRC_PID
mxl_start_engine $MXL_TMP/config.json
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear (see engine.log)"
sleep 8

mxl_stop $src
sleep 3   # the engine's buffer drains
v0=$(head_of $MXL_FLOW_VIDEO); a0=$(head_of $MXL_FLOW_AUDIO)
sleep 5
v1=$(head_of $MXL_FLOW_VIDEO); a1=$(head_of $MXL_FLOW_AUDIO)
echo "RESULT during 5 s with no source: video head moved $((v1 - v0)) grains (25/s would be 125), audio head moved $((a1 - a0)) samples (48000/s would be 240000)"
mxl_log "video head $v0 -> $v1, audio head $a0 -> $a1"

mxl_start_source tone
sleep 18
mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "a tap reported a side pipeline error"

mxl_read_video 30 50 $MXL_TMP/v.y4m || mxl_fail "a reader started after the source returned got no video"
pics=$(mxl_distinct_frames $MXL_TMP/v.y4m)
[ "$pics" -ge 40 ] || mxl_fail "after the source returned the programme shows only $pics different pictures in 50 frames"
mxl_read_audio 2 $MXL_TMP/a.wav || mxl_fail "a reader started after the source returned got no audio"
db=$(mxl_tone_db $MXL_TMP/a.wav 1000)
awk -v d="$db" 'BEGIN { exit !(d > -35) }' || mxl_fail "no 1 kHz tone in the programme audio after the source returned (${db} dB)"

mxl_test_end
