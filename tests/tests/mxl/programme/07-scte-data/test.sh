#!/usr/bin/env bash
# The SCTE-35 data flow of an MXL programme output (Step 5, WP2 writer, plan section 4.5 and 4.6).
# The source carries splice_insert sections on a data PID; the engine reads them and, because the "mxl" output names a "data" flow,
# writes them into that flow: one grain for every video frame, a cue in every grain from when it is known until its splice frame,
# each copy with a countdown to the splice. The flow is read with GStreamer and decoded by tests/framework/mxl_cues.py, which does
# not share code with the engine. Checks:
#   - every grain decodes (parity, checksums, fragments), every section has a valid CRC-32;
#   - there is a grain for every frame time in the window (no holes), empty ones included;
#   - every section the engine read after the reader started is in the flow, byte for byte, and nothing else is;
#   - for every cue, splice time (grain time + countdown * 40 ms) is the same in every copy, and the countdown falls by one per grain;
#   - a cue is repeated in more than one grain (so that a late reader meets it);
#   - the picture and sound flows and the on-air output are unaffected.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info tsp python3

MXL_FLOW_DATA=7c1d2a52-3f0e-4b6e-9d54-0a6e5b1f8a31
export MXL_EXTRA_OUTPUT="{ \"type\": \"mxl\", \"name\": \"programme\", \"mxl\": { \"domain\": \"$MXL_DOMAIN\", \"video\": { \"flowId\": \"$MXL_FLOW_VIDEO\" }, \"audio\": { \"flowId\": \"$MXL_FLOW_AUDIO\", \"channels\": 2 }, \"data\": { \"flowId\": \"$MXL_FLOW_DATA\" } } }"
mxl_write_config $MXL_TMP/config.json ""
mxl_start_source scte
mxl_start_engine $MXL_TMP/config.json

mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the video flow did not appear (see engine.log)"
mxl_wait_flow $MXL_FLOW_DATA 40  || mxl_abort "the data flow did not appear (see engine.log)"
[ "$(mxl_flow_field $MXL_FLOW_DATA 'Grain/sample rate')" = "25/1" ] || mxl_fail "the data flow's rate is not 25/1"

t0=$(date +%H:%M:%S.%N | cut -c1-12)
timeout -s INT 30 gst-launch-1.0 -e -v mxlsrc data-flow-id=$MXL_FLOW_DATA domain=$MXL_DOMAIN ! identity silent=false ! filesink location=$MXL_TMP/anc.bin > $MXL_TMP/gst.log 2>&1 &
reader=$!
MXL_PIDS+=("$reader")
wait $reader 2> /dev/null || true
t1=$(date +%H:%M:%S.%N | cut -c1-12)
mxl_log "reader ran from $t0 to $t1"

# The sections the engine read from the source, with the time of the log line. (Lines: "[ 11/11/26 10:26:19.742 ] ... Acquired SCTE-35 section: <base64>".)
grep 'Acquired SCTE-35 section' $MXL_TMP/engine.log | tr -d '\r' \
    | sed -E 's/^\[ [0-9/]+ ([0-9:.]+) \].*Acquired SCTE-35 section: (.*)$/\1 \2/' > $MXL_TMP/acquired.txt
echo "RESULT the engine read $(wc -l < $MXL_TMP/acquired.txt) SCTE-35 sections from the source"

python3 -I ${SCRIPT_DIR}/../../../../framework/mxl_cues.py $MXL_TMP/anc.bin $MXL_TMP/gst.log > $MXL_TMP/grains.jsonl 2> $MXL_TMP/decode.err || true
[ -s $MXL_TMP/decode.err ] && mxl_fail "the decoder wrote errors: $(head -3 $MXL_TMP/decode.err)"
python3 -I ${SCRIPT_DIR}/../../../../framework/mxl_cues.py check $MXL_TMP/grains.jsonl $MXL_TMP/acquired.txt "$t0" "$t1" | tee -a $MXL_LOG > $MXL_TMP/check.txt || true
grep '^RESULT' $MXL_TMP/check.txt
grep -q '^FAIL' $MXL_TMP/check.txt && mxl_fail "$(grep -m3 '^FAIL' $MXL_TMP/check.txt | tr '\n' ';')"

# What the engine says about the flow: cues taken, none refused.
queued=$(grep -c 'MXL data flow .*SCTE-35 cue of .* bytes queued' $MXL_TMP/engine.log || true)
echo "RESULT the engine queued $queued cues for the data flow"
[ "$queued" -gt 0 ] || mxl_fail "the engine logged no cue queued for the data flow"
grep -q 'is over the .* bytes a grain can hold' $MXL_TMP/engine.log && mxl_fail "a cue was refused as too large"

# The other flows and the on-air output are unaffected.
mxl_alive $MXL_ENGINE_PID || mxl_fail "the engine stopped"
grep -q 'side pipeline error' $MXL_TMP/engine.log && mxl_fail "a tap reported a side pipeline error"
mxl_read_video 30 50 $MXL_TMP/v.y4m || mxl_fail "could not read 50 video frames"
pics=$(mxl_distinct_frames $MXL_TMP/v.y4m)
[ "$pics" -ge 40 ] || mxl_fail "the programme video shows only $pics different pictures in 50 frames"
mxl_capture_output 6 $MXL_TMP/out.ts
[ "$(mxl_decode_errors $MXL_TMP/out.ts)" = "0" ] || mxl_fail "the on-air output has decode errors"

mxl_test_end
