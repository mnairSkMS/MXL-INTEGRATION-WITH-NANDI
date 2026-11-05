#!/usr/bin/env bash
# Lip-sync through the loopback. The source flashes the picture and beeps at the same instants. Channel A
# publishes its programme as MXL flows, channel B reads them and encodes them. The flash-to-beep offset is
# measured in A's programme flows, in A's on-air output and in B's output. Both channels' encoders add
# their own delay to the audio (about 43 ms each), so B's output is compared with A's: reading the flows
# and putting them on B's timeline must not add more than two frames (80 ms) to it. The limit is provisional (Gate A).

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine
mxl_require mxlsink mxlsrc mxl-info videoanalyse level

MXL_OUT_PORT=4445 MXL_EXTRA_OUTPUT="$(mxl_programme_json)" mxl_write_config $MXL_TMP/a.json ""
MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/b.json ""

mxl_start_source sync
mxl_start_engine $MXL_TMP/a.json a
mxl_wait_flow $MXL_FLOW_VIDEO 40 || mxl_abort "channel A's video flow did not appear (see engine-a.log)"
mxl_wait_flow $MXL_FLOW_AUDIO 40 || mxl_abort "channel A's audio flow did not appear (see engine-a.log)"
mxl_start_engine $MXL_TMP/b.json b
sleep 10

mxl_bg ffmpeg -v error -y -i "udp://127.0.0.1:4445?fifo_size=5000000&overrun_nonfatal=1" -t 14 -c copy $MXL_TMP/a-onair.ts
cap_a=$MXL_LAST_PID
mxl_bg ffmpeg -v error -y -i "udp://127.0.0.1:${MXL_PORT_OUT}?fifo_size=5000000&overrun_nonfatal=1" -t 14 -c copy $MXL_TMP/b-onair.ts
cap_b=$MXL_LAST_PID
mxl_lipsync_flows 14 $MXL_TMP/flows.log
wait $cap_a $cap_b 2>/dev/null || true

read -r flows_ms flows_n <<< "$(mxl_lipsync_pairs "$(sed -n 1p $MXL_TMP/flows.log.onsets)" "$(sed -n 2p $MXL_TMP/flows.log.onsets)")"
read -r a_ms a_n <<< "$(mxl_lipsync_file $MXL_TMP/a-onair.ts)"
read -r b_ms b_n <<< "$(mxl_lipsync_file $MXL_TMP/b-onair.ts)"
echo "RESULT flash-minus-beep in A's programme flows:  ${flows_ms} ms (${flows_n} pairs)"
echo "RESULT flash-minus-beep in A's on-air output:    ${a_ms} ms (${a_n} pairs)"
echo "RESULT flash-minus-beep in B's output:           ${b_ms} ms (${b_n} pairs)"

[ "${a_n:-0}" -ge 5 ] && [ "${b_n:-0}" -ge 5 ] || mxl_abort "could not measure the outputs (pairs: A ${a_n:-0}, B ${b_n:-0})"
awk -v a="$b_ms" -v b="$a_ms" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 80) }' \
    || mxl_fail "B's output is $(awk -v a=$b_ms -v b=$a_ms 'BEGIN { print a - b }') ms from A's on-air offset; limit 80 ms"

mxl_test_end
