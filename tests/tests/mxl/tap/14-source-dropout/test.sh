#!/usr/bin/env bash
# The source stops and comes back. This is what a live feed does: an encoder restarts, a link drops.
#   a short gap (1.5 s): the taps carry on. They are not restarted, and new readers get picture and sound.
#   a long gap (8 s): the flows are closed after 3 s without data, and opened again when the source
#       returns. Readers that start after that get picture and sound.
# In both cases the engine keeps running and no tap reports a side pipeline error. (The timestamps of
# frames that arrive after a gap used to be close enough together to land in one slot of the flow,
# which the MXL writer refuses; see the grid in MxlTap::_place.)

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info

# "<pictures that differ in 50 frames> <1 kHz level in dB>" from fresh readers; "0 none" when they hang.
probe() {
    local pics=0 db=none
    if mxl_read_video 20 50 $MXL_TMP/v.y4m; then pics=$(mxl_distinct_frames $MXL_TMP/v.y4m); fi
    if mxl_read_audio 2 $MXL_TMP/a.wav; then db=$(mxl_tone_db $MXL_TMP/a.wav 1000); fi
    echo "$pics $db"
}
healthy() {  # healthy "<probe output>"
    read -r pics db <<< "$1"
    [ "$pics" -ge 40 ] && awk -v d="$db" 'BEGIN { exit !(d != "none" && d > -35) }'
}
# Only the lines that say a tap started (the statistics line also contains the word "started").
starts() { grep -Ec "MXL tap [0-9a-f-]+ \((video|audio)\) started" $MXL_TMP/engine.log || true; }

mxl_write_config $MXL_TMP/config.json "$(mxl_tap_json)"
mxl_start_source tone; src=$MXL_SRC_PID
mxl_start_engine $MXL_TMP/config.json
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "the audio flow did not appear (see engine.log)"
sleep 6
r=$(probe); healthy "$r" || mxl_abort "before the gap the flows are not healthy: $r"
[ "$(starts)" = "2" ] || mxl_abort "expected 2 tap starts before the gap, found $(starts)"

# ---- short gap
mxl_log "short gap"
mxl_stop $src; sleep 1.5; mxl_start_source tone; src=$MXL_SRC_PID
sleep 14
r=$(probe); echo "RESULT after the 1.5 s gap: pictures/50 and tone level: $r; tap starts: $(starts)"
healthy "$r" || mxl_fail "after the short gap the flows are not healthy: $r"
[ "$(starts)" = "2" ] || mxl_fail "the short gap restarted a tap (starts: $(starts), expected 2)"
mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped in the short gap"

# ---- long gap
mxl_log "long gap"
mxl_stop $src; sleep 8
closed=$(grep -c "no data for 3 s" $MXL_TMP/engine.log || true)
echo "RESULT flows closed by the idle watchdog during the 8 s gap: $closed (2 expected: video and audio)"
[ "$closed" = "2" ] || mxl_fail "expected both taps to close their flows during the long gap, found $closed"
mxl_start_source tone; src=$MXL_SRC_PID
sleep 16
r=$(probe); echo "RESULT after the 8 s gap: pictures/50 and tone level: $r; tap starts: $(starts)"
healthy "$r" || mxl_fail "after the long gap the flows are not healthy: $r"
[ "$(starts)" = "4" ] || mxl_fail "expected 4 tap starts in all after the long gap (2 at the start, 2 on return), found $(starts)"
mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped in the long gap"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "a tap reported a side pipeline error"

mxl_test_end
