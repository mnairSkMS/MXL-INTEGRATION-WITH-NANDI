#!/usr/bin/env bash
# A wrong "mxl" block on an output is refused when the engine starts, with a message that names the key.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
. ${SCRIPT_DIR}/../../../../framework/mxl.sh
mxl_test_begin "$SCRIPT_DIR"
mxl_require_engine

V=$MXL_FLOW_VIDEO
A=$MXL_FLOW_AUDIO
D=$MXL_DOMAIN

refused() {  # refused <description> <output json> <text expected in the message>
    local desc="$1" json="$2" expect="$3"
    MXL_EXTRA_OUTPUT="$json" mxl_write_config $MXL_TMP/bad.json ""
    mxl_start_engine $MXL_TMP/bad.json
    local pid=$MXL_ENGINE_PID
    for ((i = 0; i < 100; i++)); do mxl_alive $pid || break; sleep 0.1; done
    if mxl_alive $pid; then
        mxl_fail "$desc: the engine started"
        mxl_stop $pid
    elif ! grep -q -- "$expect" $MXL_TMP/engine.log; then
        mxl_fail "$desc: refused, but the message does not contain '$expect'"
    else
        mxl_log "ok: $desc"
    fi
}

refused "no mxl block"          '{ "type": "mxl", "name": "p" }'                                                                               "transcoder.outputs\[1\].mxl is required"
refused "no domain"             "{ \"type\": \"mxl\", \"name\": \"p\", \"mxl\": { \"video\": { \"flowId\": \"$V\" } } }"                         "mxl.domain is required"
refused "not a UUID"            "{ \"type\": \"mxl\", \"name\": \"p\", \"mxl\": { \"domain\": \"$D\", \"video\": { \"flowId\": \"nope\" } } }"   "is not a UUID"
refused "no flow"               "{ \"type\": \"mxl\", \"name\": \"p\", \"mxl\": { \"domain\": \"$D\" } }"                                        "needs a video or an audio flow"
refused "equal flow ids"        "{ \"type\": \"mxl\", \"name\": \"p\", \"mxl\": { \"domain\": \"$D\", \"video\": { \"flowId\": \"$V\" }, \"audio\": { \"flowId\": \"$V\" } } }" "different flow ids"
refused "9 audio channels"      "{ \"type\": \"mxl\", \"name\": \"p\", \"mxl\": { \"domain\": \"$D\", \"audio\": { \"flowId\": \"$A\", \"channels\": 9 } } }" "channels must be 1 to 8"
refused "negative width"        "{ \"type\": \"mxl\", \"name\": \"p\", \"mxl\": { \"domain\": \"$D\", \"video\": { \"flowId\": \"$V\", \"width\": -1 } } }" "width must be positive"
refused "rate not a number"     "{ \"type\": \"mxl\", \"name\": \"p\", \"mxl\": { \"domain\": \"$D\", \"video\": { \"flowId\": \"$V\", \"rate\": \"fast\" } } }" "rate must be a whole number or a fraction"

mxl_test_end
