#!/usr/bin/env bash
# Scheduled cues across a split channel (Step 5, plan tests S1 and S2). The cues come from the main engine's scheduler: a splice_insert out
# and its return to network (the section is built by the engine), and a time_signal with a segmentation descriptor and UPID given as a
# ready section in base64. The main engine writes its programme as MXL flows (video, audio, SCTE-35 data) and also encodes it with SCTE-35
# on PID 500 (the in-process reference); the worker reads the flows and encodes them with SCTE-35 on PID 500. Expected, for each cue: the
# worker's section is the main output's section byte for byte apart from pts_adjustment and CRC_32 (and a pts_time of 0 where the worker's
# reader zeroes it, see framework/scte_sections.py), and its splice lands within one picture of where the main output's does. The picture carries its frame number, so the picture is read, not inferred from clocks.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
. ${SCRIPT_DIR}/../../../../framework/mxl_worker.sh
worker_domain_in_memory
mxl_test_begin "$SCRIPT_DIR"
worker_trap
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info tsp tstables tstabcomp sqlite3

FW=${SCRIPT_DIR}/../../../../framework

MXL_OUT_PORT=4445 MXL_OUT_SCTE="$WORKER_SCTE" \
MXL_EXTRA_OUTPUT="{ \"type\": \"mxl\", \"name\": \"programme\", \"mxl\": { \"domain\": \"$MXL_DOMAIN\", \"video\": { \"flowId\": \"$MXL_FLOW_VIDEO\" }, \"audio\": { \"flowId\": \"$MXL_FLOW_AUDIO\", \"channels\": 2 }, \"data\": { \"flowId\": \"$MXL_FLOW_DATA\" } } }" \
    mxl_write_config $MXL_TMP/main.json ""
MXL_INPUT_TRANSPORT="$(MXL_INPUT_DATA_FLOW=$MXL_FLOW_DATA mxl_input_json)" MXL_OUT_SCTE="$WORKER_SCTE" mxl_write_config $MXL_TMP/w.json ""

# the operator's section: time_signal, segmentation type 0x34 (provider placement opportunity start), an ADI UPID
cat > $MXL_TMP/signal.xml <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<tsduck>
  <splice_information_table>
    <time_signal pts_time="0"/>
    <splice_segmentation_descriptor identifier="0x43554549" segmentation_event_id="2000" segmentation_event_cancel="false"
        web_delivery_allowed="true" no_regional_blackout="true" archive_allowed="true" device_restrictions="3"
        segmentation_duration="900000" segmentation_type_id="0x34" segment_num="1" segments_expected="1" sub_segment_num="0" sub_segments_expected="0">
      <segmentation_upid type="0x09">5349474E414C3A303132333435</segmentation_upid>
    </splice_segmentation_descriptor>
  </splice_information_table>
</tsduck>
XML
tstabcomp $MXL_TMP/signal.xml -o $MXL_TMP/signal.bin >> $MXL_LOG 2>&1
[ -s $MXL_TMP/signal.bin ] || mxl_abort "the time_signal section could not be built"
SIGNAL=$(base64 -w0 $MXL_TMP/signal.bin)

worker_start_barcode_source 90
worker_start_engine_db $MXL_TMP/main.json main
pid_m=$MXL_ENGINE_PID
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "the main engine's video flow did not appear (see engine-main.log)"
mxl_wait_flow $MXL_FLOW_DATA 40 || mxl_abort "the main engine's data flow did not appear (see engine-main.log)"
worker_start_engine_db $MXL_TMP/w.json w
pid_w=$MXL_ENGINE_PID
sleep 6

mxl_bg tsp -I ip 4445 -P until --seconds 40 -O file $MXL_TMP/main.ts
capm=$MXL_LAST_PID
mxl_bg tsp -I ip 4444 -P until --seconds 40 -O file $MXL_TMP/w.ts
capw=$MXL_LAST_PID

# the scheduler injects 4 s (its pre-roll) before the time of the event
worker_event main 10 scte35_spliceinsert '{"scte35_spliceinsert":{"splice_event_id":700,"duration":30000,"auto_return":false}}'
worker_event main 19 scte35_returntonetwork '{"scte35_returntonetwork":{"splice_event_id":700}}'
worker_event main 28 scte35_timesignal "{\"scte35_timesignal\":{\"scte35_base64\":\"$SIGNAL\"}}"

sleep 43
wait $capm $capw 2> /dev/null || true
mxl_alive $pid_m || mxl_fail "the main engine stopped"
mxl_alive $pid_w || mxl_fail "the worker stopped"
mxl_cleanup

python3 -I $FW/scte_sections.py compare $MXL_TMP/main.ts $MXL_TMP/w.ts 1 insert:700:out insert:700:in signal:2000 > $MXL_TMP/cues.txt || true
grep -E '^(RESULT|FAIL)' $MXL_TMP/cues.txt | cut -c1-400
grep -q '^RESULT sections' $MXL_TMP/cues.txt || mxl_fail "the outputs could not be compared"
grep -q '^FAIL' $MXL_TMP/cues.txt && mxl_fail "$(grep -m3 '^FAIL' $MXL_TMP/cues.txt | tr '\n' ';')"

mxl_test_end
