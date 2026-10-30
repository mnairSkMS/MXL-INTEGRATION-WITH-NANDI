#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e

source $SCRIPT_DIR/_functions.sh

ARG_TIME=
ARG_EVENT_ID=0


function show_usage() {
    echo "$0 [-e <event_id>] [-t <start_offset_sec>]"
    exit 1
}

while getopts ':e:t:' flag; do
    case "${flag}" in
        t) ARG_TIME=${OPTARG}
        ;;
        e) ARG_EVENT_ID=${OPTARG}
        ;;
        *) show_usage
        ;;
        
    esac
done

payloadTemplate='
[
  {
    "type": "scte35_returntonetwork",
    "time": $startTime,
    "details": {
        "scte35_returntonetwork": {
            "splice_event_id": $eventId
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
    --arg eventId $ARG_EVENT_ID \
    "$payloadTemplate"
)

if [ $ARG_TIME == \"\" ]; then
    payload=$(echo "$payload" | jq 'del(.[].time)')
fi

call_post "scheduled_events" "'$payload'"
