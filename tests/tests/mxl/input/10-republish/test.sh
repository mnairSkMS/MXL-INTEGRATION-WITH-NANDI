#!/usr/bin/env bash
# An MXL input can itself be tapped and can feed a programme output: channel A publishes its programme as flows,
# channel B reads them and publishes the picture again twice, as a source tap on its MXL input and as a programme
# output of its own (other flow ids, same domain). Both republished pairs of flows must carry A's moving picture
# and tone, and the engine must not report a side pipeline error.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

TAP_VIDEO=0e1c2a3b-4d5e-4f60-8a7b-9c0d1e2f3a41; TAP_AUDIO=0e1c2a3b-4d5e-4f60-8a7b-9c0d1e2f3a42
OUT_VIDEO=0e1c2a3b-4d5e-4f60-8a7b-9c0d1e2f3a51; OUT_AUDIO=0e1c2a3b-4d5e-4f60-8a7b-9c0d1e2f3a52

MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config $MXL_TMP/a.json ""
tap="{ \"domain\": \"$MXL_DOMAIN\", \"video\": { \"flowId\": \"$TAP_VIDEO\" }, \"audio\": { \"flowId\": \"$TAP_AUDIO\", \"channels\": 2 } }"
programme="{ \"type\": \"mxl\", \"name\": \"programme\", \"mxl\": { \"domain\": \"$MXL_DOMAIN\", \"video\": { \"flowId\": \"$OUT_VIDEO\" }, \"audio\": { \"flowId\": \"$OUT_AUDIO\", \"channels\": 2 } } }"
MXL_INPUT_TRANSPORT="$(mxl_input_json)" MXL_EXTRA_OUTPUT="$programme" mxl_write_config $MXL_TMP/b.json "$tap"

mxl_start_source tone
mxl_start_engine $MXL_TMP/a.json a
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "channel A's video flow did not appear (see engine-a.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "channel A's audio flow did not appear (see engine-a.log)"
mxl_start_engine $MXL_TMP/b.json b
pid_b=$MXL_ENGINE_PID
for f in $TAP_VIDEO $TAP_AUDIO $OUT_VIDEO $OUT_AUDIO; do
    mxl_wait_flow $f 40 || mxl_abort "channel B's flow $f did not appear (see engine-b.log)"
done
sleep 6

check() {  # check <name> <video flow> <audio flow>
    timeout 30 gst-launch-1.0 -q -e mxlsrc video-flow-id=$2 domain=$MXL_DOMAIN num-buffers=50 \
        ! videoconvert ! "video/x-raw,format=I420" ! y4menc ! filesink location=$MXL_TMP/$1.y4m >> $MXL_LOG 2>&1 || mxl_fail "$1: the video reader did not finish"
    timeout 30 gst-launch-1.0 -q -e mxlsrc audio-flow-id=$3 domain=$MXL_DOMAIN num-buffers=3000 \
        ! audioconvert ! wavenc ! filesink location=$MXL_TMP/$1.wav >> $MXL_LOG 2>&1 || mxl_fail "$1: the audio reader did not finish"
    local pics on off
    pics=$(mxl_distinct_frames $MXL_TMP/$1.y4m)
    on=$(mxl_tone_db $MXL_TMP/$1.wav 1000); off=$(mxl_tone_db $MXL_TMP/$1.wav 4000)
    echo "RESULT $1: ${pics} different pictures in 50 frames; 1 kHz peak ${on} dB, 4 kHz peak ${off} dB"
    [ "$pics" -ge 40 ] || mxl_fail "$1: the picture does not move ($pics different pictures in 50 frames)"
    awk -v a="$on" -v b="$off" 'BEGIN { exit !(a > -35 && b < a - 25) }' || mxl_fail "$1: the 1 kHz tone is not in the audio"
}
check tap $TAP_VIDEO $TAP_AUDIO
check programme $OUT_VIDEO $OUT_AUDIO

mxl_alive $pid_b || mxl_fail "channel B stopped"
grep -q 'side pipeline error' $MXL_TMP/engine-b.log && mxl_fail "channel B reported a side pipeline error"

mxl_test_end
