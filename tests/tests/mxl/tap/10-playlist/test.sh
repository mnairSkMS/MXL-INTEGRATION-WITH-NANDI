#!/usr/bin/env bash
# A playlist input. The tap is on the playlist's own streams, which carry every item in turn, so the
# flows run on from one item to the next: item 1 has a 1 kHz tone and item 2 a 2 kHz tone.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

ITEM_SECONDS=14
for n in 1 2; do
    freq=$(( n * 1000 ))
    ffmpeg -v error -y -f lavfi -i "testsrc2=size=1280x720:rate=25" -f lavfi -i "sine=frequency=${freq}:sample_rate=48000" \
        -t $ITEM_SECONDS -c:v libx264 -preset veryfast -g 50 -b:v 3M -c:a aac -ac 2 -f mpegts $MXL_TMP/item$n.ts >> $MXL_LOG 2>&1
done
{
    echo "#EXTM3U"
    for n in 1 2; do echo "#EXTINF:${ITEM_SECONDS},item$n"; echo "file://$MXL_TMP/item$n.ts"; done
} > $MXL_TMP/list.m3u8

export MXL_INPUT_TRANSPORT="\"uri\": [\"pls+file://$MXL_TMP/list.m3u8\"]"
mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_engine $MXL_TMP/config.json

mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear (see engine.log)"
t0=$(date +%s.%N)

tone() {
    mxl_read_audio 2 $MXL_TMP/a.wav || return 0
    local lo hi
    lo=$(mxl_tone_db $MXL_TMP/a.wav 1000); hi=$(mxl_tone_db $MXL_TMP/a.wav 2000)
    awk -v l="$lo" -v h="$hi" 'BEGIN { if (l > h + 8) print "1k"; else if (h > l + 8) print "2k"; else print "unclear" }'
}

sleep 2
first=$(tone)
[ "$first" = "1k" ] || mxl_fail "during item 1 the audio flow carries '$first', expected 1 kHz"

second=""
for ((i = 0; i < 12; i++)); do
    second=$(tone)
    [ "$second" = "2k" ] && break
done
echo "RESULT audio flow during item 1: $first, after the change of item: $second ($(mxl_seconds_since $t0) s after the flows appeared)"
[ "$second" = "2k" ] || mxl_fail "after the change of item the audio flow carries '$second', expected 2 kHz"

mxl_read_video 20 50 $MXL_TMP/v.y4m || mxl_fail "the video flow could not be read after the change of item"
pics=$(mxl_distinct_frames $MXL_TMP/v.y4m)
[ "$pics" -ge 40 ] || mxl_fail "after the change of item the video flow shows only $pics different pictures in 50 frames"
[ "$(mxl_flow_field $MXL_FLOW_VIDEO 'Grain/sample rate')" = "25/1" ] || mxl_fail "video flow rate is not 25/1"

mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped"
started=$(grep -c "MXL tap $MXL_FLOW_VIDEO (video) started" $MXL_TMP/engine.log)
echo "RESULT the video tap started $started time(s)"
[ "$started" -ge 1 ] || mxl_fail "the video tap never started"
grep -q 'another stream already writes' $MXL_TMP/engine.log && mxl_fail "two streams competed for one flow"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "the tap reported a side pipeline error"

mxl_test_end
