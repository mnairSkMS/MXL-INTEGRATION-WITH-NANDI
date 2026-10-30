#!/usr/bin/env bash
# A wrong mxlTap block is refused when the engine starts, with a message that names it: no UUID,
# no domain, no flow, equal ids, too many channels. A correct block starts.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine

U1=$MXL_FLOW_VIDEO; U2=$MXL_FLOW_AUDIO
check_refused() {  # check_refused <name> <tap json> <text the message must contain>
    mxl_write_config $MXL_TMP/$1.json "$2"
    : > $MXL_TMP/e.sqlite
    local rc=0
    timeout 15 "$MXL_ENGINE" -c $MXL_TMP/$1.json -l "$MXL_LICENSE" -b $MXL_TMP/e.sqlite > $MXL_TMP/$1.log 2>&1 || rc=$?
    if [ $rc -eq 0 ] || [ $rc -eq 124 ]; then
        mxl_fail "$1: the engine did not refuse the configuration (exit status $rc)"
    elif ! grep -q "$3" $MXL_TMP/$1.log; then
        mxl_fail "$1: refused, but the message does not contain '$3': $(grep -m1 'InvalidArgument' $MXL_TMP/$1.log | cut -c1-200)"
    else
        mxl_log "ok: $1 refused with exit status $rc"
    fi
}

check_refused not-a-uuid   "{\"domain\":\"/run/mxl\",\"video\":{\"flowId\":\"12345\"}}"                                  "is not a UUID"
check_refused no-domain    "{\"video\":{\"flowId\":\"$U1\"}}"                                                             "mxlTap.domain is required"
check_refused no-flow      "{\"domain\":\"/run/mxl\"}"                                                                     "needs a video or an audio flow"
check_refused same-ids     "{\"domain\":\"/run/mxl\",\"video\":{\"flowId\":\"$U1\"},\"audio\":{\"flowId\":\"$U1\"}}"     "different flow ids"
check_refused nine-channels "{\"domain\":\"/run/mxl\",\"audio\":{\"flowId\":\"$U2\",\"channels\":9}}"                     "must be 1 to 8"
check_refused no-flowid    "{\"domain\":\"/run/mxl\",\"video\":{}}"                                                       "flowId is required"

# A correct block is accepted: the engine starts and keeps running.
mxl_write_config $MXL_TMP/good.json "$(mxl_tap_json /run/mxl)"
mxl_start_engine $MXL_TMP/good.json
sleep 6
mxl_alive $MXL_ENGINE_PID || mxl_fail "a correct mxlTap block was refused"

mxl_test_end
