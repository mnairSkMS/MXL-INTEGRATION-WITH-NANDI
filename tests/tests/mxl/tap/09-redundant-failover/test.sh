#!/usr/bin/env bash
# An input with two redundant URIs. The tap follows what the input passes on, not one of the two
# sources, so when the active source dies and the engine switches to the other, the MXL flows carry
# on with the other source's picture and sound. The sources differ in tone: 1 kHz and 2 kHz.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

export MXL_INPUT_TRANSPORT='"uri": ["udp://127.0.0.1:2222", "udp://127.0.0.1:2223"]'
mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"

MXL_SRC_PORT=2222 MXL_SRC_FREQ=1000 mxl_start_source tone
source_1k=$MXL_SRC_PID
MXL_SRC_PORT=2223 MXL_SRC_FREQ=2000 mxl_start_source tone
source_2k=$MXL_SRC_PID
mxl_start_engine $MXL_TMP/config.json

mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear (see engine.log)"
sleep 4

# Which source is in the audio flow, and is the picture moving.
tone() {
    mxl_read_audio 2 $MXL_TMP/a.wav || return 0
    local lo hi
    lo=$(mxl_tone_db $MXL_TMP/a.wav 1000); hi=$(mxl_tone_db $MXL_TMP/a.wav 2000)
    awk -v l="$lo" -v h="$hi" 'BEGIN { if (l > h + 8) print "1k"; else if (h > l + 8) print "2k"; else print "unclear" }'
}
moving() {
    mxl_read_video 20 50 $MXL_TMP/v.y4m || { echo 0; return 0; }
    mxl_distinct_frames $MXL_TMP/v.y4m
}

# Whichever input fills its buffer first goes on air; the engine does not prefer the first URI.
first=$(tone)
case "$first" in
    1k) active=$source_1k; other="2k" ;;
    2k) active=$source_2k; other="1k" ;;
    *)  mxl_abort "before the failure the audio flow carries '$first', expected one of the two tones" ;;
esac
pics=$(moving)
[ "$pics" -ge 40 ] || mxl_fail "before the failure the video flow shows only $pics different pictures in 50 frames"

mxl_log "the $first source is on air; stopping it"
mxl_stop $active

# The switch takes a few seconds; wait for the flow to show the other source.
second=""
for ((i = 0; i < 20; i++)); do
    second=$(tone)
    [ "$second" = "$other" ] && break
done
echo "RESULT audio flow before the failure: $first, after: $second"
[ "$second" = "$other" ] || mxl_fail "the audio flow did not switch to the redundant source ('$second' after the failure)"
sleep 3
pics=$(moving)
[ "$pics" -ge 40 ] || mxl_fail "after the switch the video flow shows only $pics different pictures in 50 frames (frozen?)"

mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped"
grep -q 'Switching Standard input source' $MXL_TMP/engine.log || mxl_fail "the engine did not log a switch of source"
started=$(grep -c "MXL tap $MXL_FLOW_VIDEO (video) started" $MXL_TMP/engine.log)
[ "$started" = "1" ] || mxl_fail "the video tap started $started times; the switch should not restart it"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "the tap reported a side pipeline error"

mxl_test_end
