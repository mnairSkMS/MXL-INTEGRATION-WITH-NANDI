#!/usr/bin/env bash
# Where a cue lands against the picture after it has crossed an MXL data flow (Step 5, plan test S1 and Gate A: a cue within one frame of
# its place). The source is a black picture that flashes white for 2 frames at the start of every second and carries splice_insert
# events. Channel A takes it and writes its programme as MXL flows (video, audio, SCTE-35 data) and also encodes it with SCTE-35 on PID 500
# (the in-process output). Channel B reads the three flows and encodes them with SCTE-35 on PID 500. For each event the distance from the
# splice to the latest flash frame is measured in the source, in A's output and in B's (tests/framework/scte_align.py). B must be within
# one frame (plus 5 ms) of the source's own placement. A is reported: the muxer places a splice by the source's times, so A drifts from the
# picture when the source's clock differs from the engine's (tsp's pacing of the file does, by about 0.3 %).

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info tsp tstables

MXL_FLOW_DATA=7c1d2a52-3f0e-4b6e-9d54-0a6e5b1f8a31
SCTE='{ "pid": 500, "mode": "passthrough" }'

# Channel A: the source to the MXL flows (video, audio, SCTE-35), and its own on-air output on port 4445 with the cues for reference
MXL_OUT_PORT=4445 MXL_OUT_SCTE="$SCTE" \
MXL_EXTRA_OUTPUT="{ \"type\": \"mxl\", \"name\": \"programme\", \"mxl\": { \"domain\": \"$MXL_DOMAIN\", \"video\": { \"flowId\": \"$MXL_FLOW_VIDEO\" }, \"audio\": { \"flowId\": \"$MXL_FLOW_AUDIO\", \"channels\": 2 }, \"data\": { \"flowId\": \"$MXL_FLOW_DATA\" } } }" \
    mxl_write_config $MXL_TMP/a.json ""
# Channel B: the three flows to UDP with the cues on PID 500
MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA MXL_INPUT_TRANSPORT="$(MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA mxl_input_json)" MXL_OUT_SCTE="$SCTE" mxl_write_config $MXL_TMP/b.json ""

mxl_start_source scte-sync
mxl_start_engine $MXL_TMP/a.json a
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "channel A's video flow did not appear (see engine-a.log)"
mxl_wait_flow $MXL_FLOW_DATA 40  || mxl_abort "channel A's data flow did not appear (see engine-a.log)"

# Capture both outputs for the whole run
mxl_bg tsp -I ip 4445 -P until --seconds 32 -O file $MXL_TMP/a.ts
capA=$MXL_LAST_PID
mxl_start_engine $MXL_TMP/b.json b
pid_b=$MXL_ENGINE_PID
mxl_bg tsp -I ip 4444 -P until --seconds 30 -O file $MXL_TMP/b.ts
capB=$MXL_LAST_PID
sleep 36
wait $capA $capB 2> /dev/null || true

mxl_alive $pid_b || { tail -20 $MXL_TMP/engine-b.log >> $MXL_LOG; mxl_abort "channel B is not running (see engine-b.log)"; }

python3 -I ${SCRIPT_DIR}/../../../../framework/scte_align.py $MXL_TMP/scte.ts $MXL_TMP/a.ts $MXL_TMP/b.ts 45 | tee $MXL_TMP/align.txt > /dev/null || true
grep '^RESULT' $MXL_TMP/align.txt
grep -q '^FAIL' $MXL_TMP/align.txt && mxl_fail "$(grep -m3 '^FAIL' $MXL_TMP/align.txt | tr '\n' ';')"
mxl_alive $pid_b || mxl_fail "channel B stopped"

mxl_test_end
