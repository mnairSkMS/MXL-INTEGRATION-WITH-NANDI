#!/usr/bin/env bash
# A source that also carries SCTE-35 sections on a data PID. The tap copies only decoded video and
# audio, so the data stream must not take a tap or disturb the others: both flows appear with the
# right content, one tap per kind starts, and the on-air output is clean. The engine logs every
# SCTE-35 section it reads from the input, and the test counts those that are not splice_null, to show
# that the sections were really read while the tap ran.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info tsp tstables

mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_source scte

# The source itself carries the sections (checked on the file that is played out).
in_source=$(tstables --pid 500 --tid 0xFC --max-tables 200 $MXL_TMP/scte.ts 2>/dev/null | grep -c 'SpliceInsert\|TimeSignal' || true)
echo "RESULT SCTE-35 sections found on the source's PID 500: $in_source"
[ "$in_source" -gt 0 ] || mxl_abort "the test source carries no SCTE-35"

mxl_start_engine $MXL_TMP/config.json
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear (see engine.log)"
sleep 5

[ "$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')" = "25/1" ]    || mxl_fail "video flow rate is not 25/1"
[ "$(mxl_flow_field $MXL_FLOW_AUDIO 'Grain/sample rate')" = "48000/1" ] || mxl_fail "audio flow rate is not 48000"

mxl_read_video 20 50 $MXL_TMP/v.y4m || mxl_fail "could not read 50 video frames"
pics=$(mxl_distinct_frames $MXL_TMP/v.y4m)
[ "$pics" -ge 40 ] || mxl_fail "the video flow shows only $pics different pictures in 50 frames"
mxl_read_audio 3 $MXL_TMP/a.wav || mxl_fail "could not read the audio flow"
db=$(mxl_tone_db $MXL_TMP/a.wav 1000)
awk -v d="$db" 'BEGIN { exit !(d > -35) }' || mxl_fail "no 1 kHz tone in the audio flow (${db} dB)"

taps=$(grep -Ec 'MXL tap [0-9a-f-]+ \((video|audio)\) started' $MXL_TMP/engine.log)
[ "$taps" = "2" ] || mxl_fail "expected 2 taps (video, audio), found $taps"
mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "the tap reported a side pipeline error"

mxl_capture_output 8 $MXL_TMP/out.ts
[ "$(mxl_decode_errors $MXL_TMP/out.ts)" = "0" ] || mxl_fail "the on-air output has decode errors"
# splice_null is /DARAAAAAAAAAP/wAAAAAHpPv/8= in base64; the others are the splice_insert events.
read_events=$(grep 'Acquired SCTE-35 section' $MXL_TMP/engine.log | grep -vc '/DARAAAAAAAAAP/wAAAAAHpPv/8=' || true)
echo "RESULT SCTE-35 events (not splice_null) the engine read from the input: $read_events"
[ "$read_events" -gt 0 ] || mxl_fail "the engine read no SCTE-35 events from the source"

mxl_test_end
