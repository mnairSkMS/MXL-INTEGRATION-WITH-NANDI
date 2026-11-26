#!/usr/bin/env bash
# HLS cue tags on a worker (Step 5, plan test S4). The main engine writes its programme as MXL flows (video, audio, SCTE-35 data) and also
# as HLS (the in-process reference); the worker reads the flows and writes HLS. The main engine's scheduler runs an auto-returning break and
# then a break closed by a return to network. Expected: the same EXT-X-CUE-OUT and EXT-X-CUE-IN tags in the same order in both playlists,
# each on a segment that starts within one segment length (2 s, 50 pictures) of the other playlist's; the picture carries its frame number.
# One segment, not one picture: HLS places a cue no more precisely than that, and the two engines cut their segments at different pictures
# near a return to network. The engine as received also tags a return twice on adjacent segments at times (in process too); that counts once.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
. ${SCRIPT_DIR}/../../../../framework/mxl_worker.sh
worker_domain_in_memory
mxl_test_begin "$SCRIPT_DIR"
worker_trap
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info sqlite3

FW=${SCRIPT_DIR}/../../../../framework
mkdir -p $MXL_TMP/hls-main $MXL_TMP/hls-w

# a config of mxl_write_config with its UDP output turned into HLS of 2 s segments, all of them kept
function hls_instead_of_udp() {
    sed -i "s|\"uri\": \"udp://127.0.0.1:${2}\" } \],|\"uri\": \"hls+file://${3}\", \"hls\": { \"numSegments\": 60, \"segmentLength\": 2, \"singleDirectory\": true, \"adMarkers\": [ \"elemental\" ] } } ],|" "$1"
    grep -q 'hls+file' "$1" || mxl_abort "the HLS output was not put into $1"
}

MXL_OUT_PORT=4445 MXL_OUT_SCTE="$WORKER_SCTE" \
MXL_EXTRA_OUTPUT="{ \"type\": \"mxl\", \"name\": \"programme\", \"mxl\": { \"domain\": \"$MXL_DOMAIN\", \"video\": { \"flowId\": \"$MXL_FLOW_VIDEO\" }, \"audio\": { \"flowId\": \"$MXL_FLOW_AUDIO\", \"channels\": 2 }, \"data\": { \"flowId\": \"$MXL_FLOW_DATA\" } } }" \
    mxl_write_config $MXL_TMP/main.json ""
MXL_INPUT_TRANSPORT="$(MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA mxl_input_json)" MXL_OUT_SCTE="$WORKER_SCTE" mxl_write_config $MXL_TMP/w.json ""
hls_instead_of_udp $MXL_TMP/main.json 4445 $MXL_TMP/hls-main
hls_instead_of_udp $MXL_TMP/w.json 4444 $MXL_TMP/hls-w

worker_start_barcode_source 90
worker_start_engine_db $MXL_TMP/main.json main
pid_m=$MXL_ENGINE_PID
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
mxl_wait_flow $MXL_FLOW_DATA 40 || mxl_abort "the main engine's data flow did not appear (see engine-main.log)"
worker_start_engine_db $MXL_TMP/w.json w
pid_w=$MXL_ENGINE_PID
sleep 6

# the scheduler injects 4 s (its pre-roll) before the time of the event
worker_event main 10 scte35_spliceinsert '{"scte35_spliceinsert":{"splice_event_id":800,"duration":8000,"auto_return":true}}'
worker_event main 25 scte35_spliceinsert '{"scte35_spliceinsert":{"splice_event_id":801,"duration":30000,"auto_return":false}}'
worker_event main 33 scte35_returntonetwork '{"scte35_returntonetwork":{"splice_event_id":801}}'

sleep 46
mxl_alive $pid_m || mxl_fail "the main engine stopped"
mxl_alive $pid_w || mxl_fail "the worker stopped"
mxl_cleanup

python3 -I $FW/hls_cues.py $MXL_TMP/hls-main $MXL_TMP/hls-w 50 > $MXL_TMP/cues.txt || true
grep -E '^(RESULT|FAIL)' $MXL_TMP/cues.txt | cut -c1-500
grep -q '^RESULT worker' $MXL_TMP/cues.txt || mxl_fail "the playlists could not be compared"
grep -q '^FAIL' $MXL_TMP/cues.txt && mxl_fail "$(grep -m3 '^FAIL' $MXL_TMP/cues.txt | tr '\n' ';')"

mxl_test_end
