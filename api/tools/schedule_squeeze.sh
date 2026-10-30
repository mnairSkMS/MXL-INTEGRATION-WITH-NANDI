#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e
#set -x

source $SCRIPT_DIR/_functions.sh

ARG_TIME=
ARG_SRC_X=null
ARG_SRC_Y=null
ARG_SRC_W=null
ARG_SRC_H=null
ARG_DST_X=null
ARG_DST_Y=null
ARG_DST_W=null
ARG_DST_H=null
ARG_NAME=
ARG_DURATION=60000

function show_usage() {
    echo "$0 [-x <src_x>] [-y <src_y>] [-w <src_w>] [-h <src_h>] [-X <dst_x>] [-Y <dst_y>] [-W <dst_w>] [-H <dst_h>] [-n <name>] [-d <duration_ms>] [-t <start_offset_sec>]"
    exit 1
}

while getopts ':x:y:w:h:X:Y:W:H:n:d:t:' flag; do
    case "${flag}" in
        x) ARG_SRC_X=${OPTARG}
        ;;
        y) ARG_SRC_Y=${OPTARG}
        ;;
        w) ARG_SRC_W=${OPTARG}
        ;;
        h) ARG_SRC_H=${OPTARG}
        ;;
        X) ARG_DST_X=${OPTARG}
        ;;
        Y) ARG_DST_Y=${OPTARG}
        ;;
        W) ARG_DST_W=${OPTARG}
        ;;
        H) ARG_DST_H=${OPTARG}
        ;;
        n) ARG_NAME=${OPTARG}
        ;;
        d) ARG_DURATION=${OPTARG}
        ;;
        t) ARG_TIME=${OPTARG}
        ;;
        *) show_usage
        ;;
        
    esac
done

payloadTemplate='
[
  {
    "type": "squeeze",
    "time": $startTime,
    "details": {
        "squeeze": {
            "duration": $duration
        }
    }
  }
]'


if [ "x$ARG_TIME" != "x" ]; then
    if [ "$ARG_TIME" == "null" ]; then
        ARG_TIME=null
    else
        ARG_TIME=\"$(get_timestamp $ARG_TIME)\"
    fi
else
    ARG_TIME=\"\"
fi

payload=$(jq -n \
    --argjson startTime $ARG_TIME \
    --arg duration $ARG_DURATION \
    "$payloadTemplate"
)

if [ $ARG_TIME == \"\" ]; then
    payload=$(echo "$payload" | jq 'del(.[].time)')
fi

if [ -z $ARG_NAME ]; then
    payload=$(echo "$payload" | jq \
        --arg x $ARG_SRC_X \
        --arg y $ARG_SRC_Y \
        --arg w $ARG_SRC_W \
        --arg h $ARG_SRC_H \
        '.[0].details.squeeze.input_window_override |= . + { x: $x|fromjson, y: $y|fromjson, width: $w|fromjson, height: $h|fromjson }'\
    )

    payload=$(echo "$payload" | jq \
        --arg x $ARG_DST_X \
        --arg y $ARG_DST_Y \
        --arg w $ARG_DST_W \
        --arg h $ARG_DST_H \
        '.[0].details.squeeze.output_window_override |= . + { x: $x|fromjson, y: $y|fromjson, width: $w|fromjson, height: $h|fromjson }'\
    )

else
    payload=$(echo "$payload" | jq --arg name $ARG_NAME '.[0].details.squeeze.squeeze_name = $name')
fi

call_post "scheduled_events" "'$payload'"
