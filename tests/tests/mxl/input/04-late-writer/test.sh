#!/usr/bin/env bash
# The channel is started before anything writes its flows. It must wait, not fail, and start working when the
# flows appear: here a second channel (A) starts 10 s later and publishes its programme. Channel B's output
# must then carry A's moving picture and tone, and B must not have been restarted.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config $MXL_TMP/a.json ""
MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/b.json ""

mxl_start_engine $MXL_TMP/b.json b
pid_b=$MXL_ENGINE_PID
sleep 10
mxl_alive $pid_b || mxl_abort "channel B stopped while its flows were missing (see engine-b.log)"
echo "RESULT channel B waited 10 s for flows that did not exist, and was running"

mxl_start_source tone
mxl_start_engine $MXL_TMP/a.json a
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "channel A's video flow did not appear (see engine-a.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "channel A's audio flow did not appear (see engine-a.log)"
sleep 10

mxl_capture_output 8 $MXL_TMP/out.ts
mxl_alive $pid_b || mxl_fail "channel B stopped"
pics=$(ffmpeg -v error -y -i $MXL_TMP/out.ts -an -vf "fps=5,scale=160:90,format=gray" -f framemd5 - 2>/dev/null | awk '!/^#/ { print $NF }' | sort -u | wc -l)
echo "RESULT B's picture: ${pics} different pictures in 40 sampled"
[ "$pics" -ge 25 ] || mxl_fail "B shows no moving picture after the flows appeared ($pics different pictures in 40 samples)"
ffmpeg -v error -y -i $MXL_TMP/out.ts -vn -ac 1 $MXL_TMP/a.wav >> $MXL_LOG 2>&1 || true
on=$(mxl_tone_db $MXL_TMP/a.wav 1000); off=$(mxl_tone_db $MXL_TMP/a.wav 4000)
echo "RESULT B's audio: 1 kHz peak ${on} dB, 4 kHz peak ${off} dB"
awk -v a="$on" -v b="$off" 'BEGIN { exit !(a > -35 && b < a - 25) }' || mxl_fail "the 1 kHz tone did not arrive in B's output"

mxl_test_end
