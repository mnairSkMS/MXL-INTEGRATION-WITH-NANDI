#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e

source $SCRIPT_DIR/_functions.sh

ARG_DURATION=60000
ARG_URL=
ARG_TIME=

function show_usage() {
    echo "$0 [-t <start_offset_sec>] [-d <duration_sec>] -u <url>"
    exit 1
}

while getopts ':u:t:d:' flag; do
    case "${flag}" in
        u) ARG_URL=${OPTARG}
        ;;
        t) ARG_TIME=${OPTARG}
        ;;
        d) ARG_DURATION=${OPTARG}
        ;;
        *) show_usage
        ;;
    esac
done

if [ -z "${ARG_URL}" ]; then
    echo "Image URL was not specified!"

    show_usage
fi

payloadTemplate='
[
  {
    "type": "slate",
    "time": $startTime,
    "details": {
      "slate": {
        "duration": $duration,
        "url": $url
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
    --arg url $ARG_URL \
    "$payloadTemplate"
)

if [ $ARG_TIME == \"\" ]; then
    payload=$(echo "$payload" | jq 'del(.[].time)')
fi

call_post "scheduled_events" "'$payload'"
