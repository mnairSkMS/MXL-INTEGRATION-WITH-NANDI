#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e

source $SCRIPT_DIR/_functions.sh

ARG_X=
ARG_Y=
ARG_W=
ARG_H=
ARG_F_IN=0
ARG_F_OUT=0
ARG_OPACITY=1
ARG_DELAY=50
ARG_DURATION=
ARG_URLS=()
ARG_TIME=

function show_usage() {
    echo "$0 [-x <pos_x>] [-y <pos_y>] [-w <width>] [-h <height>] [-i <fade_in_ms>] [-o <fade_out_ms>] [-a <opacity>] [-t <start_offset_sec>] [-d <duration_sec>] [-e <delay_beetwen_images_ms>] -u <url> [-u <url>]"
    exit 1
}

while getopts ':x:y:w:h:i:o:a:u:e:t:d:' flag; do
    case "${flag}" in
        x) ARG_X=${OPTARG}
        ;;
        y) ARG_Y=${OPTARG}
        ;;
        w) ARG_W=${OPTARG}
        ;;
        h) ARG_H=${OPTARG}
        ;;
        i) ARG_F_IN=${OPTARG}
        ;;
        o) ARG_F_OUT=${OPTARG}
        ;;
        a) ARG_OPACITY=${OPTARG}
        ;;
        u) ARG_URLS+=(${OPTARG})
        ;;
        e) ARG_DELAY=${OPTARG}
        ;;
        t) ARG_TIME=${OPTARG}
        ;;
        d) ARG_DURATION=${OPTARG}
        ;;
        *) show_usage
        ;;
    esac
done

if [ ${#ARG_URLS[@]} -eq 0 ]; then
    echo "No image URL was specified!"

    show_usage
fi

payloadTemplate='
[
  {
    "type": "image_overlay",
    "time": $startTime,
    "details": {
      "image_overlay": {
        "geometry": {
        },
        "image_urls": [
        ],
        "opacity": $opacity|fromjson,
        "fade_in": $fadeIn|fromjson,
        "fade_out": $fadeOut|fromjson,
        "delay_between_images": $delay|fromjson
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
    --arg fadeIn $ARG_F_IN \
    --arg fadeOut $ARG_F_OUT \
    --arg opacity $ARG_OPACITY \
    --arg delay $ARG_DELAY \
    "$payloadTemplate"
)

if [ $ARG_TIME == \"\" ]; then
    payload=$(echo "$payload" | jq 'del(.[].time)')
fi

if [ ! -z "$ARG_DURATION" ]; then
    payload=$(echo "$payload" | jq --arg duration "$ARG_DURATION" '.[0].details.image_overlay.duration = ($duration|fromjson)')
fi

if [ ! -z $ARG_X ]; then
    payload=$(echo "$payload" | jq --arg x $ARG_X '.[0].details.image_overlay.geometry |= . + { x: $x|fromjson }')
fi

if [ ! -z $ARG_Y ]; then
    payload=$(echo "$payload" | jq --arg y $ARG_Y '.[0].details.image_overlay.geometry |= . + { y: $y|fromjson }')
fi

if [ ! -z $ARG_W ]; then
    payload=$(echo "$payload" | jq --arg w $ARG_W '.[0].details.image_overlay.geometry |= . + { width: $w|fromjson }')
fi

if [ ! -z $ARG_H ]; then
    payload=$(echo "$payload" | jq --arg h $ARG_H '.[0].details.image_overlay.geometry |= . + { height: $h|fromjson }')
fi

for ((idx=0; idx < ${#ARG_URLS[@]}; idx++)); do
    payload=$(echo "$payload" | jq --arg uri "${ARG_URLS[$idx]}" '.[0].details.image_overlay.image_urls |= . + [ $uri ]')
done

call_post "scheduled_events" "'$payload'"
