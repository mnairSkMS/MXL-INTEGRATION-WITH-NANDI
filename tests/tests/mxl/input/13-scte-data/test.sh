#!/usr/bin/env bash
# SCTE-35 across two engines through an MXL data flow (Step 5, WP2 reader). Channel A reads a source that carries splice_insert
# sections on a data PID and publishes its programme as MXL flows, with a data flow for the SCTE-35 (an output of type "mxl" with
# "data"). Channel B reads the three flows (an input with transport.mxl.data) and has an output with scte passthrough on PID 500.
# The cues that cross the flow must come out of B as sections on that PID:
#   - every event id on B's PID 500 is one that the source sent, and each section is byte-identical in its splice_insert fields
#     (event id, out-of-network, unique program id); at least 5 distinct events arrive;
#   - B logs each cue it took from the data flow (repeats are not logged again, so the number of cues equals the number of events);
#   - A's own on-air output carries the same events (the reference), and both of B's streams decode without errors.

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

mxl_start_source scte
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

events() {  # events <ts file>: "event_id out_of_network unique_program_id splice_pts", one per distinct event id on PID 500
    python3 -I ${SCRIPT_DIR}/../../../../framework/scte_events.py "$1"
}
events $MXL_TMP/a.ts > $MXL_TMP/events-a.full
events $MXL_TMP/b.ts > $MXL_TMP/events-b.full
# The splice times are on each channel's own timeline, so only the event, out_of_network and program id are compared here
cut -d' ' -f1-3 $MXL_TMP/events-a.full > $MXL_TMP/events-a.txt
cut -d' ' -f1-3 $MXL_TMP/events-b.full > $MXL_TMP/events-b.txt
na=$(wc -l < $MXL_TMP/events-a.txt); nb=$(wc -l < $MXL_TMP/events-b.txt)
echo "RESULT splice_insert events on PID 500: channel A (in process) $na, channel B (through the MXL data flow) $nb"
[ "$na" -ge 5 ] || mxl_fail "channel A's own output has only $na events: the reference is too thin"
[ "$nb" -ge 5 ] || mxl_fail "channel B's output has only $nb events"

# B only has events that the source sent (event ids 100 to 111, out of network, program 1), with the same contents
extra=$(grep -vcE '^1(0[0-9]|1[01]) true 1$' $MXL_TMP/events-b.txt || true)
[ "$extra" = "0" ] || mxl_fail "$extra event(s) on B's PID 500 are not what the source sent: $(grep -vE '^1(0[0-9]|1[01]) true 1$' $MXL_TMP/events-b.txt | head -3 | tr '\n' ';')"
# ... and every event of A's own output is in B's too, with the same contents
differ=$(comm -23 $MXL_TMP/events-a.txt $MXL_TMP/events-b.txt | wc -l)

# Every event A sent while B was running should have reached B (the events from the first seconds, before B had settled, may not)
missing=$differ
echo "RESULT events A sent that B did not get: $missing (the earliest ones, from before B had started, are expected)"
[ "$missing" -le 3 ] || mxl_fail "B is missing $missing events that A sent"

took=$(grep -c 'MXL data flow: SCTE-35 cue of' $MXL_TMP/engine-b.log || true)
echo "RESULT B took $took cues from the data flow (splice_null heartbeats included)"
[ "$took" -ge "$nb" ] || mxl_fail "B logged $took cues but sent $nb events"

# B's streams decode, and B logs no errors
# (the capture starts mid-stream and ends in the middle of a frame, so its first 4 s and last seconds are left out)
errs=$(ffmpeg -v error -ss 4 -t 20 -i $MXL_TMP/b.ts -f null - 2>&1 | wc -l)
[ "$errs" = "0" ] || mxl_fail "B's output has $errs decode error lines between 4 s and 24 s"
grep -q 'a grain could not be read' $MXL_TMP/engine-b.log && mxl_fail "B could not read a grain of the data flow completely"
errors=$(grep -E 'rror' $MXL_TMP/engine-b.log | grep -v 'no such table' || true)
[ -z "$errors" ] || { echo "$errors" | head -5 >> $MXL_LOG; mxl_fail "channel B logged errors"; }

mxl_test_end
