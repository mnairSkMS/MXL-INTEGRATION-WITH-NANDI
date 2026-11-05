#!/usr/bin/env bash
# A wrong MXL input is refused when the engine starts, with a message that names the key: a uri that is not
# an MXL domain, a domain on another host, no flows, flows that are not UUIDs, the same flow twice, two uris,
# a delay that is negative, too long, fractional or not a number, a tap that writes a flow the input reads. A correct
# input starts and keeps running.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine

U1=$MXL_FLOW_VIDEO; U2=$MXL_FLOW_AUDIO
D=/run/mxl
check_refused() {  # check_refused <name> <transport members> <text the message must contain> [tap json]
    MXL_INPUT_TRANSPORT="$2" mxl_write_config $MXL_TMP/$1.json "$4"
    : > $MXL_TMP/e.sqlite
    local rc=0
    timeout 15 "$MXL_ENGINE" -c $MXL_TMP/$1.json -l "$MXL_LICENSE" -b $MXL_TMP/e.sqlite > $MXL_TMP/$1.log 2>&1 || rc=$?
    if [ $rc -eq 0 ] || [ $rc -eq 124 ]; then
        mxl_fail "$1: the engine did not refuse the configuration (exit status $rc)"
    elif ! grep -q "$3" $MXL_TMP/$1.log; then
        mxl_fail "$1: refused, but the message does not contain '$3': $(grep -m1 'InvalidArgument' $MXL_TMP/$1.log | cut -c1-220)"
    else
        mxl_log "ok: $1 refused with exit status $rc"
    fi
}

check_refused no-block      "\"uri\": \"mxl://$D\""                                                                   "mxl must name the flows"
check_refused not-mxl-uri   "\"uri\": \"udp://127.0.0.1:2222\", \"mxl\": {\"video\": {\"flowId\": \"$U1\"}}"          "must start with mxl://"
check_refused other-host    "\"uri\": \"mxl://studio-a$D\", \"mxl\": {\"video\": {\"flowId\": \"$U1\"}}"               "another host"
check_refused no-domain     "\"uri\": \"mxl:///\", \"mxl\": {\"video\": {\"flowId\": \"$U1\"}}"                        "needs the domain directory"
check_refused no-flow       "\"uri\": \"mxl://$D\", \"mxl\": {}"                                                       "needs a video or an audio flow"
check_refused not-a-uuid    "\"uri\": \"mxl://$D\", \"mxl\": {\"video\": {\"flowId\": \"12345\"}}"                    "is not a UUID"
check_refused no-flowid     "\"uri\": \"mxl://$D\", \"mxl\": {\"audio\": {}}"                                          "flowId is required"
check_refused same-flow     "\"uri\": \"mxl://$D\", \"mxl\": {\"video\": {\"flowId\": \"$U1\"}, \"audio\": {\"flowId\": \"$U1\"}}" "different flow ids"
check_refused two-uris      "\"uri\": [\"mxl://$D\", \"mxl://$D\"], \"mxl\": {\"video\": {\"flowId\": \"$U1\"}}"       "redundant MXL input is not supported"
check_refused delay-negative "\"uri\": \"mxl://$D\", \"mxl\": {\"video\": {\"flowId\": \"$U1\"}, \"delayMs\": -1}"        "delayMs must be a whole number of milliseconds from 0 to 2000"
check_refused delay-too-long "\"uri\": \"mxl://$D\", \"mxl\": {\"video\": {\"flowId\": \"$U1\"}, \"delayMs\": 2001}"      "delayMs must be a whole number of milliseconds from 0 to 2000"
check_refused delay-fraction "\"uri\": \"mxl://$D\", \"mxl\": {\"video\": {\"flowId\": \"$U1\"}, \"delayMs\": 12.5}"      "delayMs must be a whole number of milliseconds from 0 to 2000"
check_refused delay-text     "\"uri\": \"mxl://$D\", \"mxl\": {\"video\": {\"flowId\": \"$U1\"}, \"delayMs\": \"fast\"}"   "delayMs must be a whole number of milliseconds from 0 to 2000"
check_refused tap-on-input  "$(mxl_input_json "$U1" "$U2" "$D")" "which the same input reads" "{\"domain\": \"$D\", \"video\": {\"flowId\": \"$U1\"}}"

# A correct input starts and keeps running (its flows are not there yet: the channel waits for them), with the
# default delay and with the longest allowed.
MXL_INPUT_TRANSPORT="$(mxl_input_json)" mxl_write_config $MXL_TMP/good.json ""
mxl_start_engine $MXL_TMP/good.json good
sleep 6
mxl_alive $MXL_ENGINE_PID || mxl_fail "a correct MXL input was refused"
mxl_stop $MXL_ENGINE_PID

MXL_INPUT_TRANSPORT="$(MXL_INPUT_DELAY_MS=2000 mxl_input_json)" mxl_write_config $MXL_TMP/good-delay.json ""
mxl_start_engine $MXL_TMP/good-delay.json good-delay
sleep 6
mxl_alive $MXL_ENGINE_PID || mxl_fail "a correct MXL input with a delay of 2000 ms was refused"

mxl_test_end
