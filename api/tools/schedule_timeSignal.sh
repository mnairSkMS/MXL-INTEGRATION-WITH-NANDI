#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e

source $SCRIPT_DIR/_functions.sh

ARG_TIME=
ARG_EVENT_ID=0
ARG_EVENT_DURATION_MS=
ARG_EVENT_TYPE_ID=0x00 # Not indicated
ARG_REGIONAL_BLACKOUT="NO_REGIONAL_BLACKOUT"
ARG_WEB_DELIVERY_ALLOWED="WEB_DELIVERY_ALLOWED"


function show_usage() {
    echo "$0 [-e <event_id>] [-i <type_id_hex>] [-d <event_duration_ms>] [-t <start_offset_sec>] [-r]"
    echo " -r - regional blackout"
    echo " -w - WEB delivery not allowed"
    exit 1
}

while getopts ':e:i:t:d:rw' flag; do
    case "${flag}" in
        t) ARG_TIME=${OPTARG}
        ;;
        e) ARG_EVENT_ID=${OPTARG}
        ;;
        i) ARG_EVENT_TYPE_ID=${OPTARG}
        ;;
        d) ARG_EVENT_DURATION_MS=${OPTARG}
        ;;
        r) ARG_REGIONAL_BLACKOUT="REGIONAL_BLACKOUT"
        ;;
        w) ARG_WEB_DELIVERY_ALLOWED="WEB_DELIVERY_NOT_ALLOWED"
        ;;
        *) show_usage
        ;;
        
    esac
done

payloadTemplate='
[
  {
    "type": "scte35_timesignal",
    "time": $startTime,
    "details": {
        "scte35_timesignal": {
            "scte35_descriptors": [
                {
                    "delivery_restrictions": {
                        "archive_allowed_flag": "ARCHIVE_NOT_ALLOWED",
                        "device_restrictions": "NONE",
                        "no_regional_blackout_flag": $regionalBlackout,
                        "web_delivery_allowed_flag": $webDeliveryAllowed
                    },
                    "segmentation_cancel_indicator": "SEGMENTATION_EVENT_NOT_CANCELED",
                    "segmentation_event_id": $eventId,
                    "segmentation_type_id": $typeId,
                    "segmentation_upid": "abcdef",
                    "segmentation_upid_type": 12,
                    "segment_num": 1,
                    "segments_expected": 2,
                    "sub_segment_num": 3,
                    "sub_segments_expected": 4
                }
            ]
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
    --arg typeId $(printf "%u" ${ARG_EVENT_TYPE_ID}) \
    --arg eventId $(printf "%u" ${ARG_EVENT_ID}) \
    --arg webDeliveryAllowed "${ARG_WEB_DELIVERY_ALLOWED}" \
    --arg regionalBlackout "${ARG_REGIONAL_BLACKOUT}" \
    "$payloadTemplate"
)

if [ $ARG_TIME == \"\" ]; then
    payload=$(echo "$payload" | jq 'del(.[].time)')
fi

if [ "x$ARG_EVENT_DURATION_MS" != "x" ]; then
    payload=$(echo "$payload" | jq \
        --arg duration $((90 * $ARG_EVENT_DURATION_MS)) \
        '.[0].details.scte35_timesignal.scte35_descriptors[0] |= . + { segmentation_duration: $duration }' \
    )
fi

call_post "scheduled_events" "'$payload'"
