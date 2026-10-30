#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e

source $SCRIPT_DIR/_functions.sh

ARG_TIME=
ARG_EVENT_ID=0
ARG_EVENT_DURATION_MS=-1


function show_usage() {
    echo "$0 [-e <event_id>] [-d <event_duration_ms>] [-t <start_offset_sec>]"
    exit 1
}

while getopts ':e:d:t:' flag; do
    case "${flag}" in
        t) ARG_TIME=${OPTARG}
        ;;
        e) ARG_EVENT_ID=${OPTARG}
        ;;
        d) ARG_EVENT_DURATION_MS=${OPTARG}
        ;;
        *) show_usage
        ;;
        
    esac
done

payloadTemplate='
[
  {
    "type": "scte35_spliceinsert",
    "time": $startTime,
    "details": {
        "scte35_spliceinsert": {
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

if [ $ARG_EVENT_DURATION_MS -ge 0 ]; then
    payload=$(echo "$payload" | jq \
        --arg duration $ARG_EVENT_DURATION_MS \
        '.[0].details.scte35_spliceinsert |= . + { duration: $duration }'\
    )    
fi

call_post "scheduled_events" "'$payload'"