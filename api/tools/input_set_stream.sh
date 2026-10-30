#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

set -e

source $SCRIPT_DIR/_functions.sh

ARG_INPUT_ID=
ARG_STREAM_ID=

function show_usage() {
    echo "Usage: $0 -i <input_id> -s <stream_id>"
    exit 1
}

while getopts ':i:s:' flag; do
    case "${flag}" in
        i) ARG_INPUT_ID=${OPTARG}
        ;;
        s) ARG_STREAM_ID=${OPTARG}
        ;;
        *) show_usage
        ;;
        
    esac
done

if [ -z "$ARG_INPUT_ID" ] || [ -z "$ARG_STREAM_ID" ]; then
    show_usage
fi

payloadTemplate='
{
    "currentStreamId": $streamId
}
'

payload=$(jq -n \
    --arg streamId $ARG_STREAM_ID \
    "$payloadTemplate"
)

call_post "inputs/${ARG_INPUT_ID}" "'$payload'"
