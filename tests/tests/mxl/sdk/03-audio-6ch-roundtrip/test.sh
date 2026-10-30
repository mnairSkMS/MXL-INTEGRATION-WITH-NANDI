#!/usr/bin/env bash
# 5.1 Float32 audio through MXL: a -6 dBFS tone written on channel 2 must come out on channel 2
# only, at the same level, at 48 kHz with 6 channels.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require mxlsink mxlsrc mxl-info ffprobe ffmpeg

TONE_CH=2

mxl_write_audio $MXL_FLOW_AUDIO 6 $TONE_CH 500
mxl_wait_flow $MXL_FLOW_AUDIO 10 || mxl_abort "audio flow did not appear in the domain"

[ "$(mxl_flow_field $MXL_FLOW_AUDIO Format)" = "Audio" ]              || mxl_fail "flow is not an audio flow"
[ "$(mxl_flow_field $MXL_FLOW_AUDIO 'Grain/sample rate')" = "48000/1" ] || mxl_fail "flow sample rate is not 48000"

timeout 20 gst-launch-1.0 -q -e mxlsrc audio-flow-id=$MXL_FLOW_AUDIO domain=$MXL_DOMAIN num-buffers=200 \
    ! audioconvert ! wavenc ! filesink location=$MXL_TMP/out.wav >> $MXL_LOG 2>&1 || mxl_fail "reader did not finish"

probe() { ffprobe -v error -show_entries stream=$1 -of default=nw=1:nk=1 $MXL_TMP/out.wav; }
[ "$(probe sample_rate)" = "48000" ]    || mxl_fail "sample rate is not 48000"
[ "$(probe channels)" = "6" ]           || mxl_fail "channel count is not 6"
[ "$(probe codec_name)" = "pcm_f32le" ] || mxl_fail "sample format is not Float32"

peak() { ffmpeg -v info -i $MXL_TMP/out.wav -af "pan=mono|c0=c$1,volumedetect" -f null - 2>&1 | awk '/max_volume/ { print $(NF-1) }'; }
for ch in 0 1 2 3 4 5; do
    level=$(peak $ch)
    mxl_log "channel $ch peak: ${level} dB"
    if [ $ch -eq $TONE_CH ]; then
        awk -v l="$level" 'BEGIN { exit !(l > -6.5 && l < -5.5) }' || mxl_fail "channel $ch peak is $level dB, expected -6.0 dB"
    else
        awk -v l="$level" 'BEGIN { exit !(l < -80) }' || mxl_fail "channel $ch is not silent ($level dB): channel order broken"
    fi
done

mxl_test_end
